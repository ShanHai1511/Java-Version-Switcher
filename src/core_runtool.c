#include "core.h"
#include "util.h"
#include <shlobj.h>
#include <stdio.h>
#include <process.h>

/* ================================================================
   Constants
   ================================================================ */

#define RUSTUP_TOOLCHAINS_DIR  "%USERPROFILE%\\.rustup\\toolchains"
#define RUSTUP_EXE             "%USERPROFILE%\\.cargo\\bin\\rustup.exe"
#define NVM_DIR                "%APPDATA%\\nvm"
#define NODE_DIR_PROGFILES     "C:\\Program Files\\nodejs"
#define NODE_DIR_PROGFILES86   "C:\\Program Files (x86)\\nodejs"
#define NODE_DIR_LOCAL         "%LOCALAPPDATA%\\Programs"
#define REG_ENV_PATH           "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
#define RUSTUP_SETTINGS        "%USERPROFILE%\\.rustup\\settings.toml"

/* ================================================================
   ToolList helpers
   ================================================================ */

ToolList *tool_list_new(void) {
    ToolList *tl = (ToolList *)calloc(1, sizeof(ToolList));
    if (!tl) return NULL;
    tl->cap = 4;
    tl->items = (ToolInfo **)malloc((size_t)tl->cap * sizeof(ToolInfo *));
    return tl;
}

void tool_list_free(ToolList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        ToolInfo *ti = list->items[i];
        if (!ti) continue;
        free(ti->version); free(ti->path); free(ti->channel);
        free(ti);
    }
    free(list->items);
    free(list);
}

static void tool_list_add(ToolList *tl, ToolInfo *ti) {
    if (!tl || !ti) return;
    if (tl->count >= tl->cap) {
        tl->cap = tl->cap < 8 ? 8 : tl->cap * 2;
        tl->items = (ToolInfo **)realloc(tl->items, (size_t)tl->cap * sizeof(ToolInfo *));
    }
    tl->items[tl->count++] = ti;
}

/* ================================================================
   Subprocess helpers
   ================================================================ */

/* Run a command and capture its stdout; returns malloc'd string or NULL. */
static char *run_capture(const char *cmdline) {
    if (!cmdline || !*cmdline) return NULL;

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return NULL;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = NULL;

    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, (LPSTR)cmdline, NULL, NULL, TRUE,
                         0, NULL, NULL, &si, &pi)) {
        CloseHandle(hWrite); CloseHandle(hRead);
        return NULL;
    }
    CloseHandle(hWrite);
    CloseHandle(pi.hThread);

    char buf[4096] = {0};
    DWORD read = 0, total = 0;
    while (ReadFile(hRead, buf + total, (DWORD)(sizeof(buf) - total - 1), &read, NULL) && read > 0)
        total += read;
    buf[total] = '\0';
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    if (exit_code != 0) { free(buf); return NULL; }

    return str_dup(buf);
}

/* ================================================================
   Rust: get current default channel
   ================================================================ */

