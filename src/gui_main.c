#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "app_settings.h"
#include "cleaner.h"
#include "gui_booster.h"
#include "gui_cleaner.h"
#include "gui_cleanup.h"
#include "gui_drives.h"
#include "gui_monitor.h"
#include "gui_nav.h"
#include "gui_quarantine.h"
#include "gui_scan.h"
#include "gui_settings.h"
#include "gui_theme.h"
#include "gui_treemap.h"
#include "nav_model.h"
#include "quarantine.h"
#include "scan_history.h"
#include "ui_lang.h"

/*
 * 메인 창: 왼쪽 사이드바(gui_nav) + 페이지 제목 + 페이지 패널. 이 파일은 창 골격·배치·페이지 전환과
 * 자체 검사 진행만 맡고, 각 화면의 동작은 패널 모듈(gui_scan·gui_drives·gui_cleanup·gui_cleaner·
 * gui_quarantine·gui_monitor·gui_booster)이 맡는다.
 *
 * 메뉴(목적별, nav_model):
 *   프로젝트 분석  폴더 스캔 · 드라이브
 *   정리           프로젝트 정리 · 시스템 정리
 *   캐시           개발 캐시(시스템 정리 패널을 개발 캐시·가상 디스크 범위로 하나 더 만든 것)
 *   격리·복원      격리 폴더
 *   시스템         모니터 · 부스터
 *   (아래 끝)      설정(테마·표시 언어·스캔 기록)
 *
 * 테마: 시작할 때 저장된 테마(app_settings)를 적용하고, [설정]에서 바꾸면 바로 창 전체에 적용한다.
 * "시스템 설정 따르기"면 Windows 의 앱 모드가 바뀔 때(WM_SETTINGCHANGE "ImmersiveColorSet") 따라간다.
 */

/* 공용 컨트롤 v6(비주얼 스타일)·DPI 인식은 앱 매니페스트(res/gui.manifest, 리소스로 넣음)에서 켠다. */

/* 실행 중임을 알리는 뮤텍스. 설치·제거 프로그램(Inno Setup AppMutex)이 앱이 켜져 있는지 확인하는 데 쓴다.
 * 여러 개 실행을 막지는 않는다. installer/bytesieve.iss 의 AppMutex 와 이름이 같아야 한다. */
#define BYTESIEVE_APP_MUTEX L"ByteSieve.Gui.Running"

/* --self-test 의 전체 제한 시간. */
#define SELF_TEST_TIMEOUT_MS 60000
#define TIMER_SELF_TEST 1
/* --self-test-monitor 가 검증 전에 기다리는 측정 횟수. */
#define MONITOR_SELF_TEST_SAMPLES 4

enum {
    IDC_NAV = 100,
    IDC_TITLE,
    IDC_PAGE_HOST, /* 페이지 패널을 담는 스크롤 자리 */
    /* 페이지 패널: IDC_PAGE_BASE + NavPage */
    IDC_PAGE_BASE = 200
};

#define IDC_SCANPAGE (IDC_PAGE_BASE + NAV_PAGE_SCAN)
#define IDC_DRIVES (IDC_PAGE_BASE + NAV_PAGE_DRIVES)
#define IDC_CLEANUP (IDC_PAGE_BASE + NAV_PAGE_CLEANUP)
#define IDC_CLEANER (IDC_PAGE_BASE + NAV_PAGE_CLEANER)
#define IDC_CACHE (IDC_PAGE_BASE + NAV_PAGE_CACHE)
#define IDC_QUARANTINE (IDC_PAGE_BASE + NAV_PAGE_QUARANTINE)
#define IDC_MONITOR (IDC_PAGE_BASE + NAV_PAGE_MONITOR)
#define IDC_BOOSTER (IDC_PAGE_BASE + NAV_PAGE_BOOSTER)
#define IDC_SETTINGS (IDC_PAGE_BASE + NAV_PAGE_SETTINGS)

typedef enum {
    SELF_TEST_NONE = 0,
    SELF_TEST_SCAN,  /* 스캔 → 화면 검증 → 종료 */
    SELF_TEST_CANCEL, /* 스캔 시작 직후 취소 → 화면 검증 → 종료 */
    SELF_TEST_DRIVES, /* 드라이브 페이지 새로고침 → 화면 검증 → 종료 */
    SELF_TEST_CLEANUP,       /* 프로젝트 정리(정리 후보) 페이지 탐지 → 화면 검증 → 종료 */
    SELF_TEST_CLEANUP_CANCEL, /* 정리 후보 탐지 시작 직후 취소 → 화면 검증 → 종료 */
    SELF_TEST_TRASH, /* 정리 후보 탐지 → 모두 선택 → 휴지통 이동(확인 창 없이) → 화면 검증 → 종료 */
    SELF_TEST_MONITOR, /* 모니터 페이지 측정 몇 회 → 화면·갱신 간격·실패 표시 검증 → 종료 */
    SELF_TEST_QUARANTINE, /* 정리(격리 폴더로) → 격리 폴더 페이지 → 복원(같은 이름 1개는 건너뜀) →
                           * 남은 항목 영구 삭제 → 각 단계 화면 검증 → 종료 */
    SELF_TEST_CHANGES,  /* 스캔 → 직전 스냅숏과 비교한 변화 보기 검증·출력 → 종료 */
    SELF_TEST_NO_AUTOSAVE, /* 처음 설정 출력 → 자동 저장 끔 → 스캔 → 저장하지 않았는지 검증 → 종료 */
    SELF_TEST_CLEAR_HISTORY, /* 스캔 → 기록 삭제(확인 창 없이) → 화면 반영 검증·출력 → 종료 */
    SELF_TEST_EXPORT, /* 스캔 → CSV·HTML 내보내기(대화 상자 없이) → 화면 검증·출력 → 종료 */
    SELF_TEST_PRECISE, /* [정밀 스캔] 체크 → 스캔 → 하드 링크 보정 열·보기·분석 줄 검증·출력 → 종료 */
    SELF_TEST_LANG,   /* 지정한 언어로 창을 만들어 번역 문구·글꼴·최소 창 크기의 배치 검증 → 종료 */
    SELF_TEST_CLEANER, /* 시스템 정리 분석·검증 → 정리 → 개발 캐시 분석·검증 → 정리 → 결과 검증 → 종료 */
    SELF_TEST_BOOSTER, /* 부스터 페이지 목록·CPU 측정 → 화면·보호 판정 검증(아무것도 바꾸지 않음) → 종료 */
    SELF_TEST_NAV,    /* 사이드바 키보드·마우스 이동 → 페이지 전환·제목·처음 열 때 조회 횟수 검증 → 종료 */
    SELF_TEST_THEME,  /* 테마 밝게 → 어둡게 → 시스템(어둡게·밝게) → 밝게 고정, 단계마다 컨트롤 색 검증 → 종료 */
    SELF_TEST_SETTINGS, /* [설정]에서 언어 바꾸기(저장 값·즉시 적용)·자동 저장 끄고 켜기 → 저장 값·두 페이지 검증 → 종료 */
    SELF_TEST_LANG_SWITCH, /* 스캔(한국어) → 언어를 en·ja·zh-Hans·ko 로 차례로 바꾸며 결과·상태 줄·문구 다시 쓰기 검증 → 종료 */
    SELF_TEST_DPI_CHANGE, /* 실행 중 DPI 변경(WM_DPICHANGED) 96→144→192→120→96 마다 글꼴·배치·입력값·화면 상태 검증 → 종료 */
    SELF_TEST_LAYOUT /* 네 언어 × 여러 창 크기에서 모든 페이지의 문구 잘림·겹침·패널 밖 넘침·입력값 유지 검증 → 종료 */
} SelfTest;

typedef struct {
    HWND hwnd;
    HWND nav;
    HWND title;
    HWND host; /* 페이지 패널의 부모(스크롤 자리). 패널 알림은 메인 창으로 넘긴다 */
    HWND pages[NAV_PAGE_COUNT];
    int scroll[NAV_PAGE_COUNT][2]; /* 페이지별 가로·세로 스크롤 위치(자리가 페이지 최소 크기보다 작을 때) */
    NavPage page; /* 보이는 페이지 */
    HFONT font;       /* 지금 언어의 UI 글꼴(사이드바·폴더 스캔·설정) */
    HFONT base_font;  /* 시작할 때 만든 글꼴(언어를 바꿔도 끝날 때 해제) */
    HFONT title_font;
    int dpi;
    int font_dpi; /* 시스템 글꼴 크기의 기준 DPI(실제 화면 DPI). 배치 검사가 dpi 를 바꾸면 글꼴을 그 비율로 키운다 */
    ThemeMode theme_mode; /* 지금 테마 설정(시스템·밝게·어둡게) */

    /* 페이지를 열 때 시작한 조회(처음 열 때 조회·열 때마다 다시 읽기) 횟수. */
    unsigned loads[NAV_PAGE_COUNT];
    int dry_load; /* SELF_TEST_NAV: 조회를 세기만 하고 실제로 시작하지 않음(사용자 시스템을 읽지 않음) */

    SelfTest self_test;
    int self_test_exit;
    size_t trash_expect; /* SELF_TEST_TRASH·QUARANTINE: 선택해 옮기기 시작한 항목 수 */
    const wchar_t *trash_root; /* SELF_TEST_TRASH: 탐지 경로(드라이브 종류를 알리는 데 씀) */
    const wchar_t *quarantine_root;     /* SELF_TEST_QUARANTINE: 탐지 경로(가짜 볼륨 루트) */
    const wchar_t *quarantine_conflict; /* SELF_TEST_QUARANTINE: 복원 전에 만들 같은 이름 파일 */
    const wchar_t *export_prefix;  /* SELF_TEST_EXPORT: 내보낼 파일 경로(확장자 제외) */
    const wchar_t *cleaner_root;   /* SELF_TEST_CLEANER: 분석한 위치가 모두 이 폴더 안이어야 정리한다 */
    size_t cleaner_expect;         /* SELF_TEST_CLEANER: 골라 정리를 시작한 대상 수 */
    int export_enabled_before_scan; /* SELF_TEST_EXPORT: 스캔 전 내보내기 버튼이 켜져 있었으면 1 */
} App;

static App g_app;

static int scale(int px)
{
    return MulDiv(px, g_app.dpi, 96);
}

/* ---- 레이아웃 ---- */

/* 바깥 여백·제목 줄 높이(96 DPI 기준). */
#define MAIN_MARGIN 12
#define PAGE_HOST_CLASS L"ByteSievePageHost"
#define MAIN_TITLE_H 44

static int nav_width(void)
{
    return g_app.nav != NULL ? nav_view_width(g_app.nav) : scale(200);
}

/* 페이지 패널이 잘리지 않는 최소 높이(96 DPI 기준). 자리가 이보다 낮으면 세로로 스크롤한다. */
#define PAGE_MIN_H 300

/* 페이지 패널이 잘리지 않는 최소 높이. [설정]은 줄을 쌓는 배치라 그 높이, 다른 페이지는 PAGE_MIN_H. */
static int page_min_height(int page)
{
    int h = scale(PAGE_MIN_H);

    if (page == NAV_PAGE_SETTINGS) {
        int need = settings_panel_min_height(g_app.pages[page]);
        if (need > h) {
            h = need;
        }
    }
    return h;
}

/* 페이지 패널이 잘리지 않는 최소 폭. 자리가 이보다 좁으면 가로로 스크롤한다. */
static int page_min_width(int page)
{
    HWND h = g_app.pages[page];

    switch (page) {
    case NAV_PAGE_SCAN:
        return scan_panel_min_width(h);
    case NAV_PAGE_DRIVES:
        return drive_panel_min_width(h);
    case NAV_PAGE_CLEANUP:
        return cleanup_panel_min_width(h);
    case NAV_PAGE_CLEANER:
    case NAV_PAGE_CACHE:
        return cleaner_panel_min_width(h);
    case NAV_PAGE_QUARANTINE:
        return quarantine_panel_min_width(h);
    case NAV_PAGE_BOOSTER:
        return booster_panel_min_width(h);
    case NAV_PAGE_SETTINGS:
        return settings_panel_min_width(h);
    default:
        return 0;
    }
}

/*
 * 창이 있는 모니터의 작업 영역(작업 표시줄 제외) 크기. 창을 만들기 전이면 주 모니터.
 * 배치·DPI 자체 검사는 BYTESIEVE_TEST_WORKAREA=<폭>x<높이> 로 작은 화면을 흉내 낸다.
 */
static void work_area_size(int *w, int *h)
{
    MONITORINFO mi;
    HMONITOR mon;
    POINT origin = {0, 0};

    if (g_app.self_test == SELF_TEST_LAYOUT || g_app.self_test == SELF_TEST_DPI_CHANGE) {
        wchar_t env[32];
        DWORD n = GetEnvironmentVariableW(L"BYTESIEVE_TEST_WORKAREA", env, 32);
        if (n > 0 && n < 32 && swscanf_s(env, L"%dx%d", w, h) == 2 && *w > 0 && *h > 0) {
            return;
        }
    }
    mon = g_app.hwnd != NULL ? MonitorFromWindow(g_app.hwnd, MONITOR_DEFAULTTONEAREST)
                             : MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (mon != NULL && GetMonitorInfoW(mon, &mi)) {
        *w = mi.rcWork.right - mi.rcWork.left;
        *h = mi.rcWork.bottom - mi.rcWork.top;
    } else {
        *w = GetSystemMetrics(SM_CXMAXIMIZED);
        *h = GetSystemMetrics(SM_CYMAXIMIZED);
    }
}

/*
 * 최소 창 크기: 기본 800×380(96 DPI 기준)에서, 문구가 긴 언어에서도 폴더 스캔·설정 페이지의 줄이
 * 패널 안에 들어가도록 넓히고(사이드바 폭 + 여백 + 패널 최소 폭), 사이드바 항목이 겹치지 않게 높인다.
 */
