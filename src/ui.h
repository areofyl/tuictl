#ifndef UI_H
#define UI_H

#include "menu.h"

typedef struct {
    WINDOW *win;
    MenuState menu;
    int running;
    void (*refresh_all)(MenuState *state);
} UIState;

void ui_init(UIState *ui, MenuItem *root);
void ui_loop(UIState *ui);
void ui_cleanup(UIState *ui);

#endif