char *get_current_rust_channel(void) {
    /* Try reading rustup settings file */
    char settings_path[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_SETTINGS, settings_path, sizeof(settings_path));

    FILE *f = fopen(settings_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        if (buf) {
            fread(buf, 1, (size_t)sz, f);
            buf[sz] = '\0';
            fclose(f);
            /* Look for "default_toolchain" */
            const char *p = strstr(buf, "default_toolchain");
            if (p) {
                p = strchr(p, '"');
                if (p) { p++; const char *q = strchr(p, '"'); if (q) {
                    size_t len = (size_t)(q - p);
                    char *ch = (char *)malloc(len + 1);
                    memcpy(ch, p, len); ch[len] = '\0';
                    free(buf);
                    return ch;
                }}
            }
            free(buf);
        } else {
            fclose(f);
        }
    }

    /* Fallback: run rustup show */
    char rustup_exe[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
    if (GetFileAttributesA(rustup_exe) != INVALID_FILE_ATTRIBUTES) {
        char *out = run_capture(fmt_alloc("\"%s\" show active-toolchain", rustup_exe));
        if (out && out[0]) {
            /* Output is like: "stable-x86_64-pc-windows-msvc (default)\n" or just "stable\n" */
            char *nl = strchr(out, '\n');
            if (nl) *nl = '\0';
            /* Remove trailing " (default)" */
            char *p = strstr(out, " (default)");
            if (p) *p = '\0';
            return out;
        }
        free(out);
    }
    return NULL;
}

/* ================================================================
   Node.js: get current version
   ================================================================ */

char *get_current_node_version(void) {
    /* Read NODE_HOME from registry */
    char *node_home = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "NODE_HOME");
    if (node_home && node_home[0]) {
        char node_exe[MAX_PATH];
        snprintf(node_exe, sizeof(node_exe), "%s\\node.exe", node_home);
        if (GetFileAttributesA(node_exe) != INVALID_FILE_ATTRIBUTES) {
            char *ver = run_capture(fmt_alloc("\"%s\" --version", node_exe));
            free(node_home);
            return ver;
        }
        free(node_home);
    }
    free(node_home);

    /* Fallback: look in PATH */
    char *path_val = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    if (path_val) {
        char *copy = str_dup(path_val);
        char *tok = strtok(copy, ";");
        while (tok) {
            char *trimmed = str_trim(tok);
            char node_exe[MAX_PATH];
            snprintf(node_exe, sizeof(node_exe), "%s\\node.exe", trimmed);
            if (GetFileAttributesA(node_exe) != INVALID_FILE_ATTRIBUTES) {
                char *ver = run_capture(fmt_alloc("\"%s\" --version", node_exe));
                free(copy); free(path_val);
                return ver;
            }
            tok = strtok(NULL, ";");
        }
        free(copy);
        free(path_val);
    }

    return NULL;
}

/* ================================================================
   Rust scan
   ================================================================ */

