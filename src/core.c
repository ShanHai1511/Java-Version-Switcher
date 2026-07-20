#include "core.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <knownfolders.h>
#include <initguid.h>
#pragma comment(lib, "shlwapi.lib")

/* ── Constants ──────────────────────────────────────── */

#define ENV_REG_PATH   "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
#define JDK_REG_PATH64 "SOFTWARE\\JavaSoft\\JDK"
#define JDK_REG_PATH32 "SOFTWARE\\WOW6432Node\\JavaSoft\\JDK"
#define MAX_VENDORS    11

/* ── Vendor patterns ────────────────────────────────── */

typedef struct { const char *pattern; const char *vendor; } VendorRule;

static const VendorRule VENDOR_RULES[MAX_VENDORS] = {
    {"Zulu",                                    "Azul Zulu"},
    {"Temurin|Eclipse Foundation|AdoptOpenJDK", "Adoptium"},
    {"GraalVM",                                 "GraalVM"},
    {"OpenJDK",                                 "OpenJDK"},
    {"Java\\s*\\(?\\s*TM",                      "Oracle"},
    {"Microsoft",                               "Microsoft"},
    {"SAP",                                     "SAP"},
    {"Liberica|BellSoft",                       "Liberica"},
    {"Corretto",                                "Amazon Corretto"},
    {"Dragonwell",                              "Alibaba Dragonwell"},
    {"HotSpot",                                 "Oracle HotSpot"},
};

/* ── JDKInfo ────────────────────────────────────────── */

JDKInfo *jdk_info_new(void) {
    JDKInfo *j = (JDKInfo *)calloc(1, sizeof(JDKInfo));
    return j;
}

void jdk_info_free(JDKInfo *j) {
    if (!j) return;
    free(j->version); free(j->vendor); free(j->path); free(j->tag);
    free(j);
}

char *jdk_display_name(const JDKInfo *j) {
    if (!j) return str_dup("(null)");
    if (j->tag[0])
        return fmt_alloc("JDK %s  %s  %s", j->version, j->vendor, j->tag);
    return fmt_alloc("JDK %s  %s", j->version, j->vendor);
}

/* ── Version parsing ────────────────────────────────── */

static int parse_major(const char *ver) {
    if (!ver || !*ver) return 0;
    if (ver[0] == '1' && ver[1] == '.') {
        int n = atoi(ver + 2);
        return (n > 0) ? n : 0;
    }
    return atoi(ver);
}

static int is_substr(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return 0;
    return (strstr(haystack, needle) != NULL);
}

static char *resolve_version(const char *java_exe, char **out_vendor) {
    char cmdline[MAX_PATH * 2];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -version", java_exe);

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
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
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

    /* extract version string */
    char *version = NULL;
    const char *p = buf;
    while (*p) {
        if ((p[0] == '"' || p[0] == '\'') &&
            (isdigit((unsigned char)p[1]) || (p[1] == '1' && p[2] == '.'))) {
            const char *q = p + 1, *end = q;
            while (*end && *end != '"' && *end != '\'') end++;
            if (end > q) {
                size_t len = (size_t)(end - q);
                version = (char *)malloc(len + 1);
                memcpy(version, q, len);
                version[len] = '\0';
                break;
            }
        }
        p++;
    }
    if (!version) return NULL;

    /* detect vendor */
    const char *vendor = "Unknown";
    for (int i = 0; i < MAX_VENDORS; i++) {
        const char *pat = VENDOR_RULES[i].pattern;
        char tmp[256];
        strncpy(tmp, pat, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *tok = strtok(tmp, "|");
        while (tok) {
            /* skip non-alpha prefix chars */
            while (*tok && !isalpha((unsigned char)*tok)) tok++;
            if (*tok && is_substr(buf, tok)) { vendor = VENDOR_RULES[i].vendor; break; }
            tok = strtok(NULL, "|");
        }
        if (vendor[0] != 'U') break;
    }

    *out_vendor = str_dup(vendor);
    return version;
}

/* ── Directory scan ─────────────────────────────────── */

typedef struct { JDKInfo **items; int count; int cap; } JdkBuf;

static void jdkbuf_add(JdkBuf *b, JDKInfo *j) {
    if (!j) return;
    if (b->count >= b->cap) {
        b->cap = b->cap < 8 ? 8 : b->cap * 2;
        b->items = (JDKInfo **)realloc(b->items, (size_t)b->cap * sizeof(JDKInfo *));
    }
    b->items[b->count++] = j;
}

static JDKInfo *validate_jdk(const char *jdk_path) {
    char java_exe[MAX_PATH], javac_exe[MAX_PATH];
    snprintf(java_exe, sizeof(java_exe), "%s\\bin\\java.exe", jdk_path);
    snprintf(javac_exe, sizeof(javac_exe), "%s\\bin\\javac.exe", jdk_path);

    if (GetFileAttributesA(java_exe) == INVALID_FILE_ATTRIBUTES) return NULL;
    if (GetFileAttributesA(javac_exe) == INVALID_FILE_ATTRIBUTES) return NULL;

    char *vendor = NULL;
    char *ver    = resolve_version(java_exe, &vendor);
    if (!ver || !*ver) { free(vendor); free(ver); return NULL; }

    JDKInfo *j = jdk_info_new();
    j->version = ver;
    j->major   = parse_major(ver);
    j->vendor  = vendor;
    j->path    = str_dup(jdk_path);
    return j;
}

void jdk_list_free(JDKList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) jdk_info_free(list->items[i]);
    free(list->items);
    free(list);
}

