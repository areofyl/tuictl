#ifndef BACKEND_H
#define BACKEND_H

#include "menu.h"

typedef struct {
    const char *name;
    MenuItem *(*build_menu)(void);
    void (*refresh_fn)(MenuItem *module_root);
    void (*get_status)(char *buf, size_t size); /* Short status for top-level label */
    void (*cleanup)(void);
} BackendModule;

/* Command execution helpers */
int run_cmd(const char *cmd, char *output, size_t output_size);
int run_cmd_silent(const char *cmd);
char **run_cmd_lines(const char *cmd, int *line_count);
void free_lines(char **lines, int count);

/* Module declarations */
BackendModule *mod_wifi_init(void);
BackendModule *mod_bluetooth_init(void);
BackendModule *mod_audio_init(void);
BackendModule *mod_network_init(void);

#endif
