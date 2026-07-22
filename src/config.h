#ifndef JVS_CONFIG_H
#define JVS_CONFIG_H

#include "util.h"

/* ── Config struct ──────────────────────────────────── */

typedef struct {
    char **scan_paths;
    int    scan_paths_count;
    int    scan_paths_cap;

    /* per-tool extra scan paths */
    char **scan_paths_python;
    int    scan_paths_python_count;
    int    scan_paths_python_cap;
    char **scan_paths_go;
    int    scan_paths_go_count;
    int    scan_paths_go_cap;

    char  *last_used;
    char  *mirror;
    int    auto_extract;
    int    always_on_top;
    int    start_minimized;
    int    dark_mode;
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

int  config_add_scan_path_python(Config *c, const char *path);
int  config_add_scan_path_go(Config *c, const char *path);

/* ── Config backup / restore ────────────────────────── */

int config_backup(const char *backup_path);
int config_restore(const char *backup_path);

#endif /* JVS_CONFIG_H */
