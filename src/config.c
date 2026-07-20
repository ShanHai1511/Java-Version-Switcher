#include "config.h"
#include <shlobj.h>

#define CONFIG_SECTION_JDK   "JDK"
#define CONFIG_SECTION_DL    "Download"
#define CONFIG_SECTION_UI    "UI"
#define DEFAULT_MIRROR       "https://repo.huaweicloud.com/java/jdk/"

/* ── Defaults ───────────────────────────────────────── */

Config *config_default(void) {
    Config *c = (Config *)calloc(1, sizeof(Config));
    if (!c) return NULL;
    c->scan_paths_cap = 4;
    c->scan_paths = (char **)malloc((size_t)c->scan_paths_cap * sizeof(char *));
    c->auto_extract = 1;
    c->mirror = str_dup(DEFAULT_MIRROR);
    return c;
}

void config_free(Config *c) {
    if (!c) return;
    for (int i = 0; i < c->scan_paths_count; i++) free(c->scan_paths[i]);
    free(c->scan_paths);
    free(c->last_used);
    free(c->mirror);
    free(c);
}

/* ── Config file location ───────────────────────────── */

static char *config_path_alloc(void) {
    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata))) {
        return fmt_alloc("%s\\JVS\\jvs.ini", appdata);
    }
    return fmt_alloc("%s\\JVS\\jvs.ini", getenv("USERPROFILE"));
}

const char *config_path(void) {
    static char *g_path = NULL;
    if (!g_path) g_path = config_path_alloc();
    return g_path;
}

const char *config_dir(void) {
    static char *g_dir = NULL;
    if (!g_dir) {
        char *p = path_dirname(config_path());
        g_dir = p;
    }
    return g_dir;
}

/* ── Load / Save ────────────────────────────────────── */

Config *config_load(const char *path) {
    Config *c = config_default();
    if (!c) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return c; /* not-found → defaults */

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return c; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    IniTable *t = ini_parse(buf);
    free(buf);

    /* Scan paths: jdk.scan_paths_0 … */
    char key[64];
    for (int i = 0; ; i++) {
        snprintf(key, sizeof(key), "%s.scan_paths_%d", CONFIG_SECTION_JDK, i);
        char *val = ini_get(t, key);
        if (!val) break;
        config_add_scan_path(c, val);
    }

    c->last_used = str_dup(ini_get_def(t, "jdk.last_used", ""));

    char *mirror = ini_get(t, "download.mirror");
    if (mirror) { free(c->mirror); c->mirror = str_dup(mirror); }

    c->auto_extract = (ini_get(t, "download.auto_extract") != NULL);

    c->always_on_top   = (ini_get(t, "ui.always_on_top")   != NULL);
    c->start_minimized = (ini_get(t, "ui.start_minimized") != NULL);

    ini_free(t);
    return c;
}

static void ini_write_str(FILE *f, const char *key, const char *val) {
    fprintf(f, "%s = %s\n", key, val);
}

int config_save(const Config *c, const char *path) {
    /* ensure directory exists */
    char *dir = path_dirname(path);
    CreateDirectoryA(dir, NULL); /* ignore errors, dir may exist */
    free(dir);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "[JDK]\n");
    for (int i = 0; i < c->scan_paths_count; i++) {
        fprintf(f, "scan_paths_%d = %s\n", i, c->scan_paths[i]);
    }
    if (c->last_used && c->last_used[0]) {
        fprintf(f, "last_used = %s\n", c->last_used);
    }

    fprintf(f, "\n[Download]\n");
    fprintf(f, "mirror = %s\n", c->mirror ? c->mirror : DEFAULT_MIRROR);
    fprintf(f, "auto_extract = %s\n", c->auto_extract ? "true" : "false");

    fprintf(f, "\n[UI]\n");
    fprintf(f, "always_on_top = %s\n", c->always_on_top   ? "true" : "false");
    fprintf(f, "start_minimized = %s\n", c->start_minimized ? "true" : "false");

    fclose(f);
    return 0;
}

/* ── Mutators ───────────────────────────────────────── */

int config_add_scan_path(Config *c, const char *path) {
    char *p = str_dup(path);
    str_trim(p);
    /* deduplicate */
    for (int i = 0; i < c->scan_paths_count; i++) {
        if (str_ieq(c->scan_paths[i], p)) { free(p); return 0; }
    }
    if (c->scan_paths_count >= c->scan_paths_cap) {
        c->scan_paths_cap = c->scan_paths_cap < 8 ? 8 : c->scan_paths_cap * 2;
        c->scan_paths = (char **)realloc(c->scan_paths, (size_t)c->scan_paths_cap * sizeof(char *));
    }
    c->scan_paths[c->scan_paths_count++] = p;
    return 0;
}

int config_remove_scan_path(Config *c, const char *path) {
    for (int i = 0; i < c->scan_paths_count; i++) {
        if (str_ieq(c->scan_paths[i], path)) {
            free(c->scan_paths[i]);
            memmove(&c->scan_paths[i], &c->scan_paths[i + 1],
                    (size_t)(c->scan_paths_count - i - 1) * sizeof(char *));
            c->scan_paths_count--;
            return 0;
        }
    }
    return -1;
}

int config_update_last_used(Config *c, const char *path) {
    free(c->last_used);
    c->last_used = str_dup(path ? path : "");
    return config_save(c, config_path());
}
