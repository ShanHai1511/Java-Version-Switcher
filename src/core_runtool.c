#include "core.h"
#include "util.h"
#include <shlobj.h>
#include <stdio.h>
#include <process.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

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
#define PYTHON_DIR_LOCAL       "%LOCALAPPDATA%\\Programs\\Python"
#define PY_LAUNCHER            "C:\\Windows\\py.exe"
#define GOROOT_REG_PATH        "SOFTWARE\\Go"

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
   MavenList helpers
   ================================================================ */

static MvnList *mvn_list_new(void) {
    MvnList *ml = (MvnList *)calloc(1, sizeof(MvnList));
    if (!ml) return NULL;
    ml->cap = 4;
    ml->items = (MvnInfo **)malloc((size_t)ml->cap * sizeof(MvnInfo *));
    return ml;
}

static void mvn_list_add(MvnList *ml, MvnInfo *mi) {
    if (!ml || !mi) return;
    if (ml->count >= ml->cap) {
        ml->cap = ml->cap < 8 ? 8 : ml->cap * 2;
        ml->items = (MvnInfo **)realloc(ml->items, (size_t)ml->cap * sizeof(MvnInfo *));
    }
    ml->items[ml->count++] = mi;
}

void mvn_list_free(MvnList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        MvnInfo *mi = list->items[i];
        if (!mi) continue;
        free(mi->version); free(mi->path); free(mi);
    }
    free(list->items);
    free(list);
}

/* ================================================================
   GradleList helpers
   ================================================================ */

static GradleList *gradle_list_new(void) {
    GradleList *gl = (GradleList *)calloc(1, sizeof(GradleList));
    if (!gl) return NULL;
    gl->cap = 4;
    gl->items = (GradleInfo **)malloc((size_t)gl->cap * sizeof(GradleInfo *));
    return gl;
}

static void gradle_list_add(GradleList *gl, GradleInfo *gi) {
    if (!gl || !gi) return;
    if (gl->count >= gl->cap) {
        gl->cap = gl->cap < 8 ? 8 : gl->cap * 2;
        gl->items = (GradleInfo **)realloc(gl->items, (size_t)gl->cap * sizeof(GradleInfo *));
    }
    gl->items[gl->count++] = gi;
}

void gradle_list_free(GradleList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        GradleInfo *gi = list->items[i];
        if (!gi) continue;
        free(gi->version); free(gi->path); free(gi);
    }
    free(list->items);
    free(list);
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
            char *nl = strchr(out, '\n');
            if (nl) *nl = '\0';
            char *p = strstr(out, " (default)");
            if (p) *p = '\0';
            return out;
        }
        free(out);
    }
    return NULL;
}

/* ================================================================
   Python: get current active version
   ================================================================ */

