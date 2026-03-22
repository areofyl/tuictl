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

/* Single call to get both volume and mute state from one wpctl invocation */
static void get_volume_and_mute(const char *target, int *vol_out, int *muted_out) {
    char cmd[128], buf[128];
    snprintf(cmd, sizeof(cmd), "wpctl get-volume %s 2>/dev/null", target);
    run_cmd(cmd, buf, sizeof(buf));
    /* Output: "Volume: 0.50" or "Volume: 0.50 [MUTED]" */
    float vol = 0;
    if (sscanf(buf, "Volume: %f", &vol) == 1)
        *vol_out = (int)(vol * 100 + 0.5);
    else
        *vol_out = -1;
    *muted_out = (strstr(buf, "[MUTED]") != NULL);
}

/* Parse a wpctl status device line, return 1 on success */
static int parse_device_line(char *line, int *id_out, char *name_out,
                             size_t name_size, int *is_default_out) {
    char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '|') p++;
    while ((unsigned char)*p >= 0x80) p++;
    while (*p == ' ') p++;

    *is_default_out = (*p == '*');
    if (*is_default_out) p++;

    if (sscanf(p, "%d.", id_out) != 1) return 0;

    char *dot = strchr(p, '.');
    if (!dot) return 0;
    dot++;
    while (*dot == ' ') dot++;

    strncpy(name_out, dot, name_size - 1);
    name_out[name_size - 1] = '\0';
    char *bracket = strstr(name_out, " [vol:");
    if (bracket) *bracket = '\0';
    return 1;
}

/* Single wpctl status call, populates both sinks and sources categories */
static void rebuild_sinks_and_sources(MenuItem *sinks_cat, MenuItem *sources_cat) {
    menu_free_children(sinks_cat);
    menu_free_children(sources_cat);

    int count = 0;
    char **lines = run_cmd_lines("wpctl status 2>/dev/null", &count);

    enum { SECTION_NONE, SECTION_SINKS, SECTION_SOURCES } section = SECTION_NONE;

    for (int i = 0; i < count; i++) {
        char *line = lines[i];

        if (strstr(line, "Audio/Sink")) {
            section = SECTION_SINKS;
            continue;
        }
        if (strstr(line, "Audio/Source")) {
            section = SECTION_SOURCES;
            continue;
        }
        if (section != SECTION_NONE &&
            (line[0] == '\0' || strstr(line, "Video/") || strstr(line, "Streams:"))) {
            section = SECTION_NONE;
            continue;
        }
        if (section == SECTION_NONE) continue;

        int id, is_default;
        char name[128];
        if (!parse_device_line(line, &id, name, sizeof(name), &is_default))
            continue;

        MenuItem *target_cat = (section == SECTION_SINKS) ? sinks_cat : sources_cat;
        int is_sink = (section == SECTION_SINKS);

        char label[128];
        snprintf(label, sizeof(label), "%s%s", name, is_default ? " *" : "");

        char desc[128];
        snprintf(desc, sizeof(desc), "Set %s as default %s", name,
                 is_sink ? "output" : "input");

        MenuItem *item = menu_item_new(label, desc, MENU_ACTION);
        item->on_activate = is_sink ? sink_activate : source_activate;
        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%d", id);
        item->userdata = strdup(id_str);
        menu_add_child(target_cat, item);
    }

    if (menu_child_count(sinks_cat) == 0)
        menu_add_child(sinks_cat, menu_item_new("No sinks found", NULL, MENU_INFO));
    if (menu_child_count(sources_cat) == 0)
        menu_add_child(sources_cat, menu_item_new("No sources found", NULL, MENU_INFO));

    free_lines(lines, count);
}

