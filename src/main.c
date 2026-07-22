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
    printf("  --switch-rust <ch>  Switch Rust toolchain (requires admin)\n");
    printf("  --switch-node <dir> Switch Node.js (requires admin)\n");
    printf("  --switch-python <dir> Switch Python (requires admin)\n");
    printf("  --switch-go <dir>   Switch Go (requires admin)\n");
    printf("  --switch-maven <dir> Switch Maven (requires admin)\n");
    printf("  --switch-gradle <dir> Switch Gradle (requires admin)\n");
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

/* ── CLI: subprocess result writer ─────────────────────── */

static int write_result_json(const char *success, const char *old_home,
                              const char *new_home, int path_cleaned,
                              const char *error) {
    char rp[MAX_PATH];
    snprintf(rp, sizeof(rp), "%s\\jvs_switch_result.json", getenv("TEMP"));
    FILE *f = fopen(rp, "w");
    if (!f) return -1;
    fprintf(f, "{\"success\":%s,\"old_home\":\"%s\",\"new_home\":\"%s\","
               "\"path_cleaned\":%d,\"error\":\"%s\"}\n",
            success, old_home ? old_home : "", new_home ? new_home : "",
            path_cleaned, error ? error : "");
    fclose(f);
    return 0;
}

/* ── CLI: --switch (JDK) subprocess mode ──────────────── */

static int do_subprocess_switch(int argc, char *argv[]) {
    const char *jdk_path = NULL;
    const char *backup_file = NULL;

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
    const char *success = r->success ? "true" : "false";
    write_result_json(success, r->old_home, r->new_home, r->path_cleaned, r->error);

    switch_result_free(r);
    return r->success ? 0 : 1;
}

/* ── CLI: --switch-rust subprocess mode ────────────────── */

static int do_subprocess_switch_rust(int argc, char *argv[]) {
    const char *toolchain = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (toolchain == NULL) {
            toolchain = argv[i];
        }
    }

    if (!toolchain) {
        fprintf(stderr, "Error: missing toolchain\n");
        return 1;
    }

    int rc = switch_rust(toolchain, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, toolchain, 0,
                      rc == 0 ? NULL : "switch_rust failed");
    return rc == 0 ? 0 : 1;
}

/* ── CLI: --switch-node subprocess mode ────────────────── */

static int do_subprocess_switch_node(int argc, char *argv[]) {
    const char *version_dir = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (version_dir == NULL) {
            version_dir = argv[i];
        }
    }

    if (!version_dir) {
        fprintf(stderr, "Error: missing Node.js path\n");
        return 1;
    }

    int rc = switch_nodejs(version_dir, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, version_dir, 0,
                      rc == 0 ? NULL : "switch_nodejs failed");
    return rc == 0 ? 0 : 1;
}

/* ── CLI: --switch-python subprocess mode ──────────────── */

static int do_subprocess_switch_python(int argc, char *argv[]) {
    const char *version_dir = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (version_dir == NULL) {
            version_dir = argv[i];
        }
    }

    if (!version_dir) {
        fprintf(stderr, "Error: missing Python path\n");
        return 1;
    }

    int rc = switch_python(version_dir, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, version_dir, 0,
                      rc == 0 ? NULL : "switch_python failed");
    return rc == 0 ? 0 : 1;
}

/* ── CLI: --switch-go subprocess mode ──────────────────── */

static int do_subprocess_switch_go(int argc, char *argv[]) {
    const char *goroot = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (goroot == NULL) {
            goroot = argv[i];
        }
    }

    if (!goroot) {
        fprintf(stderr, "Error: missing Go GOROOT\n");
        return 1;
    }

    int rc = switch_go(goroot, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, goroot, 0,
                      rc == 0 ? NULL : "switch_go failed");
    return rc == 0 ? 0 : 1;
}

/* ── CLI: --switch-maven subprocess mode ───────────────── */

static int do_subprocess_switch_maven(int argc, char *argv[]) {
    const char *install_dir = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (install_dir == NULL) {
            install_dir = argv[i];
        }
    }

    if (!install_dir) {
        fprintf(stderr, "Error: missing Maven install dir\n");
        return 1;
    }

    int rc = switch_maven(install_dir, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, install_dir, 0,
                      rc == 0 ? NULL : "switch_maven failed");
    return rc == 0 ? 0 : 1;
}

/* ── CLI: --switch-gradle subprocess mode ──────────────── */

static int do_subprocess_switch_gradle(int argc, char *argv[]) {
    const char *install_dir = NULL;
    const char *backup_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--backup-file") == 0 && i + 1 < argc) {
            backup_file = argv[i + 1];
            i++;
        } else if (install_dir == NULL) {
            install_dir = argv[i];
        }
    }

    if (!install_dir) {
        fprintf(stderr, "Error: missing Gradle install dir\n");
        return 1;
    }

    int rc = switch_gradle(install_dir, backup_file);
    write_result_json(rc == 0 ? "true" : "false", NULL, install_dir, 0,
                      rc == 0 ? NULL : "switch_gradle failed");
    return rc == 0 ? 0 : 1;
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

    /* ── --switch (JDK) subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch") == 0) {
        return do_subprocess_switch(argc, argv);
    }

    /* ── --switch-rust subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-rust") == 0) {
        return do_subprocess_switch_rust(argc, argv);
    }

    /* ── --switch-node subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-node") == 0) {
        return do_subprocess_switch_node(argc, argv);
    }

    /* ── --switch-python subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-python") == 0) {
        return do_subprocess_switch_python(argc, argv);
    }

    /* ── --switch-go subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-go") == 0) {
        return do_subprocess_switch_go(argc, argv);
    }

    /* ── --switch-maven subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-maven") == 0) {
        return do_subprocess_switch_maven(argc, argv);
    }

    /* ── --switch-gradle subprocess mode ── */
    if (argc >= 2 && strcmp(argv[1], "--switch-gradle") == 0) {
        return do_subprocess_switch_gradle(argc, argv);
    }

    /* ── --version ── */
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("Java Version Switcher v%s (C + Win32)\n", VERSION);
        return 0;
    }

    /* ── --scan ── */
    if (argc >= 2 && strcmp(argv[1], "--scan") == 0) {
        return do_cli_scan();
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
