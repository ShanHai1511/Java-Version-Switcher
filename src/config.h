#ifndef JVS_CONFIG_H
#define JVS_CONFIG_H

#include "util.h"

/* ── Config struct ──────────────────────────────────── */

typedef struct {
    char **scan_paths;
    int    scan_paths_count;
    int    scan_paths_cap;
    char  *last_used;
    char  *mirror;
    int    auto_extract;
    int    always_on_top;
    int    start_minimized;
} Config;

/* ── Lifecycle ──────────────────────────────────────── */

Config *config_default(void);
void    config_free(Config *c);
Config *config_load(const char *path);
int     config_save(const Config *c, const char *path);

/* ── Helpers ────────────────────────────────────────── */

const char *config_path(void);
const char *config_dir(void);

int  config_add_scan_path(Config *c, const char *path);
int  config_remove_scan_path(Config *c, const char *path);
int  config_update_last_used(Config *c, const char *path);

#endif /* JVS_CONFIG_H */
