/*
 * 2024/07/02
 * Minkeun Park
 */

#include "client.h"
#include "module/service.h"
#include "module/termui.h"
#include "module/sockutil.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

int g_quit = 0;
static int g_servsock;
struct client_status g_client_status;
struct inven_item *g_items;
struct worker g_client_workers[CLIENT_WORKER_NUM];

static void
sig_winch(int signo)
{
	get_winsize(STDIN_FILENO);
	g_client_status.swin.size = g_client_status.win.row - LOGO_ROW_NUM - STAT_BAR_ROW_NUM - 1;
	set_status_msg(STAT_BAR_NORMAL, "%d X %d", g_client_status.win.column, g_client_status.win.row);
	flush_screen();
}

static void
sig_exit(int signo)
{
	terminate_client();
}

static void 
terminate_client()
{
	close(g_servsock);
	g_quit = 1;
	set_status_msg(STAT_BAR_HIGHLIGHT, "Goodbye");
	refresh_screen();
	usleep(1000 * 100);			// g_client_status.dcontent.opt_item이 free되기 전에 screen_worker가 화면을 업데이트하도록 기다림.
	destruct_termui();
	printf("\n");
}

static void*
screen_worker(void *p)
{
	while (!g_quit) {
		pthread_mutex_lock(&g_client_workers[WORKER_ID_SCREEN].mutex);
		pthread_cond_wait(&g_client_workers[WORKER_ID_SCREEN].cond, &g_client_workers[WORKER_ID_SCREEN].mutex);
		flush_screen();
		pthread_mutex_unlock(&g_client_workers[WORKER_ID_SCREEN].mutex);
	}

	return NULL;
}

static void*
print_pbar(void* p) 
{
	struct trans_stat *stat = (struct trans_stat *)p;
	double progress = 0.0;
	char bar[WIN_COLUMN_MAX] = {'\0', };

	printf("\033[?25l");
	while (1) {
		fflush(stdout);
		if (stat->transmitted < 0)
			return NULL;
		if (0 == stat->total)
			continue;
		progress = (double) stat->transmitted / (double) stat->total * 100.0F;
		printf("\033[G[%3.f%%]", progress);
		int barlen = (g_client_status.win.column - 5) * progress / 100;
		for (int i = 0; i < barlen; i++)
			printf("#");
		fflush(stdout);
		if (stat->total == stat->transmitted)
			break;
	}
	// progress = (double) stat->transmitted / (double) stat->total * 100.0F;
	flush_screen();
	// refresh_screen();
	return NULL;
}


static void
upload_service()
{
	char file_path[256];
	char alv;
	while (1) {
		fgets(file_path, 255, stdin);
		file_path[strcspn(file_path, "\n")] = '\0';
		if (file_exists(file_path))
			break;
		set_status_msg(STAT_BAR_HIGHLIGHT, "No such file exists. Enter the file path.");
		refresh_screen();
		usleep(1000 * 100);
	}
	
	set_status_msg(STAT_BAR_HIGHLIGHT, "public: 0, private: 1");
	refresh_screen();
	usleep(1000 * 100);
	while (1) {
		alv = fgetc(stdin) - '0';
		if (alv == PUBLIC_ACCES || alv == PRIVATE_ACCESS)
			break;
		refresh_screen();
		usleep(1000 * 100);
	}

	hide_cursor();
	tty_cbreak(STDIN_FILENO);
	refresh_screen();
	
	// Send request.
	int64_t flen = sizeof_file(file_path);
	struct trans_stat rate = { 0, 0 };
	pthread_t pbar_worker = 0;
	pthread_create(&pbar_worker, NULL, print_pbar, (void *)&rate);

	int result = client_upload_service(g_servsock, file_path, flen, alv, &rate);

	pthread_join(pbar_worker, NULL);

	g_client_status.curr_screen = SCREEN_START;
	load_start_screen();

	if (result < 0) {
		set_status_msg(STAT_BAR_HIGHLIGHT, svc_errstr());
		refresh_screen();
		return;
	}

	set_status_msg(STAT_BAR_HIGHLIGHT, "File transfer successful.");
	refresh_screen();
}

static int
fetch_file_inven()
{
	int ret = 0;
	struct trans_stat rate = { 0, 0 };
	pthread_t pbar_worker = 0;
	pthread_create(&pbar_worker, NULL, print_pbar, (void *)&rate);

	if (client_inquiry_service(g_servsock, &rate) < 0) {
		set_status_msg(STAT_BAR_HIGHLIGHT, svc_errstr());
		refresh_screen();
		ret = -1;
	}

	pthread_join(pbar_worker, NULL);

	return ret;
}

