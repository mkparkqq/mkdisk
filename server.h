#ifndef _SERVER_H_

#include "module/hashmap.h"
#include "module/queue.h"
#include "module/service.h"

#include <stdarg.h>

#define TIMESTAMP_MSEC_LEN			25
#define TIMESTAMP_SEC_LEN			20
#define MAX_FILE_ITEMS				10000
#define MAX_CONNECTIONS				1000
#define SESSION_WORKER_NUM			500
#define CLI_ARGS_IDX_PORTNO			1
#define DEFAULT_SERVER_PORT			23455
#define HASHMAP_BUCKET_NUM			100

#define MSEC						1

// 서버가 처리하는 이벤트 종류
enum EVENT_TYPE {
	EVENT_NEW_CONNECTION, 
	EVENT_SERVICE_REQUEST,
	EVENT_WORKER_MSG
};

enum ERR_CAUSE {
	ERR_MALLOC = -1,
	ERR_EPOLL_CTL = -2,
	ERR_EPOLL_WAIT = -3,
	ERR_ACCEPT = -4,
	ERR_SOCKUTIL = -5,
	ERR_LISTEN = -6
};

struct event {
	int sockfd;
	enum EVENT_TYPE type;
};

struct worker {
	int wid;
	pthread_t tid;
	int pipefd[2];
	int dpipefd[2];
	void *event;
};

struct task_cmpl_msg {
	int wid;
	int clsock;
};

struct inventory {
	size_t capacity;
	struct inven_item *items;	// struct inven_item 배열
	struct queue *fidq;			// items 배열의 빈 인덱스
	struct hashmap *nametb;		// file name -> file id 매핑 정보
	pthread_rwlock_t *ilock;	// items 보호
};

/**
 * @brief  timestamp 출력 함수
 *
 * @param msopt 1이면 ms단위로 출력. 0이면 s단위로 출력.
 * @param fmt 출력 형식 지정 문자열.
 * @param ...
 */
void timestamp(int msopt, const char *fmt, ...);

#endif // _SERVER_H_
