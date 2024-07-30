#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

#define STDIN_FILENO 				0
#define COLUMN_MAX					90
#define ROW_MAX						30
#define CONTENT_START_ROW			7

typedef struct _screen_buffer {
	char				logo[CONTENT_START_ROW - 1][COLUMN_MAX];
	char				usage[COLUMN_MAX];
	pthread_mutex_t		mutex;
	char 				items[ROW_MAX - CONTENT_START_ROW][COLUMN_MAX];
	int					item_cursor;
	int 				item_num;
	int 				window_columns;
	int 				window_rows;
	int					flushed;
} screen_buffer_t;

typedef enum TTY_STATE {
	RESET = 0,
	CBREAK
} TTY_STATE_E;

typedef enum P_STATE {
	START = 0,
	INPUT,
	PROCESSING,
	EXIT
} P_STATE_E;

static struct termios 		save_termios;
static int					ttysavefd = -1;
static screen_buffer_t 		g_screen_buffer;
static P_STATE_E			g_pstate = START;
static TTY_STATE_E 			ttystate = RESET;

void
hide_cursor(void) 
{
	printf("\033[?25l");
}

void
show_cursor(void)
{
	printf("\033[?25h");
}

static void
get_winsize(int fd) 
{
	struct winsize size;
	if (ioctl(fd, TIOCGWINSZ, (char*) &size) < 0)
		perror("TIOCGWINSZ");
	g_screen_buffer.window_rows = size.ws_row;
	g_screen_buffer.window_columns = size.ws_col;
}

static void
sig_winch(int signo)
{
	pthread_mutex_lock(&g_screen_buffer.mutex);
	get_winsize(STDIN_FILENO);
	g_screen_buffer.flushed = 0;
	pthread_mutex_unlock(&g_screen_buffer.mutex);
}

void
print_errmsg(const char* msg)
{
	printf("\n\033[31m[ERROR]\033[0m\n");
	printf("[%s] %s\n", __func__, msg);
}

/*
 * Set terminal to cbreak mode.
 * Read 1-byte at once, no timer.
 */
int tty_cbreak(int fd)
{
	int	err;
	struct termios buf;
	
	if (ttystate != RESET) {
		print_errmsg("ttystate is invalid");
		return (-1);
	}
	if (tcgetattr(fd, &buf) < 0) {
		print_errmsg("tcgetattr");
		return (-1);
	}
	save_termios = buf;

	// Turn off echo mode.
	buf.c_lflag &= ~(ECHO | ICANON);

	// Read 1-byte at once, no timeout.
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &buf) < 0) {
		print_errmsg("tcsetattr");
		return (-1);
	}

	ttystate = CBREAK;
	ttysavefd = fd;
	return (0);
}

int
tty_reset(int fd)
{
	if (ttystate == RESET)
		return (0);
	if (tcsetattr(fd, TCSAFLUSH, &save_termios) < 0) {
		print_errmsg("tcsetattr");
		return (-1);
	}
	ttystate = RESET;
	return (0);
}

static void
destruct_process(void)
{

}


#define SCREEN_WORKER			0

/*
 * Dependencies: g_screen_buffer.
 */
static void
flush_buffer(screen_buffer_t* buf)
{
	printf("\033[2J\033[H");
	if (buf->window_columns < 60)
		printf("T-storage  ver.0.0.1\n");
	else {
		for(int i = 0; i < 6; i++)
			printf("%-*s\n", buf->window_columns, buf->logo[i]);
	}
	printf("%-*s\n", buf->window_columns, buf->usage);
	for (int i = 0; i < buf->item_num; i++) {
		if (i == buf->item_cursor)
			printf("\033[1;48;5;7;38;5;0m%-*s\033[0m\n", buf->window_columns, buf->items[i]);
		else
			printf("%-*s\n", buf->window_columns, buf->items[i]);
	}
}

/*
 * Dependencies: g_pstate, g_screen_buffer.
 */
static void*
screen_worker(void* p)
{
	hide_cursor();
	get_winsize(STDIN_FILENO);
	tty_cbreak(STDIN_FILENO);

	g_screen_buffer.item_cursor = 0;

	while (g_pstate != EXIT) {
		pthread_mutex_lock(&g_screen_buffer.mutex);
		/* Critical section start. */
		if (g_screen_buffer.flushed == 0) {
			flush_buffer(&g_screen_buffer);
			g_screen_buffer.flushed = 1;
		}
		/* Critical section end. */
		pthread_mutex_unlock(&g_screen_buffer.mutex);
	}

	return NULL;
}

/*
 * Dependencies: g_pstate, g_screen_buffer.
 */
static void
move_cursor(int diff)
{
	pthread_mutex_lock(&g_screen_buffer.mutex);
	/* Critical section start. */
	if (g_screen_buffer.flushed == 1) {
		g_screen_buffer.item_cursor += diff;
		if (g_screen_buffer.item_cursor < 0)
			g_screen_buffer.item_cursor = g_screen_buffer.item_num - 1;
		if (g_screen_buffer.item_cursor > g_screen_buffer.item_num - 1)
			g_screen_buffer.item_cursor = 0;
		g_screen_buffer.flushed = 0;
	}
	/* Critical section end. */
	pthread_mutex_unlock(&g_screen_buffer.mutex);
}



int
main(int argc, const char* argv[])
{
	signal(SIGWINCH, sig_winch);
	// signal(SIGINT, SIG_IGN);

	strcpy(g_screen_buffer.logo[0], " ______        _");
	strcpy(g_screen_buffer.logo[1], "/_   _/    ___| |_ ___  _ __ __ _  __ _  ___");
	strcpy(g_screen_buffer.logo[2], "  | | ___ / __| __/ _ \\| '__/ _` |/ _` |/ _ \\");
	strcpy(g_screen_buffer.logo[3], "  | |/___/\\__ \\ || (_) | | | (_| | (_| |  __/");
	strcpy(g_screen_buffer.logo[4], "  |_|     |___/\\__\\___/|_|  \\__,_|\\__, |\\___|     ver.0.0.1");
	strcpy(g_screen_buffer.logo[5], "  		                  |___/");
	strcpy(g_screen_buffer.usage, "j: down, k: up, o: select, q: quit");
	pthread_mutex_init(&g_screen_buffer.mutex, NULL);
	g_screen_buffer.item_cursor = 0;
	strcpy(g_screen_buffer.items[0], "Upload");
	strcpy(g_screen_buffer.items[1], "Download");
	g_screen_buffer.item_num = 2;
	g_screen_buffer.flushed = 0;
	
	pthread_t workers[4];
	pthread_create(&workers[SCREEN_WORKER], NULL, screen_worker, NULL);

	int n = 0;
	char cmd;
	while ((n = read(STDIN_FILENO, &cmd, 1)) == 1) {
		cmd &= 255;
		switch (cmd) {
			case 'j':
				move_cursor(-1);
				break;
			case 'k':
				move_cursor(1);
				break;
			case 'q':
				tty_reset(STDIN_FILENO);
				show_cursor();
				return 0;
			default:
				break;
		}
		n = 0;
	}

	return 0;
}
