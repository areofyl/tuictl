#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static void mute_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("wpctl set-mute @DEFAULT_AUDIO_SINK@ 1");
    else
        run_cmd_silent("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0");
}

static void mic_mute_activate(MenuItem *self) {
    if (self->toggled)
        run_cmd_silent("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1");
    else
        run_cmd_silent("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0");
}

static void vol_up_activate(MenuItem *self) {
    (void)self;
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+");
}

static void vol_down_activate(MenuItem *self) {
    (void)self;
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
}

static void mic_vol_up_activate(MenuItem *self) {
    (void)self;
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 5%+");
}

static void mic_vol_down_activate(MenuItem *self) {
    (void)self;
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 5%-");
}

static void sink_activate(MenuItem *self) {
    /* userdata stores the node ID as a string */
    const char *id = (const char *)self->userdata;
    if (!id) return;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "wpctl set-default %s", id);
    run_cmd_silent(cmd);
}

static void source_activate(MenuItem *self) {
    const char *id = (const char *)self->userdata;
    if (!id) return;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "wpctl set-default %s", id);
    run_cmd_silent(cmd);
}

static int get_volume(const char *target) {
    char cmd[128], buf[128];
    snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", target);
    run_cmd(cmd, buf, sizeof(buf));
    /* Output: "Volume: 0.50" or "Volume: 0.50 [MUTED]" */
    float vol = 0;
    if (sscanf(buf, "Volume: %f", &vol) == 1)
        return (int)(vol * 100 + 0.5);
    return -1;
}

static int is_muted(const char *target) {
    char cmd[128], buf[128];
    snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", target);
    run_cmd(cmd, buf, sizeof(buf));
    return strstr(buf, "[MUTED]") != NULL;
}

