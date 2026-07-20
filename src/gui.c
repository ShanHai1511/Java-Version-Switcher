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
#define WND_W         860
#define WND_H         600
#define SIDEBAR_W     200
#define HDR_H         50
#define STATUS_H      30
#define ITEM_H        50
#define SBAR_W        14          /* scrollbar track width */

/* ================================================================
   亮色主题 — 番茄小说风格
   底色：暖白 #F8F5F0  |  侧边栏：#F0EBE3
   文字：#3D3229（深棕）  |  次文字：#8B7E6B（棕灰）
   强调：#E85A4F（番茄红） |  成功：#5CB85C
   ================================================================ */

#define BG_ROOT      RGB(0xF8, 0xF5, 0xF0)  /* 主底色 暖白 */
#define BG_SIDEBAR   RGB(0xF0, 0xEB, 0xE3)  /* 侧边栏 */
#define BG_CARD      RGB(0xFF, 0xFF, 0xFF)  /* 纯白卡片 */
#define BG_CARD_H    RGB(0xF5, 0xF0, 0xE8)  /* 卡片悬停/选中 */
#define BG_INPUT     RGB(0xF5, 0xF0, 0xE8)  /* 输入框 */
#define BG_HDR       RGB(0xFF, 0xFF, 0xFF)  /* 标题栏 */

#define FG1          RGB(0x3D, 0x32, 0x29)  /* 主文字 深棕 */
#define FG2          RGB(0x6B, 0x5E, 0x50)  /* 次文字 中棕 */
#define FG3          RGB(0x8B, 0x7E, 0x6B)  /* 弱文字 棕灰 */

#define ACCENT       RGB(0xE8, 0x5A, 0x4F)  /* 番茄红 */
#define ACCENT2      RGB(0xF4, 0xA2, 0x61)  /* 橙色 */
#define ACCENT_OK    RGB(0x5C, 0xB8, 0x5C)  /* 绿色 */
#define ACCENT_BD    RGB(0x3D, 0x85, 0xA4)  /* 蓝色 */
#define ACCENT_YEL   RGB(0xE9, 0xC4, 0x6A)  /* 金黄 */

#define BORDER       RGB(0xDC, 0xD5, 0xC8)  /* 浅边框 */
#define BORDER_HI    RGB(0xC8, 0xBF, 0xAD)  /* 深边框 */
#define SEL_BG       RGB(0xFD, 0xF4, 0xEC)  /* 选中行底色 */

/* ================================================================
   Globals
   ================================================================ */
HINSTANCE         g_hInst        = NULL;
static HWND       g_hWnd         = NULL;
static Config    *g_cfg          = NULL;
static JDKList   *g_jdks         = NULL;
static int        g_sel          = -1;       /* selected index (g_sel >= first_visible) */
static int        g_first_vis    = 0;        /* first visible item in list */
static int        g_max_vis      = 0;        /* computed each paint */
static wchar_t    g_status[512]  = L"准备就绪";
static COLORREF   g_status_color = FG2;
static int        g_dl_ver       = 17;
static float      g_dl_progress  = 0;
static int        g_dl_running   = 0;        /* download thread active */

static HFONT      g_hFontLg=NULL, g_hFontB=NULL, g_hFont=NULL, g_hFontSm=NULL;

/* ================================================================
   Theme brushes
   ================================================================ */
static void init_theme(void) { }
static void free_theme(void) { }

/* ================================================================
   Fonts
   ================================================================ */
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
/* tracks first visible item; callers update g_first_vis then call
   fix_scroll() to clamp it back into range */
static void fix_scroll(void) {
    if (!g_jdks) { g_first_vis=0; return; }
    if (g_first_vis < 0) g_first_vis = 0;
    /* g_max_vis is set by the paint caller before fix_scroll */
    int total = g_jdks->count;
    if (total <= g_max_vis) g_first_vis = 0;
    else if (g_first_vis > total - g_max_vis) g_first_vis = total - g_max_vis;
}
static void scroll_by(int delta) {
    g_first_vis += delta; fix_scroll(); InvalidateRect(g_hWnd,NULL,FALSE); }

