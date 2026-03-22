#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "ui.h"
#include "backend.h"

static BackendModule *modules[16];
static int module_count = 0;

static void register_module(BackendModule *mod, MenuItem *root) {
    MenuItem *subtree = mod->build_menu();
    if (subtree) {
        menu_add_child(root, subtree);
        modules[module_count++] = mod;
    }
}

void refresh_all(MenuState *state) {
    /* Walk up from current_menu to find which top-level module we're in */
    MenuItem *active = state->current_menu;
    while (active->parent && active->parent != state->root)
        active = active->parent;

    /* If we're at root, refresh all. Otherwise only the active module. */
    MenuItem *child = state->root->children;
    int i = 0;
    while (child && i < module_count) {
        if (child == active || active == state->root) {
            if (modules[i]->refresh_fn)
                modules[i]->refresh_fn(child);
        }
        child = child->next;
        i++;
    }
}

int main(void) {
    MenuItem *root = menu_item_new("root", NULL, MENU_CATEGORY);

    register_module(mod_wifi_init(), root);
    register_module(mod_bluetooth_init(), root);
    register_module(mod_audio_init(), root);
    register_module(mod_network_init(), root);

    UIState ui;
    ui_init(&ui, root);
    ui.refresh_all = refresh_all;
    ui_loop(&ui);
    ui_cleanup(&ui);

    for (int i = 0; i < module_count; i++) {
        if (modules[i]->cleanup)
            modules[i]->cleanup();
    }

    menu_free(root);
    return 0;
}