/* ── Registry scan ──────────────────────────────────── */

char **reg_enum_subkeys(HKEY root, const char *subkey, int *out_count) {
    HKEY hKey;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        *out_count = 0; return NULL;
    }
    DWORD count = 0;
    RegQueryInfoKeyA(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (count == 0) { RegCloseKey(hKey); *out_count = 0; return NULL; }

    char **names = (char **)malloc((size_t)count * sizeof(char *));
    for (DWORD i = 0; i < count; i++) {
        char buf[256];
        DWORD sz = sizeof(buf);
        if (RegEnumKeyExA(hKey, i, buf, &sz, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            names[i] = str_dup(buf);
        else
            names[i] = NULL;
    }
    RegCloseKey(hKey);
    *out_count = (int)count;
    return names;
}

char *reg_read_string(HKEY root, const char *subkey, const char *name) {
    HKEY hKey;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return NULL;
    char buf[4096];
    DWORD sz = sizeof(buf), type = 0;
    LONG rc = RegQueryValueExA(hKey, name, NULL, &type, (LPBYTE)buf, &sz);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return NULL;
    buf[sz] = '\0';
    return str_dup(buf);
}

int reg_write_string(HKEY root, const char *subkey, const char *name, const char *val) {
    HKEY hKey;
    DWORD disp;
    if (RegCreateKeyExA(root, subkey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return -1;
    int rc = (RegSetValueExA(hKey, name, 0, REG_SZ, (const BYTE *)val, (DWORD)(strlen(val) + 1)) == ERROR_SUCCESS) ? 0 : -1;
    RegCloseKey(hKey);
    return rc;
}

int reg_delete_value(HKEY root, const char *subkey, const char *name) {
    HKEY hKey;
    if (RegOpenKeyExA(root, subkey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return -1;
    int rc = (RegDeleteValueA(hKey, name) == ERROR_SUCCESS) ? 0 : -1;
    RegCloseKey(hKey);
    return rc;
}

/* ── JAVA_HOME ──────────────────────────────────────── */

char *get_current_java_home(void) {
    return reg_read_string(HKEY_LOCAL_MACHINE, ENV_REG_PATH, "JAVA_HOME");
}

/* ── Broadcast (static helper, no nested funcs) ───────── */

static int do_broadcast_env_change(void) {
    typedef LRESULT (WINAPI *pSendMsgTO_t)(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PULONG_PTR);
    static pSendMsgTO_t pFunc = NULL;
    static HMODULE hMod = NULL;
    if (!hMod) {
        hMod = LoadLibraryA("user32.dll");
        if (!hMod) return -1;
        pFunc = (pSendMsgTO_t)GetProcAddress(hMod, "SendMessageTimeoutW");
        if (!pFunc) return -1;
    }
    HWND hBroadcast = (HWND)0xFFFF;
    UINT msg        = 0x001A;
    ULONG_PTR res   = 0;
    WCHAR envW[]    = L"Environment";
    LRESULT ret = pFunc(hBroadcast, msg, 0, (LPARAM)envW, 0x0002, 5000, &res);
    return (ret == 0) ? -1 : 0;
}

/* ── Registry scan ──────────────────────────────────── */

static JDKList *scan_registry_path(HKEY root, const char *reg_path) {
    JDKList *list = (JDKList *)calloc(1, sizeof(JDKList));
    if (!list) return NULL;

    int count = 0;
    char **names = reg_enum_subkeys(root, reg_path, &count);
    if (!names || count == 0) { free(names); return list; }

    for (int i = 0; i < count; i++) {
        if (!names[i]) continue;
        char sub[MAX_PATH];
        snprintf(sub, sizeof(sub), "%s\\%s", reg_path, names[i]);
        char *java_home = reg_read_string(root, sub, "JavaHome");
        if (java_home) {
            JDKInfo *j = validate_jdk(java_home);
            if (j) {
                free(j->version);
                j->version = str_dup(names[i]);
                j->major   = parse_major(names[i]);
                jdkbuf_add((JdkBuf *)list, j);
            }
            free(java_home);
        }
        free(names[i]);
    }
    free(names);
    return list;
}

/* ── Standard scan dirs ─────────────────────────────── */

typedef struct { char *path; int depth; } ScanEntry;
typedef struct { ScanEntry *items; int count; int cap; } DirBuf;

static void add_dir_if_exists(DirBuf *b, const char *d, int depth) {
    DWORD attr = GetFileAttributesA(d);
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return;
    if (b->count >= b->cap) {
        b->cap = b->cap < 16 ? 16 : b->cap * 2;
        b->items = (ScanEntry *)realloc(b->items, (size_t)b->cap * sizeof(ScanEntry));
    }
    b->items[b->count].path  = str_dup(d);
    b->items[b->count].depth = depth;
    b->count++;
}

static ScanEntry *scan_dirs(int *out_count) {
    DirBuf b = {0};
    char pf[MAX_PATH] = {0}, pf86[MAX_PATH] = {0}, home[MAX_PATH] = {0};

    ExpandEnvironmentStringsA("%ProgramFiles%", pf, sizeof(pf));
    ExpandEnvironmentStringsA("%ProgramFiles(x86)%", pf86, sizeof(pf86));
    ExpandEnvironmentStringsA("%USERPROFILE%", home, sizeof(home));

    add_dir_if_exists(&b, pf, 2);                            /* broad: depth 2 */
    add_dir_if_exists(&b, pf86, 2);                          /* broad: depth 2 */
    add_dir_if_exists(&b, fmt_alloc("%s\\Java", pf), 2);    /* broad: depth 2 */
    if (pf86[0]) add_dir_if_exists(&b, fmt_alloc("%s\\Java", pf86), 2);

    const char *vendors[] = {
        "Eclipse Adoptium", "Eclipse Foundation", "Amazon Corretto",
        "Microsoft", "BellSoft", "Liberica JDK", "GraalVM", NULL
    };
    for (int i = 0; vendors[i]; i++) {
        add_dir_if_exists(&b, fmt_alloc("%s\\%s", pf, vendors[i]), 3);    /* specific: depth 3 */
        if (pf86[0]) add_dir_if_exists(&b, fmt_alloc("%s\\%s", pf86, vendors[i]), 3);
    }

    add_dir_if_exists(&b, fmt_alloc("%s\\.jvs\\jdk", home), 3);

    /* custom / ad-hoc Java roots — depth 3 */
    add_dir_if_exists(&b, "D:\\install\\java", 3);
    add_dir_if_exists(&b, "D:\\install\\Android Studio\\jbr", 3);
    add_dir_if_exists(&b, "D:\\install\\IntelliJ IDEA 2024.1\\jbr", 3);
    add_dir_if_exists(&b, "D:\\install\\PyCharm Community Edition 2025.1.2\\jbr", 3);
    add_dir_if_exists(&b, "E:\\install\\DevEco Studio\\jbr", 3);
    add_dir_if_exists(&b, "E:\\install\\IntelliJ IDEA 2025.2.4\\jbr", 3);

    char *java_home = get_current_java_home();
    if (java_home && java_home[0]) {
        char *parent = path_dirname(java_home);
        add_dir_if_exists(&b, parent, 3);
        add_dir_if_exists(&b, java_home, 3);
        free(parent);
        free(java_home);
    } else {
        free(java_home);
    }

    /* Scoop — depth 3 */
    char scoop[MAX_PATH];
    snprintf(scoop, sizeof(scoop), "%s\\scoop\\apps", home);
    WIN32_FIND_DATAA fd;
    char scoop_pat[MAX_PATH];
    snprintf(scoop_pat, sizeof(scoop_pat), "%s\\*", scoop);
    HANDLE h = FindFirstFileA(scoop_pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (_strnicmp(fd.cFileName, "jdk", 3) == 0) {
                char scoop_jdk[MAX_PATH];
                snprintf(scoop_jdk, sizeof(scoop_jdk), "%s\\%s\\current", scoop, fd.cFileName);
                add_dir_if_exists(&b, scoop_jdk, 3);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    *out_count = b.count;
    return b.items;
}

/* ── Merge helper (top-level, not nested) ───────────── */

static void merge_list(JDKList *dst, JDKList *src) {
    if (!src) return;
    for (int i = 0; i < src->count; i++) {
        int found = 0;
        for (int j = 0; j < dst->count; j++) {
            if (str_ieq(dst->items[j]->path, src->items[i]->path)) { found = 1; break; }
        }
        if (!found) {
            if (dst->count >= dst->cap) {
                dst->cap = dst->cap < 16 ? 16 : dst->cap * 2;
                dst->items = (JDKInfo **)realloc(dst->items, (size_t)dst->cap * sizeof(JDKInfo *));
            }
            dst->items[dst->count++] = src->items[i];
            src->items[i] = NULL;
        } else {
            jdk_info_free(src->items[i]);
        }
    }
    free(src->items);
    free(src);
}

/* ── Directory scan (recursive, depth-limited) ─────────── */

static JDKList *scan_directory_inner(const char *root, int depth, int max_depth) {
    JDKList *list = (JDKList *)calloc(1, sizeof(JDKList));
    if (!list) return NULL;
    if (depth > max_depth) return list;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", root);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return list;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char sub[MAX_PATH];
        snprintf(sub, sizeof(sub), "%s\\%s", root, fd.cFileName);

        JDKInfo *j = validate_jdk(sub);
        if (j) { jdkbuf_add((JdkBuf *)list, j); continue; }
        if (depth >= max_depth) continue;

        JDKList *sub_list = scan_directory_inner(sub, depth + 1, max_depth);
        merge_list(list, sub_list);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return list;
}

JDKList *scan_directory(const char *root, int max_depth) {
    return scan_directory_inner(root, 1, max_depth);
}

/* ── ScanAll ────────────────────────────────────────── */

JDKList *scan_all(const char **user_scan_paths, int user_scan_count) {
    JDKList *list = (JDKList *)calloc(1, sizeof(JDKList));
    if (!list) return NULL;

    int dir_count = 0;
    ScanEntry *dirs = scan_dirs(&dir_count);

    for (int i = 0; i < dir_count; i++) {
        JDKList *r = scan_directory(dirs[i].path, dirs[i].depth);
        merge_list(list, r);
        free(dirs[i].path);
    }
    free(dirs);

    JDKList *r64 = scan_registry_path(HKEY_LOCAL_MACHINE, JDK_REG_PATH64);
    merge_list(list, r64);
    JDKList *r32 = scan_registry_path(HKEY_LOCAL_MACHINE, JDK_REG_PATH32);
    merge_list(list, r32);

    for (int i = 0; i < user_scan_count; i++) {
        JDKList *r = scan_directory(user_scan_paths[i], 3);
        if (r) {
            for (int k = 0; k < r->count; k++) r->items[k]->is_portable = 1;
            merge_list(list, r);
        }
    }

    char *cur = get_current_java_home();
    if (cur) {
        for (int i = 0; i < list->count; i++) {
            if (str_ieq(list->items[i]->path, cur)) {
                list->items[i]->is_current = 1;
                break;
            }
        }
        free(cur);
    }

    for (int i = 0; i < list->count - 1; i++) {
        for (int j = i + 1; j < list->count; j++) {
            if (list->items[j]->major > list->items[i]->major) {
                JDKInfo *tmp = list->items[i];
                list->items[i] = list->items[j];
                list->items[j] = tmp;
            }
        }
    }
    return list;
}

/* ── Tag helpers ────────────────────────────────────── */

const char *determine_tag(int major) {
    switch (major) {
        case  8: return "[Minecraft 1.12-]";
        case 17: return "[Minecraft 1.18-1.20]";
        case 21: return "[Minecraft 1.21+]";
        default: return "";
    }
}

/* ── Backup / Restore ───────────────────────────────── */

int backup_env_vars(const char *filepath) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ENV_REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return -1;

    char javaHome[4096] = {0}, pathVal[4096] = {0};
    DWORD sz, type;

    sz = sizeof(javaHome); type = 0;
    RegQueryValueExA(hKey, "JAVA_HOME", NULL, &type, (LPBYTE)javaHome, &sz);

    sz = sizeof(pathVal); type = 0;
    RegQueryValueExA(hKey, "Path", NULL, &type, (LPBYTE)pathVal, &sz);

    RegCloseKey(hKey);

    char *dir = path_dirname(filepath);
    CreateDirectoryA(dir, NULL);
    free(dir);

    FILE *f = fopen(filepath, "wb");
    if (!f) return -1;

    fprintf(f, "Windows Registry Editor Version 5.00\r\n\r\n");
    fprintf(f, "[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment]\r\n");
    if (javaHome[0]) fprintf(f, "\"JAVA_HOME\"=\"%s\"\r\n", javaHome);
    if (pathVal[0])  fprintf(f, "\"Path\"=\"%s\"\r\n", pathVal);
    fclose(f);
    return 0;
}

int restore_env_vars(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '"') {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *name  = str_trim(line + 1);
            char *value = str_trim(eq + 1);
            char *endq  = strrchr(value, '"');
            if (endq) *endq = '\0';
            reg_write_string(HKEY_LOCAL_MACHINE, ENV_REG_PATH, name, value);
        }
    }
    fclose(f);
    do_broadcast_env_change();
    return 0;
}

/* ── Path cleanup ───────────────────────────────────── */

char *clean_path(const char *path, const char *old_jdk_dir, int *removed_out) {
    if (!path) path = "";
    if (removed_out) *removed_out = 0;
    if (!path[0]) return str_dup("%JAVA_HOME%\\bin");

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

        int skip = (strcmp(low, "%java_home%\\bin") == 0);

        if (!skip && old_jdk_dir && old_jdk_dir[0]) {
            char old_low[MAX_PATH];
            strncpy(old_low, old_jdk_dir, sizeof(old_low) - 1);
            old_low[sizeof(old_low) - 1] = '\0';
            _strlwr_s(old_low, sizeof(old_low));
            if (strcmp(low, old_low) == 0) skip = 1;
            else {
                char prefix[MAX_PATH];
                snprintf(prefix, sizeof(prefix), "%s\\", old_low);
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

    size_t total = strlen("%JAVA_HOME%\\bin") + 1;
    for (int i = 0; i < count; i++) total += strlen(parts[i]) + 1;

    char *result = (char *)malloc(total + 1);
    result[0] = '\0';
    strcat(result, "%JAVA_HOME%\\bin");
    for (int i = 0; i < count; i++) {
        strcat(result, ";");
        strcat(result, parts[i]);
        free(parts[i]);
    }
    free(parts);
    return result;
}

/* ── Switch ─────────────────────────────────────────── */

SwitchResult *switch_jdk(const char *jdk_path, const char *backup_file) {
    SwitchResult *r = (SwitchResult *)calloc(1, sizeof(SwitchResult));
    r->new_home = str_dup(jdk_path ? jdk_path : "");

    char bf[MAX_PATH];
    if (backup_file && backup_file[0]) {
        strncpy(bf, backup_file, sizeof(bf) - 1);
    } else {
        PWSTR appdataW = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdataW))) {
            WideCharToMultiByte(CP_UTF8, 0, appdataW, -1, bf, sizeof(bf), NULL, NULL);
            CoTaskMemFree(appdataW);
        } else {
            ExpandEnvironmentStringsA("%APPDATA%", bf, sizeof(bf));
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(bf + strlen(bf), sizeof(bf) - strlen(bf),
                 "\\JVS\\backup\\%04d%02d%02d_%02d%02d%02d.reg",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
    }

    char java_exe[MAX_PATH];
    snprintf(java_exe, sizeof(java_exe), "%s\\bin\\java.exe", jdk_path);
    if (GetFileAttributesA(java_exe) == INVALID_FILE_ATTRIBUTES) {
        r->error = fmt_alloc("path does not contain bin\\java.exe: %s", jdk_path);
        return r;
    }

    r->old_home = get_current_java_home();

    if (backup_env_vars(bf) != 0) {
        r->error = fmt_alloc("failed to backup env vars");
        return r;
    }

    if (reg_write_string(HKEY_LOCAL_MACHINE, ENV_REG_PATH, "JAVA_HOME", jdk_path) != 0) {
        r->error = fmt_alloc("failed to write JAVA_HOME");
        restore_env_vars(bf);
        return r;
    }

    char *old_path = reg_read_string(HKEY_LOCAL_MACHINE, ENV_REG_PATH, "Path");
    int cleaned = 0;
    char *new_path = clean_path(old_path ? old_path : "",
                                 r->old_home ? r->old_home : "", &cleaned);
    r->path_cleaned = cleaned;
    free(old_path);

    if (reg_write_string(HKEY_LOCAL_MACHINE, ENV_REG_PATH, "Path", new_path) != 0) {
        r->error = fmt_alloc("failed to write Path");
        restore_env_vars(bf);
        free(new_path);
        return r;
    }
    free(new_path);

    if (do_broadcast_env_change() != 0) {
        r->error = fmt_alloc("broadcast failed (env changed, may need restart)");
    }

    return r;
}

void switch_result_free(SwitchResult *r) {
    if (!r) return;
    free(r->old_home); free(r->new_home); free(r->error); free(r);
}
