#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"
#include "ui.h"

static void wifi_toggle_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("nmcli radio wifi on");
    else
        run_cmd_silent("nmcli radio wifi off");
}

static void wifi_scan_activate(MenuItem *self) {
    (void)self;
    run_cmd_silent("nmcli device wifi rescan 2>/dev/null");
}

static void wifi_connect_activate(MenuItem *self) {
    /* Extract SSID from label (format: "SSID (XX%)") */
    char ssid[128];
    strncpy(ssid, self->label, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    char *paren = strrchr(ssid, '(');
    if (paren && paren > ssid) {
        paren--;
        while (paren > ssid && *paren == ' ') paren--;
        *(paren + 1) = '\0';
    }

    /* Try connecting without password first */
    char cmd[512], result[256];
    snprintf(cmd, sizeof(cmd), "nmcli device wifi connect \"%s\" 2>&1", ssid);
    run_cmd(cmd, result, sizeof(result));

    /* If it needs a password, prompt */
    if (strstr(result, "Secrets were required") || strstr(result, "No suitable") ||
        strstr(result, "Error")) {
        char pass[128];
        char title[64];
        snprintf(title, sizeof(title), "Connect to %s", ssid);
        if (ui_input_popup(title, "Password:", pass, sizeof(pass), 1)) {
            snprintf(cmd, sizeof(cmd),
                     "nmcli device wifi connect \"%s\" password \"%s\" 2>&1", ssid, pass);
            run_cmd_silent(cmd);
        }
    }
}

/* Clear and rebuild the network list under a category */
static void rebuild_network_list(MenuItem *nets_cat) {
    menu_free_children(nets_cat);

    int count = 0;
    char **lines = run_cmd_lines(
        "nmcli -t -f SSID,SIGNAL,SECURITY,IN-USE device wifi list --rescan no 2>/dev/null", &count);

    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        /* Format: SSID:SIGNAL:SECURITY:IN-USE */
        char *ssid = strtok(line, ":");
        char *signal = strtok(NULL, ":");
        char *security = strtok(NULL, ":");
        char *in_use = strtok(NULL, ":");

        if (!ssid || !signal || ssid[0] == '\0') continue;

        char label[128];
        snprintf(label, sizeof(label), "%s (%s%%)%s%s",
                 ssid, signal,
                 (in_use && *in_use == '*') ? " *" : "",
                 (security && *security) ? "" : " [open]");

        char desc[256];
        snprintf(desc, sizeof(desc), "Connect to %s (%s)", ssid,
                 security ? security : "open");

        MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
        item->on_activate = wifi_connect_activate;
        menu_add_child(nets_cat, item);
    }

    if (menu_child_count(nets_cat) == 0) {
        menu_add_child(nets_cat,
            menu_item_new("No networks found", "Try scanning first", MENU_INFO));
    }

    free_lines(lines, count);
}

static void wifi_scan_and_refresh(MenuItem *self) {
    wifi_scan_activate(self);
    /* Find the "Available Networks" sibling */
    MenuItem *sibling = self->parent->children;
    while (sibling) {
        if (sibling->type == MENU_CATEGORY)
            rebuild_network_list(sibling);
        sibling = sibling->next;
    }
}

static void wifi_refresh(MenuItem *module_root) {
    /* Update WiFi toggle state */
    char buf[64];
    run_cmd("nmcli radio wifi", buf, sizeof(buf));
    MenuItem *toggle = module_root->children;
    if (toggle && toggle->type == MENU_TOGGLE)
        toggle->toggled = (strcmp(buf, "enabled") == 0);

    /* Update connected network info */
    char ssid[128];
    int ret = run_cmd("nmcli -t -f active,ssid dev wifi | grep '^yes'", ssid, sizeof(ssid));
    MenuItem *info = toggle ? toggle->next : NULL;
    if (info && info->type == MENU_INFO) {
        if (ret == 0 && ssid[0]) {
            char *s = strchr(ssid, ':');
            snprintf(info->label, sizeof(info->label), "Connected: %s", s ? s + 1 : ssid);
        } else {
            strncpy(info->label, "Not connected", sizeof(info->label));
        }
    }

    /* Rebuild network list */
    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_CATEGORY)
            rebuild_network_list(child);
        child = child->next;
    }
}

static void wifi_lazy_load(MenuItem *root) {
    MenuItem *toggle = menu_item_new("WiFi Enabled", "Toggle WiFi radio on/off", MENU_TOGGLE);
    toggle->on_activate = wifi_toggle_activate;
    char buf[64];
    run_cmd("nmcli radio wifi", buf, sizeof(buf));
    toggle->toggled = (strcmp(buf, "enabled") == 0);
    menu_add_child(root, toggle);

    MenuItem *conn_info = menu_item_new("Not connected", NULL, MENU_INFO);
    char ssid[128];
    int ret = run_cmd("nmcli -t -f active,ssid dev wifi | grep '^yes'", ssid, sizeof(ssid));
    if (ret == 0 && ssid[0]) {
        char *s = strchr(ssid, ':');
        snprintf(conn_info->label, sizeof(conn_info->label), "Connected: %s", s ? s + 1 : ssid);
    }
    menu_add_child(root, conn_info);

    MenuItem *scan = menu_item_new("Scan Networks", "Scan and refresh network list", MENU_ACTION);
    scan->on_activate = wifi_scan_and_refresh;
    menu_add_child(root, scan);

    MenuItem *nets = menu_item_new("Available Networks", "Browse available WiFi networks", MENU_CATEGORY);
    menu_add_child(root, nets);
    rebuild_network_list(nets);
}

static MenuItem *wifi_build_menu(void) {
    MenuItem *root = menu_item_new("WiFi", "Manage wireless networks", MENU_CATEGORY);
    root->on_lazy_load = wifi_lazy_load;
    return root;
}

static void wifi_get_status(char *buf, size_t size) {
    char radio[16];
    run_cmd("nmcli radio wifi", radio, sizeof(radio));
    if (strcmp(radio, "enabled") != 0) {
        snprintf(buf, size, "off");
        return;
    }
    char ssid[128];
    int ret = run_cmd("nmcli -t -f active,ssid dev wifi | grep '^yes'", ssid, sizeof(ssid));
    if (ret == 0 && ssid[0]) {
        char *s = strchr(ssid, ':');
        snprintf(buf, size, "%s", s ? s + 1 : ssid);
    } else {
        snprintf(buf, size, "disconnected");
    }
}

static void wifi_cleanup(void) {}

static BackendModule wifi_module = {
    .name = "wifi",
    .build_menu = wifi_build_menu,
    .refresh_fn = wifi_refresh,
    .get_status = wifi_get_status,
    .cleanup = wifi_cleanup,
};

BackendModule *mod_wifi_init(void) {
    return &wifi_module;
}