static void audio_refresh(MenuItem *module_root) {
    /* 1 call for sink volume+mute, 1 call for source volume+mute (was 4) */
    int sink_vol, sink_muted, src_vol, src_muted;
    get_volume_and_mute("@DEFAULT_AUDIO_SINK@", &sink_vol, &sink_muted);
    get_volume_and_mute("@DEFAULT_AUDIO_SOURCE@", &src_vol, &src_muted);

    /* Find sink and source category nodes for the single wpctl status call */
    MenuItem *sinks_cat = NULL, *sources_cat = NULL;

    MenuItem *child = module_root->children;
    while (child) {
        if (child->type == MENU_TOGGLE && strstr(child->label, "Mute") && !strstr(child->label, "Mic"))
            child->toggled = sink_muted;
        else if (child->type == MENU_TOGGLE && strstr(child->label, "Mic"))
            child->toggled = src_muted;
        else if (child->type == MENU_INFO && strstr(child->label, "Volume") && sink_vol >= 0)
            snprintf(child->label, sizeof(child->label), "Volume: %d%%", sink_vol);
        else if (child->type == MENU_INFO && strstr(child->label, "Mic Level") && src_vol >= 0)
            snprintf(child->label, sizeof(child->label), "Mic Level: %d%%", src_vol);
        else if (child->type == MENU_CATEGORY && strstr(child->label, "Output"))
            sinks_cat = child;
        else if (child->type == MENU_CATEGORY && strstr(child->label, "Input"))
            sources_cat = child;
        child = child->next;
    }

    /* 1 call for both sinks and sources (was 2) */
    if (sinks_cat && sources_cat)
        rebuild_sinks_and_sources(sinks_cat, sources_cat);
}

static void audio_lazy_load(MenuItem *root) {
    int sink_vol, sink_muted;
    get_volume_and_mute("@DEFAULT_AUDIO_SINK@", &sink_vol, &sink_muted);

    MenuItem *vol_info = menu_item_new("Volume: ??%", NULL, MENU_INFO);
    if (sink_vol >= 0)
        snprintf(vol_info->label, sizeof(vol_info->label), "Volume: %d%%", sink_vol);
    menu_add_child(root, vol_info);

    MenuItem *mute = menu_item_new("Mute", "Toggle audio mute", MENU_TOGGLE);
    mute->on_activate = mute_activate;
    mute->toggled = sink_muted;
    menu_add_child(root, mute);

    MenuItem *vup = menu_item_new("Volume +5%", "Increase volume by 5%", MENU_ACTION);
    vup->on_activate = vol_up_activate;
    menu_add_child(root, vup);

    MenuItem *vdown = menu_item_new("Volume -5%", "Decrease volume by 5%", MENU_ACTION);
    vdown->on_activate = vol_down_activate;
    menu_add_child(root, vdown);

    MenuItem *sinks = menu_item_new("Output Devices", "Select default audio output", MENU_CATEGORY);
    menu_add_child(root, sinks);

    int src_vol, src_muted;
    get_volume_and_mute("@DEFAULT_AUDIO_SOURCE@", &src_vol, &src_muted);

    MenuItem *mic_info = menu_item_new("Mic Level: ??%", NULL, MENU_INFO);
    if (src_vol >= 0)
        snprintf(mic_info->label, sizeof(mic_info->label), "Mic Level: %d%%", src_vol);
    menu_add_child(root, mic_info);

    MenuItem *mic_mute = menu_item_new("Mic Mute", "Toggle microphone mute", MENU_TOGGLE);
    mic_mute->on_activate = mic_mute_activate;
    mic_mute->toggled = src_muted;
    menu_add_child(root, mic_mute);

    MenuItem *mup = menu_item_new("Mic Volume +5%", "Increase mic volume by 5%", MENU_ACTION);
    mup->on_activate = mic_vol_up_activate;
    menu_add_child(root, mup);

    MenuItem *mdown = menu_item_new("Mic Volume -5%", "Decrease mic volume by 5%", MENU_ACTION);
    mdown->on_activate = mic_vol_down_activate;
    menu_add_child(root, mdown);

    MenuItem *sources = menu_item_new("Input Devices", "Select default audio input", MENU_CATEGORY);
    menu_add_child(root, sources);

    rebuild_sinks_and_sources(sinks, sources);
}

static MenuItem *audio_build_menu(void) {
    MenuItem *root = menu_item_new("Audio", "Audio output and input settings", MENU_CATEGORY);
    root->on_lazy_load = audio_lazy_load;
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
