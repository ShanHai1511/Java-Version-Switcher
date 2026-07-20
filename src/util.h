#ifndef JVS_UTIL_H
#define JVS_UTIL_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ── String ─────────────────────────────────────────── */

char *str_dup(const char *s);
char *str_trim(char *s);
char *str_concat(const char *a, const char *b);
int   str_ieq(const char *a, const char *b);           /* case-insensitive equal */
int   str_starts_with(const char *s, const char *pfx); /* case-insensitive */

/* ── Format ─────────────────────────────────────────── */

char *fmt_alloc(const char *fmt, ...);

/* ── Path ───────────────────────────────────────────── */

char *path_join(const char *a, const char *b);
char *path_dirname(const char *p);

/* ── Ini parser ─────────────────────────────────────── */

typedef struct { char *key; char *val; } IniEntry;

typedef struct {
    IniEntry *entries;
    int       count;
    int       cap;
} IniTable;

void   ini_free(IniTable *t);
IniTable *ini_parse(const char *data);
char  *ini_get(IniTable *t, const char *key);         /* first match, NULL if absent */
char  *ini_get_def(IniTable *t, const char *key, const char *def);

#endif /* JVS_UTIL_H */
