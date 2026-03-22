#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "backend.h"

UIState *g_ui = NULL;

void ui_notify(const char *msg) {
    if (!g_ui) return;
    strncpy(g_ui->notification, msg, sizeof(g_ui->notification) - 1);
    g_ui->notification[sizeof(g_ui->notification) - 1] = '\0';
    g_ui->notify_time = time(NULL);
}

void ui_init(UIState *ui, MenuItem *root) {
    setlocale(LC_ALL, "");
    set_escdelay(25);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_BLACK, COLOR_CYAN);
    init_pair(3, COLOR_WHITE, -1);
    init_pair(4, COLOR_GREEN, -1);
    init_pair(5, COLOR_RED, -1);

    ui->win = stdscr;
    ui->running = 1;
    ui->notification[0] = '\0';
    ui->notify_time = 0;
    ui->mode = MODE_NORMAL;
    ui->cmdbuf[0] = '\0';
    ui->cmdpos = 0;
    ui->pending_key = 0;
    ui->repeat_count = 0;
    g_ui = ui;
    ui->menu.root = root;
    ui->menu.current_menu = root;
    ui->menu.cursor = 0;
    ui->menu.scroll_offset = 0;
    ui->refresh_all = NULL;
}

static MenuItem *selected_item(MenuState *menu) {
    return menu_child_at(menu->current_menu, menu->cursor);
}