char *get_current_python_version(void) {
    /* 1. Try py --list-paths to find current */
    char *out = run_capture("py --list-paths");
    if (out) {
        /* Output: "-V:3.12  C:\Python312\python.exe" - first non-dash line is current */
        char *line = out;
        int found_current = 0;
        while (*line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            char *trimmed = str_trim(line);
            if (trimmed[0] == '-') {
                line = nl ? nl + 1 : line + strlen(line);
                continue;
            }
            /* This line is the current Python */
            if (trimmed[0]) {
                /* Extract path after the space */
                char *sp = strchr(trimmed, ' ');
                if (sp) {
                    char *path = str_trim(sp + 1);
                    /* Remove trailing \python.exe */
                    char *p = strstr(path, "\\python.exe");
                    if (p) *p = '\0';
                    /* Get version */
                    char ver_cmd[MAX_PATH * 2];
                    snprintf(ver_cmd, sizeof(ver_cmd), "\"%s\\python.exe\" --version", path);
                    char *ver = run_capture(ver_cmd);
                    free(out);
                    return ver;
                }
            }
            line = nl ? nl + 1 : line + strlen(line);
            if (found_current) break;
            found_current = 1;
        }
        free(out);
    }

    /* 2. Try PY_PYTHON registry or PATH */
    char *py_home = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "PY_HOME");
    if (py_home && py_home[0]) {
        char py_exe[MAX_PATH];
        snprintf(py_exe, sizeof(py_exe), "%s\\python.exe", py_home);
        if (GetFileAttributesA(py_exe) != INVALID_FILE_ATTRIBUTES) {
            char *ver = run_capture(fmt_alloc("\"%s\" --version", py_exe));
            free(py_home);
            return ver;
        }
        free(py_home);
    }

    /* 3. Fallback: search PATH */
    char *path_val = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    if (path_val) {
        char *copy = str_dup(path_val);
        char *tok = strtok(copy, ";");
        while (tok) {
            char *trimmed = str_trim(tok);
            char py_exe[MAX_PATH];
            snprintf(py_exe, sizeof(py_exe), "%s\\python.exe", trimmed);
            if (GetFileAttributesA(py_exe) != INVALID_FILE_ATTRIBUTES) {
                char *ver = run_capture(fmt_alloc("\"%s\" --version", py_exe));
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
   Go: get current GOROOT
   ================================================================ */

char *get_current_goroot(void) {
    /* 1. Try go env GOROOT via PATH */
    char *path_val = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    if (path_val) {
        char *copy = str_dup(path_val);
        char *tok = strtok(copy, ";");
        while (tok) {
            char *trimmed = str_trim(tok);
            char go_exe[MAX_PATH];
            snprintf(go_exe, sizeof(go_exe), "%s\\go.exe", trimmed);
            if (GetFileAttributesA(go_exe) != INVALID_FILE_ATTRIBUTES) {
                char *out = run_capture(fmt_alloc("\"%s\" env GOROOT", go_exe));
                if (out && out[0]) {
                    char *nl = strchr(out, '\n');
                    if (nl) *nl = '\0';
                    char *cr = strchr(out, '\r');
                    if (cr) *cr = '\0';
                    free(copy); free(path_val);
                    return out;
                }
                free(out);
            }
            tok = strtok(NULL, ";");
        }
        free(copy);
        free(path_val);
    }

    /* 2. Try registry GOROOT */
    char *goroot = reg_read_string(HKEY_LOCAL_MACHINE, GOROOT_REG_PATH, "GoRoot");
    if (goroot && goroot[0]) return goroot;
    free(goroot);

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
   Python scan
   ================================================================ */

static ToolInfo *scan_python_dir(const char *dir) {
    char py_exe[MAX_PATH];
    snprintf(py_exe, sizeof(py_exe), "%s\\python.exe", dir);
    if (GetFileAttributesA(py_exe) == INVALID_FILE_ATTRIBUTES) return NULL;

    char *ver_out = run_capture(fmt_alloc("\"%s\" --version", py_exe));
    if (!ver_out || !ver_out[0]) { free(ver_out); return NULL; }

    ToolInfo *ti = (ToolInfo *)calloc(1, sizeof(ToolInfo));
    if (!ti) { free(ver_out); return NULL; }

    /* python --version outputs "Python 3.12.4\n" */
    char *v = ver_out;
    /* Strip "Python " prefix if present */
    char *p = strstr(v, "Python ");
    if (p) v = p + 7;
    char *nl = strchr(v, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(v, '\r');
    if (cr) *cr = '\0';

    /* Detect LTS/Latest via --version.latest */
    int is_lts = 0;
    char *lts_out = run_capture(fmt_alloc("\"%s\" --version.latest", py_exe));
    if (lts_out && lts_out[0] && strstr(lts_out, "LTS")) {
        is_lts = 1;
    }
    free(lts_out);

    ti->version   = str_dup(v);
    ti->path      = str_dup(dir);
    ti->channel   = NULL;
    ti->is_lts    = is_lts;
    ti->is_current = 0;

    free(ver_out);
    return ti;
}

ToolList *scan_python(void) {
    ToolList *tl = tool_list_new();
    if (!tl) return NULL;

    /* Get current active Python */
    char *current_ver = get_current_python_version();
    char current_clean[128] = {0};
    if (current_ver && current_ver[0]) {
        char *p = current_ver;
        if (strstr(p, "Python ")) p += 7;
        strncpy(current_clean, p, sizeof(current_clean) - 1);
        char *nl = strchr(current_clean, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(current_clean, '\r');
        if (cr) *cr = '\0';
    }

    /* 1. %LOCALAPPDATA%\Programs\Python\* */
    char py_local[MAX_PATH];
    ExpandEnvironmentStringsA(PYTHON_DIR_LOCAL, py_local, sizeof(py_local));
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", py_local);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "%s\\%s", py_local, fd.cFileName);
            int is_cur = (current_clean[0] && str_ieq(current_clean, fd.cFileName)) ? 1 : 0;
            ToolInfo *ti = scan_python_dir(full);
            if (ti) {
                ti->is_current = is_cur;
                tool_list_add(tl, ti);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* 2. C:\Python* */
    {
        char pat2[MAX_PATH];
        snprintf(pat2, sizeof(pat2), "C:\\Python*");
        HANDLE h2 = FindFirstFileA(pat2, &fd);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                char full[MAX_PATH];
                snprintf(full, sizeof(full), "C:\\%s", fd.cFileName);
                int is_cur = (current_clean[0] && str_ieq(current_clean, fd.cFileName + 6)) ? 1 : 0;
                ToolInfo *ti = scan_python_dir(full);
                if (ti) {
                    ti->is_current = is_cur;
                    tool_list_add(tl, ti);
                }
            } while (FindNextFileA(h2, &fd));
            FindClose(h2);
        }
    }

    /* 3. py.exe launcher --list-paths */
    {
        char *py_out = run_capture("py --list-paths");
        if (py_out) {
            char *line = py_out;
            int first = 1;
            while (*line) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                char *trimmed = str_trim(line);
                if (trimmed[0] == '-') {
                    line = nl ? nl + 1 : line + strlen(line);
                    continue;
                }
                if (trimmed[0] && first) {
                    /* First non-dash line is current - already handled above, skip */
                    first = 0;
                    line = nl ? nl + 1 : line + strlen(line);
                    continue;
                }
                /* Extract path: format is "3.12 C:\Python312\python.exe" */
                char *sp = strchr(trimmed, ' ');
                if (sp) {
                    sp = str_trim(sp + 1);
                    /* Remove trailing \python.exe */
                    char *pe = strstr(sp, "\\python.exe");
                    if (pe) *pe = '\0';
                    /* Check not already added */
                    int found = 0;
                    for (int i = 0; i < tl->count; i++) {
                        if (str_ieq(tl->items[i]->path, sp)) { found = 1; break; }
                    }
                    if (!found) {
                        ToolInfo *ti = scan_python_dir(sp);
                        if (ti) tool_list_add(tl, ti);
                    }
                }
                line = nl ? nl + 1 : line + strlen(line);
            }
            free(py_out);
        }
    }

    free(current_ver);
    return tl;
}

/* ================================================================
   Go scan
   ================================================================ */

static ToolInfo *scan_go_dir(const char *dir) {
    char go_exe[MAX_PATH];
    snprintf(go_exe, sizeof(go_exe), "%s\\bin\\go.exe", dir);
    if (GetFileAttributesA(go_exe) == INVALID_FILE_ATTRIBUTES) return NULL;

    char *ver_out = run_capture(fmt_alloc("\"%s\" version", go_exe));
    if (!ver_out || !ver_out[0]) { free(ver_out); return NULL; }

    ToolInfo *ti = (ToolInfo *)calloc(1, sizeof(ToolInfo));
    if (!ti) { free(ver_out); return NULL; }

    /* go version outputs "go version go1.23.3 windows/amd64" */
    char *v = strstr(ver_out, "go");
    if (v) v = strstr(v, "go"); /* second "go" */
    if (v) v += 2; /* skip "go" to get version number */
    if (v) {
        char *sp = strchr(v, ' ');
        if (sp) *sp = '\0';
        char *nl = strchr(v, '\n');
        if (nl) *nl = '\0';
    }
    if (!v || !*v) { free(ver_out); free(ti); return NULL; }

    ti->version   = str_dup(v);
    ti->path      = str_dup(dir);
    ti->channel   = NULL;
    ti->is_lts    = 0;
    ti->is_current = 0;

    free(ver_out);
    return ti;
}

ToolList *scan_go(void) {
    ToolList *tl = tool_list_new();
    if (!tl) return NULL;

    /* Get current GOROOT */
    char *current_goroot = get_current_goroot();

    /* 1. C:\Go* */
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "C:\\Go*");
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "C:\\%s", fd.cFileName);
            int is_cur = (current_goroot && str_ieq(current_goroot, full)) ? 1 : 0;
            ToolInfo *ti = scan_go_dir(full);
            if (ti) {
                ti->is_current = is_cur;
                tool_list_add(tl, ti);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* 2. %USERPROFILE%\go */
    {
        char home_go[MAX_PATH];
        ExpandEnvironmentStringsA("%USERPROFILE%\\go", home_go, sizeof(home_go));
        int is_cur = (current_goroot && str_ieq(current_goroot, home_go)) ? 1 : 0;
        ToolInfo *ti = scan_go_dir(home_go);
        if (ti) {
            ti->is_current = is_cur;
            tool_list_add(tl, ti);
        }
    }

    /* Sort: current first */
    for (int i = 0; i < tl->count - 1; i++) {
        for (int j = i + 1; j < tl->count; j++) {
            if (tl->items[j]->is_current && !tl->items[i]->is_current) {
                ToolInfo *tmp = tl->items[i]; tl->items[i] = tl->items[j]; tl->items[j] = tmp;
            }
        }
    }

    free(current_goroot);
    return tl;
}

/* ================================================================
   Maven scan
   ================================================================ */

static char *read_maven_version(const char *mvn_bat) {
    /* Try: <dir>\bin\mvn.cmd --version or <dir>\bin\mvn.bat --version */
    char *out = run_capture(fmt_alloc("\"%s\" --version 2>&1", mvn_bat));
    if (!out) return NULL;

    /* Output starts with "Apache Maven X.Y.Z" */
    char *p = strstr(out, "Apache Maven");
    if (p) {
        p += 14; /* skip "Apache Maven " */
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *ver = str_dup(p);
        free(out);
        return ver;
    }
    free(out);
    return NULL;
}

static char *find_maven_home(void) {
    /* 1. Registry: HKLM\SOFTWARE\Apache\Apache Maven\CurrentVersion → HKLM\SOFTWARE\Apache\Apache Maven\<ver>\InstallLocation */
    char *ver = reg_read_string(HKEY_LOCAL_MACHINE, "SOFTWARE\\Apache\\Apache Maven", "CurrentVersion");
    if (ver && ver[0]) {
        char sub[MAX_PATH];
        snprintf(sub, sizeof(sub), "SOFTWARE\\Apache\\Apache Maven\\%s", ver);
        char *home = reg_read_string(HKEY_LOCAL_MACHINE, sub, "InstallLocation");
        free(ver);
        if (home && home[0]) return home;
        free(home);
    }
    free(ver);

    /* 2. Environment variable MAVEN_HOME */
    char *maven_home = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "MAVEN_HOME");
    if (maven_home && maven_home[0]) return maven_home;
    free(maven_home);

    /* 3. M2_HOME */
    maven_home = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "M2_HOME");
    if (maven_home && maven_home[0]) return maven_home;
    free(maven_home);

    return NULL;
}

MvnList *scan_maven(void) {
    MvnList *ml = mvn_list_new();
    if (!ml) return NULL;

    char *current_home = find_maven_home();

    /* Scan common install dirs */
    const char *scan_dirs[] = {
        "C:\\apache-maven-*",
        "C:\\Program Files\\apache-maven*",
        "C:\\Program Files (x86)\\apache-maven*",
        NULL
    };

    for (int d = 0; scan_dirs[d]; d++) {
        WIN32_FIND_DATAA fdd;
        char pat[MAX_PATH];
        strncpy(pat, scan_dirs[d], sizeof(pat) - 1);
        pat[sizeof(pat) - 1] = '\0';
        HANDLE h = FindFirstFileA(pat, &fdd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fdd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fdd.cFileName[0] == '.') continue;
            char mvn_cmd[MAX_PATH];
            snprintf(mvn_cmd, sizeof(mvn_cmd), "%s\\%s\\bin\\mvn.cmd",
                     strrchr(pat, '\\') ? strrchr(pat, '\\') + 1 : "", fdd.cFileName);
            char *ver = read_maven_version(mvn_cmd);
            if (!ver) {
                /* try .bat */
                snprintf(mvn_cmd, sizeof(mvn_cmd), "%s\\%s\\bin\\mvn.bat",
                         strrchr(pat, '\\') ? strrchr(pat, '\\') + 1 : "", fdd.cFileName);
                ver = read_maven_version(mvn_cmd);
            }
            if (!ver) {
                /* build full path and try */
                char full_dir[MAX_PATH];
                const char *base = strrchr(pat, '\\');
                snprintf(full_dir, sizeof(full_dir), "%s\\%s", base ? base + 1 : "", fdd.cFileName);
                snprintf(mvn_cmd, sizeof(mvn_cmd), "%s\\bin\\mvn.cmd", full_dir);
                ver = read_maven_version(mvn_cmd);
            }
            if (ver) {
                MvnInfo *mi = (MvnInfo *)calloc(1, sizeof(MvnInfo));
                if (mi) {
                    mi->version = ver;
                    mi->path = fmt_alloc("%s\\%s",
                                         strrchr(pat, '\\') ? strrchr(pat, '\\') + 1 : "",
                                         fdd.cFileName);
                    mi->is_current = (current_home && str_ieq(mi->path, current_home)) ? 1 : 0;
                    mvn_list_add(ml, mi);
                }
            }
        } while (FindNextFileA(h, &fdd));
        FindClose(h);
    }

    /* Also scan %USERPROFILE%\scoop\apps\maven\current */
    {
        char home[MAX_PATH];
        ExpandEnvironmentStringsA("%USERPROFILE%", home, sizeof(home));
        char scoop[MAX_PATH];
        snprintf(scoop, sizeof(scoop), "%s\\scoop\\apps\\maven\\current", home);
        char mvn_cmd[MAX_PATH];
        snprintf(mvn_cmd, sizeof(mvn_cmd), "%s\\bin\\mvn.cmd", scoop);
        char *ver = read_maven_version(mvn_cmd);
        if (ver) {
            MvnInfo *mi = (MvnInfo *)calloc(1, sizeof(MvnInfo));
            if (mi) {
                mi->version = ver;
                mi->path = str_dup(scoop);
                mi->is_current = (current_home && str_ieq(mi->path, current_home)) ? 1 : 0;
                mvn_list_add(ml, mi);
            }
        }
    }

    free(current_home);
    return ml;
}

