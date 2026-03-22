#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static void bt_power_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("bluetoothctl power on 2>/dev/null");
    else
        run_cmd_silent("bluetoothctl power off 2>/dev/null");
}

static void bt_rfkill_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("rfkill unblock bluetooth 2>/dev/null");
    else
        run_cmd_silent("rfkill block bluetooth 2>/dev/null");
}

static void bt_discoverable_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("bluetoothctl discoverable on 2>/dev/null");
    else
        run_cmd_silent("bluetoothctl discoverable off 2>/dev/null");
}

static void bt_scan_activate(MenuItem *self) {
    (void)self;
    /* Brief scan: start, wait, stop */
    run_cmd_silent("timeout 4 bluetoothctl scan on 2>/dev/null");
}

static void bt_device_activate(MenuItem *self) {
    /* userdata stores the MAC address */
    const char *mac = (const char *)self->userdata;
    if (!mac) return;

    /* Check if connected, toggle */
    char cmd[256];
    char buf[512];
    snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>/dev/null", mac);
    run_cmd(cmd, buf, sizeof(buf));

    if (strstr(buf, "Connected: yes")) {
        snprintf(cmd, sizeof(cmd), "bluetoothctl disconnect %s 2>/dev/null", mac);
    } else {
        snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s 2>/dev/null", mac);
    }
    run_cmd_silent(cmd);
}

static void rebuild_device_list(MenuItem *devices_cat) {
    MenuItem *child = devices_cat->children;
    while (child) {
        MenuItem *next = child->next;
        free(child->userdata);
        child->userdata = NULL;
        menu_free(child);
        child = next;
    }
    devices_cat->children = NULL;

    int count = 0;
    char **lines = run_cmd_lines("bluetoothctl devices 2>/dev/null", &count);

    for (int i = 0; i < count; i++) {
        /* Format: Device XX:XX:XX:XX:XX:XX Name */
        char *line = lines[i];
        if (strncmp(line, "Device ", 7) != 0) continue;

        char *mac = line + 7;
        char *name = strchr(mac, ' ');
        if (!name) continue;
        *name = '\0';
        name++;

        /* Check connection status */
        char cmd[256], buf[512];
        snprintf(cmd, sizeof(cmd), "bluetoothctl info %s 2>/dev/null", mac);
        run_cmd(cmd, buf, sizeof(buf));
        int connected = strstr(buf, "Connected: yes") != NULL;

        char label[128];
        snprintf(label, sizeof(label), "%s [%s]", name,
                 connected ? "connected" : "disconnected");

        char desc[256];
        snprintf(desc, sizeof(desc), "%s %s",
                 connected ? "Disconnect" : "Connect to", name);

        MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
        item->on_activate = bt_device_activate;
        item->userdata = strdup(mac);
        menu_add_child(devices_cat, item);
    }

    if (menu_child_count(devices_cat) == 0) {
        menu_add_child(devices_cat,
            menu_item_new("No devices found", "Try scanning first", MENU_INFO));
    }

    free_lines(lines, count);
}

static void bt_scan_and_refresh(MenuItem *self) {
    bt_scan_activate(self);
    /* Find devices category in siblings */
    MenuItem *sibling = self->parent->children;
    while (sibling) {
        if (sibling->type == MENU_CATEGORY)
            rebuild_device_list(sibling);
        sibling = sibling->next;
    }
}

static void bt_refresh(MenuItem *module_root) {
    /* Update power toggle */
    char buf[256];
    run_cmd("bluetoothctl show 2>/dev/null", buf, sizeof(buf));

    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_TOGGLE && strcmp(child->label, "Bluetooth Enabled") == 0) {
            /* Check rfkill */
            char rfk[64];
            run_cmd("rfkill list bluetooth -o SOFT -n 2>/dev/null", rfk, sizeof(rfk));
            child->toggled = (strstr(rfk, "unblocked") != NULL);
        } else if (child->type == MENU_TOGGLE && strcmp(child->label, "Powered") == 0) {
            child->toggled = (strstr(buf, "Powered: yes") != NULL);
        } else if (child->type == MENU_TOGGLE && strcmp(child->label, "Discoverable") == 0) {
            child->toggled = (strstr(buf, "Discoverable: yes") != NULL);
        } else if (child->type == MENU_CATEGORY) {
            rebuild_device_list(child);
        }
        child = child->next;
    }
}

static MenuItem *bt_build_menu(void) {
    MenuItem *root = menu_item_new("Bluetooth", "Manage Bluetooth devices", MENU_CATEGORY);

    /* RF kill toggle */
    MenuItem *rf = menu_item_new("Bluetooth Enabled", "Toggle Bluetooth radio (rfkill)", MENU_TOGGLE);
    rf->on_activate = bt_rfkill_activate;
    char rfk[64];
    run_cmd("rfkill list bluetooth -o SOFT -n 2>/dev/null", rfk, sizeof(rfk));
    rf->toggled = (strstr(rfk, "unblocked") != NULL);
    menu_add_child(root, rf);

    /* Power toggle */
    MenuItem *pwr = menu_item_new("Powered", "Toggle Bluetooth controller power", MENU_TOGGLE);
    pwr->on_activate = bt_power_activate;
    char buf[256];
    run_cmd("bluetoothctl show 2>/dev/null", buf, sizeof(buf));
    pwr->toggled = (strstr(buf, "Powered: yes") != NULL);
    menu_add_child(root, pwr);

    /* Discoverable */
    MenuItem *disc = menu_item_new("Discoverable", "Make device discoverable", MENU_TOGGLE);
    disc->on_activate = bt_discoverable_activate;
    disc->toggled = (strstr(buf, "Discoverable: yes") != NULL);
    menu_add_child(root, disc);

    /* Scan */
    MenuItem *scan = menu_item_new("Scan for Devices", "Search for nearby Bluetooth devices (4s)", MENU_ACTION);
    scan->on_activate = bt_scan_and_refresh;
    menu_add_child(root, scan);

    /* Paired/known devices */
    MenuItem *devices = menu_item_new("Devices", "Known and paired Bluetooth devices", MENU_CATEGORY);
    menu_add_child(root, devices);
    rebuild_device_list(devices);

    return root;
}

static void bt_cleanup(void) {}

static BackendModule bt_module = {
    .name = "bluetooth",
    .build_menu = bt_build_menu,
    .refresh_fn = bt_refresh,
    .cleanup = bt_cleanup,
};

BackendModule *mod_bluetooth_init(void) {
    return &bt_module;
}