static void min_window_size(int *cx, int *cy)
{
    RECT frame = {0, 0, 0, 0};
    int need, panel_w, settings_w;

    *cx = scale(800);
    *cy = scale(380);
    if (g_app.pages[NAV_PAGE_SCAN] == NULL) {
        return; /* 컨트롤을 만들기 전(WM_CREATE 이전) */
    }
    panel_w = scan_panel_min_width(g_app.pages[NAV_PAGE_SCAN]);
    settings_w = settings_panel_min_width(g_app.pages[NAV_PAGE_SETTINGS]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    settings_w = quarantine_panel_min_width(g_app.pages[NAV_PAGE_QUARANTINE]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    settings_w = cleanup_panel_min_width(g_app.pages[NAV_PAGE_CLEANUP]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    settings_w = cleaner_panel_min_width(g_app.pages[NAV_PAGE_CLEANER]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    settings_w = drive_panel_min_width(g_app.pages[NAV_PAGE_DRIVES]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    settings_w = booster_panel_min_width(g_app.pages[NAV_PAGE_BOOSTER]);
    if (settings_w > panel_w) {
        panel_w = settings_w;
    }
    need = nav_width() + 2 * scale(MAIN_MARGIN) + panel_w;
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
    need += frame.right - frame.left;
    if (need > *cx) {
        *cx = need;
    }
    need = nav_view_min_height(g_app.nav) + frame.bottom - frame.top;
    if (need > *cy) {
        *cy = need;
    }
    /* 모니터 작업 영역보다 크게 하지 않는다(예: 1920×1080 화면의 200% 배율). 모자란 자리는 사이드바와
     * 페이지가 스크롤로 보여 준다(page_min_width·PAGE_MIN_H, nav_view 스크롤). */
    {
        int work_w, work_h;
        work_area_size(&work_w, &work_h);
        if (*cx > work_w) {
            *cx = work_w;
        }
        if (*cy > work_h) {
            *cy = work_h;
        }
    }
}

/* 페이지 패널 자리(메인 창 클라이언트 좌표). */
static void page_rect(RECT *out)
{
    RECT rc;
    int m = scale(MAIN_MARGIN);

    GetClientRect(g_app.hwnd, &rc);
    out->left = nav_width() + m;
    out->top = m + scale(MAIN_TITLE_H);
    out->right = rc.right - m;
    out->bottom = rc.bottom - m;
    if (out->right < out->left) {
        out->right = out->left;
    }
    if (out->bottom < out->top) {
        out->bottom = out->top;
    }
}

/*
 * 스크롤 자리 안에 페이지 패널을 놓는다. 자리가 페이지의 최소 크기(page_min_width·PAGE_MIN_H)보다 작으면
 * 패널을 최소 크기로 두고 스크롤 막대로 옮겨 보게 한다(맨 위·왼쪽의 주요 단추는 스크롤 0 에서 보임).
 * 페이지마다 스크롤 위치를 기억한다. 자리가 넉넉하면 막대 없이 자리를 채운다(예전과 같음).
 */
static void host_layout(void)
{
    static int busy;
    RECT wr;
    int W, H, i, sbw, sbh;

    if (g_app.host == NULL || busy) {
        return;
    }
    busy = 1; /* 막대를 보이고 숨기면 WM_SIZE 가 다시 온다 */
    GetWindowRect(g_app.host, &wr);
    W = wr.right - wr.left;
    H = wr.bottom - wr.top;
    sbw = GetSystemMetricsForDpi(SM_CXVSCROLL, (UINT)g_app.dpi);
    sbh = GetSystemMetricsForDpi(SM_CYHSCROLL, (UINT)g_app.dpi);
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        int need_w = page_min_width(i), need_h = page_min_height(i), hs = 0, vs = 0, k, cw, ch, pw, ph;
        int *pos = g_app.scroll[i];
        if (g_app.pages[i] == NULL) {
            continue;
        }
        for (k = 0; k < 2; k++) {
            hs = need_w > W - (vs ? sbw : 0);
            vs = need_h > H - (hs ? sbh : 0);
        }
        cw = W - (vs ? sbw : 0);
        ch = H - (hs ? sbh : 0);
        pw = need_w > cw ? need_w : cw;
        ph = need_h > ch ? need_h : ch;
        if (pos[0] > pw - cw) {
            pos[0] = pw - cw;
        }
        if (pos[1] > ph - ch) {
            pos[1] = ph - ch;
        }
        if (pos[0] < 0) {
            pos[0] = 0;
        }
        if (pos[1] < 0) {
            pos[1] = 0;
        }
        MoveWindow(g_app.pages[i], -pos[0], -pos[1], pw > 0 ? pw : 0, ph > 0 ? ph : 0, TRUE);
        if (i == (int)g_app.page) {
            SCROLLINFO si;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMax = hs ? pw - 1 : 0;
            si.nPage = hs ? (UINT)cw : 0;
            si.nPos = pos[0];
            SetScrollInfo(g_app.host, SB_HORZ, &si, TRUE);
            si.nMax = vs ? ph - 1 : 0;
            si.nPage = vs ? (UINT)ch : 0;
            si.nPos = pos[1];
            SetScrollInfo(g_app.host, SB_VERT, &si, TRUE);
        }
    }
    busy = 0;
}

static void host_scroll(int bar, int code)
{
    SCROLLINFO si;
    int pos, line = scale(32);

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(g_app.host, bar, &si) || si.nPage == 0) {
        return;
    }
    pos = si.nPos;
    switch (code) {
    case SB_LINEUP:
        pos -= line;
        break;
    case SB_LINEDOWN:
        pos += line;
        break;
    case SB_PAGEUP:
        pos -= (int)si.nPage;
        break;
    case SB_PAGEDOWN:
        pos += (int)si.nPage;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        pos = si.nTrackPos;
        break;
    case SB_TOP:
        pos = 0;
        break;
    case SB_BOTTOM:
        pos = si.nMax;
        break;
    default:
        return;
    }
    g_app.scroll[g_app.page][bar == SB_HORZ ? 0 : 1] = pos;
    host_layout();
}

/* 스크롤 자리: 패널이 부모(GetParent)에게 보내는 알림을 메인 창으로 넘기고, 막대·휠로 페이지를 옮긴다. */
static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    case WM_HSCROLL:
        if (lp == 0) {
            host_scroll(SB_HORZ, LOWORD(wp));
        }
        return 0;
    case WM_VSCROLL:
        if (lp == 0) {
            host_scroll(SB_VERT, LOWORD(wp));
        }
        return 0;
    case WM_MOUSEWHEEL:
        /* 패널 안의 목록 등이 쓰지 않은 휠만 올라온다. 세로 막대가 있을 때만 옮긴다. */
        if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL) {
            g_app.scroll[g_app.page][1] -= MulDiv(GET_WHEEL_DELTA_WPARAM(wp), scale(48), WHEEL_DELTA);
            host_layout();
        }
        return 0;
    case WM_SIZE:
        host_layout();
        return 0;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, theme_bg_brush());
        return 1;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void layout(void)
{
    RECT rc, page;
    int m = scale(MAIN_MARGIN), nw = nav_width();

    GetClientRect(g_app.hwnd, &rc);
    MoveWindow(g_app.nav, 0, 0, nw, rc.bottom, TRUE);
    if (nav_width() != nw) {
        /* 사이드바가 낮아 스크롤 막대가 생기거나 없어졌으면 그 폭을 반영한다. */
        MoveWindow(g_app.nav, 0, 0, nav_width(), rc.bottom, TRUE);
    }
    page_rect(&page);
    MoveWindow(g_app.title, page.left, m, page.right - page.left, scale(MAIN_TITLE_H) - scale(4),
               TRUE);
    MoveWindow(g_app.host, page.left, page.top, page.right - page.left, page.bottom - page.top, TRUE);
    host_layout(); /* 자리 크기가 같아도 페이지 최소 크기(언어·DPI)가 바뀌었을 수 있다 */
}

/* ---- 페이지 전환 ---- */

/*
 * 페이지를 열 때 하는 조회. 드라이브·시스템 정리·개발 캐시는 처음 열 때 한 번, 격리 폴더는 열 때마다
 * 목록을 다시 읽고, 모니터·부스터는 측정을 시작한다(이미 측정 중이면 그대로, 페이지를 옮겨도 계속).
 */
static void load_on_open(NavPage page)
{
    HWND h = g_app.pages[page];
    int start = 0;

    switch (page) {
    case NAV_PAGE_DRIVES:
        start = g_app.loads[page] == 0 && !drive_panel_loaded(h);
        break;
    case NAV_PAGE_CLEANER:
    case NAV_PAGE_CACHE:
        start = g_app.loads[page] == 0 && !cleaner_panel_loaded(h);
        break;
    case NAV_PAGE_QUARANTINE:
    case NAV_PAGE_MONITOR:
    case NAV_PAGE_BOOSTER:
        start = 1;
        break;
    default:
        break;
    }
    if (!start) {
        return;
    }
    g_app.loads[page]++;
    if (g_app.dry_load) {
        return;
    }
    switch (page) {
    case NAV_PAGE_DRIVES:
        drive_panel_refresh(h);
        break;
    case NAV_PAGE_CLEANER:
    case NAV_PAGE_CACHE:
        cleaner_panel_refresh(h);
        break;
    case NAV_PAGE_QUARANTINE:
        quarantine_panel_refresh(h);
        break;
    case NAV_PAGE_MONITOR:
        monitor_panel_start(h);
        break;
    case NAV_PAGE_BOOSTER:
        booster_panel_start(h);
        break;
    default:
        break;
    }
}

/* 고른 페이지의 패널만 보이게 하고 제목·사이드바 선택을 맞춘 뒤, 열 때 하는 조회를 한다. */
static void show_page(NavPage page)
{
    int i;

    if ((unsigned)page >= (unsigned)NAV_PAGE_COUNT) {
        return;
    }
    g_app.page = page;
    nav_view_select(g_app.nav, page, 0);
    SetWindowTextW(g_app.title, ui_text(nav_page_text(page)));
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        ShowWindow(g_app.pages[i], i == (int)page ? SW_SHOW : SW_HIDE);
    }
    host_layout(); /* 이 페이지의 스크롤 막대·위치 */
    if (page == NAV_PAGE_SETTINGS) {
        /* 폴더 스캔 페이지에서 바꾼 자동 저장 상태·고대비 여부를 다시 보인다(읽기만 함). */
        settings_panel_sync(g_app.pages[NAV_PAGE_SETTINGS],
                            scan_panel_autosave(g_app.pages[NAV_PAGE_SCAN]), 1);
    }
    load_on_open(page);
}

/* ---- 테마 ---- */

/* 지금 테마 설정을 실제 모드(시스템 앱 모드·고대비 반영)로 바꿔 창 전체에 적용한다. */
static void apply_theme(void)
{
    theme_set_dark(settings_theme_is_dark(g_app.theme_mode, settings_system_dark(),
                                          settings_high_contrast()));
    theme_apply_window(g_app.hwnd);
}

/* ---- 표시 언어 ---- */

