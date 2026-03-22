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

/* Popup input dialog. Returns 1 if user confirmed, 0 if cancelled (ESC).
   Result written to buf. If password=1, input is masked with '*'. */
int ui_input_popup(const char *title, const char *prompt, char *buf, size_t size, int password);

#endif
