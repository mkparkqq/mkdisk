#ifndef _TERMUI_H_
#define _TERMUI_H_

#include "../client.h"
void hide_cursor(void);
void show_cursor(void);
int tty_cbreak(int);
int tty_default(int);

int init_termui();
void destruct_termui();
void flush_screen();
void move_cursor(enum CURSOR_DIRECTION d);
int get_winsize(int fd);
void refresh_screen();
void set_status_msg(enum STAT_BAR ch, const char *format, ...);

void load_start_screen(void);
void load_upload_screen(void);
void load_download_screen(void);
void load_delete_screen(void);
void load_rename_screen(void);

#endif // _TERMUI_H_
