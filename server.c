#include "server.h"
#include "module/fileutil.h"
#include "module/sockutil.h"
#include "module/timeutil.h"
#include "module/hashmap.h"
#include "module/queue.h"
#include "module/service.h"

#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

/*
 * Internal states.
 */
int g_running = 0;
static struct queue *g_sworkerid_queue = NULL;
static struct worker *g_sworker_pool = NULL;
// Caches
struct inventory g_inven_cache;
static const char *g_item_cache_path = "inven_item.cache";
static const char *g_fidq_cache_path = "inven_fidq.cache";
static const char *g_nametb_cache_path = "inven_nametb.cache";
// Statistics
static int g_session_count = 0;
static int g_refused_count = 0;
// Network
static int listener_fd;
static struct sockaddr_in serv_addr;
static int g_epollfd;

static void *session_worker_routine(void *);
static int handle_request(int);

void
timestamp(int msopt, const char *fmt, ...)
{
	char msg[128];
	char tstmp[TIMESTAMP_MS_LEN];

	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	int stmplen = (msopt != 0) ? TIMESTAMP_MS_LEN : TIMESTAMP_LEN;

	if (msopt != 0)
		tstamp_msec(tstmp, stmplen);
	else
		tstamp_sec(tstmp, stmplen);

	printf("[%-*s] %s\n", stmplen, tstmp, msg);
}


/* TODO
 * items, fidq, nametb를 파일에서 로드 & 파일로 내리기
 */
static int
init_inven_cache(size_t max_item, size_t bnum)
{
	g_inven_cache.capacity = max_item;

	g_inven_cache.items = (struct inven_item *) malloc(max_item * sizeof(struct inven_item));
	if (NULL == g_inven_cache.items) {
		timestamp(MSEC, "Failed to initialize item array.");
		return -1;
	}
	g_inven_cache.fidq = init_queue(max_item, sizeof(int));
	for (int i = 0; i < max_item; i++) {
		snprintf(g_inven_cache.items[i].status,
				sizeof(g_inven_cache.items[i].status),
				"%d", ITEM_STAT_DELETED);
		enqueue(g_inven_cache.fidq, &i);
	}

	if (NULL == g_inven_cache.fidq) {
		timestamp(MSEC, "Failed to initialize fidq.");
		free(g_inven_cache.items);
		return -1;
	}
	g_inven_cache.nametb = init_hashmap(bnum);
	if (NULL == g_inven_cache.nametb) {
		timestamp(MSEC, "Failed to initialize hashmap.");
		free(g_inven_cache.items);
		destruct_queue(g_inven_cache.fidq);
		return -1;
	}
	g_inven_cache.ilock = (pthread_rwlock_t *) malloc(max_item * sizeof(pthread_rwlock_t));
	if (NULL == g_inven_cache.ilock) {
		timestamp(MSEC, "Failed to initialize ilock.");
		free(g_inven_cache.items);
		destruct_queue(g_inven_cache.fidq);
		free(g_inven_cache.ilock);
		return -1;
	}
	for (int i = 0; i < max_item; i++)
		pthread_rwlock_init(&g_inven_cache.ilock[i], NULL);

	timestamp(MSEC, "[init_inven_cache] successed.");
	return 0;
}

static int
init_session_workers(size_t wpool_size)
{
	g_sworkerid_queue = init_queue(wpool_size, sizeof(int));
	if (NULL == g_sworkerid_queue) {
		timestamp(MSEC, "Failed to initialize g_sworkerid_queue.");
		return -1;
	}
	g_sworker_pool = (struct worker *) malloc(wpool_size * sizeof(struct worker));
	if (NULL == g_sworker_pool) {
		timestamp(MSEC, "Failed to initialize g_sworker_pool.");
		destruct_queue(g_sworkerid_queue);
		return -1;
	}

	for (int i = 0; i < wpool_size; i++) {
		g_sworker_pool[i].wid = i;
		if (pipe(g_sworker_pool[i].pipefd) < 0)
			goto create_worker_failed;
		if (pipe(g_sworker_pool[i].dpipefd) < 0)
			goto create_worker_failed;
		/*
		if (fcntl(g_sworker_pool[i].pipefd[0], F_SETFL, O_NONBLOCK) < 0)
			goto create_worker_failed;
		if (fcntl(g_sworker_pool[i].pipefd[1], F_SETFL, O_NONBLOCK) < 0)
			goto create_worker_failed;
			*/
		if (enqueue(g_sworkerid_queue, &i) < 0) {
			close(g_sworker_pool[i].pipefd[0]);
			close(g_sworker_pool[i].pipefd[1]);
			goto create_worker_failed;
		}
		if (pthread_create(&g_sworker_pool[i].tid, NULL, session_worker_routine, &g_sworker_pool[i]) < 0) {
			close(g_sworker_pool[i].pipefd[0]);
			close(g_sworker_pool[i].pipefd[1]);
			goto create_worker_failed;
		}
	}

	timestamp(MSEC, "[init_session_workers] successed(%d).", g_sworkerid_queue->count);
	return 0;

create_worker_failed:
	destruct_queue(g_sworkerid_queue);
	free(g_sworker_pool);
	timestamp(MSEC, "Failed to create worker instances.");
	return -1;
}

