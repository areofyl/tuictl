#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static void unmount_activate(MenuItem *self) {
    const char *path = (const char *)self->userdata;
    if (!path) return;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "umount \"%s\" 2>&1", path);
    run_cmd_silent(cmd);
}

static void eject_activate(MenuItem *self) {
    const char *dev = (const char *)self->userdata;
    if (!dev) return;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "udisksctl power-off -b \"%s\" 2>&1", dev);
    run_cmd_silent(cmd);
}

static void rebuild_devices(MenuItem *cat) {
    menu_free_children(cat);

    int count = 0;
    char **lines = run_cmd_lines(
        "lsblk -rno NAME,SIZE,TYPE,MOUNTPOINT,TRAN 2>/dev/null", &count);

    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        char name[64], size[32], type[32], mount[256], tran[32];
        mount[0] = '\0';
        tran[0] = '\0';

        int n = sscanf(line, "%63s %31s %31s %255s %31s", name, size, type, mount, tran);
        if (n < 3) continue;

        /* Show disks and partitions */
        if (strcmp(type, "disk") != 0 && strcmp(type, "part") != 0) continue;

        int is_disk = (strcmp(type, "disk") == 0);
        int is_usb = (tran[0] && strcmp(tran, "usb") == 0);
        int is_mounted = (n >= 4 && mount[0] && strcmp(mount, "") != 0);

        char label[128];
        if (is_disk) {
            snprintf(label, sizeof(label), "/dev/%s (%s)%s%s", name, size,
                     is_usb ? " [USB]" : "",
                     tran[0] ? "" : "");
        } else {
            snprintf(label, sizeof(label), "  /dev/%s (%s)%s", name, size,
                     is_mounted ? "" : " [unmounted]");
            if (is_mounted) {
                char full[128];
                snprintf(full, sizeof(full), "%s -> %s", label, mount);
                strncpy(label, full, sizeof(label) - 1);
            }
        }

        if (is_mounted && strcmp(type, "part") == 0) {
            char desc[128];
            snprintf(desc, sizeof(desc), "Unmount /dev/%s from %s", name, mount);
            MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
            item->on_activate = unmount_activate;
            item->userdata = strdup(mount);
            menu_add_child(cat, item);
        } else if (is_disk && is_usb) {
            char desc[128], devpath[64];
            snprintf(desc, sizeof(desc), "Safely eject /dev/%s", name);
            snprintf(devpath, sizeof(devpath), "/dev/%s", name);
            MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
            item->on_activate = eject_activate;
            item->userdata = strdup(devpath);
            menu_add_child(cat, item);
        } else {
            menu_add_child(cat, menu_item_new(label, NULL, MENU_INFO));
        }
    }

    if (menu_child_count(cat) == 0)
        menu_add_child(cat, menu_item_new("No block devices found", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void storage_lazy_load(MenuItem *root) {
    rebuild_devices(root);
}

static void storage_refresh(MenuItem *module_root) {
    rebuild_devices(module_root);
}

static MenuItem *storage_build_menu(void) {
    MenuItem *root = menu_item_new("Storage", "Disks, partitions, and USB devices", MENU_CATEGORY);
    root->on_lazy_load = storage_lazy_load;
    return root;
}

static void storage_get_status(char *buf, size_t size) {
    char out[64];
    run_cmd("lsblk -rno TRAN 2>/dev/null | grep -c usb", out, sizeof(out));
    int usb_count = atoi(out);
    if (usb_count > 0)
        snprintf(buf, size, "%d USB", usb_count);
    else
        snprintf(buf, size, "no USB");
}

static void storage_cleanup(void) {}

static BackendModule storage_module = {
    .name = "storage",
    .build_menu = storage_build_menu,
    .refresh_fn = storage_refresh,
    .get_status = storage_get_status,
    .cleanup = storage_cleanup,
};

BackendModule *mod_storage_init(void) {
    return &storage_module;
}