/* Jump to a module by name from root */
static int jump_to_module(MenuState *menu, const char *name) {
    /* Go to root first */
    while (menu->current_menu->parent)
        menu->current_menu = menu->current_menu->parent;
    menu->cursor = 0;
    menu->scroll_offset = 0;

    MenuItem *child = menu->root->children;
    int idx = 0;
    while (child) {
        /* Case-insensitive prefix match on the base label (before " [") */
        char base[128];
        strncpy(base, child->label, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *bracket = strstr(base, " [");
        if (bracket) *bracket = '\0';

        if (strncasecmp(base, name, strlen(name)) == 0) {
            menu->cursor = idx;
            menu_activate(menu);
            return 1;
        }
        child = child->next;
        idx++;
    }
    return 0;
}

/* Execute a : command */
static void execute_command(UIState *ui, const char *cmd) {
    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;

    if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        ui->running = 0;
    } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "refresh") == 0) {
        if (ui->refresh_all)
            ui->refresh_all(&ui->menu);
    } else if (strncmp(cmd, "volume ", 7) == 0) {
        int vol = atoi(cmd + 7);
        if (vol >= 0 && vol <= 100) {
            char c[64];
            snprintf(c, sizeof(c), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%", vol);
            run_cmd_silent(c);
            char msg[32];
            snprintf(msg, sizeof(msg), "Volume: %d%%", vol);
            ui_notify(msg);
        }
    } else if (strcmp(cmd, "mute") == 0) {
        run_cmd_silent("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        ui_notify("Mute toggled");
    } else if (strcmp(cmd, "scan") == 0) {
        run_cmd_silent("nmcli device wifi rescan 2>/dev/null");
        ui_notify("Scanning...");
    } else if (strncmp(cmd, "connect ", 8) == 0) {
        char c[256];
        snprintf(c, sizeof(c), "nmcli device wifi connect \"%s\" 2>&1", cmd + 8);
        run_cmd_silent(c);
        char msg[128];
        snprintf(msg, sizeof(msg), "Connecting to %s", cmd + 8);
        ui_notify(msg);
    } else if (strcmp(cmd, "help") == 0) {
        ui_notify("j/k:move  h/l:nav  gg:top  G:bottom  /:search  :cmd");
    } else {
        /* Try as module name */
        if (!jump_to_module(&ui->menu, cmd)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Unknown: %s", cmd);
            ui_notify(msg);
        }
    }
}

static void handle_normal(UIState *ui, int ch) {
    MenuItem *sel = selected_item(&ui->menu);
    int count = ui->repeat_count > 0 ? ui->repeat_count : 1;

    /* Handle pending 'g' key */
    if (ui->pending_key == 'g') {
        ui->pending_key = 0;
        if (ch == 'g') {
            /* gg: jump to top */
            ui->menu.cursor = 0;
            ui->menu.scroll_offset = 0;
        }
        ui->repeat_count = 0;
        return;
    }

    /* Number prefix for repeat */
    if (ch >= '1' && ch <= '9' && ui->repeat_count == 0) {
        ui->repeat_count = ch - '0';
        return;
    }
    if (ch >= '0' && ch <= '9' && ui->repeat_count > 0) {
        ui->repeat_count = ui->repeat_count * 10 + (ch - '0');
        return;
    }

    switch (ch) {
    case 'k':
    case KEY_UP:
        for (int i = 0; i < count; i++)
            menu_cursor_up(&ui->menu);
        break;
    case 'j':
    case KEY_DOWN:
        for (int i = 0; i < count; i++)
            menu_cursor_down(&ui->menu);
        break;
    case 'l':
    case KEY_RIGHT:
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
        if (sel && sel->on_left)
            sel->on_left(sel);
        else if (ui->menu.current_menu->parent)
            menu_go_back(&ui->menu);
        break;
    case 27: /* ESC */
        if (ui->menu.current_menu->parent)
            menu_go_back(&ui->menu);
        break;
    case 'q':
        ui->running = 0;
        break;
    case 'g':
        ui->pending_key = 'g';
        ui->repeat_count = 0;
        return; /* Don't clear repeat_count yet */
    case 'G':
        /* Jump to last item */
        ui->menu.cursor = menu_child_count(ui->menu.current_menu) - 1;
        break;
    case 'H': {
        /* Top of visible area */
        ui->menu.cursor = ui->menu.scroll_offset;
        break;
    }
    case 'M': {
        /* Middle of visible area */
        int maxy, maxx;
        getmaxyx(ui->win, maxy, maxx);
        (void)maxx;
        int visible = maxy - 12;
        int total = menu_child_count(ui->menu.current_menu);
        int mid = ui->menu.scroll_offset + visible / 2;
        if (mid >= total) mid = total - 1;
        ui->menu.cursor = mid;
        break;
    }
    case 'L': {
        /* Bottom of visible area */
        int maxy, maxx;
        getmaxyx(ui->win, maxy, maxx);
        (void)maxx;
        int visible = maxy - 12;
        int total = menu_child_count(ui->menu.current_menu);
        int bot = ui->menu.scroll_offset + visible - 1;
        if (bot >= total) bot = total - 1;
        ui->menu.cursor = bot;
        break;
    }
    case 4: /* Ctrl+D: half page down */
    {
        int maxy, maxx;
        getmaxyx(ui->win, maxy, maxx);
        (void)maxx;
        int half = (maxy - 12) / 2;
        int total = menu_child_count(ui->menu.current_menu);
        for (int i = 0; i < half; i++) {
            if (ui->menu.cursor < total - 1)
                ui->menu.cursor++;
        }
        break;
    }
    case 21: /* Ctrl+U: half page up */
    {
        int maxy, maxx;
        getmaxyx(ui->win, maxy, maxx);
        (void)maxx;
        int half = (maxy - 12) / 2;
        for (int i = 0; i < half; i++) {
            if (ui->menu.cursor > 0)
                ui->menu.cursor--;
        }
        break;
    }
    case ':':
        ui->mode = MODE_COMMAND;
        ui->cmdbuf[0] = '\0';
        ui->cmdpos = 0;
        break;
    case '/':
        ui->mode = MODE_SEARCH;
        ui->cmdbuf[0] = '\0';
        ui->cmdpos = 0;
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
    ui->repeat_count = 0;
}

static void handle_command(UIState *ui, int ch) {
    if (ch == '\n' || ch == KEY_ENTER) {
        execute_command(ui, ui->cmdbuf);
        ui->mode = MODE_NORMAL;
    } else if (ch == 27) { /* ESC */
        ui->mode = MODE_NORMAL;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (ui->cmdpos > 0)
            ui->cmdbuf[--ui->cmdpos] = '\0';
        else
            ui->mode = MODE_NORMAL;
    } else if (ui->cmdpos < (int)sizeof(ui->cmdbuf) - 1 && ch >= 32 && ch < 127) {
        ui->cmdbuf[ui->cmdpos++] = ch;
        ui->cmdbuf[ui->cmdpos] = '\0';
    }
}

static void handle_search(UIState *ui, int ch) {
    if (ch == '\n' || ch == KEY_ENTER || ch == 27) {
        ui->mode = MODE_NORMAL;
        return;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (ui->cmdpos > 0)
            ui->cmdbuf[--ui->cmdpos] = '\0';
        else {
            ui->mode = MODE_NORMAL;
            return;
        }
    } else if (ui->cmdpos < (int)sizeof(ui->cmdbuf) - 1 && ch >= 32 && ch < 127) {
        ui->cmdbuf[ui->cmdpos++] = ch;
        ui->cmdbuf[ui->cmdpos] = '\0';
    }

    /* Live filter: jump to first matching item */
    if (ui->cmdbuf[0]) {
        int total = menu_child_count(ui->menu.current_menu);
        for (int i = 0; i < total; i++) {
            MenuItem *item = menu_child_at(ui->menu.current_menu, i);
            if (item && strcasestr(item->label, ui->cmdbuf)) {
                ui->menu.cursor = i;
                break;
            }
        }
    }
}

void ui_loop(UIState *ui) {
    menu_render(&ui->menu, ui->win);
    wtimeout(ui->win, 1);
    int first_refresh = 1;

    while (ui->running) {
        if (!first_refresh)
            menu_render(&ui->menu, ui->win);

        /* Draw mode indicator and command line at bottom */
        int maxy, maxx;
        getmaxyx(ui->win, maxy, maxx);
        int margin = maxx / 12;
        if (margin < 2) margin = 2;
        if (margin > 12) margin = 12;

        if (ui->mode == MODE_COMMAND) {
            mvwhline(ui->win, maxy - 1, 0, ' ', maxx);
            wattron(ui->win, A_BOLD);
            mvwprintw(ui->win, maxy - 1, margin, ":%s", ui->cmdbuf);
            wattroff(ui->win, A_BOLD);
            curs_set(1);
        } else if (ui->mode == MODE_SEARCH) {
            mvwhline(ui->win, maxy - 1, 0, ' ', maxx);
            wattron(ui->win, A_BOLD);
            mvwprintw(ui->win, maxy - 1, margin, "/%s", ui->cmdbuf);
            wattroff(ui->win, A_BOLD);
            curs_set(1);
        } else {
            curs_set(0);
        }
        wrefresh(ui->win);

        int ch = wgetch(ui->win);
        if (ch == ERR) {
            if (ui->refresh_all && ui->mode == MODE_NORMAL)
                ui->refresh_all(&ui->menu);
            if (first_refresh) {
                first_refresh = 0;
                wtimeout(ui->win, 5000);
            }
            continue;
        }

        switch (ui->mode) {
        case MODE_NORMAL:
            handle_normal(ui, ch);
            break;
        case MODE_COMMAND:
            handle_command(ui, ch);
            break;
        case MODE_SEARCH:
            handle_search(ui, ch);
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

    wattron(popup, A_BOLD | COLOR_PAIR(1));
    mvwprintw(popup, 0, 2, " %s ", title);
    wattroff(popup, A_BOLD | COLOR_PAIR(1));

    mvwprintw(popup, 2, 2, "%s", prompt);

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
        } else if (ch == 27) {
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
