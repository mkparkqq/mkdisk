/*
 * 2024/07/02
 * Minkeun Park
*/

#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "module/service.h"

#include <pthread.h>
#include <termios.h>

#define STDIN_FILENO 				0

#define OPT_ITEM_MAX				10000
#define WIN_COLUMN_MAX				90
#define WIN_ROW_MAX					30

#define LOGO_ROW_NUM				4

typedef unsigned int nnum;		// Natural number.

enum STAT_BAR {
	STAT_BAR_HIGHLIGHT = 0,
	STAT_BAR_NORMAL,
	STAT_BAR_ROW_NUM
};

enum TTY_MODE {
	TTY_MODE_DEFAULT = 0,
	TTY_MODE_CBREAK,
	TTY_MODE_NUM
};

enum SCREEN_ID {
	SCREEN_START = 0,
	SCREEN_UPLOAD,
	SCREEN_DOWNLOAD,
	SCREEN_PROCESSING,
	SCREEN_RENAME,
};

enum CURSOR_DIRECTION {
	CURSOR_UP = -1,
	CURSOR_DOWN = 1
};

enum LAST_TX {
	TX_SUCCESSED,
	TX_FAILED
};

struct window {
	nnum row;
	nnum column;
};

struct scroll_window {
	nnum sindex;
	nnum size;
	nnum cursor;
};

struct asset {
	char logo[LOGO_ROW_NUM][WIN_COLUMN_MAX];
	char logo_sm[LOGO_ROW_NUM][WIN_COLUMN_MAX];
	nnum bwidth; // breakpoint
	char usage[WIN_COLUMN_MAX];
	char services[SVC_NUM][WIN_COLUMN_MAX];
};

struct dynamic_content {
	nnum item_num;
	char **opt_items;
	char status_bar[STAT_BAR_ROW_NUM][WIN_COLUMN_MAX];
};

struct client_status {
	enum TTY_MODE ttymode;
	struct termios ttycfg[TTY_MODE_NUM];
	struct window win;
	struct scroll_window swin;
	struct asset asset;
	struct dynamic_content dcontent;
	enum SCREEN_ID curr_screen;
	enum LAST_TX ltx;
	struct sockaddr_in sa;
};

static inline int
max(int a, int b) {
	return (a > b) ? a : b;
}

static inline int
min(int a, int b)
{
	return (a < b) ? a : b;
}

enum CLIENT_WORKER_ID {
	WORKER_ID_CMD = 0,
	WORKER_ID_SCREEN,
	CLIENT_WORKER_NUM
	// WORKER_ID_NET,
};

struct worker {
	pthread_t wid;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

struct cmd_worker_param {
	struct worker *screen_worker;
	struct worker *net_worker;
};

/* signal handlers */
static void sig_winch(int signo);
static void sig_exit(int signo);

/* threads */
static void *cmd_worker(void *p);
static void *screen_worker(void *p);

/* command handlers */
static void terminate_client();

#endif	//_CLIENT_H_
