#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

/* Forward declarations */
static void get_volume_and_mute(const char *target, int *vol_out, int *muted_out);

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

/* Update slider label: "Volume [====------] 45%" */
static void update_slider_label(MenuItem *self, const char *target, const char *prefix) {
    int vol, muted;
    get_volume_and_mute(target, &vol, &muted);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    int filled = vol / 5; /* 20 chars wide */
    char bar[32];
    int i;
    for (i = 0; i < filled && i < 20; i++) bar[i] = '=';
    for (; i < 20; i++) bar[i] = '-';
    bar[20] = '\0';
    snprintf(self->label, sizeof(self->label), "%s [%s] %d%%%s",
             prefix, bar, vol, muted ? " [MUTED]" : "");
}

static void vol_right(MenuItem *self) {
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+");
    update_slider_label(self, "@DEFAULT_AUDIO_SINK@", "Volume");
}

static void vol_left(MenuItem *self) {
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-");
    update_slider_label(self, "@DEFAULT_AUDIO_SINK@", "Volume");
}

static void mic_vol_right(MenuItem *self) {
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 5%+");
    update_slider_label(self, "@DEFAULT_AUDIO_SOURCE@", "Mic");
}

static void mic_vol_left(MenuItem *self) {
    run_cmd_silent("wpctl set-volume @DEFAULT_AUDIO_SOURCE@ 5%-");
    update_slider_label(self, "@DEFAULT_AUDIO_SOURCE@", "Mic");
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
        else if (child->type == MENU_INFO && child->on_left && strstr(child->label, "Volume"))
            update_slider_label(child, "@DEFAULT_AUDIO_SINK@", "Volume");
        else if (child->type == MENU_INFO && child->on_left && strstr(child->label, "Mic"))
            update_slider_label(child, "@DEFAULT_AUDIO_SOURCE@", "Mic");
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

    /* Volume slider: left/right to adjust */
    MenuItem *vol_slider = menu_item_new("Volume", "Use left/right to adjust volume", MENU_INFO);
    vol_slider->on_left = vol_left;
    vol_slider->on_right = vol_right;
    update_slider_label(vol_slider, "@DEFAULT_AUDIO_SINK@", "Volume");
    menu_add_child(root, vol_slider);

    MenuItem *mute = menu_item_new("Mute", "Toggle audio mute", MENU_TOGGLE);
    mute->on_activate = mute_activate;
    mute->toggled = sink_muted;
    menu_add_child(root, mute);

    MenuItem *sinks = menu_item_new("Output Devices", "Select default audio output", MENU_CATEGORY);
    menu_add_child(root, sinks);

    int src_vol, src_muted;
    get_volume_and_mute("@DEFAULT_AUDIO_SOURCE@", &src_vol, &src_muted);

    /* Mic slider: left/right to adjust */
    MenuItem *mic_slider = menu_item_new("Mic", "Use left/right to adjust mic volume", MENU_INFO);
    mic_slider->on_left = mic_vol_left;
    mic_slider->on_right = mic_vol_right;
    update_slider_label(mic_slider, "@DEFAULT_AUDIO_SOURCE@", "Mic");
    menu_add_child(root, mic_slider);

    MenuItem *mic_mute = menu_item_new("Mic Mute", "Toggle microphone mute", MENU_TOGGLE);
    mic_mute->on_activate = mic_mute_activate;
    mic_mute->toggled = src_muted;
    menu_add_child(root, mic_mute);

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

static void audio_get_status(char *buf, size_t size) {
    int vol, muted;
    get_volume_and_mute("@DEFAULT_AUDIO_SINK@", &vol, &muted);
    if (muted)
        snprintf(buf, size, "muted");
    else if (vol >= 0)
        snprintf(buf, size, "%d%%", vol);
    else
        snprintf(buf, size, "?");
}

static BackendModule audio_module = {
    .name = "audio",
    .build_menu = audio_build_menu,
    .refresh_fn = audio_refresh,
    .get_status = audio_get_status,
    .cleanup = audio_cleanup,
};

BackendModule *mod_audio_init(void) {
    return &audio_module;
}