static void
handle_enter_cmd()
{
	if (SCREEN_START == g_client_status.curr_screen) {
		if (SVC_UPLOAD == g_client_status.swin.cursor) {
			load_upload_screen();
			refresh_screen();
			g_client_status.curr_screen = SCREEN_UPLOAD;
			upload_service();
		}
		else if (SVC_DOWNLOAD == g_client_status.swin.cursor) {
			if (fetch_file_inven() < 0)
				set_status_msg(STAT_BAR_HIGHLIGHT, svc_errstr());
			else
				load_download_screen();
				g_client_status.curr_screen = SCREEN_DOWNLOAD;
			refresh_screen();
		}
		else if (SVC_RENAME == g_client_status.swin.cursor)
			load_rename_screen();
		else if (SVC_DELETE == g_client_status.swin.cursor)
			load_delete_screen();
	} else if (SCREEN_DOWNLOAD == g_client_status.curr_screen) {
		// TODO
		// download_service(g_items[g_client_status.swin.curosr].fname);
	}
}

static void*
cmd_worker(void *p)
{
	int n = 0;
	char cmd;
	while (((n = read(STDIN_FILENO, &cmd, 1)) == 1)) {
		cmd &= 255;
		switch (cmd) {
			case 'j':
				move_cursor(CURSOR_DOWN);
				refresh_screen();
				break;
			case 'k':
				move_cursor(CURSOR_UP);
				refresh_screen();
				break;
			case 'o':
				handle_enter_cmd();
				break;
			case 'u':
				// fetch_file_inven();
				refresh_screen();
				return NULL;
			case 'q':
				terminate_client();
				return NULL;
			case 'h':
				g_client_status.curr_screen = SCREEN_START;
				load_start_screen();
				refresh_screen();
			default:
				break;
		}
		n = 0;
	}

}

static void
init_sighandler()
{
	if (signal(SIGWINCH, sig_winch) < 0)
		set_status_msg(STAT_BAR_NORMAL, "[Error] [signal] SIGWINCH");
	/*
	if (signal(SIGSEGV, sig_exit))
		set_status_msg(STAT_BAR_NORMAL, "[Error] [signal] SIGSEGV");
	if (signal(SIGINT, sig_exit))
		set_status_msg(STAT_BAR_NORMAL, "[Error] [signal] SIGINT");
	refresh_screen();
	*/
}

static int
init_client_workers()
{
	for (int i = 0; i < CLIENT_WORKER_NUM; i++) {
		pthread_mutex_init(&g_client_workers[i].mutex, NULL);
		pthread_cond_init(&g_client_workers[i].cond, NULL);
	}

	int result = 0;
	result |= pthread_create(&g_client_workers[WORKER_ID_SCREEN].wid, NULL, screen_worker, NULL);
	result |= pthread_create(&g_client_workers[WORKER_ID_CMD].wid, NULL, cmd_worker, NULL);
	if (0 != result) {
		printf("[init_client_workers]");
		return -1;
	}
	
	flush_screen();
}

/*
 * ./client [server ip] [port number]
 */
static int
check_usage(int argc, const char *argv[])
{
	char usage[64] = {0, };
	snprintf(usage, 64, "Usage: %s [server ip] [port number]\n", argv[0]);

	if (3 != argc) {
		fputs(usage, stdout);
		return -1;
	}
	int port = strtol(argv[2], NULL, 10);
	if (errno == EINVAL) {
		fputs(usage, stdout);
		return -1;
	}
	return 0;
}


int
main(int argc, const char* argv[]) 
{
	if (check_usage(argc, argv) < 0)
		return 1;

	// Connect to the server
	g_servsock = create_tcpsock();
	if ( g_servsock < 0) {
		printf("\033[31m[socket]\033[0m\n");
		return 1;
	}

	struct sockaddr_in sa;
	set_sockaddr_in(argv[1], atoi(argv[2]), &sa);
	if (connect(g_servsock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		printf("Failed to connect\n");
		return 1;
	}

	if (init_termui() < 0)
		return 1;

	load_start_screen();

	init_sighandler();

	set_status_msg(STAT_BAR_HIGHLIGHT, "connected");
	refresh_screen();


	if (init_client_workers() < 0)
		return 1;

	for (int i = 0; i < CLIENT_WORKER_NUM; i++)
		pthread_join(g_client_workers[i].wid, NULL);

	for (int i = 0; i < CLIENT_WORKER_NUM; i++) {
		pthread_mutex_destroy(&g_client_workers[i].mutex);
		pthread_cond_destroy(&g_client_workers[i].cond);
	}

	return 0;
}