/* ================================================================
   Gradle scan
   ================================================================ */

static char *read_gradle_version(const char *gradle_bat) {
    char *out = run_capture(fmt_alloc("\"%s\" --version 2>&1", gradle_bat));
    if (!out) return NULL;

    /* Output has "Gradle X.Y" */
    char *p = strstr(out, "Gradle ");
    if (p) {
        p += 7;
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *sp = strchr(p, ' ');
        if (sp) *sp = '\0';
        char *ver = str_dup(p);
        free(out);
        return ver;
    }
    free(out);
    return NULL;
}

static char *find_gradle_home(void) {
    /* Try GRADLE_HOME registry/env */
    char *gh = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "GRADLE_HOME");
    if (gh && gh[0]) return gh;
    free(gh);

    /* Try PATH: look for gradle.bat */
    char *path_val = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    if (path_val) {
        char *copy = str_dup(path_val);
        char *tok = strtok(copy, ";");
        while (tok) {
            char *trimmed = str_trim(tok);
            char gb[MAX_PATH];
            snprintf(gb, sizeof(gb), "%s\\bin\\gradle.bat", trimmed);
            if (GetFileAttributesA(gb) != INVALID_FILE_ATTRIBUTES) {
                char *parent = path_dirname(path_dirname(trimmed));
                free(copy); free(path_val);
                return parent;
            }
            tok = strtok(NULL, ";");
        }
        free(copy);
        free(path_val);
    }
    return NULL;
}