/* ================================================================
   Sidebar
   ================================================================ */
typedef enum { NAV_JDK=0, NAV_DL, NAV_ST, NAV_CNT } NavId;
static NavId g_nav = NAV_JDK;

static void paint_sidebar(HDC hdc, int w, int h) {
    fill(hdc,0,0,w,h,BG_ROOT);
    /* 顶部番茄红线 */
    fill(hdc,0,0,w,4,ACCENT);

    /* Logo */
    int cx=SIDEBAR_W/2;
    rrect(hdc,cx-20,14,40,40,20,ACCENT,ACCENT);
    txt_c(hdc,L"J",cx-20,14,40,40,BG_CARD,g_hFontB);
    txt_c(hdc,L"JVS",0,60,w,22,FG1,g_hFontB);
    txt_c(hdc,L"Java Version Switcher",0,82,w,15,FG3,g_hFontSm);

    fill(hdc,10,106,w-20,1,BORDER);

    const wchar_t *labels[]={L"打包类",L"下载",L"设置"};
    int ny[]={118,162,206};
    for (int i=0;i<NAV_CNT;i++) {
        int sel=(g_nav==(NavId)i);
        if (sel) { fill(hdc,10,ny[i]-3,4,24,ACCENT); fill(hdc,12,ny[i]-3,w-24,26,BG_CARD_H); }
        txt_l(hdc,labels[i],26,ny[i],w-36,22,
              sel?FG1:FG3, sel?g_hFontB:g_hFont);
    }

    fill(hdc,10,248,w-20,1,BORDER);
    txt_l(hdc,L"当前活跃",14,260,w-28,15,FG3,g_hFontSm);

    int found=-1;
    if (g_jdks) for (int i=0;i<g_jdks->count;i++)
        if (g_jdks->items[i]->is_current){found=i;break;}

    if (found>=0) {
        JDKInfo *j=g_jdks->items[found];
        wchar_t v[64]; _snwprintf(v,_countof(v),L"JDK %S",j->version);
        txt_l(hdc,v,14,276,w-28,22,ACCENT,g_hFontB);
        if (j->vendor[0])
            txt_l(hdc,mb2w(j->vendor),14,298,w-28,15,FG3,g_hFontSm);
        wchar_t pd[MAX_PATH];
        if (wcslen(mb2w(j->path))>26)
            _snwprintf(pd,_countof(pd),L"...%S",j->path+(int)strlen(j->path)-23);
        else
            mbstowcs_s(NULL,pd,_countof(pd),j->path,_TRUNCATE);
        txt_l(hdc,pd,14,314,w-28,15,FG3,g_hFontSm);
    } else {
        txt_l(hdc,L"暂无活跃 JDK",14,276,w-28,22,FG3,g_hFont);
    }
    if (g_jdks && g_jdks->count>0) {
        wchar_t cnt[32]; _snwprintf(cnt,_countof(cnt),L"%d 个安装",g_jdks->count);
        txt_l(hdc,cnt,14,h-50,w-28,15,FG3,g_hFontSm);
    }
}

