#include "util.h"
#include <ctype.h>

/* ── String ─────────────────────────────────────────── */

char *str_dup(const char *s) {
    size_t n = strlen(s);
    char *r = (char *)malloc(n + 1);
    if (r) memcpy(r, s, n + 1);
    return r;
}

char *str_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

char *str_concat(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    char *r = (char *)malloc(na + nb + 1);
    if (r) { memcpy(r, a, na); memcpy(r + na, b, nb + 1); }
    return r;
}

static int to_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower((unsigned char)*a) != to_lower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int str_starts_with(const char *s, const char *pfx) {
    while (*pfx) {
        if (to_lower((unsigned char)*s) != to_lower((unsigned char)*pfx)) return 0;
        s++; pfx++;
    }
    return 1;
}

/* ── Format ─────────────────────────────────────────── */

char *fmt_alloc(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vscprintf(fmt, ap);
    va_end(ap);
    if (n < 0) return NULL;
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) return NULL;
    va_start(ap, fmt);
    vsprintf_s(buf, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return buf;
}

/* ── Path ───────────────────────────────────────────── */

char *path_join(const char *a, const char *b) {
    size_t na = strlen(a);
    int need_sep = (na > 0 && a[na - 1] != '\\' && a[na - 1] != '/');
    return fmt_alloc("%s%s%s", a, need_sep ? "\\" : "", b);
}

char *path_dirname(const char *p) {
    char *s = str_dup(p);
    char *last = strrchr(s, '\\');
    if (last) *last = '\0';
    return s;
}

/* ── Ini parser ─────────────────────────────────────── */

void ini_free(IniTable *t) {
    for (int i = 0; i < t->count; i++) {
        free(t->entries[i].key);
        free(t->entries[i].val);
    }
    free(t->entries);
    t->entries = NULL; t->count = t->cap = 0;
}

static void ini_add(IniTable *t, const char *key, const char *val) {
    if (t->count >= t->cap) {
        t->cap = t->cap < 8 ? 8 : t->cap * 2;
        t->entries = (IniEntry *)realloc(t->entries, (size_t)t->cap * sizeof(IniEntry));
    }
    t->entries[t->count].key = str_dup(key);
    t->entries[t->count].val = str_dup(val);
    t->count++;
}

IniTable *ini_parse(const char *data) {
    IniTable *t = (IniTable *)calloc(1, sizeof(IniTable));
    if (!t) return NULL;
    char *copy    = str_dup(data);
    char *line    = copy;
    char section[64] = {0};

    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *trimmed = str_trim(line);

        /* skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
            line = nl ? nl + 1 : line + strlen(line);
            continue;
        }

        /* section header */
        if (*trimmed == '[') {
            char *p = strrchr(trimmed, ']');
            if (p) {
                *p = '\0';
                strncpy(section, trimmed + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            line = nl ? nl + 1 : line + strlen(line);
            continue;
        }

        /* key = value */
        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key   = str_trim(trimmed);
            char *value = str_trim(eq + 1);

            /* prefix key with section for uniqueness */
            if (section[0]) {
                char full[256];
                snprintf(full, sizeof(full), "%s.%s", section, key);
                ini_add(t, full, value);
            } else {
                ini_add(t, key, value);
            }
        }

        line = nl ? nl + 1 : line + strlen(line);
    }
    free(copy);
    return t;
}

char *ini_get(IniTable *t, const char *key) {
    for (int i = 0; i < t->count; i++) {
        if (str_ieq(t->entries[i].key, key)) return t->entries[i].val;
    }
    return NULL;
}

char *ini_get_def(IniTable *t, const char *key, const char *def) {
    char *v = ini_get(t, key);
    return v ? v : (char *)def;
}