static void rebuild_sinks(MenuItem *cat) {
    MenuItem *child = cat->children;
    while (child) {
        MenuItem *next = child->next;
        free(child->userdata);
        child->userdata = NULL;
        menu_free(child);
        child = next;
    }
    cat->children = NULL;

    /* Parse wpctl status for sinks */
    int count = 0;
    char **lines = run_cmd_lines("wpctl status 2>/dev/null", &count);

    int in_sinks = 0;
    for (int i = 0; i < count; i++) {
        char *line = lines[i];

        if (strstr(line, "Audio/Sink")) {
            in_sinks = 1;
            continue;
        }
        if (in_sinks && (line[0] == '\0' || strstr(line, "Audio/Source") ||
                         strstr(line, "Video/") || strstr(line, "Streams:"))) {
            in_sinks = 0;
            continue;
        }

        if (!in_sinks) continue;

        /* Lines look like: " │   46. Starship/Matisse HD Audio Controller Analog Stereo [vol: 0.50]" */
        /* Or with *: " │  *46. Name [vol: 0.50]" */
        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '|') p++;
        /* Skip box-drawing chars (UTF-8) */
        while ((unsigned char)*p >= 0x80) p++;
        while (*p == ' ') p++;

        int is_default = (*p == '*');
        if (is_default) p++;

        int id = 0;
        if (sscanf(p, "%d.", &id) != 1) continue;

        /* Skip past "ID. " */
        char *dot = strchr(p, '.');
        if (!dot) continue;
        dot++;
        while (*dot == ' ') dot++;

        /* Extract name (up to [vol:) */
        char name[128];
        strncpy(name, dot, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *bracket = strstr(name, " [vol:");
        if (bracket) *bracket = '\0';

        char label[128];
        snprintf(label, sizeof(label), "%s%s", name, is_default ? " *" : "");

        char desc[128];
        snprintf(desc, sizeof(desc), "Set %s as default output", name);

        MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
        item->on_activate = sink_activate;
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%d", id);
        item->userdata = strdup(id_str);
        menu_add_child(cat, item);
    }

    if (menu_child_count(cat) == 0)
        menu_add_child(cat, menu_item_new("No sinks found", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void rebuild_sources(MenuItem *cat) {
    MenuItem *child = cat->children;
    while (child) {
        MenuItem *next = child->next;
        free(child->userdata);
        child->userdata = NULL;
        menu_free(child);
        child = next;
    }
    cat->children = NULL;

    int count = 0;
    char **lines = run_cmd_lines("wpctl status 2>/dev/null", &count);

    int in_sources = 0;
    for (int i = 0; i < count; i++) {
        char *line = lines[i];

        if (strstr(line, "Audio/Source")) {
            in_sources = 1;
            continue;
        }
        if (in_sources && (line[0] == '\0' || strstr(line, "Video/") ||
                           strstr(line, "Streams:"))) {
            in_sources = 0;
            continue;
        }

        if (!in_sources) continue;

        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '|') p++;
        while ((unsigned char)*p >= 0x80) p++;
        while (*p == ' ') p++;

        int is_default = (*p == '*');
        if (is_default) p++;

        int id = 0;
        if (sscanf(p, "%d.", &id) != 1) continue;

        char *dot = strchr(p, '.');
        if (!dot) continue;
        dot++;
        while (*dot == ' ') dot++;

        char name[128];
        strncpy(name, dot, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *bracket = strstr(name, " [vol:");
        if (bracket) *bracket = '\0';

        char label[128];
        snprintf(label, sizeof(label), "%s%s", name, is_default ? " *" : "");

        char desc[128];
        snprintf(desc, sizeof(desc), "Set %s as default input", name);

        MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
        item->on_activate = source_activate;
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%d", id);
        item->userdata = strdup(id_str);
        menu_add_child(cat, item);
    }

    if (menu_child_count(cat) == 0)
        menu_add_child(cat, menu_item_new("No sources found", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void update_vol_label(MenuItem *item, const char *target, const char *prefix) {
    int vol = get_volume(target);
    if (vol >= 0)
        snprintf(item->label, sizeof(item->label), "%s: %d%%", prefix, vol);
}

static void audio_refresh(MenuItem *module_root) {
    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_TOGGLE && strstr(child->label, "Mute") && !strstr(child->label, "Mic"))
            child->toggled = is_muted("@DEFAULT_AUDIO_SINK@");
        else if (child->type == MENU_TOGGLE && strstr(child->label, "Mic"))
            child->toggled = is_muted("@DEFAULT_AUDIO_SOURCE@");
        else if (child->type == MENU_INFO && strstr(child->label, "Volume"))
            update_vol_label(child, "@DEFAULT_AUDIO_SINK@", "Volume");
        else if (child->type == MENU_INFO && strstr(child->label, "Mic Level"))
            update_vol_label(child, "@DEFAULT_AUDIO_SOURCE@", "Mic Level");
        else if (child->type == MENU_CATEGORY && strstr(child->label, "Output"))
            rebuild_sinks(child);
        else if (child->type == MENU_CATEGORY && strstr(child->label, "Input"))
            rebuild_sources(child);
        child = child->next;
    }
}

static MenuItem *audio_build_menu(void) {
    MenuItem *root = menu_item_new("Audio", "Audio output and input settings", MENU_CATEGORY);

    /* Volume info */
    MenuItem *vol_info = menu_item_new("Volume: ??%", NULL, MENU_INFO);
    update_vol_label(vol_info, "@DEFAULT_AUDIO_SINK@", "Volume");
    menu_add_child(root, vol_info);

    /* Mute toggle */
    MenuItem *mute = menu_item_new("Mute", "Toggle audio mute", MENU_TOGGLE);
    mute->on_activate = mute_activate;
    mute->toggled = is_muted("@DEFAULT_AUDIO_SINK@");
    menu_add_child(root, mute);

    /* Volume controls */
    MenuItem *vup = menu_item_new("Volume +5%", "Increase volume by 5%", MENU_ACTION);
    vup->on_activate = vol_up_activate;
    menu_add_child(root, vup);

    MenuItem *vdown = menu_item_new("Volume -5%", "Decrease volume by 5%", MENU_ACTION);
    vdown->on_activate = vol_down_activate;
    menu_add_child(root, vdown);

    /* Output devices */
    MenuItem *sinks = menu_item_new("Output Devices", "Select default audio output", MENU_CATEGORY);
    menu_add_child(root, sinks);
    rebuild_sinks(sinks);

    /* Mic level info */
    MenuItem *mic_info = menu_item_new("Mic Level: ??%", NULL, MENU_INFO);
    update_vol_label(mic_info, "@DEFAULT_AUDIO_SOURCE@", "Mic Level");
    menu_add_child(root, mic_info);

    /* Mic mute */
    MenuItem *mic_mute = menu_item_new("Mic Mute", "Toggle microphone mute", MENU_TOGGLE);
    mic_mute->on_activate = mic_mute_activate;
    mic_mute->toggled = is_muted("@DEFAULT_AUDIO_SOURCE@");
    menu_add_child(root, mic_mute);

    /* Mic volume controls */
    MenuItem *mup = menu_item_new("Mic Volume +5%", "Increase mic volume by 5%", MENU_ACTION);
    mup->on_activate = mic_vol_up_activate;
    menu_add_child(root, mup);

    MenuItem *mdown = menu_item_new("Mic Volume -5%", "Decrease mic volume by 5%", MENU_ACTION);
    mdown->on_activate = mic_vol_down_activate;
    menu_add_child(root, mdown);

    /* Input devices */
    MenuItem *sources = menu_item_new("Input Devices", "Select default audio input", MENU_CATEGORY);
    menu_add_child(root, sources);
    rebuild_sources(sources);

    return root;
}

static void audio_cleanup(void) {}

static BackendModule audio_module = {
    .name = "audio",
    .build_menu = audio_build_menu,
    .refresh_fn = audio_refresh,
    .cleanup = audio_cleanup,
};

BackendModule *mod_audio_init(void) {
    return &audio_module;
}
