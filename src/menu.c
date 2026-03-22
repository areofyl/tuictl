#include <stdlib.h>
#include <string.h>
#include "menu.h"

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
    menu_free(item->children);
    menu_free(item->next);
    if (item->options) {
        for (int i = 0; i < item->option_count; i++)
            free(item->options[i]);
        free(item->options);
    }
    free(item);
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

    /* Header area: line 0 = top border, line 1 = breadcrumb */
    char breadcrumb[512];
    build_breadcrumb(state->current_menu, breadcrumb, sizeof(breadcrumb));

    wattron(win, A_BOLD | COLOR_PAIR(1));
    mvwhline(win, 0, 0, ACS_HLINE, maxx);
    mvwprintw(win, 0, 1, " tuictl ");
    wattroff(win, A_BOLD | COLOR_PAIR(1));

    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, 1, 2, "%s", breadcrumb);
    wattroff(win, COLOR_PAIR(3));

    mvwhline(win, 2, 0, ACS_HLINE, maxx);

    /* Menu items area: lines 3 to maxy-3 */
    int menu_start = 3;
    int menu_height = maxy - menu_start - 2; /* leave 2 for status bar */
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

        if (is_selected)
            wattron(win, A_REVERSE | COLOR_PAIR(2));

        /* Clear the line */
        mvwhline(win, y, 0, ' ', maxx);

        /* Draw prefix + label based on type */
        char line[256];
        switch (item->type) {
        case MENU_TOGGLE:
            snprintf(line, sizeof(line), "   %s %s",
                     item->toggled ? "[+]" : "[ ]", item->label);
            break;
        case MENU_CATEGORY:
            snprintf(line, sizeof(line), "   %s >", item->label);
            break;
        case MENU_ACTION:
            snprintf(line, sizeof(line), "   %s", item->label);
            break;
        case MENU_SELECT:
            if (item->options && item->selected_index < item->option_count)
                snprintf(line, sizeof(line), "   %s [%s]",
                         item->label, item->options[item->selected_index]);
            else
                snprintf(line, sizeof(line), "   %s", item->label);
            break;
        case MENU_INFO:
            snprintf(line, sizeof(line), "   %s", item->label);
            break;
        }

        if (is_selected)
            mvwprintw(win, y, 0, " > %s", line + 3);
        else
            mvwprintw(win, y, 0, "%s", line);

        /* Dim info items */
        if (item->type == MENU_INFO && !is_selected) {
            /* already drawn, could add A_DIM but keep simple for now */
        }

        if (is_selected)
            wattroff(win, A_REVERSE | COLOR_PAIR(2));
    }

    /* Status bar */
    int status_y = maxy - 2;
    mvwhline(win, status_y, 0, ACS_HLINE, maxx);

    /* Show description of selected item */
    MenuItem *sel = menu_child_at(state->current_menu, state->cursor);
    if (sel && sel->description[0]) {
        wattron(win, COLOR_PAIR(3));
        mvwprintw(win, maxy - 1, 2, "%s", sel->description);
        wattroff(win, COLOR_PAIR(3));
    } else {
        wattron(win, COLOR_PAIR(3));
        mvwprintw(win, maxy - 1, 2,
                  "ENTER: select  SPACE: toggle  ESC: back  q: quit  r: refresh");
        wattroff(win, COLOR_PAIR(3));
    }

    wrefresh(win);
}