ToolList *scan_rust(void) {
    ToolList *tl = tool_list_new();
    if (!tl) return NULL;

    /* Get current active channel to mark is_current */
    char *current_chan = get_current_rust_channel();

    /* Enumerate toolchains in ~/.rustup/toolchains/ */
    char toolchain_dir[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_TOOLCHAINS_DIR, toolchain_dir, sizeof(toolchain_dir));

    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", toolchain_dir);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

            char full_path[MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s\\%s", toolchain_dir, fd.cFileName);
            char rustc_path[MAX_PATH];
            snprintf(rustc_path, sizeof(rustc_path), "%s\\bin\\rustc.exe", full_path);

            if (GetFileAttributesA(rustc_path) == INVALID_FILE_ATTRIBUTES) continue;

            /* Query version via rustup run */
            char rustup_exe[MAX_PATH];
            ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
            char *ver_out = NULL;
            if (GetFileAttributesA(rustup_exe) != INVALID_FILE_ATTRIBUTES) {
                ver_out = run_capture(
                    fmt_alloc("\"%s\" run %s rustc --version", rustup_exe, fd.cFileName));
            }
            if (!ver_out) {
                /* Fallback: just run the toolchain rustc directly */
                ver_out = run_capture(
                    fmt_alloc("\"%s\" --version", rustc_path));
            }

            ToolInfo *ti = (ToolInfo *)calloc(1, sizeof(ToolInfo));
            if (!ti) { free(ver_out); continue; }
            ti->channel  = str_dup(fd.cFileName);
            ti->version  = ver_out ? ver_out : str_dup("unknown");
            ti->path     = str_dup(full_path);
            ti->is_current = (current_chan && str_ieq(current_chan, fd.cFileName)) ? 1 : 0;
            ti->is_lts   = 0;
            tool_list_add(tl, ti);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    free(current_chan);

    /* Sort: current first, then by name */
    for (int i = 0; i < tl->count - 1; i++) {
        for (int j = i + 1; j < tl->count; j++) {
            int si = tl->items[i]->is_current ? 0 : 1;
            int sj = tl->items[j]->is_current ? 0 : 1;
            if (sj < si || (si == sj && strcmp(tl->items[j]->channel, tl->items[i]->channel) < 0)) {
                ToolInfo *tmp = tl->items[i];
                tl->items[i] = tl->items[j];
                tl->items[j] = tmp;
            }
        }
    }

    return tl;
}

/* ================================================================
   Node.js scan
   ================================================================ */

static ToolInfo *scan_node_dir(const char *dir, int *is_current_out) {
    char node_exe[MAX_PATH];
    snprintf(node_exe, sizeof(node_exe), "%s\\node.exe", dir);
    if (GetFileAttributesA(node_exe) == INVALID_FILE_ATTRIBUTES) return NULL;

    char *ver_out = run_capture(fmt_alloc("\"%s\" --version", node_exe));
    if (!ver_out || !ver_out[0]) { free(ver_out); return NULL; }

    /* node --version outputs "v20.11.1\n" */
    ToolInfo *ti = (ToolInfo *)calloc(1, sizeof(ToolInfo));
    if (!ti) { free(ver_out); return NULL; }

    /* Strip leading 'v' for display */
    char *v = ver_out;
    if (v[0] == 'v' || v[0] == 'V') v++;
    /* Trim newline */
    char *nl = strchr(v, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(v, '\r');
    if (cr) *cr = '\0';

    /* Detect LTS: try node --version --lts */
    int is_lts = 0;
    char *lts_out = run_capture(fmt_alloc("\"%s\" --version --lts", node_exe));
    if (lts_out && lts_out[0] && strstr(lts_out, "LTS")) {
        is_lts = 1;
    }
    free(lts_out);

    ti->version   = str_dup(v);
    ti->path      = str_dup(dir);
    ti->channel   = NULL;
    ti->is_lts    = is_lts;
    ti->is_current = *is_current_out;

    free(ver_out);
    return ti;
}

ToolList *scan_nodejs(void) {
    ToolList *tl = tool_list_new();
    if (!tl) return NULL;

    char *current_ver = get_current_node_version();
    /* Derive a simple current-version string for comparison */
    char current_clean[128] = {0};
    if (current_ver && current_ver[0]) {
        char *p = current_ver;
        if (p[0] == 'v' || p[0] == 'V') p++;
        strncpy(current_clean, p, sizeof(current_clean) - 1);
        char *nl = strchr(current_clean, '\n');
        if (nl) *nl = '\0';
    }

    /* 1. nvm directory: %APPDATA%\nvm\v* */
    char nvm_dir[MAX_PATH];
    ExpandEnvironmentStringsA(NVM_DIR, nvm_dir, sizeof(nvm_dir));
    char nvm_pat[MAX_PATH];
    snprintf(nvm_pat, sizeof(nvm_pat), "%s\\v*", nvm_dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(nvm_pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s\\%s", nvm_dir, fd.cFileName);
            int is_cur = (current_clean[0] && str_ieq(current_clean, fd.cFileName + 1)) ? 1 : 0;
            ToolInfo *ti = scan_node_dir(full, &is_cur);
            if (ti) tool_list_add(tl, ti);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* 2. Standalone: Program Files\nodejs */
    {
        int is_cur = 0;
        ToolInfo *ti = scan_node_dir(NODE_DIR_PROGFILES, &is_cur);
        if (ti) tool_list_add(tl, ti);
    }
    {
        int is_cur = 0;
        ToolInfo *ti = scan_node_dir(NODE_DIR_PROGFILES86, &is_cur);
        if (ti) tool_list_add(tl, ti);
    }

    /* 3. %LOCALAPPDATA%\Programs\nodejs */
    {
        char local_node[MAX_PATH];
        ExpandEnvironmentStringsA(NODE_DIR_LOCAL, local_node, sizeof(local_node));
        char final_dir[MAX_PATH];
        snprintf(final_dir, sizeof(final_dir), "%s\\nodejs", local_node);
        int is_cur = 0;
        ToolInfo *ti = scan_node_dir(final_dir, &is_cur);
        if (ti) tool_list_add(tl, ti);
    }

    free(current_ver);

    /* Sort: current first, then LTS, then by version desc */
    for (int i = 0; i < tl->count - 1; i++) {
        for (int j = i + 1; j < tl->count; j++) {
            int si = tl->items[i]->is_current ? 0 : (tl->items[i]->is_lts ? 2 : 3);
            int sj = tl->items[j]->is_current ? 0 : (tl->items[j]->is_lts ? 2 : 3);
            if (sj < si) {
                ToolInfo *tmp = tl->items[i]; tl->items[i] = tl->items[j]; tl->items[j] = tmp;
            }
        }
    }

    return tl;
}

/* ================================================================
   Switch
   ================================================================ */

static char *clean_path_rust(const char *path, const char *node_home_dir, int *removed_out);

int switch_rust(const char *toolchain, const char *backup_file) {
    (void)backup_file;

    if (!toolchain || !*toolchain) return -1;

    char rustup_exe[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
    if (GetFileAttributesA(rustup_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    char *out = run_capture(fmt_alloc("\"%s\" default %s", rustup_exe, toolchain));
    if (out) { free(out); return 0; }
    return -1;
}

/* ================================================================
   Node.js switch — write NODE_HOME + adjust PATH
   Returns 0 on success, -1 on failure.
   ================================================================ */

int switch_nodejs(const char *version_dir, const char *backup_file) {
    (void)backup_file;

    if (!version_dir || !*version_dir) return -1;

    char node_exe[MAX_PATH];
    snprintf(node_exe, sizeof(node_exe), "%s\\node.exe", version_dir);
    if (GetFileAttributesA(node_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    /* Write NODE_HOME */
    if (reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "NODE_HOME", version_dir) != 0)
        return -1;

    /* Remove old NODE_HOME\bin entries from PATH, then prepend new one */
    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");

    int removed = 0;
    char *new_path = clean_path_rust(old_path ? old_path : "", version_dir, &removed);
    free(old_path);

    /* Prepend new NODE_HOME\bin */
    char node_bin[MAX_PATH];
    snprintf(node_bin, sizeof(node_bin), "%s", version_dir);
    size_t total = strlen(node_bin) + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, node_bin);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}

/* ================================================================
   Path cleanup helper for Node.js (similar to clean_path in core.c
   but specialised: removes old NODE_HOME\bin prefix rather than
   %JAVA_HOME%\bin)
   ================================================================ */

static char *clean_path_rust(const char *path, const char *node_home_dir, int *removed_out) {
    if (!path) path = "";
    if (removed_out) *removed_out = 0;
    if (!path[0]) return str_dup("");

    char node_bin[MAX_PATH];
    snprintf(node_bin, sizeof(node_bin), "%s", node_home_dir ? node_home_dir : "");

    int removed = 0, cap = 16, count = 0;
    char **parts = (char **)malloc((size_t)cap * sizeof(char *));
    char *copy   = str_dup(path);
    char *tok    = strtok(copy, ";");

    while (tok) {
        char *trimmed = str_trim(tok);
        if (trimmed[0] == '\0') { tok = strtok(NULL, ";"); continue; }

        char low[MAX_PATH];
        strncpy(low, trimmed, sizeof(low) - 1);
        low[sizeof(low) - 1] = '\0';
        _strlwr_s(low, sizeof(low));

        int skip = 0;
        if (node_bin[0]) {
            char nb_low[MAX_PATH];
            strncpy(nb_low, node_bin, sizeof(nb_low) - 1);
            nb_low[sizeof(nb_low) - 1] = '\0';
            _strlwr_s(nb_low, sizeof(nb_low));
            if (strcmp(low, nb_low) == 0) skip = 1;
            else {
                char prefix[MAX_PATH];
                snprintf(prefix, sizeof(prefix), "%s\\", nb_low);
                if (strncmp(low, prefix, strlen(prefix)) == 0) skip = 1;
            }
        }

        if (skip) { removed++; }
        else {
            if (count >= cap) { cap *= 2; parts = (char **)realloc(parts, (size_t)cap * sizeof(char *)); }
            parts[count++] = str_dup(trimmed);
        }
        tok = strtok(NULL, ";");
    }
    free(copy);
    if (removed_out) *removed_out = removed;

    size_t total = 0;
    for (int i = 0; i < count; i++) total += strlen(parts[i]) + 1;
    char *result = (char *)malloc(total + 1);
    result[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(result, ";");
        strcat(result, parts[i]);
        free(parts[i]);
    }
    free(parts);
    return result;
}
