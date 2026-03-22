#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static void airplane_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("rfkill block all 2>/dev/null");
    else
        run_cmd_silent("rfkill unblock all 2>/dev/null");
}

static void rebuild_connections(MenuItem *cat) {
    MenuItem *child = cat->children;
    while (child) {
        MenuItem *next = child->next;
        menu_free(child);
        child = next;
    }
    cat->children = NULL;

    int count = 0;
    char **lines = run_cmd_lines(
        "nmcli -t -f NAME,TYPE,DEVICE connection show --active 2>/dev/null", &count);

    for (int i = 0; i < count; i++) {
        char *line = lines[i];
        /* Format: NAME:TYPE:DEVICE */
        char *name = strtok(line, ":");
        char *type = strtok(NULL, ":");
        char *dev = strtok(NULL, ":");

        if (!name) continue;

        char label[128];
        snprintf(label, sizeof(label), "%s (%s%s%s)", name,
                 type ? type : "?",
                 dev ? " on " : "",
                 dev ? dev : "");

        menu_add_child(cat, menu_item_new(label, NULL, MENU_INFO));
    }

    if (menu_child_count(cat) == 0)
        menu_add_child(cat, menu_item_new("No active connections", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void update_info_label(MenuItem *item, const char *cmd, const char *prefix) {
    char buf[256];
    int ret = run_cmd(cmd, buf, sizeof(buf));
    if (ret == 0 && buf[0])
        snprintf(item->label, sizeof(item->label), "%s: %s", prefix, buf);
    else
        snprintf(item->label, sizeof(item->label), "%s: N/A", prefix);
}

static void network_refresh(MenuItem *module_root) {
    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_INFO && strstr(child->label, "Hostname"))
            update_info_label(child, "hostname", "Hostname");
        else if (child->type == MENU_INFO && strstr(child->label, "IP")) {
            /* Get primary IP */
            char buf[256];
            run_cmd("nmcli -t -f IP4.ADDRESS device show $(nmcli -t -f DEVICE,STATE device | grep ':connected$' | head -1 | cut -d: -f1) 2>/dev/null | head -1 | cut -d: -f2", buf, sizeof(buf));
            if (buf[0])
                snprintf(child->label, sizeof(child->label), "IP: %s", buf);
            else
                snprintf(child->label, sizeof(child->label), "IP: N/A");
        } else if (child->type == MENU_INFO && strstr(child->label, "DNS")) {
            char buf[256];
            run_cmd("nmcli -t -f IP4.DNS device show $(nmcli -t -f DEVICE,STATE device | grep ':connected$' | head -1 | cut -d: -f1) 2>/dev/null | head -1 | cut -d: -f2", buf, sizeof(buf));
            if (buf[0])
                snprintf(child->label, sizeof(child->label), "DNS: %s", buf);
            else
                snprintf(child->label, sizeof(child->label), "DNS: N/A");
        } else if (child->type == MENU_TOGGLE && strstr(child->label, "Airplane")) {
            /* Check if everything is blocked */
            char buf[256];
            run_cmd("rfkill list -o SOFT -n 2>/dev/null", buf, sizeof(buf));
            child->toggled = (strstr(buf, "unblocked") == NULL && buf[0] != '\0');
        } else if (child->type == MENU_CATEGORY) {
            rebuild_connections(child);
        }
        child = child->next;
    }
}

static MenuItem *net_build_menu(void) {
    MenuItem *root = menu_item_new("Network", "General network information", MENU_CATEGORY);

    /* Hostname */
    MenuItem *host = menu_item_new("Hostname: ?", NULL, MENU_INFO);
    update_info_label(host, "hostname", "Hostname");
    menu_add_child(root, host);

    /* IP address */
    MenuItem *ip = menu_item_new("IP: ?", NULL, MENU_INFO);
    char buf[256];
    run_cmd("nmcli -t -f IP4.ADDRESS device show $(nmcli -t -f DEVICE,STATE device | grep ':connected$' | head -1 | cut -d: -f1) 2>/dev/null | head -1 | cut -d: -f2", buf, sizeof(buf));
    if (buf[0])
        snprintf(ip->label, sizeof(ip->label), "IP: %s", buf);
    else
        snprintf(ip->label, sizeof(ip->label), "IP: N/A");
    menu_add_child(root, ip);

    /* DNS */
    MenuItem *dns = menu_item_new("DNS: ?", NULL, MENU_INFO);
    run_cmd("nmcli -t -f IP4.DNS device show $(nmcli -t -f DEVICE,STATE device | grep ':connected$' | head -1 | cut -d: -f1) 2>/dev/null | head -1 | cut -d: -f2", buf, sizeof(buf));
    if (buf[0])
        snprintf(dns->label, sizeof(dns->label), "DNS: %s", buf);
    else
        snprintf(dns->label, sizeof(dns->label), "DNS: N/A");
    menu_add_child(root, dns);

    /* Airplane mode */
    MenuItem *airplane = menu_item_new("Airplane Mode", "Block all wireless radios", MENU_TOGGLE);
    airplane->on_activate = airplane_activate;
    run_cmd("rfkill list -o SOFT -n 2>/dev/null", buf, sizeof(buf));
    airplane->toggled = (strstr(buf, "unblocked") == NULL && buf[0] != '\0');
    menu_add_child(root, airplane);

    /* Active connections */
    MenuItem *conns = menu_item_new("Active Connections", "Currently active network connections", MENU_CATEGORY);
    menu_add_child(root, conns);
    rebuild_connections(conns);

    return root;
}

static void net_cleanup(void) {}

static BackendModule net_module = {
    .name = "network",
    .build_menu = net_build_menu,
    .refresh_fn = network_refresh,
    .cleanup = net_cleanup,
};

BackendModule *mod_network_init(void) {
    return &net_module;
}
