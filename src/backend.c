#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

int run_cmd(const char *cmd, char *output, size_t output_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(output, 1, output_size - 1, fp);
    output[n] = '\0';
    /* Strip trailing newline */
    while (n > 0 && (output[n-1] == '\n' || output[n-1] == '\r'))
        output[--n] = '\0';
    return pclose(fp) >> 8;
}

int run_cmd_silent(const char *cmd) {
    char buf[256];
    return run_cmd(cmd, buf, sizeof(buf));
}

char **run_cmd_lines(const char *cmd, int *line_count) {
    FILE *fp = popen(cmd, "r");
    if (!fp) { *line_count = 0; return NULL; }

    char **lines = NULL;
    int count = 0;
    char line[512];

    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        lines = realloc(lines, (count + 1) * sizeof(char *));
        lines[count] = strdup(line);
        count++;
    }

    pclose(fp);
    *line_count = count;
    return lines;
}

void free_lines(char **lines, int count) {
    if (!lines) return;
    for (int i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}
