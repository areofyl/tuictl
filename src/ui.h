#ifndef UI_H
#define UI_H

#include <time.h>
#include "menu.h"

typedef struct {
    WINDOW *win;
    MenuState menu;
    int running;
    void (*refresh_all)(MenuState *state);
    char notification[128];
    time_t notify_time;
} UIState;

/* Global pointer so modules can send notifications */
extern UIState *g_ui;

void ui_init(UIState *ui, MenuItem *root);
void ui_loop(UIState *ui);
void ui_cleanup(UIState *ui);
void ui_notify(const char *msg);

int ui_input_popup(const char *title, const char *prompt, char *buf, size_t size, int password);

#endif
