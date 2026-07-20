#ifndef JVS_GUI_H
#define JVS_GUI_H

#include "config.h"
#include "core.h"
#include <windows.h>

/* ── Navigation IDs ───────────────────────────────────── */

typedef enum { NAV_JDK=0, NAV_DL, NAV_ST, NAV_RUST, NAV_NODE, NAV_CNT } NavId;

/* ── Public ─────────────────────────────────────────── */

int gui_run(Config *cfg);

#endif /* JVS_GUI_H */