/* 언어에 맞는 UI 글꼴(한자 모양이 언어마다 달라 글꼴 이름을 고른다). 실패하면 시스템 글꼴, 그것도 실패하면 NULL. */
static HFONT create_ui_font(UiLang lang)
{
    HFONT font = theme_create_ui_font(lang);

    if (font == NULL) {
        NONCLIENTMETRICSW ncm;
        memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        font = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    /* 배치 검사가 DPI 를 바꿨으면(BYTESIEVE_TEST_DPI) 그 배율의 Windows 처럼 글꼴도 같은 비율로 키운다. */
    if (font != NULL && g_app.font_dpi > 0 && g_app.dpi != g_app.font_dpi) {
        LOGFONTW lf;
        if (GetObjectW(font, sizeof(lf), &lf) == sizeof(lf)) {
            HFONT scaled;
            lf.lfHeight = MulDiv(lf.lfHeight, g_app.dpi, g_app.font_dpi);
            scaled = CreateFontIndirectW(&lf);
            if (scaled != NULL) {
                DeleteObject(font);
                font = scaled;
            }
        }
    }
    return font;
}

/*
 * GUI 스레드의 UI 언어를 앱 언어로 맞춘다: Windows 가 그리는 확인 창 단추([예]/[아니요])·공용 대화 상자를
 * 앱 문구와 같은 언어로 보이게 한다. 그 언어의 Windows 리소스가 없으면 Windows 가 기본 언어로 대신한다.
 */
static void apply_thread_ui_language(UiLang lang)
{
    SetThreadUILanguage(ui_lang_langid(lang));
}

/*
 * 표시 언어를 실행 중에 바꾼다: 언어별 글꼴을 새로 만들어 사이드바·제목과 모든 페이지의 문구와 글꼴을
 * 다시 쓰고, 문구 길이에 맞게 최소 창 크기·배치를 다시 계산한다. 같은 언어면 아무것도 하지 않는다.
 * 새 글꼴을 만들지 못하면 언어를 바꾸지 않는다.
 */
/*
 * 지금 언어·DPI 로 글꼴을 다시 만들어 사이드바·제목·모든 페이지에 적용한다(페이지는 문구·단추 폭·열 머리글을
 * 다시 재고 배치한다). fit 이면 최소 창 크기보다 작아지지 않게 넓힌다. 글꼴을 만들지 못하면 0.
 */
static int apply_fonts(UiLang lang, int fit)
{
    HFONT font, title_font, old_font = g_app.font, old_title = g_app.title_font;
    RECT wr;
    int cx, cy;

    font = create_ui_font(lang);
    if (font == NULL) {
        return 0;
    }
    ui_lang_set(lang);
    apply_thread_ui_language(lang);
    title_font = theme_create_title_font(font, g_app.dpi);
    g_app.font = font;
    g_app.title_font = title_font;

    nav_view_set_font(g_app.nav, font);
    SendMessageW(g_app.title, WM_SETFONT, (WPARAM)(title_font != NULL ? title_font : font), FALSE);
    SetWindowTextW(g_app.title, ui_text(nav_page_text(g_app.page)));
    scan_panel_apply_language(g_app.pages[NAV_PAGE_SCAN], font);
    cleanup_panel_apply_language(g_app.pages[NAV_PAGE_CLEANUP], font);
    cleaner_panel_apply_language(g_app.pages[NAV_PAGE_CLEANER], font);
    cleaner_panel_apply_language(g_app.pages[NAV_PAGE_CACHE], font);
    quarantine_panel_apply_language(g_app.pages[NAV_PAGE_QUARANTINE], font);
    drive_panel_apply_language(g_app.pages[NAV_PAGE_DRIVES], font);
    monitor_panel_apply_language(g_app.pages[NAV_PAGE_MONITOR], font);
    booster_panel_apply_language(g_app.pages[NAV_PAGE_BOOSTER], font);
    settings_panel_apply_language(g_app.pages[NAV_PAGE_SETTINGS], font);
    /* 이전 글꼴은 이제 어느 화면도 쓰지 않는다(시작 글꼴 base_font 는 메인 창이 끝날 때 지운다). */
    if (old_font != NULL && old_font != g_app.base_font) {
        DeleteObject(old_font);
    }
    if (old_title != NULL) {
        DeleteObject(old_title);
    }

    /* 문구가 길어진 언어면 최소 창 크기보다 작아지지 않게 넓힌다(WM_SIZE → layout). */
    GetWindowRect(g_app.hwnd, &wr);
    min_window_size(&cx, &cy);
    if (fit && (wr.right - wr.left < cx || wr.bottom - wr.top < cy)) {
        SetWindowPos(g_app.hwnd, NULL, 0, 0, wr.right - wr.left > cx ? wr.right - wr.left : cx,
                     wr.bottom - wr.top > cy ? wr.bottom - wr.top : cy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    layout();
    InvalidateRect(g_app.hwnd, NULL, TRUE);
    return 1;
}

static void apply_language(UiLang lang)
{
    if (lang == ui_lang_current() || (unsigned)lang >= (unsigned)UI_LANG_COUNT) {
        return;
    }
    apply_fonts(lang, 1);
}

/* 목록(ListView) 열 폭을 DPI 비율로 바꾼다(사용자가 바꾼 폭도 비율대로 유지). d[0] 이전, d[1] 새 DPI. */
static BOOL CALLBACK scale_columns_proc(HWND h, LPARAM lp)
{
    const int *d = (const int *)lp;
    wchar_t cls[32];

    GetClassNameW(h, cls, 32);
    if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
        int n = Header_GetItemCount(ListView_GetHeader(h)), i;
        DWORD ex = ListView_GetExtendedListViewStyle(h);
        for (i = 0; i < n; i++) {
            ListView_SetColumnWidth(h, i, MulDiv(ListView_GetColumnWidth(h, i), d[1], d[0]));
        }
        /* 체크 상자 그림은 목록이 만들 때의 DPI 로 그려 둔다. 껐다 켜서 새 DPI 로 다시 만든다(체크 상태는
         * 패널이 갖고 LVN_GETDISPINFO 로 알려 주므로 잃지 않음, 콜백 마스크는 다시 둔다). */
        if (ex & LVS_EX_CHECKBOXES) {
            UINT callback = ListView_GetCallbackMask(h);
            ListView_SetExtendedListViewStyle(h, ex & ~(DWORD)LVS_EX_CHECKBOXES);
            ListView_SetExtendedListViewStyle(h, ex);
            ListView_SetCallbackMask(h, callback);
        }
    }
    return TRUE;
}

/*
 * DPI 가 바뀌었을 때(배율이 다른 모니터로 옮김·실행 중 배율 변경 → WM_DPICHANGED, 처음 뜬 모니터의 DPI 가
 * 시스템 DPI 와 다름) 모든 크기를 새 DPI 로 다시 잡는다. 컨트롤을 다시 만들지 않으므로 입력값·선택·목록
 * 내용·스크롤 위치·고른 페이지는 그대로다. suggested 가 있으면 Windows 가 권한 창 위치·크기로 옮긴다.
 */
static void apply_dpi(int dpi, const RECT *suggested)
{
    int old = g_app.dpi, d[2], i;

    if (dpi > 0 && dpi != old) {
        g_app.dpi = dpi;
        nav_view_set_dpi(g_app.nav, dpi);
        scan_panel_set_dpi(g_app.pages[NAV_PAGE_SCAN], dpi);
        drive_panel_set_dpi(g_app.pages[NAV_PAGE_DRIVES], dpi);
        cleanup_panel_set_dpi(g_app.pages[NAV_PAGE_CLEANUP], dpi);
        cleaner_panel_set_dpi(g_app.pages[NAV_PAGE_CLEANER], dpi);
        cleaner_panel_set_dpi(g_app.pages[NAV_PAGE_CACHE], dpi);
        quarantine_panel_set_dpi(g_app.pages[NAV_PAGE_QUARANTINE], dpi);
        monitor_panel_set_dpi(g_app.pages[NAV_PAGE_MONITOR], dpi);
        booster_panel_set_dpi(g_app.pages[NAV_PAGE_BOOSTER], dpi);
        settings_panel_set_dpi(g_app.pages[NAV_PAGE_SETTINGS], dpi);
        d[0] = old;
        d[1] = dpi;
        for (i = 0; i < NAV_PAGE_COUNT; i++) {
            if (g_app.pages[i] != NULL) {
                EnumChildWindows(g_app.pages[i], scale_columns_proc, (LPARAM)d);
            }
            g_app.scroll[i][0] = MulDiv(g_app.scroll[i][0], dpi, old);
            g_app.scroll[i][1] = MulDiv(g_app.scroll[i][1], dpi, old);
        }
        apply_fonts(ui_lang_current(), suggested == NULL);
    }
    if (suggested != NULL) {
        SetWindowPos(g_app.hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    layout();
    InvalidateRect(g_app.hwnd, NULL, TRUE);
}

/* [설정] 패널 알림: 테마를 바꿨으면 적용하고, 자동 저장 요청은 폴더 스캔 패널과 같은 경로로 저장한다. */
static void on_settings_notify(int code)
{
    HWND settings = g_app.pages[NAV_PAGE_SETTINGS], scan = g_app.pages[NAV_PAGE_SCAN];

    if (code == STN_THEME_CHANGED) {
        g_app.theme_mode = settings_panel_theme(settings);
        apply_theme();
    } else if (code == STN_LANGUAGE_CHANGED) {
        /* 환경 변수 BYTESIEVE_LANG 이 있으면 그 언어가 먼저다(설정 패널이 안내를 보임). */
        UiLang lang;
        if (!settings_lang_env_override(&lang)) {
            lang = settings_panel_language(settings);
        }
        apply_language(lang);
    } else if (code == STN_AUTOSAVE) {
        int ok = scan_panel_set_autosave(scan, settings_panel_autosave_request(settings));
        settings_panel_sync(settings, scan_panel_autosave(scan), ok);
    }
}

/* ---- 자체 검사(--self-test) ---- */

#define SELF_CHECK(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            wchar_t msg_[256];                                               \
            swprintf(msg_, 256, L"self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                              \
            OutputDebugStringW(msg_);                                        \
            fwprintf(stderr, L"%ls", msg_);                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* 창 자체의 표시 상태(부모가 숨겨져 있어도 판단할 수 있게 스타일을 본다). */
static int is_shown(HWND h)
{
    return (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

/* 컨트롤 글자가 expect 와 같은지. */
static int text_is(HWND h, const wchar_t *expect)
{
    wchar_t buf[256];
    GetWindowTextW(h, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return wcscmp(buf, expect) == 0;
}

/* page 가 보이는 페이지이고, 그 패널만 보이며, 제목·사이드바 선택이 맞는지. 실패 수. */
static int verify_page_shown(NavPage page)
{
    int failures = 0, i;

    SELF_CHECK(g_app.page == page);
    SELF_CHECK(nav_view_selected(g_app.nav) == page);
    SELF_CHECK(text_is(g_app.title, ui_text(nav_page_text(page))));
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        SELF_CHECK(is_shown(g_app.pages[i]) == (i == (int)page));
    }
    SELF_CHECK(is_shown(g_app.nav) && is_shown(g_app.title));
    return failures;
}

/* 패널 검증 결과(실패 수) + 페이지 표시 상태 → 종료 코드. */
static int page_exit(int failures, NavPage page)
{
    failures += verify_page_shown(page);
    return failures == 0 ? 0 : 1;
}

/* 파일을 만든다(같은 이름이 있는 복원 상황 재현용). */
static int write_conflict_file(const wchar_t *path)
{
    static const char text[] = "conflict";
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = WriteFile(h, text, (DWORD)(sizeof(text) - 1), &written, NULL);
    CloseHandle(h);
    return ok && written == sizeof(text) - 1;
}

/*
 * 실행 중 언어 바꾸기 검사: 표시 언어를 en → ja → zh-Hans → ko 로 바꾸며 단계마다 verify(page)를 부른다
 * (결과 화면이 새 언어로 다시 쓰였는지). 한국어로 돌아와 끝나므로 이어지는 한국어 검사는 그대로다. 실패 수.
 */
static int verify_language_round_trip(int (*verify)(HWND), HWND page)
{
    static const UiLang order[] = {UI_LANG_EN, UI_LANG_JA, UI_LANG_ZH_HANS, UI_LANG_KO};
    int failures = 0;
    size_t i;

    for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        apply_language(order[i]);
        failures += verify(page);
    }
    return failures;
}

/*
 * SELF_TEST_QUARANTINE: 정리가 끝난 뒤 격리 폴더 페이지에서 목록 확인 → 같은 이름 파일을 하나 만든
 * 뒤 모두 복원(그 항목은 건너뜀) → 남은 항목 영구 삭제를 시작한다. 실패하면 0.
 */
static int self_test_quarantine_step(void)
{
    HWND q = g_app.pages[NAV_PAGE_QUARANTINE];
    size_t restored;

    if (cleanup_panel_verify_trash(g_app.pages[NAV_PAGE_CLEANUP], g_app.trash_expect) != 0) {
        return 0;
    }
    show_page(NAV_PAGE_QUARANTINE);
    /* 사용자의 실제 격리 항목은 건드리지 않도록 픽스처만 보이게 한다. */
    quarantine_panel_set_root(q, g_app.quarantine_root);
    if (page_exit(quarantine_panel_verify(q, g_app.trash_expect, g_app.trash_expect),
                  NAV_PAGE_QUARANTINE) != 0) {
        return 0;
    }
    if (!write_conflict_file(g_app.quarantine_conflict)) {
        fwprintf(stderr, L"self-test failed: could not create %ls\n", g_app.quarantine_conflict);
        return 0;
    }
    if (quarantine_panel_select_all(q) != g_app.trash_expect) {
        fwprintf(stderr, L"self-test failed: could not select quarantined items\n");
        return 0;
    }
    restored = quarantine_panel_restore(q, 0);
    if (restored + 1 != g_app.trash_expect || quarantine_panel_conflicts(q) != 1 ||
        page_exit(quarantine_panel_verify(q, 1, 1), NAV_PAGE_QUARANTINE) != 0) {
        fwprintf(stderr, L"self-test failed: restored %u of %u (conflicts %u)\n",
                 (unsigned)restored, (unsigned)g_app.trash_expect,
                 (unsigned)quarantine_panel_conflicts(q));
        return 0;
    }
    /* 실행 중 언어 바꾸기: 복원 결과(상태 줄)와 같은 이름 때문에 건너뛴 항목의 결과 열이 새 언어로 다시
     * 쓰이고 단추·열이 잘리지 않는지 확인한다(한국어로 돌아와 이어 간다). */
    if (page_exit(verify_language_round_trip(quarantine_panel_verify_lang, q), NAV_PAGE_QUARANTINE) != 0 ||
        page_exit(quarantine_panel_verify(q, 1, 1), NAV_PAGE_QUARANTINE) != 0) {
        fwprintf(stderr, L"self-test failed: quarantine page after language switch\n");
        return 0;
    }
    return quarantine_panel_select_all(q) == 1 && quarantine_panel_purge(q, 0);
}

/*
 * SELF_TEST_CLEANER 한 단계: page(시스템 정리 → 개발 캐시)의 분석이 끝나면 화면을 확인하고, 분석한 위치가
 * 모두 픽스처 안일 때만 정리를 시작한다(실제 파일 보호). 시스템 정리는 [모두 선택](휴지통 비우기는 고르지
 * 않음), 개발 캐시는 [모두 선택]이 고르지 않으므로 사용자처럼 직접 체크한다. 시작했으면 1.
 */
static int self_test_cleaner_run(NavPage page)
{
    HWND h = g_app.pages[page];
    int failures = page_exit(cleaner_panel_verify(h, g_app.cleaner_root), page);

    g_app.cleaner_expect = 0;
    if (failures == 0) {
        cleaner_panel_check_all(h, 1);
        g_app.cleaner_expect = cleaner_panel_check_kind(h, CLEANER_DEV_CACHE, 1);
    }
    if (failures != 0 || g_app.cleaner_expect == 0 || !cleaner_panel_run(h, 0)) {
        fwprintf(stderr, L"self-test failed: cleaner (%ls) did not start (%u selected)\n",
                 nav_page_name(page), (unsigned)g_app.cleaner_expect);
        return 0;
    }
    return 1;
}

/*
 * --self-test-lang: 사이드바·제목에 지금 언어 문구가 쓰이는지, 언어별 글꼴을 골랐는지,
 * 최소 창 크기에서도 사이드바 문구가 잘리지 않고 폴더 스캔 페이지의 단추·체크 상자 문구가 잘리지 않고
 * 패널 안에 있는지 확인한다.
 */
static int check_lang(LOGFONTW *out_font, int *out_min_width)
{
    const wchar_t *face = theme_font_face(ui_lang_current());
    LOGFONTW lf;
    RECT nav, item, page;
    int failures = 0, cx, cy, i;

    memset(&lf, 0, sizeof(lf));
    SELF_CHECK(GetObjectW(g_app.font, sizeof(lf), &lf) == sizeof(lf));
    SELF_CHECK(face == NULL || wcscmp(lf.lfFaceName, face) == 0);
    SELF_CHECK(text_is(g_app.title, ui_text(nav_page_text(g_app.page))));

    /* 최소 창 크기로 줄여 배치한다(WM_SIZE → layout). */
    min_window_size(&cx, &cy);
    SetWindowPos(g_app.hwnd, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    GetClientRect(g_app.nav, &nav);
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        /* 사이드바 항목 문구가 항목 사각형 안에 다 들어간다(글자 시작 여백 16 + 오른쪽 4). */
        int need = theme_text_width(g_app.nav, g_app.font, ui_text(nav_page_text((NavPage)i))) +
                   scale(20);
        SELF_CHECK(nav_view_item_rect(g_app.nav, (NavPage)i, &item));
        SELF_CHECK(item.right - item.left >= need && item.right <= nav.right);
    }
    page_rect(&page);
    SELF_CHECK(page.left >= nav.right);
    /* 사이드바 항목이 서로 겹치지 않는다([설정]은 아래 끝) */
    SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_BOOSTER, &item));
    {
        RECT settings;
        SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_SETTINGS, &settings));
        SELF_CHECK(settings.top >= item.bottom && settings.bottom <= nav.bottom);
    }
    failures += scan_panel_verify_lang(g_app.pages[NAV_PAGE_SCAN]);
    failures += cleanup_panel_verify_lang(g_app.pages[NAV_PAGE_CLEANUP]);
    failures += cleaner_panel_verify_lang(g_app.pages[NAV_PAGE_CLEANER]);
    failures += cleaner_panel_verify_lang(g_app.pages[NAV_PAGE_CACHE]);
    failures += quarantine_panel_verify_lang(g_app.pages[NAV_PAGE_QUARANTINE]);
    failures += drive_panel_verify_lang(g_app.pages[NAV_PAGE_DRIVES]);
    failures += monitor_panel_verify_lang(g_app.pages[NAV_PAGE_MONITOR]);
    failures += booster_panel_verify_lang(g_app.pages[NAV_PAGE_BOOSTER]);
    failures += settings_panel_verify_lang(g_app.pages[NAV_PAGE_SETTINGS]);
    *out_font = lf;
    *out_min_width = cx;
    return failures;
}

static int verify_lang(void)
{
    LOGFONTW lf;
    int cx, failures = check_lang(&lf, &cx);

    /* 성공했을 때만 요약을 쓴다(CTest 가 이 줄로 통과를 판정한다). */
    if (failures == 0) {
        wprintf(L"lang: %ls, font: %ls, min width: %d\n", ui_lang_name(ui_lang_current()),
                lf.lfFaceName, cx);
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/* ---- 배치 검사(--self-test-layout) ---- */

/* 검사 중인 조건(실패를 알릴 때 함께 쓴다). */
static struct {
    int cx, cy;
    const wchar_t *where;
} g_layout;

/* 컨트롤 글꼴로 잰 문구 크기. wrap_w > 0 이면 그 폭에서 줄을 바꾼 크기. */
static SIZE layout_text_size(HWND h, const wchar_t *text, int wrap_w)
{
    SIZE out = {0, 0};
    RECT r = {0, 0, 0, 0};
    HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    HDC dc = GetDC(h);
    HGDIOBJ old;

    if (dc == NULL) {
        return out;
    }
    r.right = wrap_w > 0 ? wrap_w : 0;
    old = SelectObject(dc, font != NULL ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, text, -1, &r,
              DT_CALCRECT | DT_NOPREFIX | (wrap_w > 0 ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    out.cx = r.right - r.left;
    out.cy = r.bottom - r.top;
    return out;
}

static int layout_fail(HWND h, const RECT *rc, const wchar_t *why, HWND other)
{
    wchar_t cls[64], text[96], text2[96];

    text2[0] = L'\0';
    GetClassNameW(h, cls, 64);
    GetWindowTextW(h, text, 96);
    if (other != NULL) {
        GetWindowTextW(other, text2, 96);
    }
    fwprintf(stderr,
             L"layout: %ls dpi=%d window=%dx%d %ls %ls id=%d \"%ls\" at (%ld,%ld %ldx%ld): %ls%ls%ls%ls\n",
             ui_lang_name(ui_lang_current()), g_app.dpi, g_layout.cx, g_layout.cy, g_layout.where,
             cls, GetDlgCtrlID(h), text, rc->left, rc->top, rc->right - rc->left,
             rc->bottom - rc->top, why, other != NULL ? L" \"" : L"", text2,
             other != NULL ? L"\"" : L"");
    return 1;
}

/* 문구가 컨트롤 안에 다 보이는지(단추·체크 상자·글자). 말줄임표를 쓰는 글자는 폭을 보지 않는다. */
static int layout_check_text(HWND h, const RECT *rc)
{
    wchar_t cls[32], text[512];
    LONG style = (LONG)GetWindowLongPtrW(h, GWL_STYLE);
    int w = rc->right - rc->left, hgt = rc->bottom - rc->top;
    SIZE ts;

    GetClassNameW(h, cls, 32);
    if (GetWindowTextW(h, text, 512) == 0) {
        return 0;
    }
    if (_wcsicmp(cls, L"Button") == 0) {
        LONG type = style & BS_TYPEMASK;
        if (type == BS_GROUPBOX) {
            ts = layout_text_size(h, text, 0);
            return ts.cx + scale(16) > w ? layout_fail(h, rc, L"group title clipped", NULL) : 0;
        }
        if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON ||
            type == BS_AUTORADIOBUTTON || type == BS_3STATE || type == BS_AUTO3STATE) {
            ts = layout_text_size(h, text, (style & BS_MULTILINE) ? w - scale(17) : 0);
            if (ts.cx + scale(17) > w) {
                return layout_fail(h, rc, L"check box text clipped (width)", NULL);
            }
            return ts.cy > hgt ? layout_fail(h, rc, L"check box text clipped (height)", NULL) : 0;
        }
        /* 누름 단추: 테두리 안쪽 여백(양쪽 3px)을 빼고 들어가야 한다. */
        ts = layout_text_size(h, text, (style & BS_MULTILINE) ? w - scale(6) : 0);
        if (ts.cx + scale(6) > w) {
            return layout_fail(h, rc, L"button text clipped (width)", NULL);
        }
        return ts.cy + scale(4) > hgt ? layout_fail(h, rc, L"button text clipped (height)", NULL)
                                      : 0;
    }
    if (_wcsicmp(cls, L"Static") == 0) {
        LONG type = style & SS_TYPEMASK;
        int ellipsis = (style & SS_ELLIPSISMASK) != 0;
        if (type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE) {
            ts = layout_text_size(h, text, 0);
            if (!ellipsis && ts.cx > w) {
                return layout_fail(h, rc, L"text clipped (width)", NULL);
            }
            return ts.cy > hgt ? layout_fail(h, rc, L"text clipped (height)", NULL) : 0;
        }
        if (type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT) {
            if (ellipsis) {
                ts = layout_text_size(h, text, 0);
                return ts.cy > hgt ? layout_fail(h, rc, L"text clipped (height)", NULL) : 0;
            }
            ts = layout_text_size(h, text, w > 0 ? w : 1);
            if (ts.cx > w) {
                return layout_fail(h, rc, L"text clipped (word wider than control)", NULL);
            }
            return ts.cy > hgt ? layout_fail(h, rc, L"wrapped text clipped (height)", NULL) : 0;
        }
    }
    return 0;
}

#define LAYOUT_MAX_CHILDREN 160

/*
 * parent 의 보이는 자식 컨트롤이 parent 안에 있고, 서로 겹치지 않고, 문구가 잘리지 않는지 확인한다.
 * 이 프로그램의 패널(창 클래스 ByteSieve…) 안은 다시 들어가 본다(공용 컨트롤의 내부 자식은 보지 않음).
 */
static int layout_check_children(HWND parent)
{
    HWND kids[LAYOUT_MAX_CHILDREN];
    RECT rects[LAYOUT_MAX_CHILDREN], pc;
    int n = 0, failures = 0, i, j;
    HWND h;

    GetClientRect(parent, &pc);
    for (h = GetWindow(parent, GW_CHILD); h != NULL && n < LAYOUT_MAX_CHILDREN;
         h = GetWindow(h, GW_HWNDNEXT)) {
        wchar_t cls[64];
        RECT r;
        if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE)) {
            continue;
        }
        GetWindowRect(h, &r);
        MapWindowPoints(NULL, parent, (POINT *)&r, 2);
        GetClassNameW(h, cls, 64);
        if ((_wcsicmp(cls, WC_LISTVIEWW) == 0 || _wcsicmp(cls, WC_TREEVIEWW) == 0) &&
            r.bottom - r.top < scale(48)) {
            failures += layout_fail(h, &r, L"list has no room", NULL); /* 목록이 접혀 보이지 않음 */
        }
        if (r.right <= r.left || r.bottom <= r.top) {
            continue; /* 자리가 없어 접어 둔 칸 */
        }
        if (r.left < 0 || r.top < 0 || r.right > pc.right || r.bottom > pc.bottom) {
            failures += layout_fail(h, &r, L"outside its panel", NULL);
        }
        failures += layout_check_text(h, &r);
        if (wcsncmp(cls, L"ByteSieve", 9) == 0) {
            failures += layout_check_children(h);
        }
        kids[n] = h;
        rects[n++] = r;
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            RECT x;
            if (IntersectRect(&x, &rects[i], &rects[j])) {
                failures += layout_fail(kids[i], &rects[i], L"overlaps", kids[j]);
            }
        }
    }
    return failures;
}

/* 모든 입력 칸(EDIT)에 표식 문구를 넣거나(set) 그대로인지 확인한다(창 크기·언어를 바꿔도 입력값 유지). */
static int layout_edit_marks(HWND parent, int set, int *index)
{
    HWND h;
    int failures = 0;

    for (h = GetWindow(parent, GW_CHILD); h != NULL; h = GetWindow(h, GW_HWNDNEXT)) {
        wchar_t cls[64], want[64], now[64];
        GetClassNameW(h, cls, 64);
        if (wcsncmp(cls, L"ByteSieve", 9) == 0) {
            failures += layout_edit_marks(h, set, index);
            continue;
        }
        if (_wcsicmp(cls, L"Edit") != 0 || (GetWindowLongPtrW(h, GWL_STYLE) & ES_READONLY)) {
            continue;
        }
        swprintf(want, 64, L"C:\\layout-check-%d", (*index)++);
        if (set) {
            SetWindowTextW(h, want);
        } else {
            GetWindowTextW(h, now, 64);
            if (wcscmp(now, want) != 0) {
                RECT r;
                GetWindowRect(h, &r);
                failures += layout_fail(h, &r, L"input text was not kept", NULL);
            }
        }
    }
    return failures;
}

/*
 * 자리가 모자랄 때의 스크롤: 창이 작업 영역 안이고, 보이는 페이지는 최소 크기 이상이며 모자란 만큼 스크롤
 * 막대로 끝까지 갈 수 있고, 사이드바도 항목보다 낮으면 스크롤해 모든 항목에 갈 수 있다.
 */
static int layout_check_scroll(void)
{
    RECT wr, host, page, nav;
    SCROLLINFO si;
    int failures = 0, work_w, work_h, bar;
    HWND visible = g_app.pages[g_app.page];

    GetWindowRect(g_app.hwnd, &wr);
    work_area_size(&work_w, &work_h);
    {
        int cx, cy;
        min_window_size(&cx, &cy);
        if (cx > work_w || cy > work_h) {
            failures += layout_fail(g_app.hwnd, &wr, L"minimum window larger than work area", NULL);
        }
    }
    GetClientRect(g_app.host, &host);
    GetWindowRect(visible, &page);
    if (page.right - page.left < page_min_width((int)g_app.page) ||
        page.bottom - page.top < page_min_height((int)g_app.page)) {
        failures += layout_fail(visible, &page, L"page smaller than its minimum", NULL);
    }
    for (bar = SB_HORZ; bar <= SB_VERT; bar++) {
        int need = bar == SB_HORZ ? page.right - page.left : page.bottom - page.top;
        int have = bar == SB_HORZ ? host.right : host.bottom;
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        if (need > have) {
            if (!GetScrollInfo(g_app.host, bar, &si) || si.nMax + 1 - (int)si.nPage != need - have) {
                failures += layout_fail(visible, &page, bar == SB_HORZ ? L"cannot scroll to the right edge"
                                                                       : L"cannot scroll to the bottom", NULL);
            }
        }
    }
    GetClientRect(g_app.nav, &nav);
    if (nav.bottom < nav_view_min_height(g_app.nav)) {
        memset(&si, 0, sizeof(si));
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        if (!(GetWindowLongPtrW(g_app.nav, GWL_STYLE) & WS_VSCROLL) ||
            !GetScrollInfo(g_app.nav, SB_VERT, &si) ||
            si.nMax + 1 - (int)si.nPage < nav_view_min_height(g_app.nav) - nav.bottom) {
            failures += layout_fail(g_app.nav, &nav, L"sidebar cannot scroll to every item", NULL);
        }
    } else {
        /* 사이드바 항목 문구가 항목 안에 다 들어간다(스크롤 막대가 있어도). */
        int i;
        for (i = 0; i < NAV_PAGE_COUNT; i++) {
            RECT item;
            int need = theme_text_width(g_app.nav, g_app.font, ui_text(nav_page_text((NavPage)i))) +
                       scale(20);
            if (nav_view_item_rect(g_app.nav, (NavPage)i, &item) && item.right - item.left < need) {
                failures += layout_fail(g_app.nav, &item, L"sidebar item text clipped", NULL);
            }
        }
    }
    return failures;
}

/* 창을 cx×cy 로 바꿔 제목·모든 페이지를 확인한다. */
static int layout_check_window(int cx, int cy)
{
    static const wchar_t *const names[NAV_PAGE_COUNT] = {
        L"scan",    L"drives",  L"cleanup", L"cleaner", L"cache",
        L"quarantine", L"monitor", L"booster", L"settings"};
    RECT wr, title;
    int failures = 0, i;

    SetWindowPos(g_app.hwnd, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    GetWindowRect(g_app.hwnd, &wr);
    g_layout.cx = wr.right - wr.left;
    g_layout.cy = wr.bottom - wr.top;
    g_layout.where = L"main";
    GetWindowRect(g_app.title, &title);
    MapWindowPoints(NULL, g_app.hwnd, (POINT *)&title, 2);
    failures += layout_check_text(g_app.title, &title);
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        RECT pr;
        g_layout.where = names[i];
        GetWindowRect(g_app.pages[i], &pr);
        if (pr.right - pr.left <= 0 || pr.bottom - pr.top <= 0) {
            failures += layout_fail(g_app.pages[i], &pr, L"page has no room", NULL);
            continue;
        }
        failures += layout_check_children(g_app.pages[i]);
    }
    failures += layout_check_scroll();
    return failures;
}

/*
 * --self-test-layout: 네 언어(ko·en·ja·zh-Hans)마다 최소 창 크기, 조금 큰 크기, 기본 크기, 넓고 낮은 크기,
 * 좁고 높은 크기에서 모든 페이지를 배치해 확인한다(DPI 는 BYTESIEVE_TEST_DPI). 입력 칸 값은 처음에 넣은
 * 대로 남아야 한다. 끝나면 한국어로 돌아온다.
 */
static int verify_layout(void)
{
    static const UiLang langs[] = {UI_LANG_KO, UI_LANG_EN, UI_LANG_JA, UI_LANG_ZH_HANS, UI_LANG_KO};
    int failures = 0, index = 0, l, s, cx, cy;

    layout_edit_marks(g_app.hwnd, 1, &index);
    for (l = 0; l < (int)(sizeof(langs) / sizeof(langs[0])); l++) {
        int sizes[5][2];
        apply_language(langs[l]);
        min_window_size(&cx, &cy);
        sizes[0][0] = cx, sizes[0][1] = cy;
        sizes[1][0] = cx + scale(160), sizes[1][1] = cy + scale(120);
        sizes[2][0] = cx > scale(1240) ? cx : scale(1240);
        sizes[2][1] = cy > scale(720) ? cy : scale(720);
        sizes[3][0] = cx + scale(700), sizes[3][1] = cy;
        sizes[4][0] = cx, sizes[4][1] = cy + scale(500);
        for (s = 0; s < 5; s++) {
            int pg;
            failures += layout_check_window(sizes[s][0], sizes[s][1]);
            /* 페이지마다 스크롤 자리 확인(첫 페이지는 위에서 확인) */
            for (pg = 1; pg < NAV_PAGE_COUNT && s == 0; pg++) {
                show_page((NavPage)pg);
                failures += layout_check_scroll();
            }
            show_page(NAV_PAGE_SCAN);
        }
    }
    index = 0;
    failures += layout_edit_marks(g_app.hwnd, 0, &index);
    if (failures == 0) {
        min_window_size(&cx, &cy);
        wprintf(L"layout: dpi %d, min window %dx%d, checked ko en ja zh-Hans\n", g_app.dpi, cx, cy);
        fflush(stdout);
    } else {
        wprintf(L"layout: %d problem(s)\n", failures);
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/*
 * --self-test-dpi-change: 창이 배율이 다른 모니터로 옮겨 간 것처럼 WM_DPICHANGED(권한 창 크기 포함)를
 * 96 → 144 → 192 → 120 → 96 DPI 로 보낸다. 단계마다 DPI·글꼴 크기·목록 열 폭이 비율대로 바뀌고, 그 크기와
 * 최소 창 크기에서 모든 페이지가 잘리거나 겹치지 않으며(layout_check_window), 입력값·고른 페이지·사이드바
 * 선택이 그대로인지 확인한다. 실제 모니터 이동은 Windows 가 같은 메시지를 보낸다.
 */
static int verify_dpi_change(void)
{
    static const int steps[] = {96, 144, 192, 120, 96};
    HWND list = NULL, child;
    LOGFONTW base;
    int failures = 0, index = 0, i, col0 = 0, base_dpi;

    /* 매니페스트로 모니터별 DPI 인식 V2 가 켜져 있어야 Windows 가 WM_DPICHANGED 를 보낸다. */
    SELF_CHECK(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    apply_dpi(96, NULL); /* 시작 DPI 를 96 으로 맞춘다(실제 화면 DPI 와 무관하게) */
    base_dpi = g_app.dpi;
    SELF_CHECK(GetObjectW(g_app.font, sizeof(base), &base) == sizeof(base));
    show_page(NAV_PAGE_CLEANUP);
    layout_edit_marks(g_app.hwnd, 1, &index);
    for (child = GetWindow(g_app.pages[NAV_PAGE_CLEANUP], GW_CHILD); child != NULL;
         child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t cls[32];
        GetClassNameW(child, cls, 32);
        if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
            list = child;
            col0 = ListView_GetColumnWidth(list, 0);
        }
    }
    SELF_CHECK(list != NULL && col0 > 0);
    for (i = 1; i < (int)(sizeof(steps) / sizeof(steps[0])); i++) {
        RECT wr, want;
        LOGFONTW lf;
        int dpi = steps[i], prev = g_app.dpi, cx, cy, expect;
        GetWindowRect(g_app.hwnd, &wr);
        want.left = wr.left;
        want.top = wr.top;
        want.right = wr.left + MulDiv(wr.right - wr.left, dpi, prev);
        want.bottom = wr.top + MulDiv(wr.bottom - wr.top, dpi, prev);
        SendMessageW(g_app.hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&want);
        SELF_CHECK(g_app.dpi == dpi);
        SELF_CHECK(GetObjectW(g_app.font, sizeof(lf), &lf) == sizeof(lf));
        expect = MulDiv(base.lfHeight, dpi, base_dpi);
        SELF_CHECK(lf.lfHeight >= expect - 1 && lf.lfHeight <= expect + 1);
        SELF_CHECK((HFONT)SendMessageW(list, WM_GETFONT, 0, 0) == g_app.font);
        /* 체크 상자는 다시 만들어도 그대로 켜져 있고 체크 상태는 패널이 알려 준다 */
        SELF_CHECK((ListView_GetExtendedListViewStyle(list) & LVS_EX_CHECKBOXES) &&
                   ListView_GetCallbackMask(list) == LVIS_STATEIMAGEMASK);
        expect = MulDiv(col0, dpi, base_dpi);
        SELF_CHECK(ListView_GetColumnWidth(list, 0) >= expect - 2);
        SELF_CHECK(g_app.page == NAV_PAGE_CLEANUP && nav_view_selected(g_app.nav) == NAV_PAGE_CLEANUP &&
                   (GetWindowLongPtrW(g_app.pages[NAV_PAGE_CLEANUP], GWL_STYLE) & WS_VISIBLE) != 0);
        GetWindowRect(g_app.hwnd, &wr);
        min_window_size(&cx, &cy);
        failures += layout_check_window(wr.right - wr.left > cx ? wr.right - wr.left : cx,
                                        wr.bottom - wr.top > cy ? wr.bottom - wr.top : cy);
        failures += layout_check_window(cx, cy);
        wprintf(L"dpi-change: %d, font %ld, column %d, min window %dx%d\n", dpi, lf.lfHeight,
                ListView_GetColumnWidth(list, 0), cx, cy);
    }
    index = 0;
    failures += layout_edit_marks(g_app.hwnd, 0, &index);
    if (failures == 0) {
        wprintf(L"dpi-change: ok\n");
    }
    fflush(stdout);
    return failures == 0 ? 0 : 1;
}

/*
 * --self-test-lang-switch: 한국어로 스캔을 마친 화면에서 표시 언어를 en → ja → zh-Hans → ko 로 바꾸며
 * 단계마다 언어별 글꼴, 사이드바·제목, 폴더 스캔 페이지(단추·보기·열 머리글, 상태 줄의 스캔 결과, 분석 줄,
 * 트리·변화 열)와 다른 페이지·설정 페이지가 새 언어로 다시 쓰였고 잘리지 않는지 확인한다(check_lang).
 * 시작 글꼴(끝날 때 해제)은 그대로 살아 있어야 한다.
 */
static int verify_lang_switch(void)
{
    static const UiLang order[] = {UI_LANG_EN, UI_LANG_JA, UI_LANG_ZH_HANS, UI_LANG_KO};
    LOGFONTW lf, base;
    int failures = 0, cx;
    size_t i;

    SELF_CHECK(ui_lang_current() == UI_LANG_KO);
    failures += check_lang(&lf, &cx);
    for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        apply_language(order[i]);
        SELF_CHECK(ui_lang_current() == order[i]);
        failures += check_lang(&lf, &cx);
        SELF_CHECK(GetObjectW(g_app.base_font, sizeof(base), &base) == sizeof(base));
        if (failures == 0) {
            wprintf(L"lang-switch: %ls, font: %ls\n", ui_lang_name(order[i]), lf.lfFaceName);
        }
    }
    if (failures == 0) {
        wprintf(L"lang-switch: en ja zh-Hans ko\n");
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/* 사이드바 항목 page 의 가운데를 마우스로 누른다. */
static void click_nav(NavPage page)
{
    RECT r;

    if (nav_view_item_rect(g_app.nav, page, &r)) {
        LPARAM at = MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        SendMessageW(g_app.nav, WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageW(g_app.nav, WM_LBUTTONUP, 0, at);
    }
}

/*
 * --self-test-nav: 사이드바로 모든 페이지를 키보드(↓·↑·Home·End)와 마우스로 오가며, 고른 페이지의 패널만
 * 보이고 제목이 맞는지, 그룹 머리글을 누르면 아무것도 바뀌지 않는지, 처음 열 때 조회(드라이브·시스템 정리·
 * 개발 캐시)는 한 번만, 격리 폴더는 열 때마다 하는지 확인한다. 실제 조회는 하지 않는다(dry_load).
 * 지나간 페이지 이름을 stdout 에 쓴다.
 */
static int verify_nav(void)
{
    int failures = 0, p, quarantine_visits = 0;
    RECT nav, page, item;
    NavPage seen[NAV_PAGE_COUNT * 2];
    size_t steps = 0, i;


    layout();
    failures += verify_page_shown(NAV_PAGE_SCAN);

    /* 배치: 사이드바 오른쪽에 제목, 그 아래 패널 */
    GetWindowRect(g_app.nav, &nav);
    MapWindowPoints(NULL, g_app.hwnd, (POINT *)&nav, 2);
    page_rect(&page);
    SELF_CHECK(nav.left == 0 && nav.right == nav_width() && page.left > nav.right);
    GetWindowRect(g_app.title, &item);
    MapWindowPoints(NULL, g_app.hwnd, (POINT *)&item, 2);
    SELF_CHECK(item.left == page.left && item.bottom <= page.top);
    SELF_CHECK(nav_width() >= scale(200));

    /* 키보드: ↓ 로 끝까지 */
    SetFocus(g_app.nav);
    seen[steps++] = g_app.page;
    for (p = 0; p < 2 * NAV_PAGE_COUNT; p++) {
        NavPage before = g_app.page;
        SendMessageW(g_app.nav, WM_KEYDOWN, VK_DOWN, 0);
        if (g_app.page == before) {
            break;
        }
        SELF_CHECK(g_app.page == nav_key_move(before, NAV_KEY_DOWN));
        failures += verify_page_shown(g_app.page);
        quarantine_visits += g_app.page == NAV_PAGE_QUARANTINE;
        if (steps < sizeof(seen) / sizeof(seen[0])) {
            seen[steps++] = g_app.page;
        }
    }
    SELF_CHECK(g_app.page == NAV_PAGE_SETTINGS && steps == NAV_PAGE_COUNT);
    /* ↑ 한 번, Home, End */
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_UP, 0);
    failures += verify_page_shown(NAV_PAGE_BOOSTER);
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_HOME, 0);
    failures += verify_page_shown(NAV_PAGE_SCAN);
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_END, 0);
    failures += verify_page_shown(NAV_PAGE_SETTINGS);
    /* [설정]은 사이드바 아래 끝에 붙는다 */
    SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_SETTINGS, &item));
    GetClientRect(g_app.nav, &nav);
    SELF_CHECK(item.bottom >= nav.bottom - scale(16) && item.bottom <= nav.bottom);

    /* 마우스: 모든 페이지를 차례로 누른다(두 번째 방문) */
    for (p = 0; p < NAV_PAGE_COUNT; p++) {
        click_nav((NavPage)p);
        failures += verify_page_shown((NavPage)p);
        quarantine_visits += p == NAV_PAGE_QUARANTINE;
    }
    /* 같은 항목을 다시 눌러도 그대로(조회를 다시 하지 않음) */
    click_nav(NAV_PAGE_SETTINGS);
    failures += verify_page_shown(NAV_PAGE_SETTINGS);
    /* 그룹 머리글(첫 항목 바로 위)·부스터와 설정 사이 빈 곳을 누르면 아무것도 바뀌지 않는다 */
    SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_SCAN, &item));
    SendMessageW(g_app.nav, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM((item.left + item.right) / 2, item.top - scale(8)));
    failures += verify_page_shown(NAV_PAGE_SETTINGS);
    SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_SETTINGS, &item));
    SendMessageW(g_app.nav, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM((item.left + item.right) / 2, item.top - scale(20)));
    failures += verify_page_shown(NAV_PAGE_SETTINGS);

    /* 처음 열 때 조회는 한 번만, 격리 폴더는 열 때마다 */
    SELF_CHECK(g_app.loads[NAV_PAGE_SCAN] == 0 && g_app.loads[NAV_PAGE_CLEANUP] == 0);
    SELF_CHECK(g_app.loads[NAV_PAGE_DRIVES] == 1);
    SELF_CHECK(g_app.loads[NAV_PAGE_CLEANER] == 1 && g_app.loads[NAV_PAGE_CACHE] == 1);
    SELF_CHECK(g_app.loads[NAV_PAGE_QUARANTINE] == (unsigned)quarantine_visits);
    SELF_CHECK(quarantine_visits == 2);
    /* 개발 캐시 페이지는 개발 캐시·가상 디스크만, 시스템 정리는 그 밖의 종류만 분석한다 */
    SELF_CHECK(cleaner_panel_kinds(g_app.pages[NAV_PAGE_CACHE]) == CLEANER_KINDS_CACHE);
    SELF_CHECK(cleaner_panel_kinds(g_app.pages[NAV_PAGE_CLEANER]) == CLEANER_KINDS_SYSTEM);
    SELF_CHECK(!cleaner_panel_loaded(g_app.pages[NAV_PAGE_CACHE])); /* dry_load: 실제 분석 없음 */

    if (failures == 0) {
        wprintf(L"nav:");
        for (i = 0; i < steps; i++) {
            wprintf(L" %ls", nav_page_name(seen[i]));
        }
        wprintf(L"\n");
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/* root 아래에서 처음 만나는 cls 클래스 자식 창. */
typedef struct {
    const wchar_t *cls;
    HWND found;
} FindClass;

static BOOL CALLBACK find_class_proc(HWND h, LPARAM lp)
{
    FindClass *f = (FindClass *)lp;
    wchar_t cls[64];

    if (GetClassNameW(h, cls, 64) > 0 && _wcsicmp(cls, f->cls) == 0) {
        f->found = h;
        return FALSE;
    }
    return TRUE;
}

static HWND find_class(HWND root, const wchar_t *cls)
{
    FindClass f;

    f.cls = cls;
    f.found = NULL;
    EnumChildWindows(root, find_class_proc, (LPARAM)&f);
    return f.found;
}

/* 모든 목록·트리의 색이 지금 모드와 맞는지 센다. */
typedef struct {
    int dark;
    int lists, trees, bad;
} ThemeCheck;

static BOOL CALLBACK theme_check_proc(HWND h, LPARAM lp)
{
    ThemeCheck *c = (ThemeCheck *)lp;
    wchar_t cls[64];
    COLORREF bk = c->dark ? theme_color_in(1, THEME_TOKEN_SURFACE) : GetSysColor(COLOR_WINDOW);
    COLORREF fg = c->dark ? theme_color_in(1, THEME_TOKEN_TEXT) : GetSysColor(COLOR_WINDOWTEXT);

    if (GetClassNameW(h, cls, 64) <= 0) {
        return TRUE;
    }
    if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
        c->lists++;
        c->bad += ListView_GetBkColor(h) != bk || ListView_GetTextColor(h) != fg ||
                  ListView_GetTextBkColor(h) != bk;
    } else if (_wcsicmp(cls, WC_TREEVIEWW) == 0) {
        c->trees++;
        c->bad += TreeView_GetBkColor(h) != (c->dark ? bk : (COLORREF)-1) ||
                  TreeView_GetTextColor(h) != (c->dark ? fg : (COLORREF)-1);
    }
    return TRUE;
}

