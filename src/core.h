#ifndef JVS_CORE_H
#define JVS_CORE_H

#include <windows.h>
#include <stdio.h>
#include "util.h"

/* ── JDK Info ───────────────────────────────────────── */

typedef struct {
    char *version;
    int   major;
    char *vendor;
    char *path;
    int   is_current;
    int   is_portable;
    char *tag;
} JDKInfo;

JDKInfo *jdk_info_new(void);
void     jdk_info_free(JDKInfo *j);

char *jdk_display_name(const JDKInfo *j);

/* ── Scan ───────────────────────────────────────────── */

typedef struct {
    JDKInfo **items;
    int        count;
    int        cap;
} JDKList;

JDKList *scan_all(const char **scan_paths, int scan_paths_count);
JDKList *scan_directory(const char *root, int max_depth);
void     jdk_list_free(JDKList *list);

/* ── Switch (Registry) ──────────────────────────────── */

typedef struct {
    int    success;
    char  *old_home;
    char  *new_home;
    int    path_cleaned;
    char  *error;
} SwitchResult;

SwitchResult *switch_jdk(const char *jdk_path, const char *backup_file);
void          switch_result_free(SwitchResult *r);

/* ── Registry helpers ───────────────────────────────── */

char *reg_read_string(HKEY root, const char *subkey, const char *name);
int   reg_write_string(HKEY root, const char *subkey, const char *name, const char *val);
int   reg_delete_value(HKEY root, const char *subkey, const char *name);
char **reg_enum_subkeys(HKEY root, const char *subkey, int *out_count);

char  *get_current_java_home(void);
int    broadcast_env_change(void);
int    backup_env_vars(const char *filepath);

/* ── Path cleanup ───────────────────────────────────── */

char *clean_path(const char *path, const char *old_jdk_dir, int *removed_out);

#endif /* JVS_CORE_H */
