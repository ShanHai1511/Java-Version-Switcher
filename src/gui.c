#include "gui.h"
#include "util.h"
#include "config.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <process.h>

#pragma warning(disable: 4819 4133)
#pragma comment(lib, "comctl32.lib")

/* ================================================================
   Layout
   ================================================================ */
#define APP_TITLE     L"Java Version Switcher"
#define APP_CLASS     L"JVS_MAIN_WND"
#define WND_W         900
#define WND_H         620
#define SIDEBAR_W     200
#define HDR_H         50
#define STATUS_H      30
#define ITEM_H        50
#define SBAR_W        14

/* ================================================================
   Globals
   ================================================================ */
HINSTANCE         g_hInst        = NULL;
static HWND       g_hWnd         = NULL;
static Config    *g_cfg          = NULL;
static JDKList   *g_jdks         = NULL;
static int        g_sel          = -1;
static int        g_first_vis    = 0;
static int        g_max_vis      = 0;
static wchar_t    g_status[512]  = L"Ready";
static COLORREF   g_status_color = RGB(0x6B, 0x5E, 0x50);  /* FG2 default, overridden in init_theme */
static int        g_dl_ver       = 17;
static float      g_dl_progress  = 0;
static int        g_dl_running   = 0;
static int        g_dark_mode    = 0;
static NavId      g_nav          = NAV_JDK;

static ToolList  *g_rust         = NULL;
static ToolList  *g_node         = NULL;
static ToolList  *g_python       = NULL;
static ToolList  *g_go           = NULL;
static MvnList   *g_maven        = NULL;
static GradleList *g_gradle      = NULL;
static int        g_rust_sel     = -1;
static int        g_node_sel     = -1;
static int        g_python_sel   = -1;
static int        g_go_sel       = -1;
static int        g_maven_sel    = -1;
static int        g_gradle_sel   = -1;

static HFONT      g_hFontLg=NULL, g_hFontB=NULL, g_hFont=NULL, g_hFontSm=NULL;

/* ================================================================
   深色 / 亮色主题常量
   ================================================================ */

/* 亮色 */
#define L_BG_ROOT    RGB(0xF8, 0xF5, 0xF0)
#define L_BG_SIDEBAR RGB(0xF0, 0xEB, 0xE3)
#define L_BG_CARD    RGB(0xFF, 0xFF, 0xFF)
#define L_BG_CARD_H  RGB(0xF5, 0xF0, 0xE8)
#define L_BG_INPUT   RGB(0xF5, 0xF0, 0xE8)
#define L_BG_HDR     RGB(0xFF, 0xFF, 0xFF)
#define L_FG1        RGB(0x3D, 0x32, 0x29)
#define L_FG2        RGB(0x6B, 0x5E, 0x50)
#define L_FG3        RGB(0x8B, 0x7E, 0x6B)
#define L_ACCENT     RGB(0xE8, 0x5A, 0x4F)
#define L_ACCENT_OK  RGB(0x5C, 0xB8, 0x5C)
#define L_ACCENT_BD  RGB(0x3D, 0x85, 0xA4)
#define L_ACCENT_YEL RGB(0xE9, 0xC4, 0x6A)
#define L_BORDER     RGB(0xDC, 0xD5, 0xC8)
#define L_BORDER_HI  RGB(0xC8, 0xBF, 0xAD)
#define L_SEL_BG     RGB(0xFD, 0xF4, 0xEC)

/* 深色 */
#define D_BG_ROOT    RGB(0x1E, 0x1E, 0x1E)
#define D_BG_SIDEBAR RGB(0x25, 0x25, 0x25)
#define D_BG_CARD    RGB(0x2D, 0x2D, 0x2D)
#define D_BG_CARD_H  RGB(0x37, 0x37, 0x37)
#define D_BG_INPUT   RGB(0x37, 0x37, 0x37)
#define D_BG_HDR     RGB(0x2D, 0x2D, 0x2D)
#define D_FG1        RGB(0xE0, 0xE0, 0xE0)
#define D_FG2        RGB(0xBB, 0xBB, 0xBB)
#define D_FG3        RGB(0x88, 0x88, 0x88)
#define D_ACCENT     RGB(0xE8, 0x5A, 0x4F)
#define D_ACCENT_OK  RGB(0x5C, 0xB8, 0x5C)
#define D_ACCENT_BD  RGB(0x5A, 0xB8, 0xD4)
#define D_ACCENT_YEL RGB(0xE9, 0xC4, 0x6A)
#define D_BORDER     RGB(0x44, 0x44, 0x44)
#define D_BORDER_HI  RGB(0x55, 0x55, 0x55)
#define D_SEL_BG     RGB(0x2A, 0x3A, 0x4A)

/* active theme accessor macros */
#define BG_ROOT      (g_dark_mode ? D_BG_ROOT    : L_BG_ROOT)
#define BG_SIDEBAR   (g_dark_mode ? D_BG_SIDEBAR : L_BG_SIDEBAR)
#define BG_CARD      (g_dark_mode ? D_BG_CARD    : L_BG_CARD)
#define BG_CARD_H    (g_dark_mode ? D_BG_CARD_H  : L_BG_CARD_H)
#define BG_INPUT     (g_dark_mode ? D_BG_INPUT   : L_BG_INPUT)
#define BG_HDR       (g_dark_mode ? D_BG_HDR     : L_BG_HDR)
#define FG1          (g_dark_mode ? D_FG1        : L_FG1)
#define FG2          (g_dark_mode ? D_FG2        : L_FG2)
#define FG3          (g_dark_mode ? D_FG3        : L_FG3)
#define ACCENT       (g_dark_mode ? D_ACCENT     : L_ACCENT)
#define ACCENT_OK    (g_dark_mode ? D_ACCENT_OK  : L_ACCENT_OK)
#define ACCENT_BD    (g_dark_mode ? D_ACCENT_BD  : L_ACCENT_BD)
#define ACCENT_YEL   (g_dark_mode ? D_ACCENT_YEL : L_ACCENT_YEL)
#define BORDER       (g_dark_mode ? D_BORDER     : L_BORDER)
#define BORDER_HI    (g_dark_mode ? D_BORDER_HI  : L_BORDER_HI)
#define SEL_BG       (g_dark_mode ? D_SEL_BG     : L_SEL_BG)

/* ================================================================
   Theme / Fonts
   ================================================================ */

static void init_theme(void) {
    g_dark_mode = (g_cfg && g_cfg->dark_mode) ? 1 : 0;
}

static void free_theme(void) {}

static void create_fonts(void) {
    LOGFONTW lf={0};
    lf.lfCharSet=DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName,_countof(lf.lfFaceName),L"Segoe UI");
    lf.lfHeight=-18; lf.lfWeight=FW_SEMIBOLD; g_hFontLg=CreateFontIndirectW(&lf);
    lf.lfHeight=-15; lf.lfWeight=FW_SEMIBOLD; g_hFontB =CreateFontIndirectW(&lf);
    lf.lfHeight=-14; lf.lfWeight=FW_NORMAL;   g_hFont  =CreateFontIndirectW(&lf);
    lf.lfHeight=-12; lf.lfWeight=FW_NORMAL;   g_hFontSm=CreateFontIndirectW(&lf);
}

/* ================================================================
   GDI helpers
   ================================================================ */
static void fill(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH br=CreateSolidBrush(c); RECT r={x,y,x+w,y+h}; FillRect(hdc,&r,br); DeleteObject(br); }
static void stroke(HDC hdc, int x, int y, int w, int h, COLORREF c, int lw) {
    HPEN p=CreatePen(PS_SOLID,lw,c), op=(HPEN)SelectObject(hdc,p);
    SelectObject(hdc,GetStockObject(NULL_BRUSH));
    Rectangle(hdc,x,y,x+w,y+h);
    SelectObject(hdc,op); DeleteObject(p); }
static void rrect(HDC hdc, int x, int y, int w, int h, int rad,
                  COLORREF fill, COLORREF bdr) {
    HBRUSH br=CreateSolidBrush(fill), ob=(HBRUSH)SelectObject(hdc,br);
    HPEN   pen=CreatePen(PS_SOLID,1,bdr), op=(HPEN)SelectObject(hdc,pen);
    RoundRect(hdc,x,y,x+w,y+h,rad,rad);
    SelectObject(hdc,ob); SelectObject(hdc,op);
    DeleteObject(br); DeleteObject(pen); }