static int
init_server_instance(size_t max_item, size_t bucknum, size_t max_worker)
{
	g_running = 1;

	if (init_session_workers(max_worker) < 0)
		return -1;
	if (init_inven_cache(max_item, bucknum) < 0) {
		timestamp(MSEC, "Failed to initialize g_inven_cache.");
		destruct_queue(g_sworkerid_queue);
		free(g_sworker_pool);
		return -1;
	}
	
	timestamp(MSEC, "[init_server_instance] successed.");

	return 0;
}

static int 
init_portno(int argc, const char **argv)
{
	int portno = DEFAULT_SERVER_PORT;
	if (argc > 1) {
		int input = atoi(argv[CLI_ARGS_IDX_PORTNO]);
		if (input >= 1024 && input <= 49151)
			portno = input;
	}
	return portno;
}

static int 
register_event(int sockfd, enum EVENT_TYPE ch, uint32_t events)
{
	struct event *event = (struct event *) malloc(sizeof (struct event));
	if (NULL == event)
		return ERR_MALLOC;
	event->sockfd = sockfd;
	event->type = ch;

	struct epoll_event ev;
	ev.events = events;
	ev.data.ptr = event;
	if (epoll_ctl(g_epollfd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
		return ERR_EPOLL_CTL;
	return 0;
}

static int 
remove_event(struct event *event)
{
	if (epoll_ctl(g_epollfd, EPOLL_CTL_DEL, event->sockfd, NULL) < 0)
		return ERR_EPOLL_CTL;

	free(event);
	g_session_count--;

	return 0;
}

static int
create_new_session(struct event *event)
{
	// Accept new connection.
	struct sockaddr_in claddr;
	socklen_t addrlen = sizeof(claddr);
	int clsock = accept(event->sockfd, (struct sockaddr *)&claddr, &addrlen);
	if (clsock < 0)
		return ERR_ACCEPT;

	// Refuse connection.
	if (g_session_count == MAX_SESSIONS) {
		g_refused_count++;
		close(clsock);
		return 0;
	}

	// Register a new event to the g_epollfd.
	if (register_event(clsock, EVENT_SERVICE_REQUEST, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT) < 0) {
		close(clsock);
		return ERR_EPOLL_CTL;
	}

	g_session_count++;

	timestamp(MSEC, "\033[32mClient[%d] %s:%d connected(%d)\033[0m", clsock, inet_ntoa(claddr.sin_addr), ntohs(claddr.sin_port), g_session_count);

	return 0;
}

/* 
 * EPOLLONESHOT 재활성화
 */
static void 
reactivate_oneshot_event(struct event *event)
{
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLONESHOT;
	ev.data.ptr = event;
	epoll_ctl(g_epollfd, EPOLL_CTL_MOD, event->sockfd, &ev);
}

static int
assign_worker(struct event *event)
{
	int wid = 0;
	// No free worker thread.
	if (dequeue(g_sworkerid_queue, &wid) < 0) {
		reactivate_oneshot_event(event);
		return -1;
	}
	// Free worker exists.
	g_sworker_pool[wid].event = event;
	if (write(g_sworker_pool[wid].pipefd[1], &event->sockfd, sizeof(int)) < 0)
		perror("[write] pipe ");
	
	return 0;
}

static int
reap_worker(struct event *event)
{
	struct task_cmpl_msg msg;
	read(event->sockfd, &msg, sizeof(struct task_cmpl_msg));

	reactivate_oneshot_event(g_sworker_pool[msg.wid].event);
	/*
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
	ev.data.ptr = g_sworker_pool[msg.wid].event;
	if (epoll_ctl(g_epollfd, EPOLL_CTL_MOD, msg.clsock, &ev) < 0)
		perror("[reap_worker] [epoll_ctl]");
	*/
	if (enqueue(g_sworkerid_queue, &msg.wid) < 0) {
		timestamp(MSEC, "[reap_worker] [enqueue]");
		return -1;
	}

	timestamp(MSEC, "[reap_worker] [worker %d] [client %d]", msg.wid, msg.clsock);

	return 0;
}

static int
destruct_session(struct event *event)
{
	g_session_count--;
	timestamp(MSEC, "Client[%d] disconnected(%d)", 
			event->sockfd, g_session_count);
	close(event->sockfd);
	remove_event(event);
	return 0;
}

static void *
session_worker_routine(void *p)
{
	struct worker *winfo = (struct worker *)p;
	int readpipe = winfo->pipefd[0];
	int wrpipe = winfo->pipefd[1];
	fd_set rfds;
	int clsock = 0;
	int result = 0;

	while(g_running) {
		FD_ZERO(&rfds);
		FD_SET(readpipe, &rfds);
		result = select(readpipe + 1, &rfds, NULL, NULL, NULL);
		// Read a client sockfd from main thread.
		if(result > 0 && FD_ISSET(readpipe, &rfds)) {
			// timestamp(MSEC, "[pipe %d] detected", readpipe);
			ssize_t nbytes = read(readpipe, &clsock, sizeof(int));
			// Pipe closed.
			if (nbytes == 0)
				break;
			else if (nbytes < 0) {
				perror("[session_worker_routine] [read]");
				continue;
			} else { 
				timestamp(MSEC, "[session_worker_routine] [worker %d] [pipe %d] [client %d]",
						winfo->wid, winfo->pipefd[0], clsock);
				handle_request(clsock);
				/*
				 * 메인 스레드에게 자신의 작업이 종료되었음을 알리는 방법.
				 * 1. worker가 직접 g_sworkerid_queue에 자신의 wid를 삽입한다.
				 * 		g_workerid_queue에서 한 번, epoll에 다시 sockfd를 등록하는 과정에서 한 번 경합 발생
				 * 2. worker가 읽기와 쓰기 전용 파이프를 두 개 가진다
				 * 		worker 한 개당 4개의 소켓을 가진다. 경합은 발생하지 않는다.
				 */
				struct task_cmpl_msg msg;
				msg.wid = winfo->wid;
				msg.clsock = clsock;
				write(winfo->dpipefd[1], &msg, sizeof(struct task_cmpl_msg));
			}
		}
	}
	return NULL;
}

// TODO
static int
handle_request(int clsock)
{
	struct svc_req req;

	// Receive svc_req
	if (recv(clsock, &req, sizeof(struct svc_req), 0) < 0) {
		timestamp(MSEC, "[%s] [handle_request] [recv] client %d", clsock);
		return -1;
	}

	if (SVC_UPLOAD == atoi(req.type))
		return server_upload_service(clsock, &req);
	else if (SVC_INQUIRY == atoi(req.type))
		return server_inquiry_service(clsock, MAX_FILE_ITEMS, &req);

	return 0;
}

static int
init_server_socket(int portno) 
{
	// Create & set socket address.
	listener_fd = create_tcpsock();
	if (listener_fd < 0) {
		perror(sockutil_errstr(listener_fd));
		return ERR_SOCKUTIL;
	}
	set_sockaddr_in(NULL, portno, &serv_addr);

	// Binding socket address to socket file.
	if (bind(listener_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("[bind]");
		close(listener_fd);
		return ERR_SOCKUTIL;
	}

#ifdef _DEBUG_
	timestamp(MSEC, "[address] %s:%d", 
			inet_ntoa(serv_addr.sin_addr), portno);
#endif // _DEBUG_

	if (listen(listener_fd, MAX_SESSIONS) < 0) {
		perror("[listen]");
		return ERR_LISTEN;
	}

	set_sock_nonblock(listener_fd);

	return 0;
}

int
handle_events()
{
	struct epoll_event events[FD_SETSIZE];
	int nready;
	enum EVENT_TYPE event_type;
	struct event *event;

	while(1) {
		nready = epoll_wait(g_epollfd, events, FD_SETSIZE, -1);
		if (nready < 0) {
			perror("[epoll_wait]");
			return 1;
		}
		for (int i = 0; i < nready; i++) {
			event = (struct event *) events[i].data.ptr;
			event_type = event->type;
			// 연결 종료.
			if (events[i].events & EPOLLRDHUP) {
				destruct_session(event);
			} else if (events[i].events & EPOLLHUP) {
				destruct_session(event);
			} else if (EVENT_NEW_SESSION == event_type) {
				// 클라이언트 접속 이벤트.
				create_new_session(event);
			} else if (EVENT_SERVICE_REQUEST == event_type){
				// 세션을 형성한 클라이언트의 요청 처리.
				assign_worker(event);
			} else if (EVENT_WORKER_MSG == event_type) {
				// worker thread와의 통신.
				reap_worker(event);
			}
		}
	}

	return 0;
}

static void
register_worker_events(int wpool_size)
{
	for (int i = 0; i < wpool_size; i++) {
		register_event(g_sworker_pool[i].dpipefd[0], EVENT_WORKER_MSG, EPOLLIN);
	}
}

int
main (int argc, const char *argv[])
{
	int portno = init_portno(argc, argv);

	if (init_server_instance(MAX_FILE_ITEMS, HASHMAP_BUCKET_NUM, SESSION_WORKER_NUM) < 0) 
		return 1;

	if (init_server_socket(portno) < 0)
		return 1;

	g_epollfd = epoll_create(1);
	if (g_epollfd < 0) {
		perror("[epoll_create]");
		return 1;
	}

	register_event(listener_fd, EVENT_NEW_SESSION, 
			EPOLLIN | EPOLLRDHUP);
	register_worker_events(SESSION_WORKER_NUM);

	handle_events();

	return 0;
}
