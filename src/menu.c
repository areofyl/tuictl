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
    time_t last_update;
} sysinfo_cache;

static void sysinfo_refresh(void) {
    time_t now = time(NULL);
    /* Only refresh every 5 seconds */
    if (sysinfo_cache.last_update && now - sysinfo_cache.last_update < 5)
        return;
    sysinfo_cache.last_update = now;

    /* User + host (rarely changes, but cheap) */
    char *u = getenv("USER");
    if (u) strncpy(sysinfo_cache.user, u, sizeof(sysinfo_cache.user) - 1);
    gethostname(sysinfo_cache.host, sizeof(sysinfo_cache.host));

    /* Kernel */
    struct utsname un;
    if (uname(&un) == 0)
        snprintf(sysinfo_cache.kernel, sizeof(sysinfo_cache.kernel), "%s %s", un.sysname, un.release);

    /* WiFi SSID */
    FILE *fp = popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes'", "r");
    sysinfo_cache.wifi[0] = '\0';
    if (fp) {
        char buf[128];
        if (fgets(buf, sizeof(buf), fp)) {
            char *s = strchr(buf, ':');
            if (s) {
                s++;
                size_t len = strlen(s);
                while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) s[--len] = '\0';
                strncpy(sysinfo_cache.wifi, s, sizeof(sysinfo_cache.wifi) - 1);
            }
        }
        pclose(fp);
    }

    /* Bluetooth */
    fp = popen("bluetoothctl devices Connected 2>/dev/null | wc -l", "r");
    sysinfo_cache.bt[0] = '\0';
    if (fp) {
        char buf[16];
        if (fgets(buf, sizeof(buf), fp)) {
            int n = atoi(buf);
            if (n > 0)
                snprintf(sysinfo_cache.bt, sizeof(sysinfo_cache.bt), "%d dev", n);
            else
                snprintf(sysinfo_cache.bt, sizeof(sysinfo_cache.bt), "on");
        }
        pclose(fp);
    }

    /* Volume */
    fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    sysinfo_cache.vol[0] = '\0';
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            float v;
            if (sscanf(buf, "Volume: %f", &v) == 1) {
                if (strstr(buf, "[MUTED]"))
                    snprintf(sysinfo_cache.vol, sizeof(sysinfo_cache.vol), "muted");
                else
                    snprintf(sysinfo_cache.vol, sizeof(sysinfo_cache.vol), "%d%%", (int)(v * 100 + 0.5));
            }
        }
        pclose(fp);
    }

    /* Battery */
    fp = fopen("/sys/class/power_supply/macsmc-battery/capacity", "r");
    sysinfo_cache.bat[0] = '\0';
    if (fp) {
        char buf[16];
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = '\0';
            FILE *fp2 = fopen("/sys/class/power_supply/macsmc-battery/status", "r");
            char st = ' ';
            if (fp2) {
                char sb[32];
                if (fgets(sb, sizeof(sb), fp2)) {
                    if (strstr(sb, "Charging")) st = '+';
                    else if (strstr(sb, "Discharging")) st = '-';
                }
                fclose(fp2);
            }
            snprintf(sysinfo_cache.bat, sizeof(sysinfo_cache.bat), "%s%%%c", buf, st);
        }
        fclose(fp);
    }

    /* IP */
    fp = popen("nmcli -t -f IP4.ADDRESS device show 2>/dev/null | grep IP4 | head -1 | cut -d: -f2", "r");
    sysinfo_cache.ip[0] = '\0';
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = '\0';
            strncpy(sysinfo_cache.ip, buf, sizeof(sysinfo_cache.ip) - 1);
        }
        pclose(fp);
    }
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

    /* Header */
    wattron(win, A_BOLD | COLOR_PAIR(1));
    mvwhline(win, 0, 0, ACS_HLINE, maxx);
    mvwprintw(win, 0, margin, " tuictl ");
    wattroff(win, A_BOLD | COLOR_PAIR(1));

    /* Notification (right-aligned, fades after 3s) */
    if (g_ui && g_ui->notification[0]) {
        time_t now = time(NULL);
        if (now - g_ui->notify_time < 3) {
            int len = strlen(g_ui->notification);
            int nx = maxx - len - margin - 2;
            if (nx < margin + 10) nx = margin + 10;
            wattron(win, A_BOLD | COLOR_PAIR(4));
            mvwprintw(win, 0, nx, " %s ", g_ui->notification);
            wattroff(win, A_BOLD | COLOR_PAIR(4));
        } else {
            g_ui->notification[0] = '\0';
        }
    }

    /* System info bar */
    sysinfo_refresh();
    time_t now_t = time(NULL);
    struct tm *tm = localtime(&now_t);
    char timebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M", tm);

    /* Line 1: user@host | kernel | time */
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, 1, margin, "%s@%s", sysinfo_cache.user, sysinfo_cache.host);
    wattroff(win, COLOR_PAIR(3));

    /* Right-align kernel + time */
    char right1[128];
    snprintf(right1, sizeof(right1), "%s  %s", sysinfo_cache.kernel, timebuf);
    int r1x = maxx - margin - (int)strlen(right1);
    if (r1x > margin + 20) {
        wattron(win, A_DIM);
        mvwprintw(win, 1, r1x, "%s", right1);
        wattroff(win, A_DIM);
    }

    /* Line 2: wifi | bt | vol | bat | ip */
    int col = margin;
    if (sysinfo_cache.wifi[0]) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, 2, col, "W:%s", sysinfo_cache.wifi);
        wattroff(win, COLOR_PAIR(1));
        col += strlen(sysinfo_cache.wifi) + 5;
    }
    if (sysinfo_cache.bt[0]) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, 2, col, "B:%s", sysinfo_cache.bt);
        wattroff(win, COLOR_PAIR(1));
        col += strlen(sysinfo_cache.bt) + 5;
    }
    if (sysinfo_cache.vol[0]) {
        mvwprintw(win, 2, col, "V:%s", sysinfo_cache.vol);
        col += strlen(sysinfo_cache.vol) + 5;
    }
    if (sysinfo_cache.bat[0]) {
        mvwprintw(win, 2, col, "BAT:%s", sysinfo_cache.bat);
        col += strlen(sysinfo_cache.bat) + 7;
    }
    if (sysinfo_cache.ip[0]) {
        wattron(win, A_DIM);
        mvwprintw(win, 2, col, "IP:%s", sysinfo_cache.ip);
        wattroff(win, A_DIM);
    }

    mvwhline(win, 3, 0, ACS_HLINE, maxx);

    /* Breadcrumb */
    char breadcrumb[512];
    build_breadcrumb(state->current_menu, breadcrumb, sizeof(breadcrumb));
    wattron(win, A_BOLD | COLOR_PAIR(3));
    mvwprintw(win, 4, margin, "%s", breadcrumb);
    wattroff(win, A_BOLD | COLOR_PAIR(3));

    mvwhline(win, 5, 0, ACS_HLINE, maxx);

    /* Menu area */
    int menu_start = 6;
    int menu_height = maxy - menu_start - 2;
    int count = menu_child_count(state->current_menu);

    /* Adjust scroll offset */
    if (state->cursor >= state->scroll_offset + menu_height)
        state->scroll_offset = state->cursor - menu_height + 1;
    if (state->cursor < state->scroll_offset)
        state->scroll_offset = state->cursor;

    for (int i = 0; i < menu_height && (i + state->scroll_offset) < count; i++) {
        int idx = i + state->scroll_offset;
        MenuItem *item = menu_child_at(state->current_menu, idx);
        if (!item) break;

        int y = menu_start + i;
        int is_selected = (idx == state->cursor);

        if (is_selected) {
            wattron(win, A_REVERSE | COLOR_PAIR(2));
            mvwhline(win, y, 0, ' ', maxx);
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
            mvwprintw(win, y, margin, " > %s%s%s", prefix, item->label, suffix);
        else
            mvwprintw(win, y, margin, "   %s%s%s", prefix, item->label, suffix);

        if (is_selected)
            wattroff(win, A_REVERSE | COLOR_PAIR(2));
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

    /* Status bar */
    int status_y = maxy - 2;
    mvwhline(win, status_y, 0, ACS_HLINE, maxx);

    /* Description of selected item */
    MenuItem *sel = menu_child_at(state->current_menu, state->cursor);
    if (sel && sel->description[0]) {
        wattron(win, COLOR_PAIR(3));
        mvwprintw(win, maxy - 1, margin, "%s", sel->description);
        wattroff(win, COLOR_PAIR(3));
    } else {
        wattron(win, A_DIM);
        mvwprintw(win, maxy - 1, margin,
                  "ENTER select  SPACE toggle  h/l navigate  r refresh  q quit");
        wattroff(win, A_DIM);
    }

    wrefresh(win);
}
