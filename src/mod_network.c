#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "backend.h"
#include "ui.h"

static void airplane_activate(MenuItem *self) {
    if (self->toggled) {
        run_cmd_silent("rfkill block all 2>/dev/null");
        ui_notify("Airplane mode on");
    } else {
        run_cmd_silent("rfkill unblock all 2>/dev/null");
        ui_notify("Airplane mode off");
    }
}

static void rebuild_connections(MenuItem *cat) {
    menu_free_children(cat);

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

/* Get IP and DNS from a single nmcli call instead of 2 nested subshell pipelines */
static void get_ip_and_dns(char *ip_out, size_t ip_size, char *dns_out, size_t dns_size) {
    ip_out[0] = '\0';
    dns_out[0] = '\0';

    int count = 0;
    char **lines = run_cmd_lines(
        "nmcli -t -f IP4.ADDRESS,IP4.DNS device show 2>/dev/null", &count);

    for (int i = 0; i < count; i++) {
        if (!ip_out[0] && strncmp(lines[i], "IP4.ADDRESS", 11) == 0) {
            char *val = strchr(lines[i], ':');
            if (val && *(val + 1))
                strncpy(ip_out, val + 1, ip_size - 1);
        } else if (!dns_out[0] && strncmp(lines[i], "IP4.DNS", 7) == 0) {
            char *val = strchr(lines[i], ':');
            if (val && *(val + 1))
                strncpy(dns_out, val + 1, dns_size - 1);
        }
        if (ip_out[0] && dns_out[0]) break;
    }

    free_lines(lines, count);
}

/* Cached IP to avoid duplicate nmcli calls between refresh_fn and get_status */
static char net_ip_cache[128] = "";
static time_t net_status_time = 0;

static void network_refresh(MenuItem *module_root) {
    /* 1 call for both IP and DNS (was 2 heavy nested pipelines) */
    char ip[128], dns[128];
    get_ip_and_dns(ip, sizeof(ip), dns, sizeof(dns));
    /* Update cache so get_status doesn't re-run the same query */
    strncpy(net_ip_cache, ip, sizeof(net_ip_cache) - 1);
    net_status_time = time(NULL);

    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_INFO && strstr(child->label, "Hostname")) {
            char buf[128];
            run_cmd("hostname", buf, sizeof(buf));
            if (buf[0])
                snprintf(child->label, sizeof(child->label), "Hostname: %s", buf);
        } else if (child->type == MENU_INFO && strstr(child->label, "IP")) {
            snprintf(child->label, sizeof(child->label), "IP: %s", ip[0] ? ip : "N/A");
        } else if (child->type == MENU_INFO && strstr(child->label, "DNS")) {
            snprintf(child->label, sizeof(child->label), "DNS: %s", dns[0] ? dns : "N/A");
        } else if (child->type == MENU_TOGGLE && strstr(child->label, "Airplane")) {
            char buf[256];
            run_cmd("rfkill list -o SOFT -n 2>/dev/null", buf, sizeof(buf));
            child->toggled = (strstr(buf, "unblocked") == NULL && buf[0] != '\0');
        } else if (child->type == MENU_CATEGORY) {
            rebuild_connections(child);
        }
        child = child->next;
    }
}

static void net_lazy_load(MenuItem *root) {
    MenuItem *host = menu_item_new("Hostname: ?", NULL, MENU_INFO);
    char hbuf[128];
    run_cmd("hostname", hbuf, sizeof(hbuf));
    if (hbuf[0])
        snprintf(host->label, sizeof(host->label), "Hostname: %s", hbuf);
    menu_add_child(root, host);

    char ip_str[128], dns_str[128];
    get_ip_and_dns(ip_str, sizeof(ip_str), dns_str, sizeof(dns_str));

    MenuItem *ip = menu_item_new("IP: N/A", NULL, MENU_INFO);
    if (ip_str[0])
        snprintf(ip->label, sizeof(ip->label), "IP: %s", ip_str);
    menu_add_child(root, ip);

    MenuItem *dns = menu_item_new("DNS: N/A", NULL, MENU_INFO);
    if (dns_str[0])
        snprintf(dns->label, sizeof(dns->label), "DNS: %s", dns_str);
    menu_add_child(root, dns);

    char buf[256];
    MenuItem *airplane = menu_item_new("Airplane Mode", "Block all wireless radios", MENU_TOGGLE);
    airplane->on_activate = airplane_activate;
    run_cmd("rfkill list -o SOFT -n 2>/dev/null", buf, sizeof(buf));
    airplane->toggled = (strstr(buf, "unblocked") == NULL && buf[0] != '\0');
    menu_add_child(root, airplane);

    MenuItem *conns = menu_item_new("Active Connections", "Currently active network connections", MENU_CATEGORY);
    menu_add_child(root, conns);
    rebuild_connections(conns);
}

static MenuItem *net_build_menu(void) {
    MenuItem *root = menu_item_new("Network", "General network information", MENU_CATEGORY);
    root->on_lazy_load = net_lazy_load;
    return root;
}

static void net_get_status(char *buf, size_t size) {
    time_t now = time(NULL);
    if (now - net_status_time > 1) {
        char dns[128];
        get_ip_and_dns(net_ip_cache, sizeof(net_ip_cache), dns, sizeof(dns));
        net_status_time = now;
    }
    if (net_ip_cache[0])
        snprintf(buf, size, "%s", net_ip_cache);
    else
        snprintf(buf, size, "no connection");
}

static void net_cleanup(void) {}

static BackendModule net_module = {
    .name = "network",
    .build_menu = net_build_menu,
    .refresh_fn = network_refresh,
    .get_status = net_get_status,
    .cleanup = net_cleanup,
};

BackendModule *mod_network_init(void) {
    return &net_module;
}
