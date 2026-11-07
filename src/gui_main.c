#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "gui_booster.h"
#include "gui_cleaner.h"
#include "gui_cleanup.h"
#include "gui_drives.h"
#include "gui_monitor.h"
#include "gui_nav.h"
#include "gui_quarantine.h"
#include "gui_scan.h"
#include "gui_theme.h"
#include "gui_treemap.h"
#include "nav_model.h"
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
 */

/* 공용 컨트롤 v6(비주얼 스타일). */
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* --self-test 의 전체 제한 시간. */
#define SELF_TEST_TIMEOUT_MS 60000
#define TIMER_SELF_TEST 1
/* --self-test-monitor 가 검증 전에 기다리는 측정 횟수. */
#define MONITOR_SELF_TEST_SAMPLES 4

enum {
    IDC_NAV = 100,
    IDC_TITLE,
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
    SELF_TEST_NAV     /* 사이드바 키보드·마우스 이동 → 페이지 전환·제목·처음 열 때 조회 횟수 검증 → 종료 */
} SelfTest;

typedef struct {
    HWND hwnd;
    HWND nav;
    HWND title;
    HWND pages[NAV_PAGE_COUNT];
    NavPage page; /* 보이는 페이지 */
    HFONT font;
    HFONT title_font;
    int dpi;

    /* 페이지를 열 때 시작한 조회(처음 열 때 조회·열 때마다 다시 읽기) 횟수. */
    unsigned loads[NAV_PAGE_COUNT];
    int dry_load; /* SELF_TEST_NAV: 조회를 세기만 하고 실제로 시작하지 않음(사용자 시스템을 읽지 않음) */

    SelfTest self_test;
    int self_test_exit;
    size_t trash_expect; /* SELF_TEST_TRASH·QUARANTINE: 선택해 옮기기 시작한 항목 수 */
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
#define MAIN_TITLE_H 44

static int nav_width(void)
{
    return g_app.nav != NULL ? nav_view_width(g_app.nav) : scale(200);
}

/*
 * 최소 창 크기: 기본 800×380(96 DPI 기준)에서, 문구가 긴 언어에서도 폴더 스캔 페이지의 두 줄이
 * 패널 안에 들어가도록 넓힌다(사이드바 폭 + 여백 + 패널 최소 폭).
 */
static void min_window_size(int *cx, int *cy)
{
    RECT frame = {0, 0, 0, 0};
    int need;

    *cx = scale(800);
    *cy = scale(380);
    if (g_app.pages[NAV_PAGE_SCAN] == NULL) {
        return; /* 컨트롤을 만들기 전(WM_CREATE 이전) */
    }
    need = nav_width() + 2 * scale(MAIN_MARGIN) + scan_panel_min_width(g_app.pages[NAV_PAGE_SCAN]);
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
    need += frame.right - frame.left;
    if (need > *cx) {
        *cx = need;
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

static void layout(void)
{
    RECT rc, page;
    int i, m = scale(MAIN_MARGIN);

    GetClientRect(g_app.hwnd, &rc);
    MoveWindow(g_app.nav, 0, 0, nav_width(), rc.bottom, TRUE);
    page_rect(&page);
    MoveWindow(g_app.title, page.left, m, page.right - page.left, scale(MAIN_TITLE_H) - scale(4),
               TRUE);
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        MoveWindow(g_app.pages[i], page.left, page.top, page.right - page.left,
                   page.bottom - page.top, TRUE);
    }
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
    load_on_open(page);
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
static int verify_lang(void)
{
    const wchar_t *face = theme_font_face(ui_lang_current());
    LOGFONTW lf;
    RECT nav, item, page;
    int failures = 0, cx, cy, i;

    memset(&lf, 0, sizeof(lf));
    SELF_CHECK(GetObjectW(g_app.font, sizeof(lf), &lf) == sizeof(lf));
    SELF_CHECK(face == NULL || wcscmp(lf.lfFaceName, face) == 0);
    SELF_CHECK(text_is(g_app.title, ui_text(nav_page_text(NAV_PAGE_SCAN))));

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
    failures += scan_panel_verify_lang(g_app.pages[NAV_PAGE_SCAN]);

    /* 성공했을 때만 요약을 쓴다(CTest 가 이 줄로 통과를 판정한다). */
    if (failures == 0) {
        wprintf(L"lang: %ls, font: %ls, min width: %d\n", ui_lang_name(ui_lang_current()),
                lf.lfFaceName, cx);
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
    SELF_CHECK(g_app.page == NAV_PAGE_BOOSTER && steps == NAV_PAGE_COUNT);
    /* ↑ 한 번, Home, End */
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_UP, 0);
    failures += verify_page_shown(NAV_PAGE_MONITOR);
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_HOME, 0);
    failures += verify_page_shown(NAV_PAGE_SCAN);
    SendMessageW(g_app.nav, WM_KEYDOWN, VK_END, 0);
    failures += verify_page_shown(NAV_PAGE_BOOSTER);

    /* 마우스: 모든 페이지를 차례로 누른다(두 번째 방문) */
    for (p = 0; p < NAV_PAGE_COUNT; p++) {
        click_nav((NavPage)p);
        failures += verify_page_shown((NavPage)p);
        quarantine_visits += p == NAV_PAGE_QUARANTINE;
    }
    /* 같은 항목을 다시 눌러도 그대로(조회를 다시 하지 않음) */
    click_nav(NAV_PAGE_BOOSTER);
    failures += verify_page_shown(NAV_PAGE_BOOSTER);
    /* 그룹 머리글(첫 항목 바로 위)을 누르면 아무것도 바뀌지 않는다 */
    SELF_CHECK(nav_view_item_rect(g_app.nav, NAV_PAGE_SCAN, &item));
    SendMessageW(g_app.nav, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM((item.left + item.right) / 2, item.top - scale(8)));
    failures += verify_page_shown(NAV_PAGE_BOOSTER);

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

    /* 언어에 맞는 UI 글꼴(한자 모양이 언어마다 달라 글꼴 이름을 고른다). 실패하면 시스템 글꼴. */
    g_app.font = theme_create_ui_font(ui_lang_current());
    if (g_app.font == NULL) {
        NONCLIENTMETRICSW ncm;
        memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_app.font = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    g_app.title_font = theme_create_title_font(g_app.font, g_app.dpi);

    /* 탭 순서가 화면 순서와 같도록 사이드바부터 만든다. */
    g_app.nav = nav_view_create(g_app.hwnd, IDC_NAV, g_app.font, g_app.dpi);
    g_app.title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP |
                                                         SS_CENTERIMAGE | SS_ENDELLIPSIS,
                                  0, 0, 0, 0, g_app.hwnd, (HMENU)(INT_PTR)IDC_TITLE, inst, NULL);
    SendMessageW(g_app.title, WM_SETFONT,
                 (WPARAM)(g_app.title_font != NULL ? g_app.title_font : g_app.font), FALSE);

    g_app.pages[NAV_PAGE_SCAN] = scan_panel_create(g_app.hwnd, IDC_SCANPAGE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_DRIVES] =
        drive_panel_create(g_app.hwnd, IDC_DRIVES, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CLEANUP] =
        cleanup_panel_create(g_app.hwnd, IDC_CLEANUP, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CLEANER] =
        cleaner_panel_create(g_app.hwnd, IDC_CLEANER, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_CACHE] =
        cleaner_panel_create(g_app.hwnd, IDC_CACHE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_QUARANTINE] =
        quarantine_panel_create(g_app.hwnd, IDC_QUARANTINE, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_MONITOR] =
        monitor_panel_create(g_app.hwnd, IDC_MONITOR, g_app.font, g_app.dpi);
    g_app.pages[NAV_PAGE_BOOSTER] =
        booster_panel_create(g_app.hwnd, IDC_BOOSTER, g_app.font, g_app.dpi);
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
    if (id == IDC_SCANPAGE && code == SPN_FINISHED) {
        finish_self_test(page_exit(scan_panel_verify(g_app.pages[NAV_PAGE_SCAN], scan_verify_mode(),
                                                     g_app.export_prefix,
                                                     g_app.export_enabled_before_scan),
                                   NAV_PAGE_SCAN));
        return 1;
    }
    if (id == IDC_DRIVES && code == DPN_REFRESHED && g_app.self_test == SELF_TEST_DRIVES) {
        finish_self_test(
            page_exit(drive_panel_verify(g_app.pages[NAV_PAGE_DRIVES]), NAV_PAGE_DRIVES));
        return 1;
    }
    if (id == IDC_CLEANUP && code == CPN_FINISHED &&
        (g_app.self_test == SELF_TEST_CLEANUP || g_app.self_test == SELF_TEST_CLEANUP_CANCEL)) {
        finish_self_test(page_exit(
            cleanup_panel_verify(cleanup, g_app.self_test == SELF_TEST_CLEANUP_CANCEL),
            NAV_PAGE_CLEANUP));
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
        finish_self_test(cleanup_panel_verify_trash(cleanup, g_app.trash_expect) == 0 ? 0 : 1);
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
        finish_self_test(
            page_exit(monitor_panel_verify(g_app.pages[NAV_PAGE_MONITOR]), NAV_PAGE_MONITOR));
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
            if (cleaner_panel_verify_run(g_app.pages[page], g_app.cleaner_expect) != 0) {
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
        finish_self_test(
            page_exit(booster_panel_verify(g_app.pages[NAV_PAGE_BOOSTER]), NAV_PAGE_BOOSTER));
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
    case WM_GETMINMAXINFO: {
        int cx, cy;
        min_window_size(&cx, &cy);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = cx;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = cy;
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        return theme_ctlcolor_static((HDC)wp);
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_NAV && HIWORD(wp) == NVN_SELCHANGE) {
            show_page(nav_view_selected(g_app.nav));
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
                *target = argv[2];
                break;
            case SELF_TEST_EXPORT:
                *target = argv[2];
                g_app.export_prefix = argv[3];
                break;
            case SELF_TEST_CLEANUP:
            case SELF_TEST_CLEANUP_CANCEL:
            case SELF_TEST_TRASH:
                *cleanup_target = argv[2];
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
    if (g_app.self_test == SELF_TEST_NAV) {
        g_app.dry_load = 1;
        finish_self_test(verify_nav());
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
 *   bytesieve-gui --self-test-nav         사이드바 키보드·마우스 이동과 페이지 전환을 검증하고 종료
 *                                         (페이지 조회는 세기만 하고 실제로 하지 않음)
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

    SetProcessDPIAware();
    screen = GetDC(NULL);
    g_app.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
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
    treemap_view_register(inst);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = L"ByteSieveMain";
    RegisterClassExW(&wc);

    if (CreateWindowExW(0, wc.lpszClassName, L"ByteSieve", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                        CW_USEDEFAULT, CW_USEDEFAULT, scale(1240), scale(720), NULL, NULL,
                        inst, NULL) == NULL) {
        LocalFree(argv);
        return 1;
    }
    /* Windows 11 이면 둥근 모서리·Mica 제목 표시줄(지원하지 않으면 그대로). */
    theme_apply_frame(g_app.hwnd);
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
    if (g_app.title_font != NULL) {
        DeleteObject(g_app.title_font);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