/*
 * 지금 화면이 dark 모드로 그려지는지: 모드, 모든 목록·트리 색, 창 클래스 배경 브러시, 제목(정적)·
 * 입력 칸·체크 상자의 WM_CTLCOLOR* 응답 색. 실패 수.
 */
static int verify_theme_state(int dark)
{
    int failures = 0;
    ThemeCheck c;
    HDC dc;
    HWND scan = g_app.pages[NAV_PAGE_SCAN];
    HWND edit = find_class(scan, WC_EDITW);
    COLORREF bg = theme_color_in(dark, THEME_TOKEN_BG);

    SELF_CHECK(theme_is_dark() == dark);
    memset(&c, 0, sizeof(c));
    c.dark = dark;
    EnumChildWindows(g_app.hwnd, theme_check_proc, (LPARAM)&c);
    SELF_CHECK(c.lists >= 6 && c.trees >= 1 && c.bad == 0);
    SELF_CHECK((HBRUSH)GetClassLongPtrW(g_app.hwnd, GCLP_HBRBACKGROUND) == theme_bg_brush());
    SELF_CHECK((HBRUSH)GetClassLongPtrW(g_app.nav, GCLP_HBRBACKGROUND) == theme_bg_brush());
    SELF_CHECK((HBRUSH)GetClassLongPtrW(g_app.pages[NAV_PAGE_SETTINGS], GCLP_HBRBACKGROUND) ==
               theme_bg_brush());
    SELF_CHECK(theme_color(THEME_TOKEN_BG) == bg);

    dc = CreateCompatibleDC(NULL);
    SELF_CHECK(dc != NULL && edit != NULL);
    if (dc != NULL) {
        SendMessageW(g_app.hwnd, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)g_app.title);
        SELF_CHECK(GetBkColor(dc) == bg);
        SELF_CHECK(GetTextColor(dc) ==
                   (dark ? theme_color_in(1, THEME_TOKEN_TEXT) : GetSysColor(COLOR_WINDOWTEXT)));
        if (dark && edit != NULL) {
            SELF_CHECK(SendMessageW(scan, WM_CTLCOLOREDIT, (WPARAM)dc, (LPARAM)edit) != 0);
            SELF_CHECK(GetBkColor(dc) == theme_color_in(1, THEME_TOKEN_SURFACE));
            SELF_CHECK(GetTextColor(dc) == theme_color_in(1, THEME_TOKEN_TEXT));
        }
        DeleteDC(dc);
    }
    return failures;
}

