#include "termui.h"
#include "service.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>

extern int g_quit;
extern struct client_status g_client_status;
extern struct inven_item *g_items;
extern struct worker g_client_workers[CLIENT_WORKER_NUM];

static char cbuf[WIN_COLUMN_MAX] = {'\0', };
static nnum cwidth;

static int
tty_backup(int fd)
{
	struct termios buf;
	if (g_client_status.ttymode != TTY_MODE_DEFAULT)
		return -1;
	if (tcgetattr(fd, &buf) < 0)
		return -1;
	g_client_status.ttycfg[TTY_MODE_DEFAULT] = buf;

	return 0;
}

int
tty_default(int fd)
{
	if (tcsetattr(fd, TCSAFLUSH, &g_client_status.ttycfg[TTY_MODE_DEFAULT]) < 0)
		return -1;
	g_client_status.ttymode = TTY_MODE_DEFAULT;

	return 0;
}

/*
 * Set terminal to cbreak mode.
 * Read 1-byte at once, no timer.
 */
int
tty_cbreak(int fd)
{
	struct termios buf;

	if (g_client_status.ttymode != TTY_MODE_DEFAULT)
		return -1;
	
	if (tcgetattr(fd, &buf) < 0)
		return -1;

	// Turn off echo.
	buf.c_lflag &= ~(ECHO | ICANON);
	// Read 1-byte at once, no timeout. 
	buf.c_cc[VMIN] = 1;
	buf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &buf) < 0)
		return -1;

	g_client_status.ttymode = TTY_MODE_CBREAK;
	g_client_status.ttycfg[TTY_MODE_CBREAK] = buf;

	return 0;
}

static void
init_static_contents()
{
    strncpy(g_client_status.asset.logo_sm[0], "MK-storage", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.logo_sm[1], "Client ver.0.0.1", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.logo_sm[2], "", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.logo_sm[3], "", WIN_COLUMN_MAX);

    // Initializing bwidth
    g_client_status.asset.bwidth = 71;

    // Initializing usage
    strncpy(g_client_status.asset.usage, "j:Down, k:Up, o:Select, q: exit, h: home, u: update file list", WIN_COLUMN_MAX);

    // Initializing services
    strncpy(g_client_status.asset.services[0], "Upload file", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.services[1], "Download file", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.services[2], "Rename file", WIN_COLUMN_MAX);
    strncpy(g_client_status.asset.services[3], "Delete file", WIN_COLUMN_MAX);
}

void
init_scroll_window_cursor()
{
	g_client_status.swin.sindex = 0;
	g_client_status.swin.cursor = 0;
}

static void
clip(char *cbuf, char *buf)
{
	cwidth = min(g_client_status.win.column, WIN_COLUMN_MAX);
	strncpy(cbuf, buf, WIN_COLUMN_MAX);
	cbuf[cwidth - 1] = '\0';
}

static void
clear_screen()
{
	printf("\033[2J\033[H");
}

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
print_logo_sm()
{
	for (int i = 0; i < LOGO_ROW_NUM; i++) {
		printf("%-*s\n", 
				g_client_status.win.column, g_client_status.asset.logo_sm[i]);
	}
}

static void
print_logo_area()
{
	print_logo_sm();
}

static void
print_usage()
{
	clip(cbuf, g_client_status.asset.usage);
	printf("\033[48;5;240m%-*s\033[0m\n", 
			g_client_status.win.column, cbuf);
}

static void
print_opt_items()
{
	nnum opt_rows = g_client_status.win.row - LOGO_ROW_NUM - STAT_BAR_ROW_NUM - 1;
	int s = g_client_status.swin.sindex;
	for (int i = 0; i < opt_rows; i++) {
		if (i >= g_client_status.dcontent.item_num)
			printf("\n");
		else {
			clip(cbuf, g_client_status.dcontent.opt_items[i + s]);
			if (g_client_status.swin.cursor == i + s)
				printf("\033[1;48;5;7;38;5;0m%-*s\033[0m\n", g_client_status.win.column, cbuf);
			else 
				printf("%s\n", cbuf);
		}
	}

}

static void
print_status_bar()
{
	for (int i = 0; i < STAT_BAR_ROW_NUM; i++) {
		clip(cbuf, g_client_status.dcontent.status_bar[i]);
		if (STAT_BAR_HIGHLIGHT == i)
			printf("\033[48;5;240m%-*s\033[0m\n", g_client_status.win.column, cbuf);
		else
			printf("%s", cbuf);
	}
	fflush(stdout);
}

int 
init_termui()
{
	hide_cursor();
	memset(&g_client_status, 0x00, sizeof(struct client_status));
	g_client_status.ttymode = TTY_MODE_DEFAULT;
	get_winsize(STDIN_FILENO);

	int result = 0;
	result |= tty_backup(STDIN_FILENO);
	result |= tty_cbreak(STDIN_FILENO);
	init_static_contents();

	g_client_status.dcontent.opt_items = (char **) malloc(OPT_ITEM_MAX * sizeof(char *));
	for (int i = 0; i < OPT_ITEM_MAX; i++)
		g_client_status.dcontent.opt_items[i] = (char *) malloc(WIN_COLUMN_MAX * sizeof(char));

	return ((0 == result) ? 0 : -1);
}

