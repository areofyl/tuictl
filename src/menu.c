#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>
#include "menu.h"
#include "ui.h"

/* Cached system info for the status bar */
static struct {
    char user[32];
    char host[64];
    char kernel[64];
    char wifi[64];
    char bt[32];
    char vol[16];
    char bat[16];
    char ip[32];
    char uptime[64];
    char mem[32];
    char load[16];
    time_t last_update;
} sysinfo_cache;

static char *read_cmd(const char *cmd, char *buf, size_t size) {
    buf[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (fgets(buf, size, fp))
            buf[strcspn(buf, "\n\r")] = '\0';
        pclose(fp);
    }
    return buf;
}

static char *read_file(const char *path, char *buf, size_t size) {
    buf[0] = '\0';
    FILE *fp = fopen(path, "r");
    if (fp) {
        if (fgets(buf, size, fp))
            buf[strcspn(buf, "\n\r")] = '\0';
        fclose(fp);
    }
    return buf;
}

static void sysinfo_refresh(void) {
    time_t now = time(NULL);
    if (sysinfo_cache.last_update && now - sysinfo_cache.last_update < 5)
        return;
    sysinfo_cache.last_update = now;

    char *u = getenv("USER");
    if (u) strncpy(sysinfo_cache.user, u, sizeof(sysinfo_cache.user) - 1);
    gethostname(sysinfo_cache.host, sizeof(sysinfo_cache.host));

    struct utsname un;
    if (uname(&un) == 0)
        snprintf(sysinfo_cache.kernel, sizeof(sysinfo_cache.kernel), "%s %s", un.sysname, un.release);

    /* WiFi */
    char buf[128];
    read_cmd("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes'", buf, sizeof(buf));
    sysinfo_cache.wifi[0] = '\0';
    char *s = strchr(buf, ':');
    if (s && *(s+1)) strncpy(sysinfo_cache.wifi, s + 1, sizeof(sysinfo_cache.wifi) - 1);

    /* Bluetooth */
    read_cmd("bluetoothctl devices Connected 2>/dev/null | wc -l", buf, sizeof(buf));
    int btcount = atoi(buf);
    if (btcount > 0)
        snprintf(sysinfo_cache.bt, sizeof(sysinfo_cache.bt), "%d connected", btcount);
    else
        snprintf(sysinfo_cache.bt, sizeof(sysinfo_cache.bt), "on");

    /* Volume */
    read_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf));
    sysinfo_cache.vol[0] = '\0';
    float v;
    if (sscanf(buf, "Volume: %f", &v) == 1) {
        if (strstr(buf, "[MUTED]"))
            snprintf(sysinfo_cache.vol, sizeof(sysinfo_cache.vol), "muted");
        else
            snprintf(sysinfo_cache.vol, sizeof(sysinfo_cache.vol), "%d%%", (int)(v * 100 + 0.5));
    }

    /* Battery */
    sysinfo_cache.bat[0] = '\0';
    char cap[16];
    read_file("/sys/class/power_supply/macsmc-battery/capacity", cap, sizeof(cap));
    if (cap[0]) {
        char st[32];
        read_file("/sys/class/power_supply/macsmc-battery/status", st, sizeof(st));
        char icon = ' ';
        if (strstr(st, "Charging")) icon = '+';
        else if (strstr(st, "Discharging")) icon = '-';
        snprintf(sysinfo_cache.bat, sizeof(sysinfo_cache.bat), "%s%%%c", cap, icon);
    }

    /* IP */
    read_cmd("nmcli -t -f IP4.ADDRESS device show 2>/dev/null | grep IP4 | head -1 | cut -d: -f2",
             sysinfo_cache.ip, sizeof(sysinfo_cache.ip));

    /* Uptime */
    read_cmd("uptime -p 2>/dev/null | sed 's/^up //'", sysinfo_cache.uptime, sizeof(sysinfo_cache.uptime));

    /* Memory */
    read_cmd("free -h 2>/dev/null | awk '/Mem:/{print $3\"/\"$2}'", sysinfo_cache.mem, sizeof(sysinfo_cache.mem));

    /* Load */
    read_cmd("cat /proc/loadavg 2>/dev/null | cut -d' ' -f1", sysinfo_cache.load, sizeof(sysinfo_cache.load));
}

