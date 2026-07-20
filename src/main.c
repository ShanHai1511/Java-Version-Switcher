#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "config.h"
#include "core.h"
#include "gui.h"

extern HINSTANCE g_hInst;

#define VERSION "2.0.0-c"

static void print_usage(void) {
    printf("Java Version Switcher v%s\n", VERSION);
    printf("Usage: jvs_v2.exe [command]\n\n");
    printf("Commands:\n");
    printf("  (none)              Launch GUI\n");
    printf("  --switch <path>     Switch JDK (requires admin)\n");
    printf("  --version           Print version\n");
    printf("  --scan              Scan for JDKs (JSON output)\n");
    printf("  --list              List configured scan paths\n");
}

/* ── CLI: --scan ────────────────────────────────────── */

static int do_cli_scan(void) {
    Config *cfg = config_load(config_path());
    JDKList *list = scan_all(cfg ? (const char **)cfg->scan_paths : NULL,
                              cfg ? cfg->scan_paths_count : 0);

    printf("[");
    for (int i = 0; i < list->count; i++) {
        JDKInfo *j = list->items[i];
        if (i > 0) printf(",");
        printf("{\"version\":\"%s\",\"major\":%d,\"vendor\":\"%s\","
               "\"path\":\"%s\",\"is_current\":%s}",
               j->version, j->major, j->vendor,
               j->path, j->is_current ? "true" : "false");
    }
    printf("]\n");

    jdk_list_free(list);
    config_free(cfg);
    return 0;
}

/* ── CLI: --switch subprocess ───────────────────────── */

static int do_subprocess_switch(int argc, char *argv[]) {
    const char *jdk_path = NULL;
    const char *backup_file = NULL;

    /* argv layout: argv[0]=exe, argv[1]="--switch", argv[2]=jdk_path */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (jdk_path == NULL) {
            jdk_path = argv[i];
        }
    }

    if (!jdk_path) {
        fprintf(stderr, "Error: missing JDK path\n");
        return 1;
    }

    SwitchResult *r = switch_jdk(jdk_path, backup_file);

    printf("{\"success\":%s,\"old_home\":\"%s\",\"new_home\":\"%s\","
           "\"path_cleaned\":%d,\"error\":\"%s\"}\n",
           r->success ? "true" : "false",
           r->old_home ? r->old_home : "",
           r->new_home ? r->new_home : "",
           r->path_cleaned,
           r->error ? r->error : "");

    switch_result_free(r);
    return r->success ? 0 : 1;
}

/* ══════════════════════════════════════════════════════
   WinMain
   ══════════════════════════════════════════════════════ */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hInst = hInstance;

    int   argc = __argc;
    char **argv = __argv;

    /* ── --switch subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch") == 0) {
        int rc = do_subprocess_switch(argc, argv);
        return rc;
    }

    /* ── --version ── */
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("Java Version Switcher v%s (C + Win32)\n", VERSION);
        return 0;
    }

    /* ── --scan ── */
    if (argc >= 2 && strcmp(argv[1], "--scan") == 0) {
        int rc = do_cli_scan();
        return rc;
    }

    /* ── GUI mode ── */
    Config *cfg = config_load(config_path());
    if (!cfg) {
        cfg = config_default();
    }

    /* init common controls */
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    int rc = gui_run(cfg);

    config_free(cfg);
    return rc;
}