void 
destruct_termui()
{
	show_cursor();
	tty_default(STDIN_FILENO);
	if (NULL != g_client_status.dcontent.opt_items) {
		for (int i = 0; i < OPT_ITEM_MAX; i++)
			free(g_client_status.dcontent.opt_items[i]);
		free(g_client_status.dcontent.opt_items);
	}
}

void 
flush_screen() 
{
	clear_screen();
	if (g_client_status.win.row < LOGO_ROW_NUM + 2 + STAT_BAR_ROW_NUM || g_client_status.win.column < 30) {
		printf("window too small\n");
		return;
	}
	print_logo_area();
	print_usage();
	print_opt_items();
	print_status_bar();
}

void 
move_cursor(enum CURSOR_DIRECTION d)
{
	if (0 == g_client_status.dcontent.item_num)
		return;
	int diff = d * 1;
	struct scroll_window *swin = &g_client_status.swin;
	int next_cursor = ((int)swin->cursor) + diff;
	int swin_sidx = (int) swin->sindex;
	int swin_size = (int) swin->size;
	int items = (int) g_client_status.dcontent.item_num;

	// Case1. cursor exceed window's lower bound.
	if (next_cursor >= swin_sidx + swin_size || next_cursor >= items) {
		if (items <= swin_size)
			swin->cursor = items - 1;
		else {
			swin->sindex = min(swin_sidx + diff, items - swin_size);
			swin->cursor = min(next_cursor, items - 1);
		}
	} // Case2. cursor exceed window's upper bound.
	else if (next_cursor < swin_sidx || next_cursor < 0) {
		if (next_cursor < 0)
			swin->cursor = 0;
		else {
			swin->sindex = max(0, swin_sidx + diff);
			swin->cursor = max(0, next_cursor);
		}
	} // Case3. cursor inside the window.
	else
		swin->cursor = next_cursor;

#ifdef _DEBUG_
	set_status_msg(STAT_BAR_NORMAL, "swin_size: %d swin_sidx: %d cursor: %d total items: %d", 
			swin->size,
			swin->sindex, 
			swin->cursor,
			g_client_status.dcontent.item_num);
#endif
}

int 
get_winsize(int fd)
{
	struct winsize size;
	if (ioctl(fd, TIOCGWINSZ, (char*) &size) < 0)
		return -1;
	g_client_status.win.row = size.ws_row;
	g_client_status.win.column = size.ws_col;
	g_client_status.swin.size = g_client_status.win.row - LOGO_ROW_NUM - STAT_BAR_ROW_NUM - 1;
	init_scroll_window_cursor();
	return 0;
}

void 
refresh_screen()
{
	flush_screen();
}

void 
set_status_msg(enum STAT_BAR ch, const char *format, ...)
{
	for (int i = 0; i < STAT_BAR_ROW_NUM; i++)
		memset(g_client_status.dcontent.status_bar[i], '\0', WIN_COLUMN_MAX);
	va_list args;
	va_start(args, format);

	char msg[WIN_COLUMN_MAX] = {0x00, };
	vsnprintf(msg, WIN_COLUMN_MAX, format, args);
	snprintf(g_client_status.dcontent.status_bar[ch], WIN_COLUMN_MAX, "%s", msg);
}

void 
load_start_screen(void)
{
	g_client_status.swin.cursor = 0;
	g_client_status.swin.sindex = 0;
	g_client_status.dcontent.item_num = 2;
	for (int i = 0; i < g_client_status.dcontent.item_num; i++)
		strncpy(g_client_status.dcontent.opt_items[i], g_client_status.asset.services[i], WIN_COLUMN_MAX);
}

void 
load_upload_screen(void)
{
	g_client_status.swin.cursor = 0;
	set_status_msg(STAT_BAR_HIGHLIGHT, "Enter the file path");
	g_client_status.dcontent.item_num = 0;
	show_cursor();
	tty_default(STDIN_FILENO);
}

void 
load_download_screen(void)
{
	g_client_status.swin.cursor = 0;
	char *alv = NULL;
	int idx = 0;
	for (int i = 0; i < g_client_status.dcontent.item_num; i++) {
		if (atoi(g_items[i].status) != ITEM_STAT_AVAILABLE) {
			strncpy(g_client_status.dcontent.opt_items[idx], "", WIN_COLUMN_MAX);
			continue;
		}
		if (atoi(g_items[i].alv) == PUBLIC_ACCES)
			alv = "PUBLIC";
		else
			alv = "PRIVATE";
		snprintf(g_client_status.dcontent.opt_items[idx],
				WIN_COLUMN_MAX,
				"%-*s %-*s %-*s %-*s",
				IP_ADDRESS_LEN,
				g_items[i].creator,
				FILE_NAME_LEN - 1,
				g_items[i].fname,
				REQ_FLEN_LEN,
				g_items[i].flen,
				7,
				alv
				/*
				TIMESTAMP_LEN,
				g_items[i].last_modified
				*/
				);
		idx++;
	}
	g_client_status.dcontent.item_num = idx;
	set_status_msg(STAT_BAR_HIGHLIGHT, "Select file to download");
}

void 
load_delete_screen(void)
{

}

void 
load_rename_screen(void)
{

}