MenuItem *menu_item_new(const char *label, const char *desc, MenuItemType type) {
    MenuItem *item = calloc(1, sizeof(MenuItem));
    if (!item) return NULL;
    strncpy(item->label, label, sizeof(item->label) - 1);
    if (desc)
        strncpy(item->description, desc, sizeof(item->description) - 1);
    item->type = type;
    return item;
}

void menu_add_child(MenuItem *parent, MenuItem *child) {
    child->parent = parent;
    if (!parent->children) {
        parent->children = child;
        return;
    }
    MenuItem *last = parent->children;
    while (last->next)
        last = last->next;
    last->next = child;
}

void menu_free(MenuItem *item) {
    if (!item) return;
    /* Free children and siblings iteratively to avoid double-free */
    MenuItem *sibling = item;
    while (sibling) {
        MenuItem *next_sib = sibling->next;
        /* Recursively free this node's children */
        MenuItem *child = sibling->children;
        while (child) {
            MenuItem *next_child = child->next;
            child->next = NULL;
            menu_free(child);
            child = next_child;
        }
        if (sibling->options) {
            for (int i = 0; i < sibling->option_count; i++)
                free(sibling->options[i]);
            free(sibling->options);
        }
        free(sibling->userdata);
        sibling->children = NULL;
        sibling->next = NULL;
        free(sibling);
        sibling = next_sib;
    }
}

/* Free all children of a parent, leaving the parent intact */
void menu_free_children(MenuItem *parent) {
    if (!parent || !parent->children) return;
    menu_free(parent->children);
    parent->children = NULL;
}

int menu_child_count(MenuItem *parent) {
    int count = 0;
    MenuItem *c = parent->children;
    while (c) { count++; c = c->next; }
    return count;
}

MenuItem *menu_child_at(MenuItem *parent, int index) {
    MenuItem *c = parent->children;
    for (int i = 0; i < index && c; i++)
        c = c->next;
    return c;
}

void menu_cursor_up(MenuState *state) {
    if (state->cursor > 0) {
        state->cursor--;
        if (state->cursor < state->scroll_offset)
            state->scroll_offset = state->cursor;
    }
}

void menu_cursor_down(MenuState *state) {
    int count = menu_child_count(state->current_menu);
    if (state->cursor < count - 1) {
        state->cursor++;
        /* scroll_offset adjusted in render based on window height */
    }
}

void menu_activate(MenuState *state) {
    MenuItem *item = menu_child_at(state->current_menu, state->cursor);
    if (!item) return;

    switch (item->type) {
    case MENU_CATEGORY:
        if (item->on_lazy_load && !item->lazy_loaded) {
            item->on_lazy_load(item);
            item->lazy_loaded = 1;
        }
        state->current_menu = item;
        state->cursor = 0;
        state->scroll_offset = 0;
        break;
    case MENU_TOGGLE:
        item->toggled = !item->toggled;
        if (item->on_activate)
            item->on_activate(item);
        break;
    case MENU_ACTION:
        if (item->on_activate)
            item->on_activate(item);
        break;
    case MENU_SELECT:
        item->selected_index = (item->selected_index + 1) % item->option_count;
        if (item->on_activate)
            item->on_activate(item);
        break;
    case MENU_INFO:
        break;
    }
}

void menu_toggle(MenuState *state) {
    MenuItem *item = menu_child_at(state->current_menu, state->cursor);
    if (!item || item->type != MENU_TOGGLE) return;
    item->toggled = !item->toggled;
    if (item->on_activate)
        item->on_activate(item);
}

void menu_go_back(MenuState *state) {
    if (state->current_menu->parent) {
        state->current_menu = state->current_menu->parent;
        state->cursor = 0;
        state->scroll_offset = 0;
    }
}

/* Build breadcrumb string by walking parent chain */
static void build_breadcrumb(MenuItem *menu, char *buf, size_t size) {
    if (!menu->parent) {
        strncpy(buf, "tuictl", size);
        return;
    }

    /* Walk up to collect path */
    const char *parts[16];
    int depth = 0;
    MenuItem *m = menu;
    while (m->parent && depth < 16) {
        parts[depth++] = m->label;
        m = m->parent;
    }

    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--) {
        if (buf[0] != '\0')
            strncat(buf, " / ", size - strlen(buf) - 1);
        strncat(buf, parts[i], size - strlen(buf) - 1);
    }
}