/* ================================================================
   JDK List panel — with scroll support
   ================================================================ */
 static void paint_jdk_list(HDC hdc, int x, int y, int w, int h) {
     fill(hdc,x,y,w,h,BG_ROOT);
    int cx=x+20, cw=w-40;
    int avail_w=cw-SBAR_W;

    txt_l(hdc,L"已安装的 JDK",cx,y+10,avail_w,26,FG1,g_hFontLg);

    int bw=90, bh=30;
    int bx=cx+avail_w-bw;
    rrect(hdc,bx,y+10,bw,bh,6,ACCENT,ACCENT);
    txt_c(hdc,L"刷新",bx,y+10,bw,bh,BG_CARD,g_hFont);

    int hy=y+44;
    fill(hdc,cx,hy,avail_w,22,BG_SIDEBAR);
    txt_l(hdc,L"版本",  cx+8,  hy, 80,  22,FG3,g_hFontSm);
    txt_l(hdc,L"厂商",  cx+92, hy, 120, 22,FG3,g_hFontSm);
    txt_l(hdc,L"路径",  cx+216, hy,avail_w-340,22,FG3,g_hFontSm);
    txt_r(hdc,L"操作",  cx+avail_w-100,hy,100,22,FG3,g_hFontSm);

    int list_y=hy+22;
    int list_h=h-(list_y-y)-STATUS_H;

    /* compute visible count */
    g_max_vis = list_h / ITEM_H;
    if (g_max_vis<1) g_max_vis=1;
    fix_scroll();

    if (!g_jdks || g_jdks->count==0) {
        int ey=y+(h-50)/2;
        txt_c(hdc,L"暂无 JDK",cx,ey,avail_w,24,FG3,g_hFontB);
        txt_c(hdc,L"点击「刷新」或使用「添加」",
              cx,ey+26,avail_w,18,FG3,g_hFontSm);
        fill(hdc,cx+avail_w,list_y,SBAR_W,list_h,RGB(0xE8,0xE0,0xD4));
        return;
    }

    /* ── JDK items ── */
    for (int i=g_first_vis; i<g_jdks->count && i<g_first_vis+g_max_vis; i++) {
        int ry=list_y+(i-g_first_vis)*ITEM_H;
        JDKInfo *j=g_jdks->items[i];
        int sel=(i==g_sel);

        int bg = j->is_current ? BG_CARD : BG_ROOT;
        if (sel) bg = SEL_BG;
        fill(hdc,cx,ry,avail_w,ITEM_H,
              sel?SEL_BG:(j->is_current?BG_CARD:BG_ROOT));

        COLORREF ac=j->is_current?ACCENT_OK:(sel?ACCENT:BORDER);
        fill(hdc,cx,ry,3,ITEM_H,ac);
        fill(hdc,cx,ry+ITEM_H-1,avail_w,1,BORDER);

        wchar_t ver[64]; _snwprintf(ver,_countof(ver),L"JDK %S",j->version);
        txt_l(hdc,ver,cx+12,ry+4,80,ITEM_H-8,
              j->is_current?FG1:FG2, j->is_current?g_hFontB:g_hFont);

        /* vendor badge */
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
            if (cx+92+vw < cx+avail_w-120) {
                rrect(hdc,cx+92,ry+12,vw,26,4,vb,vb);
                txt_c(hdc,mb2w(j->vendor),cx+92,ry+12,vw,26,vf,g_hFontSm);
            }
        }

        /* version tag */
        if (j->tag && j->tag[0]) {
            COLORREF tb=BG_CARD_H, tf=FG3;
            if      (strstr(j->tag,"1.12")) { tb=RGB(0xFD,0xF6,0xE7); tf=RGB(0x8B,0x67,0x1B); }
            else if (strstr(j->tag,"1.20")) { tb=RGB(0xE8,0xF5,0xE9); tf=RGB(0x2E,0x7D,0x32); }
            else if (strstr(j->tag,"1.21")) { tb=RGB(0xE3,0xF2,0xFD); tf=RGB(0x1A,0x5C,0xA8); }
            SIZE tsz; GetTextExtentPoint32W(hdc,mb2w(j->tag),(int)wcslen(mb2w(j->tag)),&tsz);
            int tw=tsz.cx+12, tx=cx+218;
            if (tx+tw < cx+avail_w-80) {
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
            txt_c(hdc,L"切换",sbx,sby,78,28,BG_CARD,g_hFont);
        }
    }

    /* ── Scrollbar ── */
    int total = g_jdks->count;
    int need_scroll = total > g_max_vis;
    if (need_scroll) {
        int track_x = cx + cw - SBAR_W + 2;
        fill(hdc,track_x,list_y,SBAR_W-4,list_h,RGB(0xE8,0xE0,0xD4));
        stroke(hdc,track_x,list_y,SBAR_W-4,list_h,BORDER,1);

        int thumb_h = (int)((double)list_h * g_max_vis / total);
        if (thumb_h < 24) thumb_h = 24;
        int thumb_y = list_y + (int)((double)(list_h-thumb_h) * g_first_vis / (total-g_max_vis));
        rrect(hdc,track_x+1,thumb_y,SBAR_W-6,thumb_h,3,RGB(0xC8,0xBF,0xAD),RGB(0xC8,0xBF,0xAD));
    } else {
        /* hidden scroll track area — reserve space so hit-testing is clean */
        fill(hdc,cx+cw-SBAR_W,list_y,SBAR_W,list_h,BG_ROOT);
    }
}

