#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static void profile_activate(MenuItem *self) {
    const char *profile = (const char *)self->userdata;
    if (!profile) return;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "tuned-adm profile %s 2>/dev/null", profile);
    run_cmd_silent(cmd);
}

static void rebuild_profiles(MenuItem *cat) {
    menu_free_children(cat);

    int count = 0;
    char **lines = run_cmd_lines("tuned-adm list 2>/dev/null", &count);

    /* Get current profile */
    char current[64];
    run_cmd("tuned-adm active 2>/dev/null | sed 's/Current active profile: //'", current, sizeof(current));

    int in_list = 0;
    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        if (strstr(line, "Available profiles:")) {
            in_list = 1;
            continue;
        }
        if (strstr(line, "Current active")) break;
        if (!in_list) continue;

        /* Lines like: "- balanced" */
        char *p = line;
        while (*p == ' ' || *p == '-') p++;
        if (!*p) continue;

        /* Strip trailing description after " - " */
        char *desc_sep = strstr(p, " - ");
        char desc_text[128] = "";
        if (desc_sep) {
            strncpy(desc_text, desc_sep + 3, sizeof(desc_text) - 1);
            *desc_sep = '\0';
        }

        int is_active = (strcmp(p, current) == 0);
        char label[128];
        snprintf(label, sizeof(label), "%s%s", p, is_active ? " *" : "");

        MenuItem *item = menu_item_new(label, desc_text[0] ? desc_text : "Switch to this profile", MENU_ACTION);
        item->on_activate = profile_activate;
        item->userdata = strdup(p);
        menu_add_child(cat, item);
    }

    if (menu_child_count(cat) == 0)
        menu_add_child(cat, menu_item_new("tuned-adm not available", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void power_lazy_load(MenuItem *root) {
    /* Battery info */
    char buf[64];
    run_cmd("cat /sys/class/power_supply/macsmc-battery/capacity 2>/dev/null", buf, sizeof(buf));
    char bat_label[128];
    if (buf[0])
        snprintf(bat_label, sizeof(bat_label), "Battery: %s%%", buf);
    else
        snprintf(bat_label, sizeof(bat_label), "Battery: N/A");
    menu_add_child(root, menu_item_new(bat_label, NULL, MENU_INFO));

    /* Charging status */
    run_cmd("cat /sys/class/power_supply/macsmc-battery/status 2>/dev/null", buf, sizeof(buf));
    char status_label[128];
    snprintf(status_label, sizeof(status_label), "Status: %s", buf[0] ? buf : "N/A");
    menu_add_child(root, menu_item_new(status_label, NULL, MENU_INFO));

    /* AC plugged in */
    run_cmd("cat /sys/class/power_supply/macsmc-ac/online 2>/dev/null", buf, sizeof(buf));
    char ac_label[128];
    snprintf(ac_label, sizeof(ac_label), "AC Power: %s",
             (buf[0] == '1') ? "Connected" : "Disconnected");
    menu_add_child(root, menu_item_new(ac_label, NULL, MENU_INFO));

    /* CPU governor */
    run_cmd("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null", buf, sizeof(buf));
    char gov_label[128];
    snprintf(gov_label, sizeof(gov_label), "CPU Governor: %s", buf[0] ? buf : "N/A");
    menu_add_child(root, menu_item_new(gov_label, NULL, MENU_INFO));

    /* Tuned profiles */
    MenuItem *profiles = menu_item_new("Power Profiles", "Select a tuned power profile", MENU_CATEGORY);
    menu_add_child(root, profiles);
    rebuild_profiles(profiles);
}

static void power_refresh(MenuItem *module_root) {
    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_INFO && strstr(child->label, "Battery:")) {
            char buf[64];
            run_cmd("cat /sys/class/power_supply/macsmc-battery/capacity 2>/dev/null", buf, sizeof(buf));
            if (buf[0])
                snprintf(child->label, sizeof(child->label), "Battery: %s%%", buf);
        } else if (child->type == MENU_INFO && strstr(child->label, "Status:")) {
            char buf[64];
            run_cmd("cat /sys/class/power_supply/macsmc-battery/status 2>/dev/null", buf, sizeof(buf));
            snprintf(child->label, sizeof(child->label), "Status: %s", buf[0] ? buf : "N/A");
        } else if (child->type == MENU_INFO && strstr(child->label, "AC Power:")) {
            char buf[64];
            run_cmd("cat /sys/class/power_supply/macsmc-ac/online 2>/dev/null", buf, sizeof(buf));
            snprintf(child->label, sizeof(child->label), "AC Power: %s",
                     (buf[0] == '1') ? "Connected" : "Disconnected");
        } else if (child->type == MENU_INFO && strstr(child->label, "CPU Governor:")) {
            char buf[64];
            run_cmd("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null", buf, sizeof(buf));
            snprintf(child->label, sizeof(child->label), "CPU Governor: %s", buf[0] ? buf : "N/A");
        } else if (child->type == MENU_CATEGORY) {
            rebuild_profiles(child);
        }
        child = child->next;
    }
}

static MenuItem *power_build_menu(void) {
    MenuItem *root = menu_item_new("Power", "Battery and power management", MENU_CATEGORY);
    root->on_lazy_load = power_lazy_load;
    return root;
}

static void power_get_status(char *buf, size_t size) {
    char cap[16], status[32];
    run_cmd("cat /sys/class/power_supply/macsmc-battery/capacity 2>/dev/null", cap, sizeof(cap));
    run_cmd("cat /sys/class/power_supply/macsmc-battery/status 2>/dev/null", status, sizeof(status));
    if (cap[0]) {
        char icon = ' ';
        if (strstr(status, "Charging")) icon = '+';
        else if (strstr(status, "Discharging")) icon = '-';
        snprintf(buf, size, "%s%%%c", cap, icon);
    } else {
        snprintf(buf, size, "N/A");
    }
}

static void power_cleanup(void) {}

static BackendModule power_module = {
    .name = "power",
    .build_menu = power_build_menu,
    .refresh_fn = power_refresh,
    .get_status = power_get_status,
    .cleanup = power_cleanup,
};

BackendModule *mod_power_init(void) {
    return &power_module;
}