GradleList *scan_gradle(void) {
    GradleList *gl = gradle_list_new();
    if (!gl) return NULL;

    char *current_home = find_gradle_home();

    /* 1. C:\gradle-* */
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "C:\\gradle-*");
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char full[MAX_PATH];
            snprintf(full, sizeof(full), "C:\\%s", fd.cFileName);
            char gb[MAX_PATH];
            snprintf(gb, sizeof(gb), "%s\\bin\\gradle.bat", full);
            char *ver = read_gradle_version(gb);
            if (ver) {
                GradleInfo *gi = (GradleInfo *)calloc(1, sizeof(GradleInfo));
                if (gi) {
                    gi->version    = ver;
                    gi->path       = str_dup(full);
                    gi->is_current = (current_home && str_ieq(gi->path, current_home)) ? 1 : 0;
                    gradle_list_add(gl, gi);
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* 2. %USERPROFILE%\gradle\wrapper\dists\* */
    {
        char home[MAX_PATH];
        ExpandEnvironmentStringsA("%USERPROFILE%", home, sizeof(home));
        char dists[MAX_PATH];
        snprintf(dists, sizeof(dists), "%s\\gradle\\wrapper\\dists", home);
        char dist_pat[MAX_PATH];
        snprintf(dist_pat, sizeof(dist_pat), "%s\\*", dists);
        HANDLE h2 = FindFirstFileA(dist_pat, &fd);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                /* Look for bin\gradle.bat inside versioned dir */
                char inner_pat[MAX_PATH];
                snprintf(inner_pat, sizeof(inner_pat), "%s\\%s\\*", dists, fd.cFileName);
                WIN32_FIND_DATAA fd2;
                HANDLE h3 = FindFirstFileA(inner_pat, &fd2);
                if (h3 != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                        char gb[MAX_PATH];
                        snprintf(gb, sizeof(gb), "%s\\%s\\%s\\bin\\gradle.bat",
                                 dists, fd.cFileName, fd2.cFileName);
                        char *ver = read_gradle_version(gb);
                        if (ver) {
                            GradleInfo *gi = (GradleInfo *)calloc(1, sizeof(GradleInfo));
                            if (gi) {
                                gi->path = fmt_alloc("%s\\%s\\%s", dists, fd.cFileName, fd2.cFileName);
                                gi->version    = ver;
                                gi->is_current = 0;
                                gradle_list_add(gl, gi);
                            }
                        }
                    } while (FindNextFileA(h3, &fd2));
                    FindClose(h3);
                }
            } while (FindNextFileA(h2, &fd));
            FindClose(h2);
        }
    }

    free(current_home);
    return gl;
}