/* ================================================================
   Download panel
   ================================================================ */
 static void paint_download(HDC hdc, int x, int y, int w, int h) {
     fill(hdc,x,y,w,h,BG_ROOT);
     int cx=x+28, cw=w-56;

    txt_l(hdc,L"下载 JDK",cx,y+14,cw,30,FG1,g_hFontLg);
    fill(hdc,cx,y+50,cw,1,BORDER);

    txt_l(hdc,L"选择版本：",cx,y+62,cw,20,FG2,g_hFont);
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
    COLORREF btn_clr = g_dl_running ? RGB(0xAA,0xAA,0xAA) : ACCENT;
    rrect(hdc,bbx,bby,bw,bh,8,btn_clr,btn_clr);
    txt_c(hdc, g_dl_running ? L"下载中..." : L"开始下载",
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
    txt_l(hdc,L"保存路径：~/.jvs/jdk/",cx,ny2+8,cw,18,FG3,g_hFontSm);
    txt_l(hdc,L"镜像：https://repo.huaweicloud.com/java/jdk/",cx,ny2+28,cw,18,FG3,g_hFontSm);
}

/* ── Download thread (simulates progress; replace with real HTTP) ── */
static unsigned __stdcall dl_thread(void *param) {
    (void)param;
    g_dl_running=1; g_dl_progress=0;
    InvalidateRect(g_hWnd,NULL,FALSE);

    /* TODO: replace with real threaded HTTP download
       using WinHTTP or libcurl, writing to ~/.jvs/jdk/  */
    for (int i=0;i<=100;i+=2) {
        Sleep(30);
        g_dl_progress=(float)i/100.0f;
        InvalidateRect(g_hWnd,NULL,FALSE);
    }

    char buf[MAX_PATH];
    ExpandEnvironmentStringsA("%USERPROFILE%\\.jvs\\jdk",buf,sizeof(buf));
    CreateDirectoryA(buf,NULL);
    set_status_clr(ACCENT_OK,L"下载完成，保存至 %S",buf);
    g_dl_running=0; InvalidateRect(g_hWnd,NULL,FALSE);
    return 0;
}

/* ================================================================
   Settings panel — add-path + mirror display
   ================================================================ */
 static void paint_settings(HDC hdc, int x, int y, int w, int h) {
     fill(hdc,x,y,w,h,BG_ROOT);
     int cx=x+28, cw=w-56;

    txt_l(hdc,L"设置",cx,y+14,cw,30,FG1,g_hFontLg);
    fill(hdc,cx,y+52,cw,1,BORDER);

    /* mirror row */
    txt_l(hdc,L"下载镜像",cx,y+64,cw,22,FG2,g_hFont);
    int mr_y=y+88;
    fill(hdc,cx,mr_y,cw,36,BG_CARD);
   stroke(hdc,cx,mr_y,cw,36,BORDER,1);
    const wchar_t *mir=g_cfg?mb2w(g_cfg->mirror):L"https://repo.huaweicloud.com/java/jdk/";
    txt_l(hdc,mir,cx+10,mr_y,cw-20,36,FG1,g_hFont);

    /* extra scan paths */
    txt_l(hdc,L"额外扫描路径",cx,y+140,cw,22,FG2,g_hFont);
    int py=y+166;
    if (g_cfg) {
        for (int i=0;i<g_cfg->scan_paths_count;i++) {
            fill(hdc,cx,py,cw,30,BG_CARD);
            stroke(hdc,cx,py,cw,30,BORDER,1);
            txt_l(hdc,mb2w(g_cfg->scan_paths[i]),cx+10,py,cw-40,30,FG1,g_hFontSm);
            rrect(hdc,cx+cw-28,py+5,20,20,4,ACCENT,ACCENT);
            txt_c(hdc,L"x",cx+cw-28,py+5,20,20,BG_CARD,g_hFontB);
            py+=32;
        }
    }

    /* add-path button */
    rrect(hdc,cx,py+4,130,32,6,BG_CARD,BORDER_HI);
    txt_c(hdc,L"+ 添加路径",cx,py+4,130,32,FG2,g_hFont);
}

/* ================================================================
   Show folder dialog → config_add_scan_path
   ================================================================ */
static void do_add_path(void) {
    BROWSEINFOW bi={0};
    bi.hwndOwner=g_hWnd;
    bi.lpszTitle=L"选择要扫描的路径";
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
            set_status(L"已添加：%s",mb);
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

    const wchar_t *titles[]={L"打包类",L"下载",L"设置"};
    txt_l(hdc,titles[g_nav],rx+20,0,rw-40,HDR_H,FG1,g_hFontB);

    int cy=HDR_H+1, ch=h-HDR_H-STATUS_H-1;
    switch(g_nav) {
        case NAV_JDK: paint_jdk_list(hdc,rx,cy,rw,ch); break;
        case NAV_DL:  paint_download(hdc,rx,cy,rw,ch);  break;
        case NAV_ST:  paint_settings(hdc,rx,cy,rw,ch);  break;
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
    set_status_clr(ACCENT,L"正在扫描...");
    if (g_jdks) jdk_list_free(g_jdks);
    g_jdks=scan_all(g_cfg?(const char**)g_cfg->scan_paths:NULL,
                    g_cfg?g_cfg->scan_paths_count:0);
    g_sel=-1; g_first_vis=0;
    if (g_jdks && g_jdks->count>0)
        set_status_clr(ACCENT_OK,L"找到 %d 个 JDK",g_jdks->count);
    else
        set_status_clr(FG3,L"未找到 JDK，请刷新");
    InvalidateRect(g_hWnd,NULL,FALSE);
    return 0;
}
static void start_scan(void) {
    uintptr_t th=_beginthreadex(NULL,0,scan_thread,NULL,0,NULL);
    if (th) CloseHandle((HANDLE)th);
}

/* ================================================================
   Switch
   ================================================================ */
static void do_switch(void) {
    if (g_sel<0 || !g_jdks || g_sel>=g_jdks->count)
        { set_status_clr(ACCENT,L"请先选中一个 JDK"); return; }
    JDKInfo *j=g_jdks->items[g_sel];
    if (j->is_current)
        { set_status_clr(FG3,L"已是当前版本"); return; }

    set_status_clr(ACCENT,L"正在请求管理员...");
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL,exe,MAX_PATH);
    wchar_t args[MAX_PATH*2];
    swprintf_s(args,MAX_PATH*2,L"--switch \"%S\"",j->path);
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"runas"; sei.lpFile=exe; sei.lpParameters=args; sei.nShow=SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
        { set_status_clr(ACCENT,L"UAC 被拒绝"); return; }

    char rp[MAX_PATH];
    snprintf(rp,sizeof(rp),"%s\\jvs_switch_result.json",getenv("TEMP"));
    int ok=0;
    for (int i=0;i<300;i++) { Sleep(200);
        FILE *f=fopen(rp,"r");
        if (f) {
            char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f);
            fclose(f); buf[n]='\0';
            char *s1=strstr(buf,"\"success\""), *s2=strstr(buf,"\"true\"");
            ok=(s1!=NULL) && (s2!=NULL);
            DeleteFileA(rp); break;
        }
    }
    if (ok) {
        set_status_clr(ACCENT_OK,L"切换成功，重新扫描...");
        if (g_jdks) jdk_list_free(g_jdks);
        g_jdks=scan_all(g_cfg?(const char**)g_cfg->scan_paths:NULL,g_cfg?g_cfg->scan_paths_count:0);
        g_sel=-1; g_first_vis=0; start_scan();
    } else {
        set_status_clr(ACCENT,L"切换失败");
    }
    InvalidateRect(g_hWnd,NULL,FALSE);
}