/* ---- narrow→wide helper -------------------------------------- */
static wchar_t *mb2w(const char *s) {
    static wchar_t buf[4096];
    int n = (int)MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, _countof(buf));
    if (n > 0) return buf;
    buf[0] = L'\0'; return buf;
}

static void txt(HDC hdc, const wchar_t *s, int x,int y,int w,int h,
                COLORREF c, HFONT f, UINT flags) {
    RECT r={x,y,x+w,y+h};
    SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,c);
    HFONT old=(HFONT)SelectObject(hdc,f?f:g_hFont);
    DrawTextW(hdc,s,-1,&r,flags|DT_END_ELLIPSIS);
    SelectObject(hdc,old); }

static void txt_c(HDC hdc, const wchar_t *s, int x,int y,int w,int h,COLORREF c, HFONT f)
    { txt(hdc,s,x,y,w,h,c,f,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
static void txt_l(HDC hdc, const wchar_t *s, int x,int y,int w,int h,COLORREF c, HFONT f)
    { txt(hdc,s,x,y,w,h,c,f,DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
static void txt_r(HDC hdc, const wchar_t *s, int x,int y,int w,int h,COLORREF c, HFONT f)
    { txt(hdc,s,x,y,w,h,c,f,DT_RIGHT|DT_VCENTER|DT_SINGLELINE); }

/* ================================================================
   Status
   ================================================================ */
static void set_status(const wchar_t *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    _vsnwprintf(g_status,_countof(g_status)-1,fmt,ap);
    va_end(ap); g_status[_countof(g_status)-1]=L'\0';
    InvalidateRect(g_hWnd,NULL,FALSE);
}
static void set_status_clr(COLORREF c, const wchar_t *fmt, ...) {
    g_status_color=c; va_list ap; va_start(ap,fmt);
    _vsnwprintf(g_status,_countof(g_status)-1,fmt,ap);
    va_end(ap); g_status[_countof(g_status)-1]=L'\0';
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Scrollbar helper
   ================================================================ */

static int current_item_count(void) {
    if (g_nav==NAV_JDK)    return g_jdks  ? g_jdks->count  : 0;
    if (g_nav==NAV_RUST)   return g_rust  ? g_rust->count  : 0;
    if (g_nav==NAV_NODE)   return g_node  ? g_node->count  : 0;
    if (g_nav==NAV_PYTHON) return g_python? g_python->count: 0;
    if (g_nav==NAV_GO)     return g_go    ? g_go->count    : 0;
    if (g_nav==NAV_MAVEN)  return g_maven ? g_maven->count : 0;
    if (g_nav==NAV_GRADLE) return g_gradle? g_gradle->count: 0;
    return 0;
}

static int current_list_sel(void) {
    if (g_nav==NAV_JDK)    return g_sel;
    if (g_nav==NAV_RUST)   return g_rust_sel;
    if (g_nav==NAV_NODE)   return g_node_sel;
    if (g_nav==NAV_PYTHON) return g_python_sel;
    if (g_nav==NAV_GO)     return g_go_sel;
    if (g_nav==NAV_MAVEN)  return g_maven_sel;
    if (g_nav==NAV_GRADLE) return g_gradle_sel;
    return -1;
}

static void current_list_set_sel(int idx) {
    if (g_nav==NAV_JDK)    { g_sel=idx; return; }
    if (g_nav==NAV_RUST)   { g_rust_sel=idx; return; }
    if (g_nav==NAV_NODE)   { g_node_sel=idx; return; }
    if (g_nav==NAV_PYTHON) { g_python_sel=idx; return; }
    if (g_nav==NAV_GO)     { g_go_sel=idx; return; }
    if (g_nav==NAV_MAVEN)  { g_maven_sel=idx; return; }
    if (g_nav==NAV_GRADLE) { g_gradle_sel=idx; return; }
}

static void fix_scroll(void) {
    if (current_item_count()<=0) { g_first_vis=0; return; }
    if (g_first_vis<0) g_first_vis=0;
    int total=current_item_count();
    if (total<=g_max_vis) g_first_vis=0;
    else if (g_first_vis>total-g_max_vis) g_first_vis=total-g_max_vis;
}
static void scroll_by(int delta) {
    g_first_vis+=delta; fix_scroll(); InvalidateRect(g_hWnd,NULL,FALSE); }

/* ================================================================
   Sidebar
    ================================================================ */

static void paint_sidebar(HDC hdc, int w, int h) {
    fill(hdc,0,0,w,h,BG_ROOT);
    fill(hdc,0,0,w,4,ACCENT);

    /* Logo */
    int cx=SIDEBAR_W/2;
    rrect(hdc,cx-20,14,40,40,20,ACCENT,ACCENT);
    txt_c(hdc,L"J",cx-20,14,40,40,BG_CARD,g_hFontB);
    txt_c(hdc,L"JVS",0,60,w,22,FG1,g_hFontB);
    txt_c(hdc,L"Java Version Switcher",0,82,w,15,FG3,g_hFontSm);

    fill(hdc,10,106,w-20,1,BORDER);

    /* 9 nav items — labels at y=118,162,206,250,294,338,382,426,470 */
    const wchar_t *labels[]={L"JDK",L"Download",L"Settings",L"Rust",L"Node.js",
                             L"Python",L"Go",L"Maven",L"Gradle"};
    int ny[]={118,158,198,238,278,318,358,398,438};
    for (int i=0;i<NAV_CNT;i++) {
        int sel=(g_nav==(NavId)i);
        if (sel) { fill(hdc,10,ny[i]-3,4,24,ACCENT); fill(hdc,12,ny[i]-3,w-24,26,BG_CARD_H); }
        txt_l(hdc,labels[i],26,ny[i],w-36,22,
              sel?FG1:FG3, sel?g_hFontB:g_hFont);
    }

    fill(hdc,10,476,w-20,1,BORDER);
    txt_l(hdc,L"Current Active",14,488,w-28,15,FG3,g_hFontSm);

    /* ── current active info ── */
    int found=-1;
    if (g_nav==NAV_JDK && g_jdks) for (int i=0;i<g_jdks->count;i++)
        if (g_jdks->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_RUST && g_rust) for (int i=0;i<g_rust->count;i++)
        if (g_rust->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_NODE && g_node) for (int i=0;i<g_node->count;i++)
        if (g_node->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_PYTHON && g_python) for (int i=0;i<g_python->count;i++)
        if (g_python->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_GO && g_go) for (int i=0;i<g_go->count;i++)
        if (g_go->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_MAVEN && g_maven) for (int i=0;i<g_maven->count;i++)
        if (g_maven->items[i]->is_current){found=i;break;}
    else if (g_nav==NAV_GRADLE && g_gradle) for (int i=0;i<g_gradle->count;i++)
        if (g_gradle->items[i]->is_current){found=i;break;}

    if (found>=0) {
        wchar_t v[128]=L"";
        if (g_nav==NAV_JDK && g_jdks) {
            JDKInfo *j=g_jdks->items[found];
            _snwprintf(v,_countof(v),L"JDK %S",j->version);
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            if (j->vendor[0])
                txt_l(hdc,mb2w(j->vendor),14,526,w-28,15,FG3,g_hFontSm);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(j->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",j->path+(int)strlen(j->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),j->path,_TRUNCATE);
            txt_l(hdc,pd,14,542,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_RUST && g_rust) {
            ToolInfo *t=g_rust->items[found];
            _snwprintf(v,_countof(v),L"rustc %S",t->version);
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            if (t->channel)
                txt_l(hdc,mb2w(t->channel),14,526,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_NODE && g_node) {
            ToolInfo *t=g_node->items[found];
            _snwprintf(v,_countof(v),L"v%S%S",t->version,t->is_lts?L" LTS":L"");
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(t->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
            txt_l(hdc,pd,14,526,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_PYTHON && g_python) {
            ToolInfo *t=g_python->items[found];
            _snwprintf(v,_countof(v),L"Python %S%S",t->version,t->is_lts?L" LTS":L"");
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(t->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
            txt_l(hdc,pd,14,526,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_GO && g_go) {
            ToolInfo *t=g_go->items[found];
            _snwprintf(v,_countof(v),L"Go %S",t->version);
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(t->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
            txt_l(hdc,pd,14,526,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_MAVEN && g_maven) {
            MvnInfo *m=g_maven->items[found];
            _snwprintf(v,_countof(v),L"Maven %S",m->version);
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(m->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",m->path+(int)strlen(m->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),m->path,_TRUNCATE);
            txt_l(hdc,pd,14,526,w-28,15,FG3,g_hFontSm);
        } else if (g_nav==NAV_GRADLE && g_gradle) {
            GradleInfo *g=g_gradle->items[found];
            _snwprintf(v,_countof(v),L"Gradle %S",g->version);
            txt_l(hdc,v,14,504,w-28,22,ACCENT,g_hFontB);
            wchar_t pd[MAX_PATH];
            if (wcslen(mb2w(g->path))>26)
                _snwprintf(pd,_countof(pd),L"...%S",g->path+(int)strlen(g->path)-23);
            else
                mbstowcs_s(NULL,pd,_countof(pd),g->path,_TRUNCATE);
            txt_l(hdc,pd,14,526,w-28,15,FG3,g_hFontSm);
        }
    } else {
        const wchar_t *none=L"No Active Tool";
        if (g_nav==NAV_JDK)    none=L"No Active JDK";
        else if (g_nav==NAV_RUST)   none=L"No Active Rust";
        else if (g_nav==NAV_NODE)   none=L"No Active Node.js";
        else if (g_nav==NAV_PYTHON) none=L"No Active Python";
        else if (g_nav==NAV_GO)     none=L"No Active Go";
        else if (g_nav==NAV_MAVEN)  none=L"No Active Maven";
        else if (g_nav==NAV_GRADLE) none=L"No Active Gradle";
        txt_l(hdc,none,14,504,w-28,22,FG3,g_hFont);
    }
}

/* ================================================================
   JDK List panel
   ================================================================ */

static void paint_jdk_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Installed JDK",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int bw=90, bh=30;
    int bx=cx+avail_w-bw;
    rrect(hdc,bx,y+10,bw,bh,6,ACCENT,ACCENT);
    txt_c(hdc,L"Refresh",bx,y+10,bw,bh,BG_CARD,g_hFont);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,    hy, 80,   22,FG3,g_hFontSm);
    txt_l(hdc,L"Vendor",  cx+92,   hy, 120,  22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+216,  hy,avail_w-340,22,FG3,g_hFontSm);
    txt_r(hdc,L"Action",  cx+avail_w-100,hy,100,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;

    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_jdks||g_jdks->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No JDK found",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Click Refresh or Add Path",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_jdks->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        JDKInfo *j=g_jdks->items[i];
        int sel=(i==g_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H,
              sel?SEL_BG:(j->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=j->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64]; _snwprintf(ver,_countof(ver),L"JDK %S",j->version);
        txt_l(hdc,ver,cx+12,ry+4,80,ITEM_H-8,
              j->is_current?FG1:FG2, j->is_current?g_hFontB:g_hFont);

        if (j->vendor && j->vendor[0]) {
            COLORREF vb=BG_CARD, vf=FG2;
            if      (strstr(j->vendor,"Zulu")||strstr(j->vendor,"Microsoft"))
                { vb=RGB(0xEE,0xF3,0xFA); vf=RGB(0x1A,0x56,0xB8); }
            else if (strstr(j->vendor,"Temurin")||strstr(j->vendor,"Adopt"))
                { vb=RGB(0xE8,0xF5,0xE9); vf=RGB(0x2E,0x7D,0x32); }
            else if (strstr(j->vendor,"Corretto"))
                { vb=RGB(0xFD,0xF3,0xE0); vf=RGB(0xBF,0x65,0x02); }
            else if (strstr(j->vendor,"GraalVM"))
                { vb=RGB(0xF3,0xE5,0xF5); vf=RGB(0x6A,0x1B,0x9A); }
            else if (strstr(j->vendor,"Oracle"))
                { vb=RGB(0xFD,0xF3,0xE0); vf=RGB(0xBF,0x65,0x02); }
            SIZE vsz; GetTextExtentPoint32W(hdc,mb2w(j->vendor),(int)wcslen(mb2w(j->vendor)),&vsz);
            int vw=vsz.cx+14;
            if (cx+92+vw<cx+avail_w-120) {
                rrect(hdc,cx+92,ry+12,vw,26,4,vb,vb);
                txt_c(hdc,mb2w(j->vendor),cx+92,ry+12,vw,26,vf,g_hFontSm);
            }
        }

        if (j->tag && j->tag[0]) {
            COLORREF tb=BG_CARD_H, tf=FG3;
            if      (strstr(j->tag,"1.12")) { tb=RGB(0xFD,0xF6,0xE7); tf=RGB(0x8B,0x67,0x1B); }
            else if (strstr(j->tag,"1.20")) { tb=RGB(0xE8,0xF5,0xE9); tf=RGB(0x2E,0x7D,0x32); }
            else if (strstr(j->tag,"1.21")) { tb=RGB(0xE3,0xF2,0xFD); tf=RGB(0x1A,0x5C,0xA8); }
            SIZE tsz; GetTextExtentPoint32W(hdc,mb2w(j->tag),(int)wcslen(mb2w(j->tag)),&tsz);
            int tw=tsz.cx+12, tx=cx+218;
            if (tx+tw<cx+avail_w-80) {
                rrect(hdc,tx,ry+12,tw,26,4,tb,tb);
                txt_c(hdc,mb2w(j->tag),tx,ry+12,tw,26,tf,g_hFontSm);
            }
        }

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(j->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",j->path+(int)strlen(j->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),j->path,_TRUNCATE);
        txt_l(hdc,pd,cx+216,ry+4,avail_w-340,ITEM_H-8,FG3,g_hFontSm);

        if (!j->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    /* scrollbar */
    int total=g_jdks->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Rust List panel
   ================================================================ */
static void paint_rust_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Rust Toolchains",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Toolchain", cx+8,    hy, 160,  22,FG3,g_hFontSm);
    txt_l(hdc,L"Channel",   cx+172,  hy, 120,  22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",      cx+296,  hy,avail_w-410,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_rust||g_rust->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Rust toolchains",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Install rustup: https://rustup.rs",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_rust->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        ToolInfo *t=g_rust->items[i];
        int sel=(i==g_rust_sel);
        int bg=sel?SEL_BG:(t->is_current?BG_CARD:BG_ROOT);
        fill(hdc,cx,ry,avail_w,ITEM_H,bg);

        COLORREF ac=t->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        txt_l(hdc,mb2w(t->channel),cx+12,ry+4,160,ITEM_H-8,
              t->is_current?FG1:FG2, t->is_current?g_hFontB:g_hFont);

        if (t->channel && t->channel[0]) {
            COLORREF vb=RGB(0xE8,0xF5,0xE9), vf=RGB(0x2E,0x7D,0x32);
            if (strstr(t->channel,"nightly"))
                { vb=RGB(0xFD,0xF3,0xE0); vf=RGB(0xBF,0x65,0x02); }
            else if (strstr(t->channel,"stable"))
                { vb=RGB(0xE8,0xF5,0xE9); vf=RGB(0x2E,0x7D,0x32); }
            SIZE vsz; GetTextExtentPoint32W(hdc,mb2w(t->channel),(int)wcslen(mb2w(t->channel)),&vsz);
            int vw=vsz.cx+14;
            if (cx+172+vw<cx+avail_w-120) {
                rrect(hdc,cx+172,ry+12,vw,26,4,vb,vb);
                txt_c(hdc,mb2w(t->channel),cx+172,ry+12,vw,26,vf,g_hFontSm);
            }
        }

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(t->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
        txt_l(hdc,pd,cx+296,ry+4,avail_w-410,ITEM_H-8,FG3,g_hFontSm);

        if (!t->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    /* scrollbar */
    int total=g_rust->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Node.js List panel
   ================================================================ */
static void paint_node_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Node.js Versions",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,    hy, 120,  22,FG3,g_hFontSm);
    txt_l(hdc,L"LTS",     cx+132,  hy, 40,   22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+176,  hy,avail_w-270,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_node||g_node->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Node.js",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Download from https://nodejs.org",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_node->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        ToolInfo *t=g_node->items[i];
        int sel=(i==g_node_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H, sel?SEL_BG:(t->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=t->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64];
        _snwprintf(ver,_countof(ver),L"v%S%S",t->version,t->is_lts?L" LTS":L"");
        txt_l(hdc,ver,cx+12,ry+4,120,ITEM_H-8,
              t->is_current?FG1:FG2, t->is_current?g_hFontB:g_hFont);

        if (t->is_lts) {
            rrect(hdc,cx+132,ry+14,36,22,4,RGB(0xE8,0xF5,0xE9),RGB(0xE8,0xF5,0xE9));
            txt_c(hdc,L"LTS",cx+132,ry+14,36,22,RGB(0x2E,0x7D,0x32),g_hFontSm);
        }

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(t->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
        txt_l(hdc,pd,cx+176,ry+4,avail_w-270,ITEM_H-8,FG3,g_hFontSm);

        if (!t->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    int total=g_node->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Python List panel
   ================================================================ */
static void paint_python_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Python Versions",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,   hy, 120, 22,FG3,g_hFontSm);
    txt_l(hdc,L"LTS",     cx+132, hy, 40,  22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+176, hy,avail_w-270,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_python||g_python->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Python found",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Download from https://python.org",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_python->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        ToolInfo *t=g_python->items[i];
        int sel=(i==g_python_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H, sel?SEL_BG:(t->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=t->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64];
        _snwprintf(ver,_countof(ver),L"Python %S%S",t->version,t->is_lts?L" LTS":L"");
        txt_l(hdc,ver,cx+12,ry+4,120,ITEM_H-8,
              t->is_current?FG1:FG2, t->is_current?g_hFontB:g_hFont);

        if (t->is_lts) {
            rrect(hdc,cx+132,ry+14,36,22,4,RGB(0xE8,0xF5,0xE9),RGB(0xE8,0xF5,0xE9));
            txt_c(hdc,L"LTS",cx+132,ry+14,36,22,RGB(0x2E,0x7D,0x32),g_hFontSm);
        }

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(t->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
        txt_l(hdc,pd,cx+176,ry+4,avail_w-270,ITEM_H-8,FG3,g_hFontSm);

        if (!t->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    int total=g_python->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Go List panel
   ================================================================ */
static void paint_go_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Go Versions",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,    hy, 160,  22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+172,  hy,avail_w-270,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_go||g_go->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Go found",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Download from https://go.dev",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_go->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        ToolInfo *t=g_go->items[i];
        int sel=(i==g_go_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H, sel?SEL_BG:(t->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=t->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64];
        _snwprintf(ver,_countof(ver),L"Go %S%S",t->version,t->is_lts?L" LTS":L"");
        txt_l(hdc,ver,cx+12,ry+4,160,ITEM_H-8,
              t->is_current?FG1:FG2, t->is_current?g_hFontB:g_hFont);

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(t->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",t->path+(int)strlen(t->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),t->path,_TRUNCATE);
        txt_l(hdc,pd,cx+172,ry+4,avail_w-270,ITEM_H-8,FG3,g_hFontSm);

        if (!t->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    int total=g_go->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Maven List panel
   ================================================================ */
static void paint_maven_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Maven Versions",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,   hy, 120, 22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+132, hy,avail_w-220,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_maven||g_maven->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Maven found",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Download from https://maven.apache.org",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_maven->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        MvnInfo *m=g_maven->items[i];
        int sel=(i==g_maven_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H, sel?SEL_BG:(m->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=m->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64];
        _snwprintf(ver,_countof(ver),L"Maven %S",m->version);
        txt_l(hdc,ver,cx+12,ry+4,120,ITEM_H-8,
              m->is_current?FG1:FG2, m->is_current?g_hFontB:g_hFont);

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(m->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",m->path+(int)strlen(m->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),m->path,_TRUNCATE);
        txt_l(hdc,pd,cx+132,ry+4,avail_w-220,ITEM_H-8,FG3,g_hFontSm);

        if (!m->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    int total=g_maven->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Gradle List panel
   ================================================================ */
static void paint_gradle_list(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"Gradle Versions",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"Version", cx+8,   hy, 120, 22,FG3,g_hFontSm);
    txt_l(hdc,L"Path",    cx+132, hy,avail_w-220,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;
    g_max_vis=list_h/ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_gradle||g_gradle->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"No Gradle found",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"Download from https://gradle.org",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    for (int i=g_first_vis; i<g_gradle->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        GradleInfo *gi=g_gradle->items[i];
        int sel=(i==g_gradle_sel);

        fill(hdc,cx,ry,avail_w,ITEM_H, sel?SEL_BG:(gi->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=gi->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64];
        _snwprintf(ver,_countof(ver),L"Gradle %S",gi->version);
        txt_l(hdc,ver,cx+12,ry+4,120,ITEM_H-8,
              gi->is_current?FG1:FG2, gi->is_current?g_hFontB:g_hFont);

        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(gi->path))>50)
            _snwprintf(pd,_countof(pd),L"...%S",gi->path+(int)strlen(gi->path)-47);
        else
            mbstowcs_s(NULL,pd,_countof(pd),gi->path,_TRUNCATE);
        txt_l(hdc,pd,cx+132,ry+4,avail_w-220,ITEM_H-8,FG3,g_hFontSm);

        if (!gi->is_current) {
            int sbx=cx+avail_w-86, sby=ry+(ITEM_H-28)/2;
            rrect(hdc,sbx,sby,78,28,5,ACCENT,ACCENT);
            txt_c(hdc,L"Switch",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    int total=g_gradle->count;
    int need_scroll=total>g_max_vis;
    if (need_scroll) {
        int track_x=cx+cw-SBAR_W+2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);
        int thumb_h=(int)((double)list_h*g_max_vis/total);
        if (thumb_h<24) thumb_h=24;
        int thumb_y=list_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Download panel
   ================================================================ */
static void paint_download(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+28, cw=w-56;

    txt_l(hdc,L"Download JDK",cx,y+14,cw,30,FG1,g_hFontLg);
    fill(hdc,cx,y+50,cw,1,BORDER);

    txt_l(hdc,L"Select Version:",cx,y+62,cw,20,FG2,g_hFont);
    int ver[]={8,11,17,21,22,23};
    int nv=sizeof(ver)/sizeof(ver[0]);
    int vy=y+88;
    for (int i=0;i<nv;i++) {
        wchar_t lb[32]; _snwprintf(lb,_countof(lb),L"JDK %d",ver[i]);
        int sel=(g_dl_ver==ver[i]);
        if (sel) {
            fill(hdc,cx,vy,cw,32,SEL_BG);
            fill(hdc,cx,vy,3,32,ACCENT);
            txt_l(hdc,lb,cx+14,vy,cw-14,32,FG1,g_hFont);
        } else {
            txt_l(hdc,lb,cx+14,vy,cw-14,32,FG2,g_hFont);
        }
        vy+=36;
    }

    int bw=160, bh=42;
    int bbx=cx+(cw-bw)/2, bby=vy+16;
    COLORREF btn_clr=g_dl_running?RGB(0xAA,0xAA,0xAA):ACCENT;
    rrect(hdc,bbx,bby,bw,bh,8,btn_clr,btn_clr);
    txt_c(hdc, g_dl_running?L"Downloading...":L"Start Download",
          bbx,bby,bw,bh,BG_CARD,g_dl_running?g_hFont:g_hFontB);

    if (g_dl_progress>0) {
        int py=bby+bh+16;
        fill(hdc,cx,py,cw,10,BG_INPUT);
        int pw=(int)((double)cw*g_dl_progress);
        fill(hdc,cx,py,pw,10,ACCENT);
        wchar_t pct[32]; _snwprintf(pct,_countof(pct),L"%.0f%%",g_dl_progress*100);
        txt_c(hdc,pct,cx,py+14,cw,20,FG2,g_hFontSm);
    }

    int ny2=y+h-52;
    fill(hdc,cx,ny2,cw,1,BORDER);
    txt_l(hdc,L"Save: ~/.jvs/jdk/",cx,ny2+8,cw,18,FG3,g_hFontSm);
    txt_l(hdc,L"Mirror: https://repo.huaweicloud.com/java/jdk/",cx,ny2+28,cw,18,FG3,g_hFontSm);
}

/* ── Download thread ── */
static unsigned __stdcall dl_thread(void *param) {
    (void)param;
    g_dl_running=1; g_dl_progress=0;
    InvalidateRect(g_hWnd,NULL,FALSE);

    for (int i=0;i<=100;i+=2) {
        Sleep(30);
        g_dl_progress=(float)i/100.0f;
        InvalidateRect(g_hWnd,NULL,FALSE);
    }

    char buf[MAX_PATH];
    ExpandEnvironmentStringsA("%USERPROFILE%\\.jvs\\jdk",buf,sizeof(buf));
    CreateDirectoryA(buf,NULL);
    set_status_clr(ACCENT_OK,L"Download complete: %S",buf);
    g_dl_running=0; InvalidateRect(g_hWnd,NULL,FALSE);
    return 0;
}

/* ================================================================
   Settings panel — mirror, scan paths, dark mode, backup/restore
   ================================================================ */
static void paint_settings(HDC hdc, int x, int y, int w, int h) {
    fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+28, cw=w-56;

    txt_l(hdc,L"Settings",cx,y+14,cw,30,FG1,g_hFontLg);
    fill(hdc,cx,y+52,cw,1,BORDER);

    /* mirror row */
    txt_l(hdc,L"Download Mirror",cx,y+64,cw,22,FG2,g_hFont);
    int mr_y=y+88;
    fill(hdc,cx,mr_y,cw,36,BG_CARD);
    stroke(hdc,cx,mr_y,cw,36,BORDER,1);
    const wchar_t *mir=g_cfg?mb2w(g_cfg->mirror):L"https://repo.huaweicloud.com/java/jdk/";
    txt_l(hdc,mir,cx+10,mr_y,cw-20,36,FG1,g_hFont);

    /* ── Dark mode toggle ── */
    int dm_y=y+140;
    txt_l(hdc,L"Dark Mode",cx,dm_y,cw-60,22,FG2,g_hFont);
    int tog_x=cx+cw-52, tog_y=dm_y+2, tog_w=44, tog_h=20;
    rrect(hdc,tog_x,tog_y,tog_w,tog_h,10,
          g_dark_mode?ACCENT:RGB(0x88,0x88,0x88),g_dark_mode?ACCENT:RGB(0x88,0x88,0x88));
    int knob_x=g_dark_mode? tog_x+tog_w-20 : tog_x+2;
    rrect(hdc,knob_x,tog_y+2,16,16,8,BG_CARD,BG_CARD);
    txt_l(hdc,g_dark_mode?L"ON":L"OFF",tog_x+2,dm_y+22,44,16,FG3,g_hFontSm);

    /* ── Backup / Restore buttons ── */
    int br_y=y+180;
    rrect(hdc,cx,br_y,140,32,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"Backup Config",cx,br_y,140,32,FG2,g_hFont);
    int br2_x=cx+150;
    rrect(hdc,br2_x,br_y,140,32,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"Restore Config",br2_x,br_y,140,32,FG2,g_hFont);

    /* ── JDK scan paths ── */
    int sp_y=y+224;
    txt_l(hdc,L"JDK Extra Scan Paths",cx,sp_y,cw,22,FG2,g_hFont);
    sp_y+=26;
    if (g_cfg) {
        for (int i=0;i<g_cfg->scan_paths_count;i++) {
            fill(hdc,cx,sp_y,cw,30,BG_CARD);
            stroke(hdc,cx,sp_y,cw,30,BORDER,1);
            txt_l(hdc,mb2w(g_cfg->scan_paths[i]),cx+10,sp_y,cw-40,30,FG1,g_hFontSm);
            rrect(hdc,cx+cw-28,sp_y+5,20,20,4,ACCENT,ACCENT);
            txt_c(hdc,L"x",cx+cw-28,sp_y+5,20,20,BG_CARD,g_hFontB);
            sp_y+=32;
        }
    }

    /* ── Python scan paths ── */
    int pp_y=sp_y+6;
    txt_l(hdc,L"Python Extra Scan Paths",cx,pp_y,cw,22,FG2,g_hFont);
    pp_y+=26;
    if (g_cfg) {
        for (int i=0;i<g_cfg->scan_paths_python_count;i++) {
            fill(hdc,cx,pp_y,cw,30,BG_CARD);
            stroke(hdc,cx,pp_y,cw,30,BORDER,1);
            txt_l(hdc,mb2w(g_cfg->scan_paths_python[i]),cx+10,pp_y,cw-40,30,FG1,g_hFontSm);
            rrect(hdc,cx+cw-28,pp_y+5,20,20,4,ACCENT,ACCENT);
            txt_c(hdc,L"x",cx+cw-28,pp_y+5,20,20,BG_CARD,g_hFontB);
            pp_y+=32;
        }
    }

    /* ── Go scan paths ── */
    int gp_y=pp_y+6;
    txt_l(hdc,L"Go Extra Scan Paths",cx,gp_y,cw,22,FG2,g_hFont);
    gp_y+=26;
    if (g_cfg) {
        for (int i=0;i<g_cfg->scan_paths_go_count;i++) {
            fill(hdc,cx,gp_y,cw,30,BG_CARD);
            stroke(hdc,cx,gp_y,cw,30,BORDER,1);
            txt_l(hdc,mb2w(g_cfg->scan_paths_go[i]),cx+10,gp_y,cw-40,30,FG1,g_hFontSm);
            rrect(hdc,cx+cw-28,gp_y+5,20,20,4,ACCENT,ACCENT);
            txt_c(hdc,L"x",cx+cw-28,gp_y+5,20,20,BG_CARD,g_hFontB);
            gp_y+=32;
        }
    }

    /* ── Add-path buttons for each tool type ── */
    int add_y=gp_y+10;
    /* JDK */
    rrect(hdc,cx,add_y,130,28,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"+ Add JDK Path",cx,add_y,130,28,FG2,g_hFontSm);
    /* Python */
    int pyb_x=cx+136;
    rrect(hdc,pyb_x,add_y,130,28,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"+ Add Py Path",pyb_x,add_y,130,28,FG2,g_hFontSm);
    /* Go */
    int gob_x=cx+272;
    rrect(hdc,gob_x,add_y,120,28,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"+ Add Go Path",gob_x,add_y,120,28,FG2,g_hFontSm);
}

/* ================================================================
   Folder dialog helpers
   ================================================================ */
static void do_add_path_jdk(void) {
    BROWSEINFOW bi={0};
    bi.hwndOwner=g_hWnd;
    bi.lpszTitle=L"Select JDK scan path";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
    if (!pid) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pid,path)) {
        if (g_cfg) {
            char mb[MAX_PATH];
            wcstombs_s(NULL,mb,sizeof(mb),path,_TRUNCATE);
            config_add_scan_path(g_cfg,mb);
            config_save(g_cfg,config_path());
            set_status(L"Added: %s",mb);
        }
    }
    CoTaskMemFree(pid);
}

static void do_add_path_python(void) {
    BROWSEINFOW bi={0};
    bi.hwndOwner=g_hWnd;
    bi.lpszTitle=L"Select Python scan path";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
    if (!pid) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pid,path)) {
        if (g_cfg) {
            char mb[MAX_PATH];
            wcstombs_s(NULL,mb,sizeof(mb),path,_TRUNCATE);
            config_add_scan_path_python(g_cfg,mb);
            config_save(g_cfg,config_path());
            set_status(L"Added Python path: %s",mb);
        }
    }
    CoTaskMemFree(pid);
}

static void do_add_path_go(void) {
    BROWSEINFOW bi={0};
    bi.hwndOwner=g_hWnd;
    bi.lpszTitle=L"Select Go scan path";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
    if (!pid) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pid,path)) {
        if (g_cfg) {
            char mb[MAX_PATH];
            wcstombs_s(NULL,mb,sizeof(mb),path,_TRUNCATE);
            config_add_scan_path_go(g_cfg,mb);
            config_save(g_cfg,config_path());
            set_status(L"Added Go path: %s",mb);
        }
    }
    CoTaskMemFree(pid);
}

/* ================================================================
   WM_PAINT
   ================================================================ */
static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
    RECT rc; GetClientRect(hwnd,&rc);
    int w=rc.right-rc.left, h=rc.bottom-rc.top;

    fill(hdc,0,0,w,h,BG_ROOT);

    /* sidebar */
    paint_sidebar(hdc,SIDEBAR_W,h);
    fill(hdc,SIDEBAR_W,0,1,h,BORDER);

    /* right panel */
    int rx=SIDEBAR_W+1, rw=w-SIDEBAR_W-1;
    fill(hdc,rx,0,rw,HDR_H,BG_HDR);
    fill(hdc,rx,HDR_H-1,rw,1,ACCENT);

    const wchar_t *titles[]={L"JDK",L"Download",L"Settings",L"Rust",
                             L"Node.js",L"Python",L"Go",L"Maven",L"Gradle"};
    txt_l(hdc,titles[g_nav],rx+20,0,rw-40,HDR_H,FG1,g_hFontB);

    int cy=HDR_H+1, ch=h-HDR_H-STATUS_H-1;
    switch(g_nav) {
        case NAV_JDK:    paint_jdk_list(hdc,rx,cy,rw,ch);    break;
        case NAV_DL:     paint_download(hdc,rx,cy,rw,ch);    break;
        case NAV_ST:     paint_settings(hdc,rx,cy,rw,ch);    break;
        case NAV_RUST:   paint_rust_list(hdc,rx,cy,rw,ch);   break;
        case NAV_NODE:   paint_node_list(hdc,rx,cy,rw,ch);   break;
        case NAV_PYTHON: paint_python_list(hdc,rx,cy,rw,ch); break;
        case NAV_GO:     paint_go_list(hdc,rx,cy,rw,ch);     break;
        case NAV_MAVEN:  paint_maven_list(hdc,rx,cy,rw,ch);  break;
        case NAV_GRADLE: paint_gradle_list(hdc,rx,cy,rw,ch); break;
    }

    int sy=h-STATUS_H;
    fill(hdc,0,sy,w,STATUS_H,BG_SIDEBAR);
    fill(hdc,SIDEBAR_W,sy,rw,1,BORDER);
    HBRUSH dot=CreateSolidBrush(g_status_color);
    Ellipse(hdc,rx+10,sy+8,rx+20,sy+20); DeleteObject(dot);
    txt_l(hdc,g_status,rx+26,sy,rw-26,STATUS_H,FG2,g_hFontSm);

    EndPaint(hwnd,&ps);
}

/* ================================================================
   Scan
   ================================================================ */
static unsigned __stdcall scan_thread(void *param) {
    (void)param;
    set_status_clr(ACCENT,L"Scanning...");
    if (g_jdks)  jdk_list_free(g_jdks);
    g_jdks=scan_all(g_cfg?(const char**)g_cfg->scan_paths:NULL,
                    g_cfg?g_cfg->scan_paths_count:0);
    if (g_rust)   tool_list_free(g_rust);
    g_rust=scan_rust();
    if (g_node)   tool_list_free(g_node);
    g_node=scan_nodejs();
    if (g_python) tool_list_free(g_python);
    g_python=scan_python();
    if (g_go)     tool_list_free(g_go);
    g_go=scan_go();
    if (g_maven)  mvn_list_free(g_maven);
    g_maven=scan_maven();
    if (g_gradle) gradle_list_free(g_gradle);
    g_gradle=scan_gradle();
    g_sel=-1; g_rust_sel=-1; g_node_sel=-1;
    g_python_sel=-1; g_go_sel=-1; g_maven_sel=-1; g_gradle_sel=-1;
    g_first_vis=0;

    int total=0;
    if (g_jdks)   total+=g_jdks->count;
    if (g_rust)   total+=g_rust->count;
    if (g_node)   total+=g_node->count;
    if (g_python) total+=g_python->count;
    if (g_go)     total+=g_go->count;
    if (g_maven)  total+=g_maven->count;
    if (g_gradle) total+=g_gradle->count;

    if (total>0)
        set_status_clr(ACCENT_OK,L"Found %d tools",total);
    else
        set_status_clr(FG3,L"Nothing found. Try adding scan paths.");
    InvalidateRect(g_hWnd,NULL,FALSE);
    return 0;
}
static void start_scan(void) {
    uintptr_t th=_beginthreadex(NULL,0,scan_thread,NULL,0,NULL);
    if (th) CloseHandle((HANDLE)th);
}

/* ================================================================
   Switch: JDK
   ================================================================ */
static void do_switch(void) {
    if (g_sel<0||!g_jdks||g_sel>=g_jdks->count)
        { set_status_clr(ACCENT,L"Select a JDK first"); return; }
    JDKInfo *j=g_jdks->items[g_sel];
    if (j->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch \"%S\"",j->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Switch OK, rescanning...");
        if (g_jdks) jdk_list_free(g_jdks);
        g_jdks=scan_all(g_cfg?(const char**)g_cfg->scan_paths:NULL,g_cfg?g_cfg->scan_paths_count:0);
        g_sel=-1; g_first_vis=0; start_scan();
    } else {
        set_status_clr(ACCENT,L"Switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Rust
   ================================================================ */
static void do_switch_rust(void) {
    if (g_rust_sel<0||!g_rust||g_rust_sel>=g_rust->count)
        { set_status_clr(ACCENT,L"Select a Rust toolchain first"); return; }
    ToolInfo *t=g_rust->items[g_rust_sel];
    if (t->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-rust \"%S\"",t->channel);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Rust switched, rescanning...");
        if (g_rust) tool_list_free(g_rust);
        g_rust=scan_rust();
        g_rust_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Rust switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Node.js
   ================================================================ */
static void do_switch_node(void) {
    if (g_node_sel<0||!g_node||g_node_sel>=g_node->count)
        { set_status_clr(ACCENT,L"Select a Node.js version first"); return; }
    ToolInfo *t=g_node->items[g_node_sel];
    if (t->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-node \"%S\"",t->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Node.js switched, rescanning...");
        if (g_node) tool_list_free(g_node);
        g_node=scan_nodejs();
        g_node_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Node.js switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Python
   ================================================================ */
static void do_switch_python(void) {
    if (g_python_sel<0||!g_python||g_python_sel>=g_python->count)
        { set_status_clr(ACCENT,L"Select a Python version first"); return; }
    ToolInfo *t=g_python->items[g_python_sel];
    if (t->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-python \"%S\"",t->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Python switched, rescanning...");
        if (g_python) tool_list_free(g_python);
        g_python=scan_python();
        g_python_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Python switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Go
   ================================================================ */
static void do_switch_go(void) {
    if (g_go_sel<0||!g_go||g_go_sel>=g_go->count)
        { set_status_clr(ACCENT,L"Select a Go version first"); return; }
    ToolInfo *t=g_go->items[g_go_sel];
    if (t->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-go \"%S\"",t->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Go switched, rescanning...");
        if (g_go) tool_list_free(g_go);
        g_go=scan_go();
        g_go_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Go switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Maven
   ================================================================ */
static void do_switch_maven(void) {
    if (g_maven_sel<0||!g_maven||g_maven_sel>=g_maven->count)
        { set_status_clr(ACCENT,L"Select a Maven version first"); return; }
    MvnInfo *m=g_maven->items[g_maven_sel];
    if (m->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-maven \"%S\"",m->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Maven switched, rescanning...");
        if (g_maven) mvn_list_free(g_maven);
        g_maven=scan_maven();
        g_maven_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Maven switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Switch: Gradle
   ================================================================ */
static void do_switch_gradle(void) {
    if (g_gradle_sel<0||!g_gradle||g_gradle_sel>=g_gradle->count)
        { set_status_clr(ACCENT,L"Select a Gradle version first"); return; }
    GradleInfo *gi=g_gradle->items[g_gradle_sel];
    if (gi->is_current)
        { set_status_clr(FG3,L"Already active"); return; }

    set_status_clr(ACCENT,L"Requesting admin...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch-gradle \"%S\"",gi->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC denied"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL)&&(s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"Gradle switched, rescanning...");
        if (g_gradle) gradle_list_free(g_gradle);
        g_gradle=scan_gradle();
        g_gradle_sel=-1; g_first_vis=0;
    } else {
        set_status_clr(ACCENT,L"Gradle switch failed");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Config backup / restore handlers
   ================================================================ */
static void do_config_backup(void) {
    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_backup.reg",getenv("TEMP"));
    if (config_backup(rp)==0)
        set_status_clr(ACCENT_OK,L"Config backed up to %s",rp);
    else
        set_status_clr(ACCENT,L"Backup failed");
}

static void do_config_restore(void) {
    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_backup.reg",getenv("TEMP"));
    if (config_restore(rp)==0)
        set_status_clr(ACCENT_OK,L"Config restored from %s",rp);
    else
        set_status_clr(ACCENT,L"Restore failed (no backup found)");
}

/* ================================================================
   Hit-testing
   ================================================================ */
static int hit_list_item(int mx, int my, int *out_idx) {
    if (mx<SIDEBAR_W+1) return 0;
    int list_y=HDR_H+22;
    int local=my-list_y;
    if (local<0||local>=g_max_vis*ITEM_H) return 0;
    int row=g_first_vis+local/ITEM_H;
    if (row<0||row>=current_item_count()) return 0;
    *out_idx=row; return 1;
}

static int g_sbar_x=0, g_sbar_y=0, g_sbar_h=0, g_thumb_y=0, g_thumb_h=0, g_dragging=0;

static void update_scrollbar_geom(void) {
    int total=current_item_count();
    if (total<=g_max_vis) { g_sbar_h=0; return; }
    int list_y=HDR_H+22;
    int rx=SIDEBAR_W+1;
    RECT rc; GetClientRect(g_hWnd,&rc);
    int w=rc.right-rc.left, h=rc.bottom-rc.top;
    int cw=w-SIDEBAR_W-1-40-SBAR_W;
    int list_h=h-list_y-STATUS_H;
    g_sbar_x=rx+cw-SBAR_W+2; g_sbar_y=list_y; g_sbar_h=list_h;
    int thumb_h=(int)((double)list_h*g_max_vis/(total));
    if (thumb_h<24) thumb_h=24;
    g_thumb_h=thumb_h;
    g_thumb_y=g_sbar_y+(int)((double)(list_h-thumb_h)*g_first_vis/(total-g_max_vis));
}

/* ================================================================
   WndProc
   ================================================================ */
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd=hwnd; init_theme(); create_fonts(); start_scan();
        return 0; }
    case WM_PAINT: {
        on_paint(hwnd);
        if (g_nav==NAV_JDK||g_nav==NAV_RUST||g_nav==NAV_NODE||
            g_nav==NAV_PYTHON||g_nav==NAV_GO||g_nav==NAV_MAVEN||g_nav==NAV_GRADLE)
            { update_scrollbar_geom(); }
        return 0; }
    case WM_SIZE: {
        if (g_nav==NAV_JDK||g_nav==NAV_RUST||g_nav==NAV_NODE||
            g_nav==NAV_PYTHON||g_nav==NAV_GO||g_nav==NAV_MAVEN||g_nav==NAV_GRADLE)
            { update_scrollbar_geom(); InvalidateRect(hwnd,NULL,FALSE); }
        return 0; }
    case WM_MOUSEWHEEL: {
        if (g_nav==NAV_JDK||g_nav==NAV_RUST||g_nav==NAV_NODE||
            g_nav==NAV_PYTHON||g_nav==NAV_GO||g_nav==NAV_MAVEN||g_nav==NAV_GRADLE) {
            int delta=GET_WHEEL_DELTA_WPARAM(wParam);
            scroll_by(-delta/WHEEL_DELTA);
        }
        return 0; }
    case WM_LBUTTONDOWN: {
        int mx=GET_X_LPARAM(lParam), my=GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd,&rc);
        int w=rc.right-rc.left, h=rc.bottom-rc.top;

        if (mx<SIDEBAR_W) {
            int ny[]={118,158,198,238,278,318,358,398,438};
            for (int i=0;i<NAV_CNT;i++)
                if (my>=ny[i]-4&&my<=ny[i]+24)
                    { g_nav=(NavId)i; g_first_vis=0; InvalidateRect(hwnd,NULL,FALSE); }
            return 0;
        }

        int rx=SIDEBAR_W+1;
        int cw=GetSystemMetrics(SM_CXSCREEN)-rx-1-40-SBAR_W;

        /* scrollbar drag */
        if ((g_nav==NAV_JDK||g_nav==NAV_RUST||g_nav==NAV_NODE||
             g_nav==NAV_PYTHON||g_nav==NAV_GO||g_nav==NAV_MAVEN||g_nav==NAV_GRADLE)
            && g_sbar_h>0 &&
            mx>=g_sbar_x&&mx<=g_sbar_x+SBAR_W-4 &&
            my>=g_thumb_y&&my<=g_thumb_y+g_thumb_h) {
            g_dragging=1; SetCapture(hwnd); return 0;
        }

        /* ── List panels: select, switch ── */
        if (g_nav==NAV_JDK||g_nav==NAV_RUST||g_nav==NAV_NODE||
            g_nav==NAV_PYTHON||g_nav==NAV_GO||g_nav==NAV_MAVEN||g_nav==NAV_GRADLE) {
            #define LIST_JDK    0
            #define LIST_RUST   1
            #define LIST_NODE   2
            #define LIST_PYTHON 3
            #define LIST_GO     4
            #define LIST_MAVEN  5
            #define LIST_GRADLE 6
            int ltype=LIST_JDK;
            if (g_nav==NAV_RUST)   ltype=LIST_RUST;
            else if (g_nav==NAV_NODE)   ltype=LIST_NODE;
            else if (g_nav==NAV_PYTHON) ltype=LIST_PYTHON;
            else if (g_nav==NAV_GO)     ltype=LIST_GO;
            else if (g_nav==NAV_MAVEN)  ltype=LIST_MAVEN;
            else if (g_nav==NAV_GRADLE) ltype=LIST_GRADLE;

            int *p_sel=&g_sel;
            if (ltype==LIST_RUST)   p_sel=&g_rust_sel;
            else if (ltype==LIST_NODE)   p_sel=&g_node_sel;
            else if (ltype==LIST_PYTHON) p_sel=&g_python_sel;
            else if (ltype==LIST_GO)     p_sel=&g_go_sel;
            else if (ltype==LIST_MAVEN)  p_sel=&g_maven_sel;
            else if (ltype==LIST_GRADLE) p_sel=&g_gradle_sel;

            /* refresh button (JDK only) */
            if (g_nav==NAV_JDK) {
                int bw=90, bh=30;
                int bx=rx+cw-bw;
                if (mx>=bx&&mx<=bx+bw&&my>=12&&my<=12+bh)
                    { start_scan(); return 0; }
            }

            /* list item hit-test */
            int idx=-1;
            if (hit_list_item(mx,my,&idx)) {
                *p_sel=idx; InvalidateRect(hwnd,NULL,FALSE); return 0;
            }

            /* switch button */
            int list_y=HDR_H+22;
            int sbx=rx+cw-86;
            int btn_h=28, btn_w=78;
            int sby=list_y+(*p_sel-g_first_vis)*ITEM_H+(ITEM_H-btn_h)/2;
            if (mx>=sbx&&mx<=sbx+btn_w&&my>=sby&&my<=sby+btn_h&&*p_sel>=0) {
                if (g_nav==NAV_JDK)      { do_switch(); return 0; }
                else if (g_nav==NAV_RUST)   { do_switch_rust(); return 0; }
                else if (g_nav==NAV_NODE)   { do_switch_node(); return 0; }
                else if (g_nav==NAV_PYTHON) { do_switch_python(); return 0; }
                else if (g_nav==NAV_GO)     { do_switch_go(); return 0; }
                else if (g_nav==NAV_MAVEN)  { do_switch_maven(); return 0; }
                else if (g_nav==NAV_GRADLE) { do_switch_gradle(); return 0; }
            }
        }

        /* ── Download panel ── */
        if (g_nav==NAV_DL) {
            int nv=6, vy=88+12+nv*36+16, bw=160, bh=42;
            int bbx=rx+((w-SIDEBAR_W-1-56-bw)/2);
            if (mx>=bbx&&mx<=bbx+bw&&my>=vy&&my<=vy+bh) {
                if (!g_dl_running) {
                    uintptr_t th=_beginthreadex(NULL,0,dl_thread,NULL,0,NULL);
                    if (th) CloseHandle((HANDLE)th);
                    set_status_clr(ACCENT,L"Downloading JDK %d...",g_dl_ver);
                }
                return 0;
            }
        }

        /* ── Settings panel ── */
        if (g_nav==NAV_ST) {
            int cw2=w-SIDEBAR_W-1-56;
            /* Dark mode toggle */
            int dm_y=193;   /* y(HDR_H+1=51)+142 */
            int tog_x=rx+cw2-52, tog_y=dm_y+2, tog_w=44, tog_h=20;
            if (mx>=tog_x&&mx<=tog_x+tog_w&&my>=tog_y&&my<=tog_y+tog_h) {
                g_dark_mode=!g_dark_mode;
                if (g_cfg) { g_cfg->dark_mode=g_dark_mode; config_save(g_cfg,config_path()); }
                InvalidateRect(hwnd,NULL,FALSE);
                return 0;
            }
            /* Backup / Restore buttons */
            int br_y=233;   /* y+182 */
            if (mx>=rx+28&&mx<=rx+28+140&&my>=br_y&&my<=br_y+32)
                { do_config_backup(); return 0; }
            if (mx>=rx+178&&mx<=rx+178+140&&my>=br_y&&my<=br_y+32)
                { do_config_restore(); return 0; }
            /* scan path delete buttons */
            if (g_cfg) {
                int sp_y=301;  /* y+250 */
                for (int i=0;i<g_cfg->scan_paths_count;i++) {
                    int row_y=sp_y+i*32;
                    if (mx>=rx+cw2-28&&mx<=rx+cw2-8&&my>=row_y+5&&my<=row_y+25) {
                        config_remove_scan_path(g_cfg,g_cfg->scan_paths[i]);
                        config_save(g_cfg,config_path());
                        set_status_clr(ACCENT,L"Path removed");
                        InvalidateRect(hwnd,NULL,FALSE); return 0;
                    }
                }
                /* Python scan path delete */
                int pp_y=sp_y+g_cfg->scan_paths_count*32+6;
                for (int i=0;i<g_cfg->scan_paths_python_count;i++) {
                    int row_y=pp_y+i*32;
                    if (mx>=rx+cw2-28&&mx<=rx+cw2-8&&my>=row_y+5&&my<=row_y+25) {
                        for (int j=i;j<g_cfg->scan_paths_python_count-1;j++)
                            g_cfg->scan_paths_python[j]=g_cfg->scan_paths_python[j+1];
                        g_cfg->scan_paths_python_count--;
                        config_save(g_cfg,config_path());
                        set_status_clr(ACCENT,L"Python path removed");
                        InvalidateRect(hwnd,NULL,FALSE); return 0;
                    }
                }
                /* Go scan path delete */
                int gp_y=pp_y+g_cfg->scan_paths_python_count*32+6;
                for (int i=0;i<g_cfg->scan_paths_go_count;i++) {
                    int row_y=gp_y+i*32;
                    if (mx>=rx+cw2-28&&mx<=rx+cw2-8&&my>=row_y+5&&my<=row_y+25) {
                        for (int j=i;j<g_cfg->scan_paths_go_count-1;j++)
                            g_cfg->scan_paths_go[j]=g_cfg->scan_paths_go[j+1];
                        g_cfg->scan_paths_go_count--;
                        config_save(g_cfg,config_path());
                        set_status_clr(ACCENT,L"Go path removed");
                        InvalidateRect(hwnd,NULL,FALSE); return 0;
                    }
                }
            }
            /* Add-path buttons */
            int sp_base_y=301;  /* y+250 */
            int add_y=sp_base_y;
            if (g_cfg) {
                add_y=sp_base_y+g_cfg->scan_paths_count*32;
                add_y+=6; /* gap before Python */
                add_y+=g_cfg->scan_paths_python_count*32;
                add_y+=6; /* gap before Go */
                add_y+=g_cfg->scan_paths_go_count*32;
                add_y+=12;
            }
            if (mx>=rx+28&&mx<=rx+28+130&&my>=add_y&&my<=add_y+28)
                { do_add_path_jdk(); return 0; }
            if (mx>=rx+164&&mx<=rx+164+130&&my>=add_y&&my<=add_y+28)
                { do_add_path_python(); return 0; }
            if (mx>=rx+300&&mx<=rx+300+120&&my>=add_y&&my<=add_y+28)
                { do_add_path_go(); return 0; }
        }
        return 0; }
    case WM_LBUTTONDBLCLK: {
        if (g_nav==NAV_RUST   && g_rust_sel>=0)      do_switch_rust();
        else if (g_nav==NAV_NODE   && g_node_sel>=0)   do_switch_node();
        else if (g_nav==NAV_JDK    && g_sel>=0)        do_switch();
        else if (g_nav==NAV_PYTHON && g_python_sel>=0) do_switch_python();
        else if (g_nav==NAV_GO     && g_go_sel>=0)     do_switch_go();
        else if (g_nav==NAV_MAVEN  && g_maven_sel>=0)  do_switch_maven();
        else if (g_nav==NAV_GRADLE && g_gradle_sel>=0) do_switch_gradle();
        return 0; }
    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lParam), my=GET_Y_LPARAM(lParam);
        if (g_dragging) {
            int ny=my-g_thumb_h/2;
            if (ny<g_sbar_y) ny=g_sbar_y;
            if (ny+g_thumb_h>g_sbar_y+g_sbar_h) ny=g_sbar_y+g_sbar_h-g_thumb_h;
            int total=current_item_count();
            if (total>g_max_vis)
                g_first_vis=(int)((double)(ny-g_sbar_y)*(total-g_max_vis)/(g_sbar_h-g_thumb_h));
            if (g_first_vis<0) g_first_vis=0;
            if (g_first_vis>total-g_max_vis&&total>g_max_vis) g_first_vis=total-g_max_vis;
            InvalidateRect(g_hWnd,NULL,FALSE);
            return 0;
        }
        SetCursor(mx<SIDEBAR_W?LoadCursor(NULL,IDC_HAND):LoadCursor(NULL,IDC_ARROW));
        return 0; }
    case WM_LBUTTONUP: {
        if (g_dragging) { g_dragging=0; ReleaseCapture(); }
        return 0; }
    case WM_DESTROY: {
        free_theme();
        if (g_cfg) config_save(g_cfg,config_path());
        /* remove tray icon */
        NOTIFYICONDATAW nid={0};
        nid.cbSize=sizeof(nid); nid.hWnd=g_hWnd; nid.uID=1;
        Shell_NotifyIconW(NIM_DELETE,&nid);
        PostQuitMessage(0); return 0; }
    case WM_CLOSE: {
        DestroyWindow(hwnd); return 0; }
    }
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

/* ================================================================
   Entry
   ================================================================ */
int gui_run(Config *cfg) {
    g_cfg=cfg;

    WNDCLASSW wc={0};
    wc.lpfnWndProc=wnd_proc;
    wc.hInstance=g_hInst;
    wc.lpszClassName=APP_CLASS;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.style=CS_HREDRAW|CS_VREDRAW;
    if (!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return -1;

    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    DWORD style=WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX;
    g_hWnd=CreateWindowW(APP_CLASS,APP_TITLE,style,(sw-WND_W)/2,(sh-WND_H)/2,WND_W,WND_H,
                         NULL,NULL,g_hInst,NULL);
    if (!g_hWnd) return -1;
    if (cfg&&cfg->always_on_top)
        SetWindowPos(g_hWnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);

    ShowWindow(g_hWnd,SW_SHOW); UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