/* WM_SETTINGCHANGE("ImmersiveColorSet")를 보내 Windows 앱 모드가 바뀐 것처럼 한다(테스트용 환경 변수로). */
static void simulate_system_theme(const wchar_t *mode)
{
    SetEnvironmentVariableW(L"BYTESIEVE_SYSTEM_THEME", mode);
    SendMessageW(g_app.hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
}

/*
 * --self-test-theme: 설정 키(테스트 키)가 비어 있어 "시스템 설정 따르기"로 시작하고 시스템은 밝게
 * (BYTESIEVE_SYSTEM_THEME=light). [설정]에서 어둡게 → 시스템 따르기(시스템 어둡게·밝게로 바뀜) →
 * 밝게(시스템이 어둡게로 바뀌어도 그대로) 차례로 바꾸며 화면 색과 저장 값을 확인한다. 지나간 단계를 stdout 에.
 */
static int verify_theme(void)
{
    HWND settings = g_app.pages[NAV_PAGE_SETTINGS];
    int failures = 0;

    show_page(NAV_PAGE_SETTINGS);
    SELF_CHECK(g_app.theme_mode == THEME_MODE_SYSTEM);
    failures += verify_theme_state(0);
    failures += settings_panel_verify(settings, scan_panel_autosave(g_app.pages[NAV_PAGE_SCAN]));

    SELF_CHECK(settings_panel_set_theme(settings, THEME_MODE_DARK));
    SELF_CHECK(g_app.theme_mode == THEME_MODE_DARK && settings_load_theme() == THEME_MODE_DARK);
    failures += verify_theme_state(1);
    failures += verify_page_shown(NAV_PAGE_SETTINGS);

    SELF_CHECK(settings_panel_set_theme(settings, THEME_MODE_SYSTEM));
    failures += verify_theme_state(0);
    simulate_system_theme(L"dark");
    failures += verify_theme_state(1);
    simulate_system_theme(L"light");
    failures += verify_theme_state(0);

    SELF_CHECK(settings_panel_set_theme(settings, THEME_MODE_LIGHT));
    SELF_CHECK(settings_load_theme() == THEME_MODE_LIGHT);
    simulate_system_theme(L"dark"); /* 밝게로 정했으면 시스템을 따르지 않는다 */
    failures += verify_theme_state(0);
    failures += settings_panel_verify(settings, scan_panel_autosave(g_app.pages[NAV_PAGE_SCAN]));

    if (failures == 0) {
        wprintf(L"theme: light dark system-dark system-light light-fixed\n");
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/*
 * --self-test-settings: [설정]에서 표시 언어를 일본어 → 한국어 → 시스템 따르기로 바꾸며 저장 값·다음 실행
 * 언어(ui_lang_detect)·안내와, 화면에 바로 적용됐는지(지금 언어·번역된 페이지 문구, check_lang)를 확인하고,
 * [스캔 기록 자동 저장]을 끄고 켜며 폴더 스캔 페이지·설정 파일과 맞는지 확인한다.
 * BYTESIEVE_LANG 이 없고 BYTESIEVE_HISTORY_DIR 이 테스트 폴더여야 한다.
 */
static int verify_settings(void)
{
    HWND settings = g_app.pages[NAV_PAGE_SETTINGS], scan = g_app.pages[NAV_PAGE_SCAN];
    wchar_t dir[MAX_PATH * 2];
    UiLang lang = UI_LANG_JA, stored;
    LOGFONTW lf;
    int failures = 0, before, cx;

    SELF_CHECK(!settings_lang_env_override(&stored));
    SELF_CHECK(GetEnvironmentVariableW(L"BYTESIEVE_HISTORY_DIR", dir, MAX_PATH * 2) > 0);
    SELF_CHECK(history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) == 0);
    show_page(NAV_PAGE_SETTINGS);
    failures += verify_page_shown(NAV_PAGE_SETTINGS);
    failures += settings_panel_verify(settings, scan_panel_autosave(scan));

    /* 언어: 저장하고 지금 화면에도 바로 적용한다(다음 실행 언어도 같다) */
    SELF_CHECK(settings_panel_set_language(settings, &lang));
    SELF_CHECK(settings_load_language(&stored) && stored == UI_LANG_JA);
    SELF_CHECK(ui_lang_detect() == UI_LANG_JA);
    SELF_CHECK(ui_lang_current() == UI_LANG_JA);
    failures += settings_panel_verify(settings, scan_panel_autosave(scan));
    failures += check_lang(&lf, &cx);
    lang = UI_LANG_KO;
    SELF_CHECK(settings_panel_set_language(settings, &lang));
    SELF_CHECK(ui_lang_detect() == UI_LANG_KO);
    SELF_CHECK(ui_lang_current() == UI_LANG_KO);
    failures += settings_panel_verify(settings, scan_panel_autosave(scan));
    failures += check_lang(&lf, &cx);
    SELF_CHECK(settings_panel_set_language(settings, NULL));
    SELF_CHECK(!settings_load_language(&stored));
    SELF_CHECK(ui_lang_detect() == ui_lang_from_langid(GetUserDefaultUILanguage()));
    SELF_CHECK(ui_lang_current() == ui_lang_from_langid(GetUserDefaultUILanguage()));
    failures += settings_panel_verify(settings, scan_panel_autosave(scan));
    failures += check_lang(&lf, &cx);

    /* 자동 저장: 설정 페이지에서 끄고 켠다(폴더 스캔 페이지·설정 파일이 함께 바뀜) */
    before = scan_panel_autosave(scan);
    settings_panel_click_autosave(settings);
    SELF_CHECK(scan_panel_autosave(scan) == !before);
    SELF_CHECK(history_autosave_enabled(dir) == !before);
    failures += settings_panel_verify(settings, !before);
    settings_panel_click_autosave(settings);
    SELF_CHECK(scan_panel_autosave(scan) == before);
    SELF_CHECK(history_autosave_enabled(dir) == before);
    failures += settings_panel_verify(settings, before);

    if (failures == 0) {
        wprintf(L"settings: language ja ko system, autosave %ls %ls\n", before ? L"off" : L"on",
                before ? L"on" : L"off");
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

/* 자체 검사를 끝낸다(타이머를 멈추고 창을 닫음). */
static void finish_self_test(int exit_code)
{
    KillTimer(g_app.hwnd, TIMER_SELF_TEST);
    g_app.self_test_exit = exit_code;
    DestroyWindow(g_app.hwnd);
}

/* 스캔 자체 검사 종류 → 폴더 스캔 패널 검증 방식. */
static ScanVerifyMode scan_verify_mode(void)
{
    switch (g_app.self_test) {
    case SELF_TEST_CANCEL:
        return SCAN_VERIFY_CANCEL;
    case SELF_TEST_CHANGES:
        return SCAN_VERIFY_CHANGES;
    case SELF_TEST_NO_AUTOSAVE:
        return SCAN_VERIFY_NO_AUTOSAVE;
    case SELF_TEST_CLEAR_HISTORY:
        return SCAN_VERIFY_CLEAR_HISTORY;
    case SELF_TEST_EXPORT:
        return SCAN_VERIFY_EXPORT;
    case SELF_TEST_PRECISE:
        return SCAN_VERIFY_PRECISE;
    default:
        return SCAN_VERIFY_SCAN;
    }
}

/* ---- 창 프로시저 ---- */

static void create_controls(void)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    int i;

    g_app.font = create_ui_font(ui_lang_current());
    g_app.base_font = g_app.font;
    g_app.title_font = theme_create_title_font(g_app.font, g_app.dpi);

    /* 탭 순서가 화면 순서와 같도록 사이드바부터 만든다. */
    g_app.nav = nav_view_create(g_app.hwnd, IDC_NAV, g_app.font, g_app.dpi);
    g_app.title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP |
                                                         SS_CENTERIMAGE | SS_ENDELLIPSIS,
                                  0, 0, 0, 0, g_app.hwnd, (HMENU)(INT_PTR)IDC_TITLE, inst, NULL);
    SendMessageW(g_app.title, WM_SETFONT,
                 (WPARAM)(g_app.title_font != NULL ? g_app.title_font : g_app.font), FALSE);
    /* 페이지 패널은 스크롤 자리 안에 둔다(자리가 모자라면 스크롤). 탭 이동이 패널 안으로 들어가게 CONTROLPARENT. */
    g_app.host = CreateWindowExW(WS_EX_CONTROLPARENT, PAGE_HOST_CLASS, L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, g_app.hwnd,
                                 (HMENU)(INT_PTR)IDC_PAGE_HOST, inst, NULL);

    g_app.pages[NAV_PAGE_SCAN] = scan_panel_create(g_app.host, IDC_SCANPAGE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_DRIVES] =
        drive_panel_create(g_app.host, IDC_DRIVES, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CLEANUP] =
        cleanup_panel_create(g_app.host, IDC_CLEANUP, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CLEANER] =
        cleaner_panel_create(g_app.host, IDC_CLEANER, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CACHE] =
        cleaner_panel_create(g_app.host, IDC_CACHE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_QUARANTINE] =
        quarantine_panel_create(g_app.host, IDC_QUARANTINE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_MONITOR] =
        monitor_panel_create(g_app.host, IDC_MONITOR, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_BOOSTER] =
        booster_panel_create(g_app.host, IDC_BOOSTER, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_SETTINGS] =
        settings_panel_create(g_app.host, IDC_SETTINGS, g_app.font, g_app.dpi);
    /* 시스템 정리와 개발 캐시는 같은 Cleaner 패널을 분석 범위만 달리해 쓴다. */
    cleaner_panel_set_kinds(g_app.pages[NAV_PAGE_CLEANER], CLEANER_KINDS_SYSTEM);
    cleaner_panel_set_kinds(g_app.pages[NAV_PAGE_CACHE], CLEANER_KINDS_CACHE);
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        if (g_app.pages[i] == NULL) {
            fwprintf(stderr, L"page %ls was not created\n", nav_page_name((NavPage)i));
        }
    }
    show_page(NAV_PAGE_SCAN);
}

/* 패널이 보낸 알림(WM_COMMAND)으로 자체 검사를 진행한다. 처리했으면 1. */
static int on_page_notify(int id, int code)
{
    HWND cleanup = g_app.pages[NAV_PAGE_CLEANUP];

    if (g_app.self_test == SELF_TEST_NONE) {
        return 0;
    }
    if (id == IDC_SCANPAGE && code == SPN_FINISHED && g_app.self_test == SELF_TEST_LANG_SWITCH) {
        /* 먼저 한국어 결과 화면이 맞는지 확인한 뒤 언어를 바꿔 간다. */
        int rc = page_exit(scan_panel_verify(g_app.pages[NAV_PAGE_SCAN], SCAN_VERIFY_SCAN, NULL, 0),
                           NAV_PAGE_SCAN);
        finish_self_test(rc != 0 ? rc : verify_lang_switch());
        return 1;
    }
    if (id == IDC_SCANPAGE && code == SPN_FINISHED) {
        finish_self_test(page_exit(scan_panel_verify(g_app.pages[NAV_PAGE_SCAN], scan_verify_mode(),
                                                     g_app.export_prefix,
                                                     g_app.export_enabled_before_scan),
                                   NAV_PAGE_SCAN));
        return 1;
    }
    if (id == IDC_DRIVES && code == DPN_REFRESHED && g_app.self_test == SELF_TEST_DRIVES) {
        /* 조회 결과(상태 줄·종류·암호화·사용률 칸)가 언어를 바꾸면 새 언어로 다시 쓰이고, 한국어로
         * 돌아와도 처음과 같은지 본다. */
        HWND drives = g_app.pages[NAV_PAGE_DRIVES];
        finish_self_test(page_exit(drive_panel_verify(drives) +
                                       verify_language_round_trip(drive_panel_verify_lang, drives) +
                                       drive_panel_verify(drives),
                                   NAV_PAGE_DRIVES));
        return 1;
    }
    if (id == IDC_CLEANUP && code == CPN_FINISHED &&
        (g_app.self_test == SELF_TEST_CLEANUP || g_app.self_test == SELF_TEST_CLEANUP_CANCEL)) {
        int rc = page_exit(cleanup_panel_verify(cleanup, g_app.self_test == SELF_TEST_CLEANUP_CANCEL),
                           NAV_PAGE_CLEANUP);
        /* 탐지 결과(상태 줄·요약·비고·복구·마지막 활동 칸)가 언어를 바꾸면 새 언어로 다시 쓰이는지 */
        if (rc == 0 && g_app.self_test == SELF_TEST_CLEANUP) {
            rc = page_exit(verify_language_round_trip(cleanup_panel_verify_lang, cleanup),
                           NAV_PAGE_CLEANUP);
        }
        finish_self_test(rc);
        return 1;
    }
    if (id == IDC_CLEANUP && code == CPN_FINISHED &&
        (g_app.self_test == SELF_TEST_TRASH || g_app.self_test == SELF_TEST_QUARANTINE)) {
        /* 탐지 화면을 확인한 뒤, 옮길 수 있는 항목을 모두 체크해 휴지통으로 옮긴다. */
        g_app.trash_expect = cleanup_panel_check_all(cleanup, 1);
        if (page_exit(cleanup_panel_verify(cleanup, 0), NAV_PAGE_CLEANUP) != 0 ||
            g_app.trash_expect == 0 || !cleanup_panel_trash(cleanup, 0)) {
            fwprintf(stderr, L"self-test failed: trash did not start (%u selected)\n",
                     (unsigned)g_app.trash_expect);
            finish_self_test(1);
        }
        return 1;
    }
    if (id == IDC_CLEANUP && code == CPN_TRASHED && g_app.self_test == SELF_TEST_TRASH) {
        size_t quarantined = cleanup_panel_quarantined(cleanup);
        if (quarantined > 0) {
            /* 이 위치에서는 휴지통을 쓸 수 없어 격리 폴더로 전환했다(이동식 드라이브, 휴지통 꺼짐·용량
             * 부족). 휴지통 경로는 검사할 수 없으므로 건너뛰고, 실제 드라이브의 격리 폴더에 테스트
             * 파일을 남기지 않게 원래 자리로 복원한다(격리 전환 자체는 gui_quarantine 이 검사한다). */
            size_t restored;
            wchar_t drive[4] = {g_app.trash_root != NULL ? g_app.trash_root[0] : L'C', L':', L'\\', 0};
            const wchar_t *why = L"recycle bin refused: disabled or too small";
            wchar_t probe[MAX_PATH * 2];
            /* 테스트 격리 루트는 그 "아래" 경로만 포함으로 보므로 탐지 경로 아래의 이름으로 묻는다. */
            swprintf(probe, MAX_PATH * 2, L"%ls\\x", g_app.trash_root != NULL ? g_app.trash_root : L"");
            if (g_app.trash_root != NULL && quarantine_test_root_contains(probe)) {
                why = L"test quarantine root";
            } else if (GetDriveTypeW(drive) == DRIVE_REMOVABLE) {
                why = L"removable drive";
            }
            wprintf(L"trash-skip: recycle bin not usable here (%ls, %u of %u items quarantined)\n", why,
                    (unsigned)quarantined, (unsigned)g_app.trash_expect);
            fflush(stdout);
            restored = cleanup_panel_restore_quarantined(cleanup);
            finish_self_test(restored == quarantined ? 0 : 1);
            return 1;
        }
        {
            int failures = cleanup_panel_verify_trash(cleanup, g_app.trash_expect);
            /* 이동 결과(정리 칸·선택 줄·상태 줄)가 언어를 바꾸면 새 언어로 다시 쓰이는지 */
            if (failures == 0) {
                failures = verify_language_round_trip(cleanup_panel_verify_lang, cleanup);
            }
            finish_self_test(failures == 0 ? 0 : 1);
        }
        return 1;
    }
    if (id == IDC_CLEANUP && code == CPN_TRASHED && g_app.self_test == SELF_TEST_QUARANTINE) {
        if (!self_test_quarantine_step()) {
            finish_self_test(1);
        }
        return 1;
    }
    /* 남은 항목(같은 이름 때문에 건너뛴 것)까지 영구 삭제했으면 목록이 비어야 한다. */
    if (id == IDC_QUARANTINE && code == QPN_PURGED && g_app.self_test == SELF_TEST_QUARANTINE) {
        finish_self_test(page_exit(quarantine_panel_verify(g_app.pages[NAV_PAGE_QUARANTINE], 0, 0),
                                   NAV_PAGE_QUARANTINE));
        return 1;
    }
    /* 첫 측정(CPU 값 없음) 뒤 몇 번 더 받아 갱신 간격까지 확인한다. */
    if (id == IDC_MONITOR && code == MPN_SAMPLED && g_app.self_test == SELF_TEST_MONITOR &&
        monitor_panel_samples(g_app.pages[NAV_PAGE_MONITOR]) >= MONITOR_SELF_TEST_SAMPLES) {
        /* 마지막에 보인 측정값(검사가 넣은 측정 실패 포함)이 언어를 바꾸면 새 언어로 다시 쓰이는지도 본다. */
        HWND monitor = g_app.pages[NAV_PAGE_MONITOR];
        finish_self_test(page_exit(monitor_panel_verify(monitor) +
                                       verify_language_round_trip(monitor_panel_verify_lang, monitor),
                                   NAV_PAGE_MONITOR));
        return 1;
    }
    /* 시스템 정리 → 개발 캐시 순서로 분석·정리한다. */
    if ((id == IDC_CLEANER || id == IDC_CACHE) && g_app.self_test == SELF_TEST_CLEANER) {
        NavPage page = id == IDC_CLEANER ? NAV_PAGE_CLEANER : NAV_PAGE_CACHE;
        if (code == CLN_ANALYZED) {
            if (!self_test_cleaner_run(page)) {
                finish_self_test(1);
            }
        } else if (code == CLN_CLEANED) {
            /* 정리 결과(상태 줄·상태 칸·선택 줄·상세 설명)가 언어를 바꾸면 새 언어로 다시 쓰이는지도 본다. */
            if (cleaner_panel_verify_run(g_app.pages[page], g_app.cleaner_expect) != 0 ||
                verify_language_round_trip(cleaner_panel_verify_lang, g_app.pages[page]) != 0) {
                finish_self_test(1);
            } else if (page == NAV_PAGE_CLEANER) {
                show_page(NAV_PAGE_CACHE);
                if (!cleaner_panel_busy(g_app.pages[NAV_PAGE_CACHE])) {
                    fwprintf(stderr, L"self-test failed: cache analysis did not start\n");
                    finish_self_test(1);
                }
            } else {
                finish_self_test(0);
            }
        }
        return 1;
    }
    if (id == IDC_BOOSTER && code == BPN_REFRESHED && g_app.self_test == SELF_TEST_BOOSTER) {
        /* 요약·정보 줄과 목록의 상태·위치·종류 칸이 언어를 바꾸면 새 언어로 다시 쓰이는지도 본다. */
        HWND booster = g_app.pages[NAV_PAGE_BOOSTER];
        finish_self_test(page_exit(booster_panel_verify(booster) +
                                       verify_language_round_trip(booster_panel_verify_lang, booster) +
                                       booster_panel_verify(booster),
                                   NAV_PAGE_BOOSTER));
        return 1;
    }
    return 0;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT wr;
        int cx, cy;
        g_app.hwnd = hwnd;
        create_controls();
        /* CreateWindow 는 최소 크기(WM_GETMINMAXINFO)를 적용하지 않으므로, 문구가 긴 언어에서 처음 크기가
         * 최소보다 작으면 넓힌다(컨트롤이 잘리지 않게). */
        GetWindowRect(hwnd, &wr);
        min_window_size(&cx, &cy);
        if (wr.right - wr.left < cx || wr.bottom - wr.top < cy) {
            SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left > cx ? wr.right - wr.left : cx,
                         wr.bottom - wr.top > cy ? wr.bottom - wr.top : cy,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_DPICHANGED:
        /* 배율이 다른 모니터로 옮기거나 배율을 바꿨다. 배치 검사는 흉내 낸 DPI 를 유지한다. */
        if (g_app.self_test != SELF_TEST_LAYOUT) {
            apply_dpi(HIWORD(wp), (const RECT *)lp);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        int cx, cy;
        min_window_size(&cx, &cy);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = cx;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = cy;
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        /* 입력 칸·콤보 상자 목록(어둡게일 때 SURFACE 색). */
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        return theme_ctlcolor_static((HDC)wp);
    case WM_SETTINGCHANGE:
        /* Windows 앱 모드(밝게·어둡게)나 고대비가 바뀌었다. 시스템 설정 따르기면 따라간다. */
        if ((lp != 0 && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) ||
            wp == SPI_SETHIGHCONTRAST) {
            apply_theme();
            settings_panel_sync(g_app.pages[NAV_PAGE_SETTINGS],
                                scan_panel_autosave(g_app.pages[NAV_PAGE_SCAN]), 1);
        }
        break;
    case WM_SYSCOLORCHANGE: {
        /* 공용 컨트롤은 시스템 색이 바뀐 것을 부모가 알려 줘야 한다. 그 뒤 밝게 쪽 색을 다시 맞춘다. */
        HWND child;
        for (child = GetWindow(hwnd, GW_CHILD); child != NULL;
             child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SYSCOLORCHANGE, wp, lp);
        }
        apply_theme();
        return 0;
    }
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        /* 강조색(사이드바 선택 막대)이 바뀌었다. */
        InvalidateRect(g_app.nav, NULL, FALSE);
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_NAV && HIWORD(wp) == NVN_SELCHANGE) {
            show_page(nav_view_selected(g_app.nav));
            return 0;
        }
        if (LOWORD(wp) == IDC_SETTINGS) {
            on_settings_notify(HIWORD(wp));
            return 0;
        }
        if (LOWORD(wp) == IDOK) {
            /* 입력 칸에서 Enter: 폴더 스캔은 스캔, 프로젝트 정리는 탐지를 시작한다. */
            if (g_app.page == NAV_PAGE_SCAN) {
                scan_panel_start(g_app.pages[NAV_PAGE_SCAN], NULL, -1);
            } else if (g_app.page == NAV_PAGE_CLEANUP) {
                cleanup_panel_start(g_app.pages[NAV_PAGE_CLEANUP], NULL);
            }
            return 0;
        }
        if (LOWORD(wp) >= IDC_PAGE_BASE && LOWORD(wp) < IDC_PAGE_BASE + NAV_PAGE_COUNT) {
            on_page_notify(LOWORD(wp), HIWORD(wp));
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_SELF_TEST) {
            fwprintf(stderr, L"self-test failed: timeout\n");
            finish_self_test(1);
        }
        return 0;
    case WM_DESTROY:
        /* 패널(자식 창)은 이 창보다 먼저 WM_DESTROY 를 받지 않으므로 여기서는 종료만 알린다.
         * 진행 중인 스캔은 폴더 스캔 패널이 자기 WM_DESTROY 에서 멈추고 기다린다. */
        PostQuitMessage(g_app.self_test_exit);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 자체 검사 인자를 읽는다. 인자가 모자라면 self_test 를 바꾸지 않는다. */
static void parse_args(int argc, wchar_t **argv, const wchar_t **target,
                       const wchar_t **cleanup_target, const wchar_t **lang_tag)
{
    static const struct {
        const wchar_t *flag;
        SelfTest test;
        int args; /* 플래그 뒤 인자 수 */
    } flags[] = {
        {L"--self-test", SELF_TEST_SCAN, 1},
        {L"--self-test-precise", SELF_TEST_PRECISE, 1},
        {L"--self-test-changes", SELF_TEST_CHANGES, 1},
        {L"--self-test-no-autosave", SELF_TEST_NO_AUTOSAVE, 1},
        {L"--self-test-clear-history", SELF_TEST_CLEAR_HISTORY, 1},
        {L"--self-test-export", SELF_TEST_EXPORT, 2},
        {L"--self-test-cancel", SELF_TEST_CANCEL, 1},
        {L"--self-test-drives", SELF_TEST_DRIVES, 0},
        {L"--self-test-cleanup", SELF_TEST_CLEANUP, 1},
        {L"--self-test-cleanup-cancel", SELF_TEST_CLEANUP_CANCEL, 1},
        {L"--self-test-trash", SELF_TEST_TRASH, 1},
        {L"--self-test-quarantine", SELF_TEST_QUARANTINE, 2},
        {L"--self-test-monitor", SELF_TEST_MONITOR, 0},
        {L"--self-test-cleaner", SELF_TEST_CLEANER, 1},
        {L"--self-test-booster", SELF_TEST_BOOSTER, 0},
        {L"--self-test-lang", SELF_TEST_LANG, 1},
        {L"--self-test-nav", SELF_TEST_NAV, 0},
        {L"--self-test-theme", SELF_TEST_THEME, 0},
        {L"--self-test-settings", SELF_TEST_SETTINGS, 0},
        {L"--self-test-lang-switch", SELF_TEST_LANG_SWITCH, 1},
        {L"--self-test-layout", SELF_TEST_LAYOUT, 0},
        {L"--self-test-dpi-change", SELF_TEST_DPI_CHANGE, 0},
    };
    size_t i;

    if (argv == NULL || argc < 2) {
        return;
    }
    for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        if (wcscmp(argv[1], flags[i].flag) == 0) {
            if (argc < 2 + flags[i].args) {
                return;
            }
            g_app.self_test = flags[i].test;
            switch (flags[i].test) {
            case SELF_TEST_SCAN:
            case SELF_TEST_PRECISE:
            case SELF_TEST_CHANGES:
            case SELF_TEST_NO_AUTOSAVE:
            case SELF_TEST_CLEAR_HISTORY:
            case SELF_TEST_CANCEL:
            case SELF_TEST_LANG_SWITCH:
                *target = argv[2];
                break;
            case SELF_TEST_EXPORT:
                *target = argv[2];
                g_app.export_prefix = argv[3];
                break;
            case SELF_TEST_CLEANUP:
            case SELF_TEST_CLEANUP_CANCEL:
                *cleanup_target = argv[2];
                break;
            case SELF_TEST_TRASH:
                *cleanup_target = argv[2];
                g_app.trash_root = argv[2];
                break;
            case SELF_TEST_QUARANTINE:
                *cleanup_target = argv[2];
                g_app.quarantine_root = argv[2];
                g_app.quarantine_conflict = argv[3];
                break;
            case SELF_TEST_CLEANER:
                g_app.cleaner_root = argv[2];
                break;
            case SELF_TEST_LANG:
                *lang_tag = argv[2];
                break;
            default:
                break;
            }
            return;
        }
    }
    if (wcsncmp(argv[1], L"--", 2) != 0) {
        *target = argv[1];
    }
}

/* 자체 검사를 시작한다(창을 만든 뒤). 시작하지 못하면 창을 닫는다. */
static void start_self_test(const wchar_t *target, const wchar_t *cleanup_target)
{
    HWND scan = g_app.pages[NAV_PAGE_SCAN];

    if (g_app.self_test == SELF_TEST_NONE) {
        return;
    }
    /* 자체 검사의 창은 숨겨져 있어 WM_SIZE 를 받지 않을 수 있으므로 직접 배치한다. */
    layout();
    if (g_app.self_test == SELF_TEST_LANG) {
        finish_self_test(verify_lang());
        return;
    }
    if (g_app.self_test == SELF_TEST_LAYOUT) {
        g_app.dry_load = 1; /* 페이지를 오가도 사용자 시스템을 조회하지 않는다 */
        finish_self_test(verify_layout());
        return;
    }
    if (g_app.self_test == SELF_TEST_DPI_CHANGE) {
        g_app.dry_load = 1;
        finish_self_test(verify_dpi_change());
        return;
    }
    if (g_app.self_test == SELF_TEST_NAV) {
        g_app.dry_load = 1;
        finish_self_test(verify_nav());
        return;
    }
    if (g_app.self_test == SELF_TEST_THEME || g_app.self_test == SELF_TEST_SETTINGS) {
        /* 설정을 바꾸므로 테스트 키에서만 한다. 끝나면 테스트 키를 지운다. */
        int rc;
        if (!settings_key_is_test()) {
            fwprintf(stderr, L"self-test failed: BYTESIEVE_SETTINGS_KEY must be under "
                             L"Software\\ByteSieveTest\\ (now %ls)\n",
                     settings_reg_key());
            finish_self_test(1);
            return;
        }
        g_app.dry_load = 1; /* 페이지를 오가도 사용자 시스템을 조회하지 않는다 */
        rc = g_app.self_test == SELF_TEST_THEME ? verify_theme() : verify_settings();
        settings_delete_test_key();
        finish_self_test(rc);
        return;
    }
    SetTimer(g_app.hwnd, TIMER_SELF_TEST, SELF_TEST_TIMEOUT_MS, NULL);
    if (g_app.self_test == SELF_TEST_NO_AUTOSAVE) {
        /* 처음 읽은 설정을 알려(재실행 뒤 유지 확인용) 자동 저장을 끈 다음 스캔한다.
         * 체크박스를 누른 것과 같은 경로를 탄다. */
        wprintf(L"autosave: %d\n", scan_panel_autosave(scan));
        fflush(stdout);
        if (scan_panel_autosave(scan) && !scan_panel_set_autosave(scan, 0)) {
            fwprintf(stderr, L"self-test failed: could not turn off autosave\n");
            finish_self_test(1);
            return;
        }
    }
    if (target != NULL) {
        g_app.export_enabled_before_scan = scan_panel_export_enabled(scan);
        if (!scan_panel_start(scan, target, g_app.self_test == SELF_TEST_PRECISE)) {
            fwprintf(stderr, L"self-test failed: scan did not start\n");
            finish_self_test(1);
            return;
        }
        if (g_app.self_test == SELF_TEST_CANCEL) {
            scan_panel_cancel(scan);
        }
        return;
    }
    switch (g_app.self_test) {
    case SELF_TEST_DRIVES:
        show_page(NAV_PAGE_DRIVES);
        break;
    case SELF_TEST_CLEANER:
        show_page(NAV_PAGE_CLEANER);
        if (!cleaner_panel_busy(g_app.pages[NAV_PAGE_CLEANER])) {
            fwprintf(stderr, L"self-test failed: cleaner analysis did not start\n");
            finish_self_test(1);
        }
        break;
    case SELF_TEST_BOOSTER:
        show_page(NAV_PAGE_BOOSTER);
        break;
    case SELF_TEST_MONITOR:
        show_page(NAV_PAGE_MONITOR);
        if (!monitor_panel_start(g_app.pages[NAV_PAGE_MONITOR])) {
            fwprintf(stderr, L"self-test failed: monitor did not start\n");
            finish_self_test(1);
        }
        break;
    default:
        break;
    }
    if (cleanup_target != NULL) {
        show_page(NAV_PAGE_CLEANUP);
        if (!cleanup_panel_start(g_app.pages[NAV_PAGE_CLEANUP], cleanup_target)) {
            fwprintf(stderr, L"self-test failed: cleanup detection did not start\n");
            finish_self_test(1);
        } else if (g_app.self_test == SELF_TEST_CLEANUP_CANCEL) {
            cleanup_panel_cancel(g_app.pages[NAV_PAGE_CLEANUP]);
        }
    }
}

/*
 * 사용법:
 *   bytesieve-gui [<directory>]          폴더를 채우고 바로 스캔
 *   bytesieve-gui --self-test <dir>       스캔 후 화면을 검증하고 종료(0 성공, 1 실패)
 *   bytesieve-gui --self-test-cancel <dir> 시작 직후 취소한 뒤 화면을 검증하고 종료
 *   bytesieve-gui --self-test-drives      드라이브 페이지를 조회한 뒤 화면을 검증하고 종료
 *   bytesieve-gui --self-test-cleanup <dir>        정리 후보를 탐지한 뒤 화면을 검증하고 종료
 *   bytesieve-gui --self-test-cleanup-cancel <dir> 탐지 시작 직후 취소한 뒤 화면을 검증하고 종료
 *   bytesieve-gui --self-test-trash <dir>          탐지 후 옮길 수 있는 항목을 모두 휴지통으로
 *                                                  옮기고(확인 창 없음) 결과 화면을 검증하고 종료
 *   bytesieve-gui --self-test-quarantine <dir> <file>
 *                                         탐지 후 모두 정리(BYTESIEVE_QUARANTINE_ROOT=<dir> 이면 격리
 *                                         폴더로) → 격리 폴더 페이지 확인 → <file> 을 만든 뒤 모두 복원
 *                                         (그 항목은 같은 이름이라 건너뜀) → 남은 항목 영구 삭제 → 종료
 *   bytesieve-gui --self-test-monitor     모니터 페이지에서 RAM·CPU 를 몇 번 측정한 뒤 화면을 검증하고 종료
 *   bytesieve-gui --self-test-precise <dir> [정밀 스캔]을 체크하고 스캔한 뒤 하드 링크 보정 열·보기·
 *                                          분석 줄을 검증하고 기본·중복 제외 집계를 stdout 에 쓴 뒤 종료
 *   bytesieve-gui --self-test-changes <dir> 스캔 후 직전 스냅숏과 비교한 변화 보기를 검증하고
 *                                          변화 목록을 stdout 에 쓴 뒤 종료(직전 스냅숏 필요)
 *   bytesieve-gui --self-test-no-autosave <dir>
 *                                         처음 읽은 자동 저장 설정을 stdout 에 쓰고 끈 뒤 스캔해
 *                                         기록하지 않았는지 검증하고 종료
 *   bytesieve-gui --self-test-clear-history <dir>
 *                                         스캔(직전 스냅숏 필요) 후 기록을 삭제(확인 창 없음)하고
 *                                         화면 반영을 검증한 뒤 지운 개수를 stdout 에 쓰고 종료
 *   bytesieve-gui --self-test-export <dir> <prefix>
 *                                         스캔 후 <prefix>.csv·<prefix>.html 로 내보내고(대화 상자 없음)
 *                                         화면을 검증한 뒤 폴더 수를 stdout 에 쓰고 종료
 *   bytesieve-gui --self-test-cleaner <dir> 시스템 정리 페이지에서 분석한 위치가 모두 <dir> 안인지 확인한
 *                                         뒤 휴지통 비우기를 뺀 정리 가능한 대상을 정리하고, 이어서 개발 캐시
 *                                         페이지에서 같은 확인 뒤 개발 캐시를 정리하고(확인 창 없음) 결과를
 *                                         검증하고 종료(TEMP·LOCALAPPDATA 등을 <dir> 안으로)
 *   bytesieve-gui --self-test-booster     부스터 페이지의 시작 프로그램·프로세스 목록과 CPU 측정을 검증하고
 *                                         종료(아무것도 바꾸지 않음)
 *   bytesieve-gui --self-test-lang <tag>  <tag> 언어(ko, en, ja, zh-Hans)로 창을 만들어 번역 문구·글꼴과
 *                                         최소 창 크기에서 문구가 잘리지 않는지 검증하고 종료
 *   bytesieve-gui --self-test-layout      네 언어 × 여러 창 크기에서 모든 페이지의 문구 잘림·컨트롤 겹침·패널 밖
 *                                         넘침과 입력값 유지를 검증하고 종료(BYTESIEVE_TEST_DPI 로 DPI 흉내)
 *   bytesieve-gui --self-test-dpi-change  실행 중 DPI 변경(WM_DPICHANGED)마다 글꼴·열 폭·배치·입력값·고른
 *                                         페이지가 맞는지 검증하고 종료
 *   bytesieve-gui --self-test-nav         사이드바 키보드·마우스 이동과 페이지 전환을 검증하고 종료
 *                                         (페이지 조회는 세기만 하고 실제로 하지 않음)
 *   bytesieve-gui --self-test-theme       테마를 바꿔 가며 컨트롤 색·저장 값을 검증하고 종료
 *                                         (BYTESIEVE_SETTINGS_KEY 가 Software\ByteSieveTest\ 아래여야 함)
 *   bytesieve-gui --self-test-settings    [설정]에서 언어 저장·자동 저장 끄고 켜기를 검증하고 종료
 *                                         (위 키 + 테스트용 BYTESIEVE_HISTORY_DIR, BYTESIEVE_LANG 없음)
 *
 * 다른 자체 검사는 화면 문구를 한국어로 확인하므로 표시 언어를 한국어로 고정한다.
 */
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    MSG msg;
    HDC screen;
    wchar_t **argv;
    const wchar_t *target = NULL;
    const wchar_t *cleanup_target = NULL;
    const wchar_t *lang_tag = NULL;
    UiLang lang = UI_LANG_KO;
    HANDLE app_mutex;
    int argc = 0;

    (void)prev;
    (void)cmd;

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    parse_args(argc, argv, &target, &cleanup_target, &lang_tag);
    if (g_app.self_test != SELF_TEST_NONE) {
        /* GUI 서브시스템이라 상속받은 stderr 가 없으면 부모 콘솔에 실패 메시지를 쓴다. */
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if ((err == NULL || err == INVALID_HANDLE_VALUE) &&
            AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE *f;
            _wfreopen_s(&f, L"CONOUT$", L"w", stderr);
        }
    }

    /* 표시 언어: 자체 검사는 화면 문구를 한국어로 확인하므로 한국어로 고정한다
     * (--self-test-lang 은 지정한 언어). 평소에는 환경 변수 → 설치 언어 → Windows 표시 언어 순. */
    if (g_app.self_test == SELF_TEST_LANG) {
        if (!ui_lang_from_tag(lang_tag, &lang)) {
            fwprintf(stderr, L"self-test failed: unsupported language '%ls'\n", lang_tag);
            LocalFree(argv);
            return 1;
        }
    } else if (g_app.self_test == SELF_TEST_NONE) {
        lang = ui_lang_detect();
    }
    ui_lang_set(lang);
    apply_thread_ui_language(lang);

    /* 테마: 평소와 테마·설정 자체 검사는 저장된 설정, 다른 자체 검사는 화면 색을 고정하려고 밝게. */
    if (g_app.self_test == SELF_TEST_NONE || g_app.self_test == SELF_TEST_THEME ||
        g_app.self_test == SELF_TEST_SETTINGS) {
        g_app.theme_mode = settings_load_theme();
    } else {
        g_app.theme_mode = THEME_MODE_LIGHT;
    }
    theme_set_dark(settings_theme_is_dark(g_app.theme_mode, settings_system_dark(),
                                          settings_high_contrast()));

    /* 매니페스트로 DPI 인식을 켰지만, 매니페스트를 읽지 못하는 환경을 위해 그대로 둔다. */
    SetProcessDPIAware();
    app_mutex = CreateMutexW(NULL, FALSE, BYTESIEVE_APP_MUTEX);
    screen = GetDC(NULL);
    g_app.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    g_app.font_dpi = g_app.dpi;
    /* 배치 검사는 BYTESIEVE_TEST_DPI(96·120·144·192 등)로 그 배율의 화면을 흉내 낸다(시스템 DPI 인식이라
     * 실제 배율에서도 시작할 때 읽은 DPI 하나로 모든 크기를 정한다). */
    if (g_app.self_test == SELF_TEST_LAYOUT) {
        wchar_t env[16];
        DWORD n = GetEnvironmentVariableW(L"BYTESIEVE_TEST_DPI", env, 16);
        int dpi = n > 0 && n < 16 ? _wtoi(env) : 0;
        if (dpi >= 72 && dpi <= 480) {
            g_app.dpi = dpi;
        }
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS |
                ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    nav_view_register(inst);
    scan_panel_register(inst);
    drive_panel_register(inst);
    cleanup_panel_register(inst);
    quarantine_panel_register(inst);
    monitor_panel_register(inst);
    cleaner_panel_register(inst);
    booster_panel_register(inst);
    settings_panel_register(inst);
    treemap_view_register(inst);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = host_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PAGE_HOST_CLASS;
    RegisterClassExW(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); /* res/bytesieve.ico */
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = L"ByteSieveMain";
    RegisterClassExW(&wc);

    if (CreateWindowExW(0, wc.lpszClassName, L"ByteSieve", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                        CW_USEDEFAULT, CW_USEDEFAULT, scale(1240), scale(720), NULL, NULL,
                        inst, NULL) == NULL) {
        LocalFree(argv);
        return 1;
    }
    /* 모니터별 DPI 인식(매니페스트 PerMonitorV2): 창이 뜬 모니터의 DPI 가 시작할 때 읽은 시스템 DPI 와 다르면
     * 그 DPI 로 다시 잡는다(창 크기도 같은 비율). 자체 검사는 시스템 DPI(또는 흉내 낸 DPI)로 고정한다. */
    if (g_app.self_test == SELF_TEST_NONE) {
        int window_dpi = (int)GetDpiForWindow(g_app.hwnd);
        if (window_dpi > 0 && window_dpi != g_app.dpi) {
            RECT wr;
            GetWindowRect(g_app.hwnd, &wr);
            wr.right = wr.left + MulDiv(wr.right - wr.left, window_dpi, g_app.dpi);
            wr.bottom = wr.top + MulDiv(wr.bottom - wr.top, window_dpi, g_app.dpi);
            apply_dpi(window_dpi, &wr);
        }
    }
    /* Windows 11 이면 둥근 모서리·Mica 제목 표시줄(지원하지 않으면 그대로). */
    theme_apply_frame(g_app.hwnd);
    /* 밝게·어둡게를 모든 컨트롤에 적용한다(어두운 제목 표시줄 포함). */
    apply_theme();
    /* 자체 검사는 화면에 띄우지 않고(컨트롤은 그대로 동작) 진행한다. */
    ShowWindow(g_app.hwnd, g_app.self_test != SELF_TEST_NONE ? SW_HIDE : show);
    UpdateWindow(g_app.hwnd);

    if (g_app.self_test != SELF_TEST_NONE) {
        start_self_test(target, cleanup_target);
    } else if (target != NULL) {
        scan_panel_start(g_app.pages[NAV_PAGE_SCAN], target, -1);
    }
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_app.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    /* 자체 검사의 경로 인자(quarantine_root 등)를 루프 동안 쓰므로 끝난 뒤 해제한다. */
    LocalFree(argv);
    DeleteObject(g_app.font);
    if (g_app.base_font != NULL && g_app.base_font != g_app.font) {
        DeleteObject(g_app.base_font);
    }
    if (g_app.title_font != NULL) {
        DeleteObject(g_app.title_font);
    }
    CoUninitialize();
    if (app_mutex != NULL) {
        CloseHandle(app_mutex);
    }
    return (int)msg.wParam;
}