/* ================================================================
   Hit-testing: list item at (mx,my), returns index or -1
   ================================================================ */
static int hit_list_item(int mx, int my, int *out_idx) {
    if (mx<SIDEBAR_W+1) return 0;
    int list_y=HDR_H+22;
    int local=my-list_y;
    if (local<0 || local>=g_max_vis*ITEM_H) return 0;
    int row=g_first_vis + local/ITEM_H;
    if (row<0 || !g_jdks || row>=g_jdks->count) return 0;
    *out_idx=row; return 1;
}

/* ── scrollbar hit-test ── */
static int hit_scrollbar(int mx, int my, int *thumb_drag) {
    (void)mx; (void)my; (void)thumb_drag;
    if (!g_jdks || g_jdks->count<=g_max_vis) return 0;
    return 0; /* drag handled in WM_MOUSEMOVE / WM_LBUTTONDOWN sequence */
}

/* store scrollbar track X so hit-testing works without full rect */
static int g_sbar_x=0, g_sbar_y=0, g_sbar_h=0, g_thumb_y=0, g_thumb_h=0, g_dragging=0;

static void update_scrollbar_geom(void) {
    if (!g_jdks || g_jdks->count<=g_max_vis) { g_sbar_h=0; return; }
    int list_y=HDR_H+22;
    int rx=SIDEBAR_W+1, w=GetSystemMetrics(SM_CXSCREEN)-rx-1;
    int cw=w-40;
    RECT rc; GetClientRect(g_hWnd,&rc);
    int list_h=rc.bottom - list_y - STATUS_H;
    g_sbar_x=rx+cw-SBAR_W+2; g_sbar_y=list_y; g_sbar_h=list_h;
    int thumb_h=(int)((double)list_h*g_max_vis/(g_jdks->count));
    if (thumb_h<24) thumb_h=24;
    g_thumb_h=thumb_h;
    g_thumb_y=g_sbar_y+(int)((double)(list_h-thumb_h)*g_first_vis/(g_jdks->count-g_max_vis));
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
        /* update scrollbar geometry after paint */
        if (g_nav==NAV_JDK) { update_scrollbar_geom(); }
        return 0; }
    case WM_SIZE: {
        if (g_nav==NAV_JDK) { update_scrollbar_geom(); InvalidateRect(hwnd,NULL,FALSE); }
        return 0; }
    case WM_MOUSEWHEEL: {
        if (g_nav==NAV_JDK) {
            int delta=GET_WHEEL_DELTA_WPARAM(wParam);
            scroll_by(-delta/WHEEL_DELTA);
        }
        return 0; }
    case WM_LBUTTONDOWN: {
        int mx=GET_X_LPARAM(lParam), my=GET_Y_LPARAM(lParam);

        if (mx<SIDEBAR_W) {
            /* sidebar nav */
            int ny[]={118,162,206};
            for (int i=0;i<NAV_CNT;i++)
                if (my>=ny[i]-4 && my<=ny[i]+24)
                    { g_nav=(NavId)i; g_first_vis=0; InvalidateRect(hwnd,NULL,FALSE); }
            return 0;
        }

        int rx=SIDEBAR_W+1;
        int cx=rx+20, cw=GetSystemMetrics(SM_CXSCREEN)-rx-1-40-SBAR_W;

        /* scrollbar drag */
        if (g_nav==NAV_JDK && g_sbar_h>0 &&
            mx>=g_sbar_x && mx<=g_sbar_x+SBAR_W-4 &&
            my>=g_thumb_y && my<=g_thumb_y+g_thumb_h) {
            g_dragging=1; SetCapture(hwnd); return 0;
        }

        if (g_nav==NAV_JDK) {
            /* refresh button */
            int bw=90, bh=30;
            int bx=cx+cw-bw;
            if (mx>=bx && mx<=bx+bw && my>=12 && my<=12+bh)
                { start_scan(); return 0; }

            /* list item hit-test */
            int idx=-1;
            if (hit_list_item(mx,my,&idx)) {
                g_sel=idx; InvalidateRect(hwnd,NULL,FALSE); return 0;
            }

            /* switch button */
            if (g_sel>=0) {
                int list_y=HDR_H+22;
                int sbx=rx+cw-86;
                int sby=list_y+(g_sel-g_first_vis)*ITEM_H+(ITEM_H-28)/2;
                if (mx>=sbx && mx<=sbx+78 && my>=sby && my<=sby+28)
                    { do_switch(); return 0; }
            }
        }

        if (g_nav==NAV_DL) {
            int nv=6, vy=88+12+nv*36+16, bw=160, bh=42;
            int bbx=rx+(GetSystemMetrics(SM_CXSCREEN)-rx-1-56-bw)/2;
            if (mx>=bbx && mx<=bbx+bw && my>=vy && my<=vy+bh) {
                if (!g_dl_running) {
                    uintptr_t th=_beginthreadex(NULL,0,dl_thread,NULL,0,NULL);
                    if (th) CloseHandle((HANDLE)th);
                    set_status_clr(ACCENT,L"开始下载 JDK %d...",g_dl_ver);
                }
                return 0;
            }
        }

        if (g_nav==NAV_ST) {
            /* + 添加路径 button: panel_top(HDR_H+1=51) + 170 = 221 in client coords */
            int py=221;
            if (mx>=rx+28 && mx<=rx+28+130 && my>=py && my<=py+32) {
                do_add_path(); return 0;
            }
            /* delete scan path: x = right-28 .. right-8, row height 30+2 */
            if (g_cfg) {
                for (int i=0;i<g_cfg->scan_paths_count;i++) {
                    int row_y=166+2+i*32;
                    if (mx>=rx+cw-28 && mx<=rx+cw-8 && my>=row_y+5 && my<=row_y+25) {
                        config_remove_scan_path(g_cfg,g_cfg->scan_paths[i]);
                        config_save(g_cfg,config_path());
                        set_status_clr(ACCENT,L"已删除路径");
                        InvalidateRect(hwnd,NULL,FALSE); return 0;
                    }
                }
            }
        }
        return 0; }
    case WM_LBUTTONDBLCLK: {
        if (g_nav==NAV_JDK && g_sel>=0) do_switch();
        return 0; }
    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lParam), my=GET_Y_LPARAM(lParam);
        if (g_dragging) {
            int ny=my-g_thumb_h/2;
            if (ny<g_sbar_y) ny=g_sbar_y;
            if (ny+g_thumb_h>g_sbar_y+g_sbar_h) ny=g_sbar_y+g_sbar_h-g_thumb_h;
            int new_off=(int)((double)(ny-g_sbar_y)*(g_jdks->count-g_max_vis)/(g_sbar_h-g_thumb_h));
            if (new_off!=g_first_vis) { g_first_vis=new_off; InvalidateRect(hwnd,NULL,FALSE); }
            return 0;
        }
        /* hand cursor on sidebar */
        SetCursor(mx<SIDEBAR_W ? LoadCursor(NULL,IDC_HAND) : LoadCursor(NULL,IDC_ARROW));
        return 0; }
    case WM_LBUTTONUP: {
        if (g_dragging) { g_dragging=0; ReleaseCapture(); }
        return 0; }
    case WM_DESTROY: {
        free_theme();
        if (g_cfg) config_save(g_cfg,config_path());
        PostQuitMessage(0); return 0; }
    case WM_CLOSE: { DestroyWindow(hwnd); return 0; }
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
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return -1;

    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    DWORD style=WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX;
    g_hWnd=CreateWindowW(APP_CLASS,APP_TITLE,style,(sw-WND_W)/2,(sh-WND_H)/2,WND_W,WND_H,
                         NULL,NULL,g_hInst,NULL);
    if (!g_hWnd) return -1;
    if (cfg && cfg->always_on_top)
        SetWindowPos(g_hWnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);

    ShowWindow(g_hWnd,SW_SHOW); UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