/* ================================================================
   Rust switch — with backup/restore protection
   ================================================================ */

static char *clean_path_rust(const char *path, const char *home_dir, int *removed_out);

int switch_rust(const char *toolchain, const char *backup_file) {
    if (!toolchain || !*toolchain) return -1;

    char rustup_exe[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
    if (GetFileAttributesA(rustup_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    /* Step 1: backup env vars */
    if (backup_file && backup_file[0]) {
        if (backup_env_vars(backup_file) != 0) return -1;
    }

    /* Step 2: execute rustup default */
    char *out = run_capture(fmt_alloc("\"%s\" default %s", rustup_exe, toolchain));
    if (!out) {
        /* Failure: restore env vars */
        if (backup_file && backup_file[0]) restore_env_vars(backup_file);
        return -1;
    }
    free(out);
    return 0;
}

/* ================================================================
   Rust: install / uninstall toolchain
   ================================================================ */

int install_rust_toolchain(const char *toolchain, const char *backup_file) {
    if (!toolchain || !*toolchain) return -1;

    char rustup_exe[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
    if (GetFileAttributesA(rustup_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    if (backup_file && backup_file[0]) {
        if (backup_env_vars(backup_file) != 0) return -1;
    }

    char *out = run_capture(fmt_alloc("\"%s\" toolchain install %s", rustup_exe, toolchain));
    if (!out) {
        if (backup_file && backup_file[0]) restore_env_vars(backup_file);
        return -1;
    }
    free(out);
    return 0;
}

int uninstall_rust_toolchain(const char *toolchain, const char *backup_file) {
    (void)backup_file;
    if (!toolchain || !*toolchain) return -1;

    char rustup_exe[MAX_PATH];
    ExpandEnvironmentStringsA(RUSTUP_EXE, rustup_exe, sizeof(rustup_exe));
    if (GetFileAttributesA(rustup_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    /* If the toolchain being uninstalled is the current default, switch to stable first */
    char *current_chan = get_current_rust_channel();
    int need_switch = (current_chan && str_ieq(current_chan, toolchain)) ? 1 : 0;
    free(current_chan);

    if (need_switch) {
        char *out = run_capture(fmt_alloc("\"%s\" default stable", rustup_exe));
        free(out);
    }

    char *out = run_capture(fmt_alloc("\"%s\" toolchain uninstall %s", rustup_exe, toolchain));
    if (!out) return -1;
    free(out);
    return 0;
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

    /* Prepend new NODE_HOME */
    size_t total = strlen(version_dir) + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, version_dir);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}

/* ================================================================
   Path cleanup helper for Node.js / Python / Go
   ================================================================ */

static char *clean_path_rust(const char *path, const char *home_dir, int *removed_out) {
    if (!path) path = "";
    if (removed_out) *removed_out = 0;
    if (!path[0]) return str_dup("");

    char target[MAX_PATH];
    snprintf(target, sizeof(target), "%s", home_dir ? home_dir : "");

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
        if (target[0]) {
            char tgt_low[MAX_PATH];
            strncpy(tgt_low, target, sizeof(tgt_low) - 1);
            tgt_low[sizeof(tgt_low) - 1] = '\0';
            _strlwr_s(tgt_low, sizeof(tgt_low));
            if (strcmp(low, tgt_low) == 0) skip = 1;
            else {
                char prefix[MAX_PATH];
                snprintf(prefix, sizeof(prefix), "%s\\", tgt_low);
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

/* ================================================================
   Python switch — write PY_HOME + PATH cleanup
   ================================================================ */

int switch_python(const char *version_dir, const char *backup_file) {
    (void)backup_file;

    if (!version_dir || !*version_dir) return -1;

    char py_exe[MAX_PATH];
    snprintf(py_exe, sizeof(py_exe), "%s\\python.exe", version_dir);
    if (GetFileAttributesA(py_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    /* Write PY_HOME */
    if (reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "PY_HOME", version_dir) != 0)
        return -1;

    /* Remove old Python entries from PATH */
    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    int removed = 0;
    char *new_path = clean_path_rust(old_path ? old_path : "", version_dir, &removed);
    free(old_path);

    /* Also prepend the new python dir (not bin — python is directly in version_dir) */
    /* Python's Scripts dir is in <ver>\Scripts */
    size_t total = strlen(version_dir) + 1 + strlen(version_dir) + 8
                 + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, version_dir);
    strcat(final_path, ";");
    char scripts[MAX_PATH];
    snprintf(scripts, sizeof(scripts), "%s\\Scripts", version_dir);
    strcat(final_path, scripts);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}

/* ================================================================
   Go switch — write GOROOT + PATH cleanup
   ================================================================ */

int switch_go(const char *goroot, const char *backup_file) {
    (void)backup_file;

    if (!goroot || !*goroot) return -1;

    char go_exe[MAX_PATH];
    snprintf(go_exe, sizeof(go_exe), "%s\\bin\\go.exe", goroot);
    if (GetFileAttributesA(go_exe) == INVALID_FILE_ATTRIBUTES) return -1;

    /* Write GOROOT registry */
    if (reg_write_string(HKEY_LOCAL_MACHINE, GOROOT_REG_PATH, "GoRoot", goroot) != 0)
        return -1;

    /* Also write GOROOT in Environment */
    if (reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "GOROOT", goroot) != 0) {
        /* non-fatal — continue */
    }

    /* Remove old Go entries from PATH, prepend new GOROOT\bin */
    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    int removed = 0;
    char *new_path = clean_path_rust(old_path ? old_path : "", goroot, &removed);
    free(old_path);

    char goroot_bin[MAX_PATH];
    snprintf(goroot_bin, sizeof(goroot_bin), "%s\\bin", goroot);
    size_t total = strlen(goroot_bin) + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, goroot_bin);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}

/* ================================================================
   Maven switch — write MAVEN_HOME + PATH cleanup
   ================================================================ */

int switch_maven(const char *install_dir, const char *backup_file) {
    (void)backup_file;

    if (!install_dir || !*install_dir) return -1;

    char mvn_exe[MAX_PATH];
    snprintf(mvn_exe, sizeof(mvn_exe), "%s\\bin\\mvn.cmd", install_dir);
    if (GetFileAttributesA(mvn_exe) == INVALID_FILE_ATTRIBUTES) {
        snprintf(mvn_exe, sizeof(mvn_exe), "%s\\bin\\mvn.bat", install_dir);
        if (GetFileAttributesA(mvn_exe) == INVALID_FILE_ATTRIBUTES) return -1;
    }

    /* Write MAVEN_HOME */
    if (reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "MAVEN_HOME", install_dir) != 0)
        return -1;

    /* Remove old Maven entries from PATH, prepend new MAVEN_HOME\bin */
    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    int removed = 0;
    char *new_path = clean_path_rust(old_path ? old_path : "", install_dir, &removed);
    free(old_path);

    char mvn_bin[MAX_PATH];
    snprintf(mvn_bin, sizeof(mvn_bin), "%s\\bin", install_dir);
    size_t total = strlen(mvn_bin) + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, mvn_bin);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}

/* ================================================================
   Gradle switch — write GRADLE_HOME + PATH cleanup
   ================================================================ */

int switch_gradle(const char *install_dir, const char *backup_file) {
    (void)backup_file;

    if (!install_dir || !*install_dir) return -1;

    char gb[MAX_PATH];
    snprintf(gb, sizeof(gb), "%s\\bin\\gradle.bat", install_dir);
    if (GetFileAttributesA(gb) == INVALID_FILE_ATTRIBUTES) return -1;

    /* Write GRADLE_HOME */
    if (reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "GRADLE_HOME", install_dir) != 0)
        return -1;

    /* Remove old Gradle entries from PATH, prepend new GRADLE_HOME\bin */
    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path");
    int removed = 0;
    char *new_path = clean_path_rust(old_path ? old_path : "", install_dir, &removed);
    free(old_path);

    char gbin[MAX_PATH];
    snprintf(gbin, sizeof(gbin), "%s\\bin", install_dir);
    size_t total = strlen(gbin) + (new_path[0] ? strlen(new_path) + 1 : 0) + 1;
    char *final_path = (char *)malloc(total);
    if (!final_path) { free(new_path); return -1; }
    final_path[0] = '\0';
    strcat(final_path, gbin);
    if (new_path[0]) { strcat(final_path, ";"); strcat(final_path, new_path); }
    free(new_path);

    int rc = reg_write_string(HKEY_LOCAL_MACHINE, REG_ENV_PATH, "Path", final_path);
    free(final_path);
    return rc;
}
