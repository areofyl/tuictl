#include <locale.h>
#include "ui.h"

void ui_init(UIState *ui, MenuItem *root) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Colors */
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);    /* header */
    init_pair(2, COLOR_BLACK, COLOR_CYAN); /* selected */
    init_pair(3, COLOR_WHITE, -1);   /* status */
    init_pair(4, COLOR_GREEN, -1);   /* toggle on */
    init_pair(5, COLOR_RED, -1);     /* toggle off */

    ui->win = stdscr;
    ui->running = 1;
    ui->menu.root = root;
    ui->menu.current_menu = root;
    ui->menu.cursor = 0;
    ui->menu.scroll_offset = 0;
    ui->refresh_all = NULL;
}

void ui_loop(UIState *ui) {
    while (ui->running) {
        menu_render(&ui->menu, ui->win);
        int ch = wgetch(ui->win);

        switch (ch) {
        case 'k':
        case KEY_UP:
            menu_cursor_up(&ui->menu);
            break;
        case 'j':
        case KEY_DOWN:
            menu_cursor_down(&ui->menu);
            break;
        case 'l':
        case KEY_RIGHT:
        case '\n':
        case KEY_ENTER:
            menu_activate(&ui->menu);
            break;
        case ' ':
            menu_toggle(&ui->menu);
            break;
        case 'h':
        case KEY_LEFT:
        case 27: /* ESC */
            if (ui->menu.current_menu->parent)
                menu_go_back(&ui->menu);
            else if (ch == 27)
                ui->running = 0;
            break;
        case 'q':
            ui->running = 0;
            break;
        case 'r':
            if (ui->refresh_all)
                ui->refresh_all(&ui->menu);
            break;
        case KEY_RESIZE:
            clear();
            refresh();
            break;
        }
    }
}

void ui_cleanup(UIState *ui) {
    (void)ui;
    endwin();
}