void menu_render(MenuState *state, WINDOW *win) {
    int maxy, maxx;
    getmaxyx(win, maxy, maxx);

    werase(win);

    /* Moderate side margins */
    int margin = maxx / 12;
    if (margin < 2) margin = 2;
    if (margin > 12) margin = 12;

    /* System info */
    sysinfo_refresh();
    time_t now_t = time(NULL);
    struct tm *tm = localtime(&now_t);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M", tm);

    int pad = 3; /* gap between info items */

    /* Row 0: blank (top breathing room) */

    /* Row 1: tuictl + time/notification */
    wattron(win, A_BOLD | COLOR_PAIR(1));
    mvwprintw(win, 1, margin, "tuictl");
    wattroff(win, A_BOLD | COLOR_PAIR(1));

    int has_notification = 0;
    if (g_ui && g_ui->notification[0]) {
        time_t now = time(NULL);
        if (now - g_ui->notify_time < 3) {
            has_notification = 1;
            int len = strlen(g_ui->notification);
            int nx = maxx - len - margin - 2;
            if (nx < margin + 10) nx = margin + 10;
            wattron(win, A_BOLD | COLOR_PAIR(4));
            mvwprintw(win, 1, nx, " %s ", g_ui->notification);
            wattroff(win, A_BOLD | COLOR_PAIR(4));
        } else {
            g_ui->notification[0] = '\0';
        }
    }
    if (!has_notification) {
        int tx = maxx - margin - (int)strlen(timebuf);
        wattron(win, A_BOLD);
        mvwprintw(win, 1, tx, "%s", timebuf);
        wattroff(win, A_BOLD);
    }

    /* Row 2: blank */

    /* Row 3: user@host + kernel + uptime */
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, 3, margin, "%s@%s", sysinfo_cache.user, sysinfo_cache.host);
    wattroff(win, COLOR_PAIR(1));

    {
        char right[128];
        snprintf(right, sizeof(right), "%s   up %s",
                 sysinfo_cache.kernel, sysinfo_cache.uptime);
        int rx = maxx - margin - (int)strlen(right);
        if (rx > margin + 20) {
            wattron(win, A_DIM);
            mvwprintw(win, 3, rx, "%s", right);
            wattroff(win, A_DIM);
        }
    }

    /* Row 5: wifi + ip + bluetooth */
    int col = margin;
    if (sysinfo_cache.wifi[0]) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, 5, col, "wifi  %s", sysinfo_cache.wifi);
        wattroff(win, COLOR_PAIR(1));
        col += strlen(sysinfo_cache.wifi) + 6 + pad;
    }
    if (sysinfo_cache.ip[0]) {
        wattron(win, A_DIM);
        mvwprintw(win, 5, col, "(%s)", sysinfo_cache.ip);
        wattroff(win, A_DIM);
        col += strlen(sysinfo_cache.ip) + 2 + pad;
    }
    if (sysinfo_cache.bt[0]) {
        mvwprintw(win, 5, col, "bluetooth  %s", sysinfo_cache.bt);
    }

    /* Row 6: volume + battery + memory + load */
    col = margin;
    if (sysinfo_cache.vol[0]) {
        mvwprintw(win, 6, col, "volume  %s", sysinfo_cache.vol);
        col += strlen(sysinfo_cache.vol) + 8 + pad;
    }
    if (sysinfo_cache.bat[0]) {
        mvwprintw(win, 6, col, "battery  %s", sysinfo_cache.bat);
        col += strlen(sysinfo_cache.bat) + 9 + pad;
    }
    if (sysinfo_cache.mem[0]) {
        mvwprintw(win, 6, col, "memory  %s", sysinfo_cache.mem);
        col += strlen(sysinfo_cache.mem) + 8 + pad;
    }
    if (sysinfo_cache.load[0]) {
        wattron(win, A_DIM);
        mvwprintw(win, 6, col, "load  %s", sysinfo_cache.load);
        wattroff(win, A_DIM);
    }

    /* Row 8: separator */
    mvwhline(win, 8, margin, ACS_HLINE, maxx - margin * 2);

    /* Breadcrumb — only show when inside a module */
    int menu_start = 9;
    char breadcrumb[512];
    build_breadcrumb(state->current_menu, breadcrumb, sizeof(breadcrumb));
    if (state->current_menu->parent) {
        wattron(win, A_BOLD | COLOR_PAIR(3));
        mvwprintw(win, 9, margin, "%s", breadcrumb);
        wattroff(win, A_BOLD | COLOR_PAIR(3));
        menu_start = 11;
    }
    int menu_height = maxy - menu_start - 4;
    int count = menu_child_count(state->current_menu);

    /* Adjust scroll offset */
    if (state->cursor >= state->scroll_offset + menu_height)
        state->scroll_offset = state->cursor - menu_height + 1;
    if (state->cursor < state->scroll_offset)
        state->scroll_offset = state->cursor;

    /* Line number gutter width */
    int num_w = 2;
    if (count >= 10) num_w = 3;
    if (count >= 100) num_w = 4;
    int item_x = margin + num_w + 1;

    for (int i = 0; i < menu_height; i++) {
        int idx = i + state->scroll_offset;
        int y = menu_start + i;

        if (idx < count) {
            MenuItem *item = menu_child_at(state->current_menu, idx);
            if (!item) break;

            int is_selected = (idx == state->cursor);

            /* Line number */
            if (is_selected) {
                wattron(win, A_BOLD | COLOR_PAIR(1));
                mvwprintw(win, y, margin, "%*d", num_w, idx + 1);
                wattroff(win, A_BOLD | COLOR_PAIR(1));
            } else {
                wattron(win, A_DIM);
                mvwprintw(win, y, margin, "%*d", num_w, idx + 1);
                wattroff(win, A_DIM);
            }

            if (is_selected) {
                wattron(win, A_REVERSE | COLOR_PAIR(2));
                mvwhline(win, y, item_x - 1, ' ', maxx - item_x + 1);
            }

            /* Build prefix + label */
            char prefix[16] = "   ";
            char suffix[8] = "";
            switch (item->type) {
            case MENU_TOGGLE:
                snprintf(prefix, sizeof(prefix), "%s ", item->toggled ? "[+]" : "[ ]");
                break;
            case MENU_CATEGORY:
                snprintf(suffix, sizeof(suffix), "  >");
                break;
            case MENU_ACTION:
            case MENU_SELECT:
            case MENU_INFO:
                break;
            }

            if (is_selected)
                mvwprintw(win, y, item_x, "> %s%s%s", prefix, item->label, suffix);
            else
                mvwprintw(win, y, item_x, "  %s%s%s", prefix, item->label, suffix);

            if (is_selected)
                wattroff(win, A_REVERSE | COLOR_PAIR(2));
        } else {
            /* Empty lines: vim-style ~ */
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, y, margin, "~");
            wattroff(win, COLOR_PAIR(1));
        }
    }

    /* Scroll indicator */
    if (count > menu_height) {
        int bar_h = (menu_height * menu_height) / count;
        if (bar_h < 1) bar_h = 1;
        int bar_start = menu_start + (state->scroll_offset * menu_height) / count;

        for (int y = menu_start; y < menu_start + menu_height; y++) {
            if (y >= bar_start && y < bar_start + bar_h)
                mvwaddch(win, y, maxx - 1, ACS_BLOCK);
            else
                mvwaddch(win, y, maxx - 1, ACS_VLINE);
        }
    }

    /* Statusline (vim-style) */
    int status_y = maxy - 3;

    /* Description of selected item */
    MenuItem *sel = menu_child_at(state->current_menu, state->cursor);
    if (sel && sel->description[0]) {
        wattron(win, COLOR_PAIR(3));
        mvwprintw(win, status_y, margin, "%s", sel->description);
        wattroff(win, COLOR_PAIR(3));
    }

    /* Position indicator right-aligned: cursor/total */
    {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", state->cursor + 1, count);
        int px = maxx - margin - (int)strlen(pos);
        wattron(win, A_DIM);
        mvwprintw(win, status_y, px, "%s", pos);
        wattroff(win, A_DIM);
    }

    /* Mode indicator (vim-style) */
    const char *mode_str = "-- NORMAL --";
    if (g_ui) {
        if (g_ui->mode == MODE_COMMAND) mode_str = "-- COMMAND --";
        else if (g_ui->mode == MODE_SEARCH) mode_str = "-- SEARCH --";
    }
    wattron(win, A_BOLD);
    mvwprintw(win, maxy - 2, margin, "%s", mode_str);
    wattroff(win, A_BOLD);

    wrefresh(win);
}
