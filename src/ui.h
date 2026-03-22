#ifndef UI_H
#define UI_H

#include <time.h>
#include "menu.h"

enum UIMode { MODE_NORMAL, MODE_COMMAND, MODE_SEARCH };

typedef struct {
    WINDOW *win;
    MenuState menu;
    int running;
    void (*refresh_all)(MenuState *state);
    char notification[128];
    time_t notify_time;
    enum UIMode mode;
    char cmdbuf[128];
    int cmdpos;
    char pending_key;  /* for multi-key sequences like gg */
    int repeat_count;  /* for number prefixes like 5j */
} UIState;

/* Global pointer so modules can send notifications */
extern UIState *g_ui;

void ui_init(UIState *ui, MenuItem *root);
void ui_loop(UIState *ui);
void ui_cleanup(UIState *ui);
void ui_notify(const char *msg);

int ui_input_popup(const char *title, const char *prompt, char *buf, size_t size, int password);

#endif
