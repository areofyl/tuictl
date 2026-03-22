#include <locale.h>
#include "ui.h"

void ui_init(UIState *ui, MenuItem *root) {
    setlocale(LC_ALL, "");
    set_escdelay(25);
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

/* Get the currently selected item, or NULL */
static MenuItem *selected_item(MenuState *menu) {
    return menu_child_at(menu->current_menu, menu->cursor);
}

void ui_loop(UIState *ui) {
    wtimeout(ui->win, 5000); /* 5 second auto-refresh */
    while (ui->running) {
        menu_render(&ui->menu, ui->win);
        int ch = wgetch(ui->win);
        if (ch == ERR) {
            /* Timeout — auto-refresh */
            if (ui->refresh_all)
                ui->refresh_all(&ui->menu);
            continue;
        }
        MenuItem *sel = selected_item(&ui->menu);

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
            /* If item has on_right (slider), use that instead of activate */
            if (sel && sel->on_right)
                sel->on_right(sel);
            else
                menu_activate(&ui->menu);
            break;
        case '\n':
        case KEY_ENTER:
            menu_activate(&ui->menu);
            break;
        case ' ':
            menu_toggle(&ui->menu);
            break;
        case 'h':
        case KEY_LEFT:
            /* If item has on_left (slider), use that instead of go back */
            if (sel && sel->on_left)
                sel->on_left(sel);
            else if (ui->menu.current_menu->parent)
                menu_go_back(&ui->menu);
            break;
        case 27: /* ESC */
            if (ui->menu.current_menu->parent)
                menu_go_back(&ui->menu);
            else
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

int ui_input_popup(const char *title, const char *prompt, char *buf, size_t size, int password) {
    int maxy, maxx;
    getmaxyx(stdscr, maxy, maxx);

    int width = 50;
    int height = 7;
    if (width > maxx - 4) width = maxx - 4;
    int starty = (maxy - height) / 2;
    int startx = (maxx - width) / 2;

    WINDOW *popup = newwin(height, width, starty, startx);
    keypad(popup, TRUE);
    box(popup, 0, 0);

    /* Title */
    wattron(popup, A_BOLD | COLOR_PAIR(1));
    mvwprintw(popup, 0, 2, " %s ", title);
    wattroff(popup, A_BOLD | COLOR_PAIR(1));

    /* Prompt */
    mvwprintw(popup, 2, 2, "%s", prompt);

    /* Input field */
    int field_y = 4;
    int field_x = 2;
    int field_w = width - 4;
    mvwhline(popup, field_y, field_x, '_', field_w);
    wmove(popup, field_y, field_x);
    wrefresh(popup);

    buf[0] = '\0';
    int pos = 0;
    int confirmed = 0;

    while (1) {
        int ch = wgetch(popup);

        if (ch == '\n' || ch == KEY_ENTER) {
            confirmed = 1;
            break;
        } else if (ch == 27) { /* ESC */
            confirmed = 0;
            break;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
            }
        } else if (pos < (int)size - 1 && ch >= 32 && ch < 127) {
            buf[pos++] = ch;
            buf[pos] = '\0';
        } else {
            continue;
        }

        /* Redraw input field */
        mvwhline(popup, field_y, field_x, '_', field_w);
        for (int i = 0; i < pos && i < field_w; i++) {
            mvwaddch(popup, field_y, field_x + i, password ? '*' : buf[i]);
        }
        wmove(popup, field_y, field_x + (pos < field_w ? pos : field_w - 1));
        wrefresh(popup);
    }

    delwin(popup);
    touchwin(stdscr);
    refresh();
    return confirmed;
}
