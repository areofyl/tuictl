#ifndef MENU_H
#define MENU_H

#include <ncurses.h>

typedef enum {
    MENU_CATEGORY,
    MENU_TOGGLE,
    MENU_ACTION,
    MENU_SELECT,
    MENU_INFO,
} MenuItemType;

typedef struct MenuItem {
    char label[128];
    char description[256];
    MenuItemType type;

    /* Tree linkage */
    struct MenuItem *parent;
    struct MenuItem *children;
    struct MenuItem *next;

    /* State */
    int toggled;
    int selected_index;
    char **options;
    int option_count;

    /* Callbacks */
    void (*on_activate)(struct MenuItem *self);
    void (*on_refresh)(struct MenuItem *self);
    void (*on_lazy_load)(struct MenuItem *self); /* Called once on first enter */
    int lazy_loaded;
    void *userdata;
} MenuItem;

typedef struct {
    MenuItem *root;
    MenuItem *current_menu;
    int cursor;
    int scroll_offset;
} MenuState;

/* Tree construction */
MenuItem *menu_item_new(const char *label, const char *desc, MenuItemType type);
void menu_add_child(MenuItem *parent, MenuItem *child);
void menu_free(MenuItem *item);
void menu_free_children(MenuItem *parent);

/* Navigation */
int menu_child_count(MenuItem *parent);
MenuItem *menu_child_at(MenuItem *parent, int index);
void menu_cursor_up(MenuState *state);
void menu_cursor_down(MenuState *state);
void menu_activate(MenuState *state);
void menu_toggle(MenuState *state);
void menu_go_back(MenuState *state);

/* Rendering */
void menu_render(MenuState *state, WINDOW *win);

#endif
