#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "devenv_history.h"
#include "gui_cleaner.h"
#include "gui_progress.h"
#include "gui_result.h"
#include "gui_status_text.h"
#include "gui_theme.h"
#include "path.h"
#include "scan_history.h"
#include "size_format.h"
#include "ui_lang.h"
#include "vdisk.h"

#define CLEANER_PANEL_CLASS L"ByteSieveCleanerPanel"
#define WM_APP_SCAN_PROGRESS (WM_APP + 1)
#define WM_APP_SCAN_DONE (WM_APP + 2)
#define WM_APP_RUN_PROGRESS (WM_APP + 3)
#define WM_APP_RUN_DONE (WM_APP + 4)

#define PROGRESS_RANGE 1000
#define FAILURE_LIST_MAX 10

#define CHECK_NONE 0
#define CHECK_OFF 1
#define CHECK_ON 2

enum {
    IDC_C_REFRESH = 100,
    IDC_C_CANCEL,
    IDC_C_STATUS,
    IDC_C_PROGRESS,
    IDC_C_INFO,
    IDC_C_CHECK_ALL,
    IDC_C_CHECK_NONE,
    IDC_C_RUN,
    IDC_C_SELECTION,
    IDC_C_LIST,
    IDC_C_DETAIL,
    IDC_C_RESULT, /* 완료 결과 영역(gui_result) */
    /* 완료 결과 영역의 다음 작업 단추 */
    IDC_C_ACT_FAILURES,
    IDC_C_ACT_RECYCLE,
    IDC_C_ACT_QUARANTINE,
    IDC_C_ACT_ANALYZE
};

/* 목록 열. 첫 열(대상)에 체크 상자가 붙는다. COL_SOURCE 는 개발 도구 캐시의 위치 근거. */
enum {
    COL_TARGET,
    COL_GROUP,
    COL_RECLAIM,
    COL_SIZE,
    COL_GROWTH, /* 개발 캐시·가상 디스크: 직전 기록보다 늘어난·줄어든 용량 */
    COL_FILES,
    COL_STATE,
    COL_EXCLUDED,
    COL_LOCATION,
    COL_SOURCE,
    COL_COUNT
};

typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

/* 상태 줄에 보인 내용의 종류(언어를 바꾸면 render_status 가 새 언어로 다시 쓴다). */
typedef enum {
    CL_STATUS_TEXT,     /* 인자 없는 문구(status_text, UI_T_COUNT 면 빈 줄) */
    CL_STATUS_SCANNING, /* 분석 진행(작업에서 읽음) */
    CL_STATUS_SCANNED,  /* 분석 결과(scanned_*) */
    CL_STATUS_RUNNING,  /* 정리 진행(작업에서 읽음) */
    CL_STATUS_RAN       /* 정리 결과(totals·run_ms) */
} ClStatusKind;

typedef struct {
    HWND hwnd;
    HWND refresh, cancel, status, progress, info, check_all, check_none, run_btn, selection, list,
        detail;
    HWND banner; /* 완료 결과 영역: 정리가 끝나면 보이고, 다시 분석·정리를 시작하면 숨긴다 */
    HFONT font;
    int dpi;

    unsigned kinds;      /* 분석 범위(CleanerOps.kinds, 0 이면 모든 종류) */
    CleanerScanJob *scan;
    CleanerList targets; /* 분석 결과(표시용: 상태·제외·위치 근거) */
    CleanPlan plan;      /* 정리 계획(targets 와 같은 순서): 선택·실행 결과 */
    int has_plan;
    int has_result;
    int loaded;
    ULONGLONG started;

    /* 선택 요약(clean_plan_summarize 에서 옮겨 둔다) */
    size_t checked_count;
    uint64_t checked_bytes;   /* 체크한 대상(휴지통 제외)의 파일 크기 합 */
    uint64_t checked_reclaim; /* 체크한 대상(휴지통 제외)의 실제 확보 가능 합 */
    int recycle_checked;
    size_t caution_checked; /* 체크한 대상 중 주의 수 */
    size_t dev_checked;     /* 체크한 대상 중 직접 골라야 하는 대상(개발 도구 캐시) 수 */
    size_t auto_count;      /* [모두 선택]이 고르는 대상 수. 0 이면 단추를 끈다(개발 캐시 페이지 등) */

    CleanPlanRunJob *run;
    int run_interactive;
    ULONGLONG run_started;
    CleanPlanTotals totals; /* 마지막 실행 합계 */
    int has_run;
    int growth_saved;       /* 마지막 분석에서 증가 기록을 저장했음 */

    /* 단추 폭(지금 글꼴로 잰 문구 폭, 예전 고정 폭이 최소) */
    int w_refresh, w_cancel, w_check_all, w_check_none, w_run;

    /* 상태 줄(render_status) */
    ClStatusKind status_kind;
    UiText status_text;
    size_t scanned_ready;      /* CL_STATUS_SCANNED */
    uint64_t scanned_total;
    int scanned_cancelled;
    uint64_t scanned_ms;
    uint64_t run_ms;           /* CL_STATUS_RAN */

    wchar_t cell[512];
} CleanerPanel;

static const wchar_t *info_text(unsigned kinds);

static CleanerPanel *panel_of(HWND hwnd)
{
    return (CleanerPanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const CleanerPanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

static const CleanerKindInfo *kind_of(const CleanerTarget *t)
{
    return cleaner_kind_info(t->kind);
}

static CleanPlanTarget *plan_at(CleanerPanel *p, size_t i)
{
    return p->has_plan && i < p->plan.len ? &p->plan.items[i] : NULL;
}

static int can_check(CleanerPanel *p, size_t i)
{
    return clean_plan_selectable(plan_at(p, i));
}

static int is_checked(CleanerPanel *p, size_t i)
{
    const CleanPlanTarget *t = plan_at(p, i);
    return t != NULL && t->selected;
}

/* Windows 오류 설명. 앱 언어로 먼저 찾고, 그 언어가 Windows 에 없으면 Windows 기본 언어로. */
static void error_text(uint32_t code, wchar_t *buf, size_t len)
{
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             code, ui_lang_langid(ui_lang_current()), buf, (DWORD)len, NULL);
    if (n == 0) {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0,
                           buf, (DWORD)len, NULL);
    }
    while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L'.' ||
                     buf[n - 1] == L' ')) {
        n--;
    }
    buf[n] = L'\0';
}

/*
 * 대상 이름(지금 언어): 개발 캐시는 도구·캐시 이름, 브라우저는 프로필 수, 가상 디스크는 배포판 이름을
 * 덧붙인다. core 가 넣은 label 은 한국어라 번역할 수 있는 것은 종류·수로 다시 만든다.
 */
static void target_text(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    const wchar_t *name = gui_cleaner_kind_name(t->kind);

    if (t->kind == CLEANER_DEV_CACHE && t->label != NULL && dev_cache_kind_info(t->dev_kind) != NULL) {
        swprintf(buf, len, L"%ls %ls", dev_cache_kind_info(t->dev_kind)->tool,
                 gui_dev_cache_label(t->dev_kind));
    } else if (t->profiles > 0) {
        wchar_t profiles[64];
        swprintf(profiles, 64, ui_text(UI_T_K_PROFILES), t->profiles);
        swprintf(buf, len, L"%ls (%ls)", name, profiles);
    } else if (t->label != NULL) {
        swprintf(buf, len, L"%ls (%ls)", name, t->label);
    } else {
        swprintf(buf, len, L"%ls", name);
    }
}

/* 위치 칸: 휴지통은 "모든 드라이브"(지금 언어), 그 밖은 폴더·파일 경로. */
static const wchar_t *location_text(const CleanerTarget *t)
{
    if (t->kind == CLEANER_RECYCLE_BIN) {
        return ui_text(UI_T_K_ALL_DRIVES);
    }
    return t->location != NULL ? t->location : L"";
}

static void state_text(CleanerPanel *p, size_t i, wchar_t *buf, size_t len)
{
    const CleanerTarget *t = &p->targets.items[i];
    wchar_t size[32], why[160];

    if (p->has_run && plan_at(p, i) != NULL && plan_at(p, i)->ran) {
        const CleanPlanTarget *r = plan_at(p, i);
        if (kind_of(t)->permanent) {
            if (r->moved > 0) {
                format_size(r->moved_bytes, size, 32);
                swprintf(buf, len, ui_text(UI_T_CL_STATE_EMPTIED), size);
            } else {
                error_text(p->totals.permanent_code, why, 160);
                swprintf(buf, len, ui_text(UI_T_CL_STATE_EMPTY_FAILED),
                         why[0] ? why : ui_text(UI_T_CL_ERROR_WORD), (unsigned)p->totals.permanent_code);
            }
            return;
        }
        format_size(r->moved_bytes, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_STATE_DONE), (unsigned)r->moved, size,
                 r->quarantined > 0 ? ui_text(UI_T_CL_PART_QUARANTINE) : L"",
                 r->failed + r->skipped + r->cancelled > 0 ? L" ·" : L"");
        if (r->skipped > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, ui_text(UI_T_CL_PART_SKIPPED), (unsigned)r->skipped);
        }
        if (r->failed > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, ui_text(UI_T_CL_PART_FAILED), (unsigned)r->failed);
        }
        if (r->cancelled > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, ui_text(UI_T_CL_PART_CANCELLED), (unsigned)r->cancelled);
        }
        return;
    }
    if (t->status == CLEANER_ERROR) {
        swprintf(buf, len, L"%ls (0x%08X)", gui_cleaner_status_text(t->status), (unsigned)t->code);
    } else if (t->kind == CLEANER_DEV_CACHE && t->dev_status != DEV_CACHE_FOUND) {
        swprintf(buf, len, L"%ls", gui_dev_cache_status_text(t->dev_status));
    } else if (t->status == CLEANER_READY && kind_of(t)->permanent) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_CL_STATE_PERMANENT));
    } else if (t->status == CLEANER_READY && cleaner_target_caution(t)) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_CL_STATE_CAUTION));
    } else {
        swprintf(buf, len, L"%ls", gui_cleaner_status_text(t->status));
    }
}

static void excluded_text(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    wchar_t size[32];
    size_t n = 0;

    buf[0] = L'\0';
    if (t->recent_count > 0) {
        format_size(t->recent_bytes, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_EXCL_RECENT), (unsigned)t->recent_count, size);
        n = wcslen(buf);
    }
    if (t->denied_count > 0) {
        swprintf(buf + n, len - n, ui_text(UI_T_CL_EXCL_DENIED), n > 0 ? L" · " : L"",
                 (unsigned)t->denied_count);
    }
}

static void recount(CleanerPanel *p)
{
    CleanPlanSummary s;

    clean_plan_summarize(p->has_plan ? &p->plan : NULL, &s);
    p->checked_count = s.targets;
    p->checked_bytes = s.bytes;
    p->checked_reclaim = s.reclaim;
    p->recycle_checked = s.permanent > 0;
    p->caution_checked = s.caution;
    p->dev_checked = s.manual;
    p->auto_count = s.auto_selectable;
}

/* 선택 줄 문구: 선택 수·실제 확보·파일 크기(휴지통 비우기·주의 개수 표시). */
static void selection_text(const CleanerPanel *p, wchar_t *text, size_t len)
{
    wchar_t size[32], reclaim[32];
    size_t n;

    format_size(p->checked_bytes, size, 32);
    format_size(p->checked_reclaim, reclaim, 32);
    swprintf(text, len, ui_text(UI_T_CL_SELECTION), (unsigned)p->checked_count, reclaim, size,
             p->recycle_checked ? ui_text(UI_T_CL_SEL_RECYCLE) : L"");
    if (p->caution_checked > 0) {
        n = wcslen(text);
        swprintf(text + n, len - n, ui_text(UI_T_CL_SEL_CAUTION), (unsigned)p->caution_checked);
    }
}

static void update_selection(CleanerPanel *p)
{
    wchar_t text[256];
    int busy = p->scan != NULL || p->run != NULL;

    selection_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->selection, p->has_plan ? text : L"");
    /* [모두 선택]은 고를 대상(MANUAL 이 아닌 고를 수 있는 대상)이 있을 때만 켠다. 개발 캐시 페이지처럼
     * 모두 직접 골라야 하는 대상이면 눌러도 아무것도 바뀌지 않으므로 끈다(행을 직접 체크). */
    EnableWindow(p->check_all, !busy && p->has_plan && p->auto_count > 0);
    EnableWindow(p->check_none, !busy && p->checked_count > 0);
    EnableWindow(p->run_btn, !busy && p->checked_count > 0);
}

static void set_busy(CleanerPanel *p, int busy)
{
    EnableWindow(p->refresh, !busy);
    EnableWindow(p->cancel, busy);
    update_selection(p);
}

static void set_checked(CleanerPanel *p, size_t i, int on)
{
    clean_plan_set_selected(&p->plan, i, on);
    recount(p);
}

/* 증감 칸: "+1.20 GiB (7일 전보다)", "변화 없음 (3일 전과 같음)", "첫 기록". 추적하지 않으면 빈칸. */
static void growth_text(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    wchar_t size[32];
    FILETIME ft;
    uint64_t now, days = 0;

    buf[0] = L'\0';
    if (!cleaner_target_tracked(t)) {
        return;
    }
    if (!t->has_growth) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_CL_GROWTH_FIRST));
        return;
    }
    GetSystemTimeAsFileTime(&ft);
    now = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (now > t->growth_since) {
        days = (now - t->growth_since) / 864000000000ull;
    }
    if (t->growth == 0) {
        swprintf(buf, len, ui_text(UI_T_CL_GROWTH_SAME), (unsigned long long)days);
        return;
    }
    format_size((uint64_t)(t->growth < 0 ? -t->growth : t->growth), size, 32);
    swprintf(buf, len, ui_text(UI_T_CL_GROWTH_DELTA), t->growth > 0 ? L'+' : L'-', size,
             (unsigned long long)days);
}

/* 마지막 실행에서 옮기지 못한 항목(건너뜀·실패·거부). 아직 시도 안 했거나 취소한 것은 아니다. */
static int entry_unmoved(const CleanPlanEntry *e)
{
    CleanupTrashStatus s = e->outcome.status;

    if (e->skip != CLEAN_SKIP_NONE) {
        return 1;
    }
    return s != CLEANUP_TRASH_PENDING && s != CLEANUP_TRASH_MOVED &&
           s != CLEANUP_TRASH_QUARANTINED && s != CLEANUP_TRASH_CANCELLED;
}

/* 고른 행의 설명·위치·제외 항목·실패 내역. */
static void update_detail(CleanerPanel *p)
{
    int row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED);
    const CleanerTarget *t;
    const wchar_t *note;
    wchar_t text[1600], excl[256], state[256], source[MAX_PATH + 40], shared[200], size[32];
    size_t used, k;

    if (row < 0 || !p->has_plan || (size_t)row >= p->targets.len) {
        SetWindowTextW(p->detail, p->has_result ? ui_text(UI_T_CL_DETAIL_HINT) : L"");
        return;
    }
    t = &p->targets.items[row];
    note = t->kind == CLEANER_DEV_CACHE ? gui_dev_cache_note(t->dev_kind) : gui_cleaner_kind_note(t->kind);
    excluded_text(t, excl, 256);
    state_text(p, (size_t)row, state, 256);
    source[0] = L'\0';
    if (t->kind == CLEANER_DEV_CACHE) {
        wcscpy_s(source, sizeof(source) / sizeof(source[0]), ui_text(UI_T_CL_DETAIL_SOURCE));
        gui_dev_cache_source_text(t->dev_source, t->dev_origin, source + wcslen(source),
                                  sizeof(source) / sizeof(source[0]) - wcslen(source));
    }
    shared[0] = L'\0';
    if (t->shared_bytes > 0) {
        format_size(t->shared_bytes, size, 32);
        swprintf(shared, sizeof(shared) / sizeof(shared[0]), ui_text(UI_T_CL_DETAIL_SHARED), size);
    }
    if (t->kind == CLEANER_VDISK) {
        /* 가상 디스크: 실제 점유와 줄이는 명령 */
        Vdisk v;
        wchar_t hint[1100], alloc[32];
        memset(&v, 0, sizeof(v));
        v.kind = t->vdisk_kind;
        v.path = t->location;
        v.sparse = t->vdisk_sparse;
        vdisk_hint(&v, hint, sizeof(hint) / sizeof(hint[0]));
        format_size(t->vdisk_alloc, alloc, 32);
        for (k = 0; hint[k] != L'\0'; k++) {
            if (hint[k] == L'\n') {
                hint[k] = L' '; /* 정적 칸은 한 덩어리로 보인다 */
            }
        }
        swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_CL_DETAIL_VDISK), note,
                 t->location != NULL ? t->location : ui_text(UI_T_CL_UNKNOWN), alloc,
                 t->vdisk_sparse ? ui_text(UI_T_CL_SPARSE) : L"", hint);
        SetWindowTextW(p->detail, text);
        return;
    }
    swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_CL_DETAIL),
             cleaner_target_caution(t) ? ui_text(UI_T_CL_CAUTION_TAG) : L"", note,
             t->kind == CLEANER_RECYCLE_BIN || t->location != NULL ? location_text(t)
                                                                    : ui_text(UI_T_CL_UNKNOWN),
             source, state, excl[0] ? ui_text(UI_T_CL_EXCL_LABEL) : L"", excl, shared);
    /* 실행 뒤: 이 대상에서 옮기지 못한 항목 몇 개 */
    used = wcslen(text);
    for (k = 0; p->has_run && plan_at(p, (size_t)row)->ran &&
                k < plan_at(p, (size_t)row)->entry_count && used + MAX_PATH + 80 < 1600;
         k++) {
        const CleanPlanEntry *o = &plan_at(p, (size_t)row)->entries[k];
        if (!entry_unmoved(o)) {
            continue;
        }
        swprintf(text + used, 1600 - used, L"\r\n  · %ls — %ls", o->item.path,
                 o->skip != CLEAN_SKIP_NONE ? gui_clean_skip_text(o->skip)
                                            : gui_trash_status_text(o->outcome.status));
        used = wcslen(text);
    }
    SetWindowTextW(p->detail, text);
}

/* ---- 분석 ---- */

static void on_scan_notify(int finished, void *ctx)
{
    PostMessageW((HWND)ctx, finished ? WM_APP_SCAN_DONE : WM_APP_SCAN_PROGRESS, 0, 0);
}

/* ---- 상태 줄 ---- */

/* 상태 줄 문구를 기억해 둔 종류·값과 지금 언어로 만든다. */
static void status_line(CleanerPanel *p, wchar_t *buf, size_t len)
{
    wchar_t size[32], recycle[96];

    buf[0] = L'\0';
    switch (p->status_kind) {
    case CL_STATUS_TEXT:
        if (p->status_text < UI_T_COUNT) {
            swprintf(buf, len, L"%ls", ui_text(p->status_text));
        }
        break;
    case CL_STATUS_SCANNING: {
        CleanerScanSnapshot s;
        if (p->scan == NULL) {
            break;
        }
        cleaner_scan_progress(p->scan, &s);
        format_size(s.bytes, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_SCANNING), gui_cleaner_kind_name(s.kind),
                 (unsigned)s.done + 1, (unsigned)(s.total > 0 ? s.total : 1), size);
        break;
    }
    case CL_STATUS_SCANNED:
        format_size(p->scanned_total, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_SCANNED),
                 ui_text(p->scanned_cancelled ? UI_T_CL_SCAN_CANCELLED_HEAD : UI_T_CL_SCAN_DONE_HEAD),
                 (unsigned)p->scanned_ready, size, (double)p->scanned_ms / 1000.0);
        break;
    case CL_STATUS_RUNNING: {
        CleanPlanSnapshot s;
        if (p->run == NULL) {
            break;
        }
        clean_plan_run_progress(p->run, &s);
        if (s.emptying_recycle) {
            swprintf(buf, len, L"%ls", ui_text(UI_T_CL_EMPTYING));
            break;
        }
        format_size(s.moved_bytes, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_RUNNING), (unsigned)s.done, (unsigned)s.total,
                 (unsigned)s.moved, size, (unsigned)s.failed);
        break;
    }
    case CL_STATUS_RAN:
        recycle[0] = L'\0';
        if (p->totals.permanent_requested) {
            format_size(p->totals.permanent_bytes, size, 32);
            if (p->totals.permanent_done) {
                swprintf(recycle, 96, ui_text(UI_T_CL_RAN_RECYCLE_DONE), size);
            } else {
                swprintf(recycle, 96, L"%ls", ui_text(UI_T_CL_RAN_RECYCLE_FAILED));
            }
        }
        format_size(p->totals.moved_bytes, size, 32);
        swprintf(buf, len, ui_text(UI_T_CL_RAN), (unsigned)p->totals.moved, size, recycle,
                 (unsigned)p->totals.skipped, (unsigned)p->totals.failed,
                 p->totals.was_cancelled ? ui_text(UI_T_CL_RAN_CANCELLED) : L"",
                 (double)p->run_ms / 1000.0);
        break;
    }
}

static void render_status(CleanerPanel *p)
{
    wchar_t text[512];

    status_line(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->status, text);
}

static void show_status(CleanerPanel *p, ClStatusKind kind)
{
    p->status_kind = kind;
    render_status(p);
}

/* 인자 없는 문구를 상태 줄에 보인다. */
static void show_text(CleanerPanel *p, UiText id)
{
    p->status_text = id;
    show_status(p, CL_STATUS_TEXT);
}

static void update_scan_progress(CleanerPanel *p)
{
    CleanerScanSnapshot s;

    if (p->scan == NULL) {
        return;
    }
    cleaner_scan_progress(p->scan, &s);
    if (s.cancel_requested) {
        return;
    }
    if (s.total > 0) {
        progress_set(p->progress, PROGRESS_RUNNING, (int)(s.done * PROGRESS_RANGE / s.total),
                     PROGRESS_RANGE);
    } else if (progress_state(p->progress) != PROGRESS_BUSY) {
        progress_set(p->progress, PROGRESS_BUSY, 0, PROGRESS_RANGE);
    }
    show_status(p, CL_STATUS_SCANNING);
}

static void free_result(CleanerPanel *p)
{
    memset(&p->totals, 0, sizeof(p->totals));
    p->has_run = 0;
}

static void free_plan(CleanerPanel *p)
{
    clean_plan_free(&p->plan);
    p->has_plan = 0;
}

/*
 * 개발 캐시·가상 디스크 증가 추적: 기록 폴더(스캔 기록과 같은 곳)의 devenv-history.tsv 와 비교하고,
 * [스캔 기록 자동 저장]이 켜져 있으면 지금 용량을 더해 저장한다. 실패해도 분석 결과는 그대로 보인다.
 */
static void record_growth(CleanerPanel *p)
{
    wchar_t dir[MAX_PATH * 2], *file;
    DevHistory hist;
    FILETIME ft;
    uint64_t now;
    int record;

    p->growth_saved = 0;
    if (p->targets.cancelled || history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0) {
        return;
    }
    file = devhist_file_in(dir);
    if (file == NULL) {
        return;
    }
    devhist_init(&hist);
    GetSystemTimeAsFileTime(&ft);
    now = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    record = history_autosave_enabled(dir);
    if (devhist_load(&hist, file) == 0 &&
        cleaner_apply_growth(&p->targets, &hist, now, record) > 0 && record &&
        history_ensure_dir(dir) == 0 && devhist_save(&hist, file, NULL) == 0) {
        p->growth_saved = 1;
    }
    devhist_free(&hist);
    free(file);
}

static void finish_scan(CleanerPanel *p)
{
    size_t i, ready = 0;
    uint64_t total = 0;
    int rc;

    if (p->scan == NULL) {
        return;
    }
    rc = cleaner_scan_finish(p->scan, &p->targets);
    p->scan = NULL;
    p->has_result = 1;
    free_plan(p);
    if (rc == 0) {
        record_growth(p);
    }
    if (rc == 0 && cleaner_build_plan(&p->targets, &p->plan) == 0) {
        p->has_plan = 1;
    }
    if (rc != 0 || !p->has_plan) {
        free_plan(p);
        cleaner_list_free(&p->targets);
        show_text(p, UI_T_CL_NO_MEMORY);
        set_busy(p, 0);
        return;
    }
    for (i = 0; i < p->targets.len; i++) {
        const CleanerTarget *t = &p->targets.items[i];
        if (cleaner_target_runnable(t)) {
            ready++;
            if (!kind_of(t)->permanent) {
                total += t->reclaim_bytes;
            }
        }
    }
    /* 결과 문구는 status_line 이 지금 언어로 만든다. */
    p->scanned_ready = ready;
    p->scanned_total = total;
    p->scanned_cancelled = p->targets.cancelled != 0;
    p->scanned_ms = GetTickCount64() - p->started;
    show_status(p, CL_STATUS_SCANNED);
    progress_set(p->progress, p->targets.cancelled ? PROGRESS_CANCELLED : PROGRESS_DONE, 0,
                 PROGRESS_RANGE);
    ListView_SetItemCountEx(p->list, (int)p->targets.len, 0);
    recount(p);
    set_busy(p, 0);
    update_detail(p);
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), CLN_ANALYZED), (LPARAM)p->hwnd);
}

/* ---- 정리 ---- */

static void on_run_notify(int finished, void *ctx)
{
    PostMessageW((HWND)ctx, finished ? WM_APP_RUN_DONE : WM_APP_RUN_PROGRESS, 0, 0);
}

static void update_run_progress(CleanerPanel *p)
{
    CleanPlanSnapshot s;

    if (p->run == NULL) {
        return;
    }
    clean_plan_run_progress(p->run, &s);
    if (s.cancel_requested) {
        return;
    }
    if (s.emptying_recycle) {
        if (progress_state(p->progress) != PROGRESS_BUSY) {
            progress_set(p->progress, PROGRESS_BUSY, 0, PROGRESS_RANGE);
        }
    } else if (s.total > 0) {
        progress_set(p->progress, PROGRESS_RUNNING, (int)(s.done * PROGRESS_RANGE / s.total),
                     PROGRESS_RANGE);
    }
    show_status(p, CL_STATUS_RUNNING);
}

/* 확인 창 1: 고른 대상·실제 확보 용량·되돌리는 방법. 기본 [아니요]. */
static int confirm_run(CleanerPanel *p)
{
    size_t i, cap = 2048 + p->targets.len * 320, used = 0;
    wchar_t *text = malloc(cap * sizeof(*text)), name[160], size[32];
    int answer;

    if (text == NULL) {
        return 0;
    }
#define APPEND(...)                                                                   \
    do {                                                                              \
        int n_ = swprintf(text + used, cap - used, __VA_ARGS__);                      \
        if (n_ > 0) {                                                                 \
            used += (size_t)n_;                                                       \
        }                                                                             \
    } while (0)
    format_size(p->checked_reclaim, size, 32);
    APPEND(ui_text(UI_T_CL_CONFIRM_HEAD), (unsigned)p->checked_count, size,
           p->recycle_checked ? ui_text(UI_T_CL_CONFIRM_RECYCLE) : L"");
    for (i = 0; i < p->targets.len; i++) {
        const CleanerTarget *t = &p->targets.items[i];
        if (!is_checked(p, i)) {
            continue;
        }
        target_text(t, name, 160);
        format_size(kind_of(t)->permanent ? t->bytes : t->reclaim_bytes, size, 32);
        APPEND(L"■ %ls — %ls%ls\n", name, size,
               kind_of(t)->permanent      ? ui_text(UI_T_CL_TAG_PERMANENT)
               : cleaner_target_caution(t) ? ui_text(UI_T_CL_TAG_CAUTION)
                                           : L"");
    }
    if (p->dev_checked > 0) {
        APPEND(ui_text(UI_T_CL_CONFIRM_DEV), (unsigned)p->dev_checked);
        if (p->caution_checked > 0) {
            APPEND(ui_text(UI_T_CL_CONFIRM_DEV_CAUTION), (unsigned)p->caution_checked);
        }
        APPEND(L"\n");
    }
    APPEND(L"%ls", ui_text(UI_T_CL_CONFIRM_INFO));
#undef APPEND
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, L"Cleaner",
                         MB_YESNO | MB_DEFBUTTON2 |
                             (p->caution_checked > 0 ? MB_ICONWARNING : MB_ICONQUESTION));
    free(text);
    return answer == IDYES;
}

/* 확인 창 2(휴지통을 골랐을 때): 1 비우고 진행, 0 휴지통은 빼고 진행, -1 모두 취소. 기본 [아니요]. */
static int confirm_recycle(CleanerPanel *p, const CleanerTarget *t)
{
    wchar_t text[640], size[32];
    int answer;

    format_size(t->bytes, size, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_CL_RECYCLE_CONFIRM),
             (unsigned long long)t->files, size);
    (void)p;
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_CL_RECYCLE_TITLE),
                         MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    return answer == IDYES ? 1 : answer == IDNO ? 0 : -1;
}

static void show_failures(CleanerPanel *p, size_t failed)
{
    size_t t, k, shown = 0, cap = 1024 + (size_t)FAILURE_LIST_MAX * (MAX_PATH + 200), used;
    wchar_t *text = malloc(cap * sizeof(*text)), why[200];
    int n;

    if (text == NULL) {
        return;
    }
    n = swprintf(text, cap, ui_text(UI_T_CL_FAILURES_HEAD), (unsigned)failed);
    used = n > 0 ? (size_t)n : 0;
    if (p->totals.permanent_requested && !p->totals.permanent_done) {
        error_text(p->totals.permanent_code, why, 200);
        n = swprintf(text + used, cap - used, ui_text(UI_T_CL_FAILURES_RECYCLE),
                     why[0] ? why : ui_text(UI_T_CL_ERROR_WORD), (unsigned)p->totals.permanent_code);
        used += n > 0 ? (size_t)n : 0;
    }
    for (t = 0; t < p->plan.len && shown < FAILURE_LIST_MAX; t++) {
        const CleanPlanTarget *pt = &p->plan.items[t];
        for (k = 0; pt->ran && k < pt->entry_count && shown < FAILURE_LIST_MAX; k++) {
            const CleanPlanEntry *o = &pt->entries[k];
            if (!entry_unmoved(o)) {
                continue;
            }
            if (o->skip != CLEAN_SKIP_NONE) {
                swprintf(why, 200, L"%ls", gui_clean_skip_text(o->skip));
            } else if (o->outcome.status == CLEANUP_TRASH_FAILED ||
                       o->outcome.status == CLEANUP_TRASH_QUARANTINE_FAILED) {
                wchar_t err[160];
                error_text(o->outcome.code, err, 160);
                swprintf(why, 200, L"%ls (0x%08X)", err[0] ? err : ui_text(UI_T_CL_MOVE_FAILED),
                         (unsigned)o->outcome.code);
            } else {
                swprintf(why, 200, L"%ls", gui_trash_status_text(o->outcome.status));
            }
            n = swprintf(text + used, cap - used, L"%ls\n    %ls\n", o->item.path, why);
            used += n > 0 ? (size_t)n : 0;
            shown++;
        }
    }
    MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_CL_RESULT_TITLE),
                MB_OK | MB_ICONWARNING);
    free(text);
}

/*
 * 완료 결과 영역: 마지막 정리의 실제 합계(p->totals)로 판정과 요약을 지금 언어로 만들어 보인다.
 * 휴지통·격리 폴더로 옮긴 것은 비우거나 영구 삭제해야 공간이 생기고, 휴지통 비우기(영구 삭제)만 실제로 확보한
 * 용량으로 알린다. 휴지통 비우기 실패도 실패로 센다.
 */
static void render_result(CleanerPanel *p)
{
    const CleanPlanTotals *t = &p->totals;
    wchar_t detail[1024], size[32];
    ResultAction actions[RESULT_BANNER_MAX_ACTIONS];
    int n = 0, bin_failed = t->permanent_requested && !t->permanent_done;
    size_t to_trash = t->moved - t->quarantined;
    OpResult result = op_result_classify(t->moved + (t->permanent_done ? 1 : 0),
                                         t->failed + (bin_failed ? 1 : 0), t->skipped,
                                         t->was_cancelled);

    detail[0] = L'\0';
    if (t->moved > 0) {
        format_size(t->moved_bytes, size, 32);
        result_text_append(detail, 1024, UI_T_RES_MOVED, (unsigned)t->moved, size);
        if (to_trash > 0) {
            result_text_append(detail, 1024, UI_T_RES_WHERE_TRASH);
        }
        if (t->quarantined > 0) {
            result_text_append(detail, 1024, UI_T_RES_WHERE_QUARANTINE, (unsigned)t->quarantined);
        }
    }
    if (t->permanent_done) {
        format_size(t->permanent_bytes, size, 32);
        result_text_append(detail, 1024, UI_T_RES_FREED, size);
    } else if (bin_failed) {
        result_text_append(detail, 1024, UI_T_RES_EMPTY_BIN_FAILED);
    }
    if (t->failed > 0) {
        result_text_append(detail, 1024, UI_T_RES_FAILED_N, (unsigned)t->failed);
    }
    if (t->skipped > 0) {
        result_text_append(detail, 1024, UI_T_RES_SKIPPED_N, (unsigned)t->skipped);
    }
    if (t->was_cancelled) {
        result_text_append(detail, 1024, UI_T_RES_CANCELLED_N, (unsigned)t->cancelled);
    }
    if (detail[0] == L'\0') {
        result_text_append(detail, 1024, UI_T_RES_NONE_DONE);
    }
    if (t->failed + t->skipped > 0 || bin_failed) {
        actions[n].id = IDC_C_ACT_FAILURES, actions[n++].text = UI_T_RES_ACT_FAILURES;
    }
    if (t->quarantined > 0) {
        actions[n].id = IDC_C_ACT_QUARANTINE, actions[n++].text = UI_T_RES_ACT_QUARANTINE;
    }
    if (to_trash > 0 && n < RESULT_BANNER_MAX_ACTIONS) {
        actions[n].id = IDC_C_ACT_RECYCLE, actions[n++].text = UI_T_RES_ACT_RECYCLE;
    }
    if (n < RESULT_BANNER_MAX_ACTIONS) {
        actions[n].id = IDC_C_ACT_ANALYZE, actions[n++].text = UI_T_RES_ACT_ANALYZE;
    }
    result_banner_show(p->banner, result, detail, actions, n);
}

static void finish_run(CleanerPanel *p)
{
    size_t i, failed, skipped;

    if (p->run == NULL) {
        return;
    }
    free_result(p);
    /* 이번 실행에 들지 않은 대상의 지난 결과는 지운다(상태 칸은 마지막 실행만 보인다). */
    for (i = 0; i < p->plan.len; i++) {
        p->plan.items[i].ran = 0;
    }
    clean_plan_run_finish(p->run, &p->plan, &p->totals);
    p->run = NULL;
    p->has_run = 1;
    failed = p->totals.failed;
    skipped = p->totals.skipped;
    recount(p);
    /* 결과 문구는 status_line 이 지금 언어로 만든다(옮긴 용량에 휴지통 비우기는 들어 있지 않고 따로 알린다). */
    p->run_ms = GetTickCount64() - p->run_started;
    show_status(p, CL_STATUS_RAN);
    render_result(p);
    /* 일부라도 실패했거나(휴지통 비우기 실패 포함) 취소했으면 막대 색으로도 알린다. */
    progress_set(p->progress,
                 failed > 0 || (p->totals.permanent_requested && !p->totals.permanent_done)
                     ? PROGRESS_FAILED
                 : p->totals.was_cancelled ? PROGRESS_CANCELLED
                                           : PROGRESS_DONE,
                 0, PROGRESS_RANGE);
    set_busy(p, 0);
    InvalidateRect(p->list, NULL, TRUE);
    update_detail(p);
    if (p->run_interactive &&
        (failed + skipped > 0 || (p->totals.permanent_requested && !p->totals.permanent_done))) {
        show_failures(p, failed + skipped);
    }
    SendMessageW(GetParent(p->hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(p->hwnd), CLN_CLEANED),
                 (LPARAM)p->hwnd);
}

/* ---- 목록 ---- */

static void toggle_rows(CleanerPanel *p, int clicked)
{
    int row, on = -1;

    if (!p->has_plan || p->scan != NULL || p->run != NULL) {
        return;
    }
    if (clicked >= 0) {
        if ((size_t)clicked < p->targets.len) {
            set_checked(p, (size_t)clicked, !is_checked(p, (size_t)clicked));
            ListView_RedrawItems(p->list, clicked, clicked);
        }
    } else {
        for (row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED); row >= 0;
             row = ListView_GetNextItem(p->list, row, LVNI_SELECTED)) {
            if (can_check(p, (size_t)row) && !is_checked(p, (size_t)row)) {
                on = 1;
            }
        }
        for (row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED); row >= 0;
             row = ListView_GetNextItem(p->list, row, LVNI_SELECTED)) {
            set_checked(p, (size_t)row, on == 1);
        }
        InvalidateRect(p->list, NULL, FALSE);
    }
    update_selection(p);
}

static void on_get_dispinfo(CleanerPanel *p, NMLVDISPINFOW *di)
{
    size_t i = (size_t)di->item.iItem;
    const CleanerTarget *t;

    if (!p->has_plan || i >= p->targets.len) {
        return;
    }
    t = &p->targets.items[i];
    if (di->item.mask & LVIF_STATE) {
        int image = can_check(p, i) ? (is_checked(p, i) ? CHECK_ON : CHECK_OFF) : CHECK_NONE;
        di->item.state = INDEXTOSTATEIMAGEMASK(image);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    }
    if (!(di->item.mask & LVIF_TEXT)) {
        return;
    }
    p->cell[0] = L'\0';
    di->item.pszText = p->cell;
    switch (di->item.iSubItem) {
    case COL_TARGET:
        target_text(t, p->cell, 512);
        break;
    case COL_GROUP:
        di->item.pszText = (wchar_t *)gui_cleaner_group_text(kind_of(t)->group);
        break;
    case COL_GROWTH:
        growth_text(t, p->cell, 512);
        break;
    case COL_RECLAIM:
        if (t->status == CLEANER_NOT_FOUND || t->status == CLEANER_ERROR ||
            t->status == CLEANER_INFO_ONLY ||
            (t->kind == CLEANER_DEV_CACHE && t->dev_status != DEV_CACHE_FOUND)) {
            swprintf(p->cell, 64, L"-");
        } else {
            format_size(t->reclaim_bytes, p->cell, 64);
        }
        break;
    case COL_SIZE:
        if (t->status == CLEANER_NOT_FOUND || t->status == CLEANER_ERROR ||
            (t->kind == CLEANER_DEV_CACHE && t->dev_status != DEV_CACHE_FOUND)) {
            swprintf(p->cell, 64, L"-");
        } else {
            format_size(t->bytes, p->cell, 64);
        }
        break;
    case COL_FILES:
        if (t->status == CLEANER_NOT_FOUND || t->status == CLEANER_ERROR) {
            swprintf(p->cell, 64, L"-");
        } else {
            swprintf(p->cell, 64, L"%llu", (unsigned long long)t->files);
        }
        break;
    case COL_STATE:
        state_text(p, i, p->cell, 512);
        break;
    case COL_EXCLUDED:
        excluded_text(t, p->cell, 512);
        break;
    case COL_LOCATION:
        di->item.pszText = (wchar_t *)location_text(t);
        break;
    case COL_SOURCE:
        if (t->kind == CLEANER_DEV_CACHE) {
            gui_dev_cache_source_text(t->dev_source, t->dev_origin, p->cell, 512);
        }
        break;
    }
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(CleanerPanel *p)
{
    RECT rc;
    int h = pscale(p, 24), gap = pscale(p, 6);
    int w, y, x, list_h, detail_h = pscale(p, 68), info_h = pscale(p, 34);

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    {
        /* 안내 문단은 창 폭·언어에 따라 줄 수가 달라지므로, 줄을 바꾼 높이만큼 늘린다(최소 두 줄 자리). */
        wchar_t info[1024];
        int need;
        GetWindowTextW(p->info, info, 1024);
        need = theme_text_wrap_height(p->hwnd, p->font, info, w);
        if (need > info_h) {
            info_h = need;
        }
    }
    y = 0;
    MoveWindow(p->refresh, 0, y, p->w_refresh, h, TRUE);
    MoveWindow(p->cancel, p->w_refresh + gap, y, p->w_cancel, h, TRUE);
    x = p->w_refresh + p->w_cancel + 3 * gap;
    MoveWindow(p->status, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->progress, 0, y, w, pscale(p, 16), TRUE);
    y += pscale(p, 16) + gap;
    {
        /* 완료 결과 영역(보일 때만 자리를 차지한다) */
        int bh = result_banner_height(p->banner, w);
        if (bh > 0) {
            MoveWindow(p->banner, 0, y, w, bh, TRUE);
            y += bh + gap;
        }
    }
    MoveWindow(p->info, 0, y, w, info_h, TRUE);
    y += info_h + gap;
    MoveWindow(p->check_all, 0, y, p->w_check_all, h, TRUE);
    x = p->w_check_all + gap;
    MoveWindow(p->check_none, x, y, p->w_check_none, h, TRUE);
    x += p->w_check_none + gap;
    MoveWindow(p->run_btn, x, y, p->w_run, h, TRUE);
    x += p->w_run + 2 * gap;
    MoveWindow(p->selection, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    list_h = rc.bottom - y - detail_h - gap;
    MoveWindow(p->list, 0, y, w, list_h > 0 ? list_h : 0, TRUE);
    MoveWindow(p->detail, 0, rc.bottom - detail_h, w, detail_h, TRUE);
}

static HWND panel_child(CleanerPanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)p->font, FALSE);
    return h;
}

/* 목록 열: 머리글 문구, 96 DPI 기준 기본 폭(머리글이 더 길면 넓힌다), 정렬. */
static const struct {
    UiText title;
    int width;
    int fmt;
} k_cols[COL_COUNT] = {
    {UI_T_CL_COL_TARGET, 200, LVCFMT_LEFT},   {UI_T_CL_COL_GROUP, 110, LVCFMT_LEFT},
    {UI_T_C_COL_RECLAIM, 90, LVCFMT_RIGHT},   {UI_T_C_COL_SIZE, 90, LVCFMT_RIGHT},
    {UI_T_CL_COL_GROWTH, 150, LVCFMT_LEFT},   {UI_T_COL_FILES, 70, LVCFMT_RIGHT},
    {UI_T_Q_COL_STATE, 220, LVCFMT_LEFT},     {UI_T_CL_COL_EXCLUDED, 220, LVCFMT_LEFT},
    {UI_T_CL_COL_LOCATION, 320, LVCFMT_LEFT}, {UI_T_CL_COL_SOURCE, 200, LVCFMT_LEFT},
};

/* 단추와 문구(왼쪽부터). */
static void button_texts(CleanerPanel *p, HWND *h, UiText *t)
{
    h[0] = p->refresh, t[0] = UI_T_CL_REFRESH;
    h[1] = p->cancel, t[1] = UI_T_SCAN_CANCEL;
    h[2] = p->check_all, t[2] = UI_T_C_CHECK_ALL;
    h[3] = p->check_none, t[3] = UI_T_C_CHECK_NONE;
    h[4] = p->run_btn, t[4] = UI_T_CL_RUN;
}
#define BUTTON_COUNT 5

/* 열 폭: 기본 폭과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(CleanerPanel *p, int col)
{
    int need = theme_text_width(p->list, p->font, ui_text(k_cols[col].title)) + pscale(p, 16);
    return pscale(p, k_cols[col].width) > need ? pscale(p, k_cols[col].width) : need;
}

/* 단추 폭: 예전 고정 폭(84·140)을 최소로 두고(한국어 화면은 그대로), 지금 글꼴로 잰 문구가 더 길면 넓힌다. */
static void measure_buttons(CleanerPanel *p)
{
    p->w_refresh = theme_button_width(p->hwnd, p->font, ui_text(UI_T_CL_REFRESH), p->dpi, pscale(p, 84));
    p->w_cancel = theme_button_width(p->hwnd, p->font, ui_text(UI_T_SCAN_CANCEL), p->dpi, pscale(p, 84));
    p->w_check_all = theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_CHECK_ALL), p->dpi, pscale(p, 84));
    p->w_check_none =
        theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_CHECK_NONE), p->dpi, pscale(p, 84));
    p->w_run = theme_button_width(p->hwnd, p->font, ui_text(UI_T_CL_RUN), p->dpi, pscale(p, 140));
}

static void panel_create_controls(CleanerPanel *p)
{
    size_t i;

    p->refresh = panel_child(p, L"BUTTON", ui_text(UI_T_CL_REFRESH), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_C_REFRESH);
    p->cancel = panel_child(p, L"BUTTON", ui_text(UI_T_SCAN_CANCEL), BS_PUSHBUTTON | WS_TABSTOP, 0,
                            IDC_C_CANCEL);
    p->status = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                            IDC_C_STATUS);
    p->progress = panel_child(p, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, IDC_C_PROGRESS);
    p->info = panel_child(p, L"STATIC", info_text(p->kinds), SS_LEFT, 0, IDC_C_INFO);
    p->check_all = panel_child(p, L"BUTTON", ui_text(UI_T_C_CHECK_ALL), BS_PUSHBUTTON | WS_TABSTOP, 0,
                               IDC_C_CHECK_ALL);
    p->check_none = panel_child(p, L"BUTTON", ui_text(UI_T_C_CHECK_NONE), BS_PUSHBUTTON | WS_TABSTOP,
                                0, IDC_C_CHECK_NONE);
    /* 정리 단추에는 단축 글자(&)를 두지 않는다: 창이 앞으로 올 때 다른 곳에 치던 글자가 눌러
     * 실행하는 일을 막는다(확인 창도 기본은 [아니요]). */
    p->run_btn = panel_child(p, L"BUTTON", ui_text(UI_T_CL_RUN), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_C_RUN);
    p->selection = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                               IDC_C_SELECTION);
    p->banner = result_banner_create(p->hwnd, IDC_C_RESULT, p->font, p->dpi);
    p->list = panel_child(p, WC_LISTVIEWW, L"",
                          LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL |
                              WS_TABSTOP,
                          WS_EX_CLIENTEDGE, IDC_C_LIST);
    p->detail = panel_child(p, L"STATIC", L"", SS_LEFT | SS_EDITCONTROL, WS_EX_STATICEDGE,
                            IDC_C_DETAIL);
    SendMessageW(p->progress, PBM_SETRANGE32, 0, PROGRESS_RANGE);
    ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                   LVS_EX_CHECKBOXES | LVS_EX_INFOTIP);
    ListView_SetCallbackMask(p->list, LVIS_STATEIMAGEMASK);
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)ui_text(k_cols[i].title);
        col.cx = column_width(p, (int)i);
        col.fmt = k_cols[i].fmt;
        SendMessageW(p->list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
    measure_buttons(p);
    set_busy(p, 0);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CleanerPanel *p = panel_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        const PanelParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        p = calloc(1, sizeof(*p));
        if (p == NULL) {
            return FALSE;
        }
        p->hwnd = hwnd;
        p->font = params->font;
        p->dpi = params->dpi;
        p->status_kind = CL_STATUS_TEXT;
        p->status_text = UI_T_COUNT; /* 처음에는 빈 상태 줄 */
        cleaner_list_init(&p->targets);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
        break;
    }
    case WM_CREATE:
        panel_create_controls(p);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        /* 입력 칸·콤보 상자 목록(어둡게일 때 SURFACE 색). */
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        /* 정적 글자·체크 상자 배경을 창 배경색(theme BG)에 맞춘다. */
        return theme_ctlcolor_static((HDC)wp);
    case WM_SIZE:
        panel_layout(p);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_C_REFRESH:
            cleaner_panel_refresh(hwnd);
            return 0;
        case IDC_C_CANCEL:
            cleaner_panel_cancel(hwnd);
            return 0;
        case IDC_C_CHECK_ALL:
            cleaner_panel_check_all(hwnd, 1);
            return 0;
        case IDC_C_CHECK_NONE:
            cleaner_panel_check_all(hwnd, 0);
            return 0;
        case IDC_C_RUN:
            cleaner_panel_run(hwnd, 1);
            return 0;
        case IDC_C_ACT_FAILURES:
            show_failures(p, p->totals.failed + p->totals.skipped);
            return 0;
        case IDC_C_ACT_RECYCLE:
            result_open_recycle_bin(hwnd);
            return 0;
        case IDC_C_ACT_QUARANTINE:
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), CLN_OPEN_QUARANTINE), (LPARAM)hwnd);
            return 0;
        case IDC_C_ACT_ANALYZE:
            cleaner_panel_refresh(hwnd);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (p == NULL || hdr->idFrom != IDC_C_LIST) {
            break;
        }
        if (hdr->code == LVN_GETDISPINFOW) {
            on_get_dispinfo(p, (NMLVDISPINFOW *)lp);
            return 0;
        }
        if (hdr->code == LVN_ITEMCHANGED) {
            update_detail(p);
            return 0;
        }
        if (hdr->code == NM_CLICK || hdr->code == NM_DBLCLK) {
            LVHITTESTINFO ht;
            memset(&ht, 0, sizeof(ht));
            ht.pt = ((NMITEMACTIVATE *)lp)->ptAction;
            if (ListView_HitTest(p->list, &ht) >= 0 && (ht.flags & LVHT_ONITEMSTATEICON)) {
                toggle_rows(p, ht.iItem);
            }
            return 0;
        }
        if (hdr->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)lp)->wVKey == VK_SPACE) {
            toggle_rows(p, -1);
            return 0;
        }
        break;
    }
    case WM_APP_SCAN_PROGRESS:
        if (p != NULL) {
            update_scan_progress(p);
        }
        return 0;
    case WM_APP_SCAN_DONE:
        if (p != NULL) {
            finish_scan(p);
        }
        return 0;
    case WM_APP_RUN_PROGRESS:
        if (p != NULL) {
            update_run_progress(p);
        }
        return 0;
    case WM_APP_RUN_DONE:
        if (p != NULL) {
            finish_run(p);
        }
        return 0;
    case WM_DESTROY:
        if (p != NULL && p->scan != NULL) {
            cleaner_scan_cancel(p->scan);
            cleaner_scan_finish(p->scan, NULL);
            p->scan = NULL;
        }
        if (p != NULL && p->run != NULL) {
            clean_plan_run_cancel(p->run);
            clean_plan_run_finish(p->run, NULL, NULL);
            p->run = NULL;
        }
        return 0;
    case WM_NCDESTROY:
        if (p != NULL) {
            free_result(p);
            free_plan(p);
            cleaner_list_free(&p->targets);
            free(p);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int cleaner_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = CLEANER_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND cleaner_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(WS_EX_CONTROLPARENT, CLEANER_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

/* 패널 위쪽 안내 문구(분석 범위에 맞춤, 지금 언어). */
static const wchar_t *info_text(unsigned kinds)
{
    if (kinds == CLEANER_KINDS_SYSTEM) {
        return ui_text(UI_T_CL_INFO_SYSTEM);
    }
    if (kinds == CLEANER_KINDS_CACHE) {
        return ui_text(UI_T_CL_INFO_CACHE);
    }
    return ui_text(UI_T_CL_INFO_ALL);
}

void cleaner_panel_set_kinds(HWND panel, unsigned kinds)
{
    CleanerPanel *p = panel_of(panel);

    if (p != NULL && p->scan == NULL && p->run == NULL) {
        p->kinds = kinds;
        SetWindowTextW(p->info, info_text(kinds));
        panel_layout(p); /* 안내 문단 길이가 바뀌면 줄 수(높이)도 바뀐다 */
    }
}

unsigned cleaner_panel_kinds(HWND panel)
{
    CleanerPanel *p = panel_of(panel);

    return p != NULL ? p->kinds : 0;
}

int cleaner_panel_refresh(HWND panel)
{
    CleanerPanel *p = panel_of(panel);

    if (p == NULL || p->scan != NULL || p->run != NULL) {
        return 0;
    }
    ListView_SetItemCountEx(p->list, 0, 0);
    free_plan(p);
    free_result(p);
    cleaner_list_free(&p->targets);
    p->has_result = 0;
    p->loaded = 1;
    recount(p);
    progress_set(p->progress, PROGRESS_BUSY, 0, PROGRESS_RANGE); /* 대상 수를 알기 전 */
    update_detail(p);
    p->started = GetTickCount64();
    {
        CleanerOps ops;
        memset(&ops, 0, sizeof(ops));
        ops.kinds = p->kinds;
        result_banner_hide(p->banner); /* 이전 정리 결과는 새 분석과 맞지 않는다 */
        p->scan = cleaner_scan_start(&ops, on_scan_notify, panel);
    }
    if (p->scan == NULL) {
        show_text(p, UI_T_CL_START_FAILED);
        return 0;
    }
    set_busy(p, 1);
    show_text(p, UI_T_CL_ANALYZING);
    return 1;
}

int cleaner_panel_loaded(HWND panel)
{
    CleanerPanel *p = panel_of(panel);
    return p != NULL && p->loaded;
}

int cleaner_panel_busy(HWND panel)
{
    CleanerPanel *p = panel_of(panel);
    return p != NULL && (p->scan != NULL || p->run != NULL);
}

void cleaner_panel_cancel(HWND panel)
{
    CleanerPanel *p = panel_of(panel);

    if (p != NULL && p->scan != NULL) {
        cleaner_scan_cancel(p->scan);
        EnableWindow(p->cancel, FALSE);
        show_text(p, UI_T_SCAN_CANCELLING);
    } else if (p != NULL && p->run != NULL) {
        clean_plan_run_cancel(p->run);
        EnableWindow(p->cancel, FALSE);
        show_text(p, UI_T_CL_CANCELLING_RUN);
    }
}

size_t cleaner_panel_check_all(HWND panel, int check)
{
    CleanerPanel *p = panel_of(panel);

    if (p == NULL || !p->has_plan || p->scan != NULL || p->run != NULL) {
        return 0;
    }
    /* 휴지통 비우기(영구 삭제)와 개발 도구 캐시(MANUAL)는 직접 체크해야 한다. 해제는 모두 한다. */
    clean_plan_select_all(&p->plan, check);
    recount(p);
    InvalidateRect(p->list, NULL, FALSE);
    update_selection(p);
    return p->checked_count;
}

size_t cleaner_panel_check_kind(HWND panel, CleanerKind kind, int check)
{
    CleanerPanel *p = panel_of(panel);
    size_t i;

    if (p == NULL || !p->has_plan || p->scan != NULL || p->run != NULL) {
        return 0;
    }
    for (i = 0; i < p->targets.len; i++) {
        if (p->targets.items[i].kind == kind) {
            clean_plan_set_selected(&p->plan, i, check);
        }
    }
    recount(p);
    InvalidateRect(p->list, NULL, FALSE);
    update_selection(p);
    return p->checked_count;
}

int cleaner_panel_run(HWND panel, int confirm)
{
    CleanerPanel *p = panel_of(panel);
    CleanPlanOps ops;
    size_t i;

    if (p == NULL || !p->has_plan || p->scan != NULL || p->run != NULL) {
        return 0;
    }
    if (p->checked_count == 0) {
        show_text(p, UI_T_CL_NOTHING_CHECKED);
        return 0;
    }
    if (confirm && !confirm_run(p)) {
        show_text(p, UI_T_CL_RUN_CANCELLED);
        return 0;
    }
    memset(&ops, 0, sizeof(ops));
    for (i = 0; i < p->plan.len; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        if (t->selected && (t->flags & CLEAN_PLAN_PERMANENT)) {
            /* 영구 삭제는 확인 창에서 직접 [예]를 고른 경우에만(자체 검사는 고르지 않음). */
            int answer = confirm ? confirm_recycle(p, &p->targets.items[i]) : 0;
            if (answer < 0) {
                show_text(p, UI_T_CL_RUN_CANCELLED);
                return 0;
            }
            if (answer == 1) {
                ops.allow_permanent = 1;
            } else {
                clean_plan_set_selected(&p->plan, i, 0);
            }
        }
    }
    recount(p);
    if (p->checked_count == 0) {
        update_selection(p);
        show_text(p, UI_T_CL_NOTHING_TO_RUN);
        return 0;
    }
    result_banner_hide(p->banner); /* 작업 중에는 진행 막대·상태 줄이 진행을 보인다 */
    p->run = clean_plan_run_start(&p->plan, &ops, on_run_notify, panel);
    if (p->run == NULL) {
        show_text(p, UI_T_CL_RUN_START_FAILED);
        return 0;
    }
    p->run_interactive = confirm;
    p->run_started = GetTickCount64();
    progress_set(p->progress, PROGRESS_IDLE, 0, PROGRESS_RANGE);
    set_busy(p, 1);
    show_text(p, UI_T_CL_RUNNING_START);
    return 1;
}

#define PANEL_CHECK(cond)                                                         \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fwprintf(stderr, L"cleaner self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                                   \
            failures++;                                                           \
        }                                                                         \
    } while (0)

/* 자체 검사 출력용 영문 식별자: 개발 캐시는 캐시 종류("npm", "gradle_wrapper"), 그 밖은 대상 종류. */
static const wchar_t *target_id(const CleanerTarget *t)
{
    if (t->kind == CLEANER_DEV_CACHE && dev_cache_kind_info(t->dev_kind) != NULL) {
        return dev_cache_kind_info(t->dev_kind)->id;
    }
    return kind_of(t)->id;
}

static int is_under(const wchar_t *path, const wchar_t *root)
{
    size_t n = wcslen(root);

    while (n > 0 && path_is_separator(root[n - 1])) {
        n--;
    }
    return path != NULL && _wcsnicmp(path, root, n) == 0 &&
           (path[n] == L'\0' || path_is_separator(path[n]));
}

int cleaner_panel_verify(HWND panel, const wchar_t *expect_root)
{
    CleanerPanel *p = panel_of(panel);
    int failures = 0, recycle_rows = 0, has_system, has_dev;
    size_t i, j, ready = 0, manual_ready = 0, dev_ready = 0;
    wchar_t buf[512], want[512];

    PANEL_CHECK(p != NULL && p->has_plan);
    if (p == NULL || !p->has_plan) {
        return failures;
    }
    /* 분석 범위: [시스템 정리]는 고정 대상 6개, [개발 캐시]는 개발 캐시·가상 디스크만 */
    has_system = cleaner_kinds_include(p->kinds, CLEANER_RECYCLE_BIN);
    has_dev = cleaner_kinds_include(p->kinds, CLEANER_DEV_CACHE);
    /* 계획은 분석 결과와 같은 순서·같은 판정이어야 한다(GUI 와 CLI 가 같은 계획을 쓴다). */
    PANEL_CHECK(p->plan.len == p->targets.len);
    for (i = 0; i < p->plan.len && i < p->targets.len; i++) {
        const CleanPlanTarget *pt = &p->plan.items[i];
        const CleanerTarget *ct = &p->targets.items[i];
        PANEL_CHECK(pt->origin == i && pt->ready == cleaner_target_runnable(ct));
        PANEL_CHECK(pt->reclaim == ct->reclaim_bytes && pt->bytes == ct->bytes);
        PANEL_CHECK(((pt->flags & CLEAN_PLAN_MANUAL) != 0) == cleaner_target_manual_only(ct));
        PANEL_CHECK(((pt->flags & CLEAN_PLAN_CAUTION) != 0) == cleaner_target_caution(ct));
        PANEL_CHECK(wcscmp(pt->id, cleaner_target_id(ct)) == 0);
        PANEL_CHECK(pt->entry_count == ct->entry_count);
    }
    PANEL_CHECK(p->scan == NULL && p->has_result && p->loaded && !p->targets.cancelled);
    PANEL_CHECK(IsWindowEnabled(p->refresh) && !IsWindowEnabled(p->cancel));
    PANEL_CHECK(SendMessageW(p->progress, PBM_GETPOS, 0, 0) == PROGRESS_RANGE);
    PANEL_CHECK(progress_state(p->progress) == PROGRESS_DONE);
    PANEL_CHECK((GetWindowLongW(p->progress, GWL_STYLE) & PBS_MARQUEE) == 0);
    PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->targets.len);
    /* 고정 대상 6개(시스템 정리) + 개발 캐시 하나 이상(개발 캐시) */
    PANEL_CHECK(p->targets.len >=
                (size_t)(has_system ? CLEANER_DEV_CACHE : 0) + (has_dev ? 1u : 0u));
    for (i = 0; i < p->targets.len; i++) {
        const CleanerTarget *t = &p->targets.items[i];
        PANEL_CHECK(cleaner_kinds_include(p->kinds, t->kind)); /* 범위 밖 종류의 행은 없다 */
        if (t->kind == CLEANER_RECYCLE_BIN) {
            recycle_rows++;
            continue;
        }
        /* 실제 사용자 파일을 건드리지 않도록 위치·항목이 모두 픽스처 안인지 먼저 본다. */
        if (t->location != NULL && !is_under(t->location, expect_root)) {
            fwprintf(stderr, L"cleaner self-test: %ls is outside %ls\n", t->location, expect_root);
            failures++;
        }
        for (j = 0; j < t->entry_count; j++) {
            PANEL_CHECK(is_under(t->entries[j].item.path, expect_root));
        }
        ready += cleaner_target_runnable(t);
        /* 개발 도구 캐시: 위치 근거 칸, 위치에 문제가 있는 캐시는 이유와 함께 정리할 수 없음 */
        if (t->kind == CLEANER_DEV_CACHE) {
            ListView_GetItemText(p->list, (int)i, COL_SOURCE, buf, 512);
            gui_dev_cache_source_text(t->dev_source, t->dev_origin, want, 512);
            PANEL_CHECK(wcscmp(buf, want) == 0);
            if (ui_lang_current() == UI_LANG_KO) {
                dev_cache_source_text(t->dev_source, t->dev_origin, want, 512);
                PANEL_CHECK(wcscmp(buf, want) == 0);
            }
            wprintf(L"dev: %ls = %ls (%ls%ls%ls)\n", target_id(t),
                    dev_cache_status_name(t->dev_status),
                    t->dev_source == DEV_CACHE_SRC_ENV      ? L"env"
                    : t->dev_source == DEV_CACHE_SRC_CONFIG ? L"config"
                                                            : L"default",
                    t->dev_origin != NULL && t->dev_source == DEV_CACHE_SRC_ENV ? L" " : L"",
                    t->dev_origin != NULL && t->dev_source == DEV_CACHE_SRC_ENV ? t->dev_origin
                                                                                : L"");
        }
        if (t->kind == CLEANER_DEV_CACHE && t->dev_status != DEV_CACHE_FOUND) {
            PANEL_CHECK(t->status == CLEANER_DENIED && !can_check(p, i) && t->entry_count == 0);
            ListView_GetItemText(p->list, (int)i, COL_STATE, buf, 512);
            PANEL_CHECK(wcscmp(buf, gui_dev_cache_status_text(t->dev_status)) == 0);
            ListView_GetItemText(p->list, (int)i, COL_RECLAIM, buf, 512);
            PANEL_CHECK(wcscmp(buf, L"-") == 0);
            continue;
        }
        /* 증감 칸: 추적 대상은 "첫 기록" 또는 증감, 아니면 빈칸 */
        growth_text(t, want, 512);
        ListView_GetItemText(p->list, (int)i, COL_GROWTH, buf, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (buf[0] != L'\0') == cleaner_target_tracked(t));
        if (cleaner_target_tracked(t)) {
            if (t->has_growth) {
                wprintf(L"growth: %ls = %lld\n", target_id(t), (long long)t->growth);
            } else {
                wprintf(L"growth: %ls = first\n", target_id(t));
            }
        }
        if (t->kind == CLEANER_VDISK) {
            /* 가상 디스크는 분석만: 고를 수 없고 실제 확보는 "-" */
            PANEL_CHECK(t->status == CLEANER_INFO_ONLY && !can_check(p, i) && t->entry_count == 0);
            ListView_GetItemText(p->list, (int)i, COL_RECLAIM, buf, 512);
            PANEL_CHECK(wcscmp(buf, L"-") == 0);
            continue;
        }
        /* 칸 내용: 실제 확보(할당 크기·하드 링크)와 파일 크기 */
        if (t->status != CLEANER_NOT_FOUND && t->status != CLEANER_ERROR) {
            ListView_GetItemText(p->list, (int)i, COL_SIZE, buf, 512);
            format_size(t->bytes, want, 512);
            PANEL_CHECK(wcscmp(buf, want) == 0);
            ListView_GetItemText(p->list, (int)i, COL_RECLAIM, buf, 512);
            format_size(t->reclaim_bytes, want, 512);
            PANEL_CHECK(wcscmp(buf, want) == 0);
            /* 항목마다 센 값의 합이어야 한다 */
            {
                uint64_t sum = 0;
                for (j = 0; j < t->entry_count; j++) {
                    PANEL_CHECK(t->entries[j].item.flags & CLEANUP_ITEM_MEASURED);
                    sum += cleanup_item_reclaim(&t->entries[j].item);
                }
                PANEL_CHECK(sum == t->reclaim_bytes);
            }
            if (t->entry_count > 0) {
                wprintf(L"reclaim: %ls = %llu/%llu shared %llu\n", target_id(t),
                        (unsigned long long)t->reclaim_bytes, (unsigned long long)t->bytes,
                        (unsigned long long)t->shared_bytes);
            }
        }
        ListView_GetItemText(p->list, (int)i, COL_STATE, buf, 512);
        if (t->status == CLEANER_READY && cleaner_target_caution(t)) {
            PANEL_CHECK(wcscmp(buf, ui_text(UI_T_CL_STATE_CAUTION)) == 0);
            wprintf(L"caution: %ls\n", target_id(t));
        } else {
            PANEL_CHECK(wcscmp(buf, gui_cleaner_status_text(t->status)) == 0 ||
                        t->status == CLEANER_ERROR);
        }
        manual_ready += cleaner_target_runnable(t) && cleaner_target_manual_only(t);
        dev_ready += cleaner_target_runnable(t) && t->kind == CLEANER_DEV_CACHE;
    }
    fflush(stdout);
    PANEL_CHECK(recycle_rows == (has_system ? 1 : 0));
    PANEL_CHECK(ready > 0);
    /* [모두 선택]은 휴지통·개발 도구 캐시를 고르지 않는다(직접 체크해야 함) */
    PANEL_CHECK(dev_ready == manual_ready && (dev_ready > 0) == (has_dev != 0));
    /* 단추는 [모두 선택]이 고를 대상이 있을 때만 켜진다(개발 캐시 페이지는 모두 직접 체크해야 하므로 꺼짐) */
    PANEL_CHECK(p->auto_count == ready - manual_ready);
    PANEL_CHECK((IsWindowEnabled(p->check_all) != 0) == (ready - manual_ready > 0));
    PANEL_CHECK(!IsWindowEnabled(p->check_none) && p->checked_count == 0);
    /* 실제 단추 누름(BM_CLICK → WM_COMMAND)으로 고른다. 꺼진 단추는 아무것도 고르지 않는다. */
    SendMessageW(p->check_all, BM_CLICK, 0, 0);
    PANEL_CHECK(p->checked_count == ready - manual_ready);
    PANEL_CHECK((IsWindowEnabled(p->check_none) != 0) == (p->checked_count > 0));
    PANEL_CHECK((IsWindowEnabled(p->run_btn) != 0) == (p->checked_count > 0));
    /* 다시 눌러도 선택은 그대로다 */
    SendMessageW(p->check_all, BM_CLICK, 0, 0);
    PANEL_CHECK(p->checked_count == ready - manual_ready);
    PANEL_CHECK(cleaner_panel_check_all(panel, 1) == ready - manual_ready);
    PANEL_CHECK(!p->recycle_checked && p->dev_checked == 0 && p->caution_checked == 0);
    for (i = 0; i < p->targets.len; i++) {
        if (cleaner_target_manual_only(&p->targets.items[i])) {
            PANEL_CHECK(!is_checked(p, i));
        }
    }
    GetWindowTextW(p->selection, buf, 512);
    selection_text(p, want, 512);
    PANEL_CHECK(wcscmp(buf, want) == 0);
    PANEL_CHECK(wcsstr(buf, ui_text(UI_T_CL_SEL_RECYCLE)) == NULL);
    if (ui_lang_current() == UI_LANG_KO) {
        PANEL_CHECK(wcsstr(buf, L"영구 삭제") == NULL && wcsstr(buf, L"주의") == NULL);
    }
    /* 개발 도구 캐시를 직접 고르면 주의 개수가 요약에 나온다 */
    PANEL_CHECK(cleaner_panel_check_kind(panel, CLEANER_DEV_CACHE, 1) == ready);
    PANEL_CHECK(p->dev_checked == dev_ready && !p->recycle_checked);
    /* 선택 요약은 실제 확보 합을 보인다 */
    {
        uint64_t sum = 0;
        for (i = 0; i < p->targets.len; i++) {
            if (is_checked(p, i)) {
                sum += p->targets.items[i].reclaim_bytes;
            }
        }
        PANEL_CHECK(sum == p->checked_reclaim);
        format_size(sum, want, 512);
        GetWindowTextW(p->selection, buf, 512);
        PANEL_CHECK(wcsstr(buf, want) != NULL);
        selection_text(p, want, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0);
        PANEL_CHECK(ui_lang_current() != UI_LANG_KO || wcsstr(buf, L"실제 확보 ") != NULL);
    }
    if (p->caution_checked > 0) {
        wchar_t head[64];
        GetWindowTextW(p->selection, buf, 512);
        swprintf(head, 64, ui_text(UI_T_CL_SEL_CAUTION), (unsigned)p->caution_checked);
        PANEL_CHECK(wcsstr(buf, head) != NULL);
    }
    /* [선택 해제] 단추는 직접 고른 대상(개발 도구 캐시)까지 모두 풀고, 실행 대상(계획)도 비운다 */
    SendMessageW(p->check_none, BM_CLICK, 0, 0);
    PANEL_CHECK(p->checked_count == 0 && p->dev_checked == 0 && p->caution_checked == 0);
    for (i = 0; i < p->targets.len; i++) {
        PANEL_CHECK(!p->plan.items[i].selected);
    }
    PANEL_CHECK(!IsWindowEnabled(p->check_none) && !IsWindowEnabled(p->run_btn));
    PANEL_CHECK((IsWindowEnabled(p->check_all) != 0) == (ready - manual_ready > 0));
    return failures;
}

int cleaner_panel_verify_run(HWND panel, size_t expect_targets)
{
    CleanerPanel *p = panel_of(panel);
    int failures = 0;
    size_t i, j, selected = 0, moved = 0;
    wchar_t buf[512];

    PANEL_CHECK(p != NULL && p->has_run && p->has_plan);
    if (p == NULL || !p->has_run || !p->has_plan) {
        return failures;
    }
    PANEL_CHECK(p->run == NULL && IsWindowEnabled(p->refresh) && !IsWindowEnabled(p->cancel));
    PANEL_CHECK(!p->totals.permanent_requested && !p->totals.permanent_done);
    /* 정리 결과가 막대 상태로도 보인다(일부 실패면 빨간색). */
    PANEL_CHECK(progress_state(p->progress) ==
                (p->totals.failed > 0 ? PROGRESS_FAILED : PROGRESS_DONE));
    PANEL_CHECK(p->totals.targets == expect_targets);
    for (i = 0; i < p->plan.len; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        if (!t->ran) {
            continue;
        }
        selected++;
        for (j = 0; j < t->entry_count; j++) {
            const CleanPlanEntry *e = &t->entries[j];
            if (e->skip == CLEAN_SKIP_NONE && (e->outcome.status == CLEANUP_TRASH_MOVED ||
                                               e->outcome.status == CLEANUP_TRASH_QUARANTINED)) {
                moved++;
                PANEL_CHECK(GetFileAttributesW(e->item.path) == INVALID_FILE_ATTRIBUTES);
                PANEL_CHECK(e->outcome.recycled_path != NULL &&
                            GetFileAttributesW(e->outcome.recycled_path) !=
                                INVALID_FILE_ATTRIBUTES);
                if (e->outcome.recycled_path != NULL) {
                    wprintf(L"%ls: %ls\n",
                            e->outcome.status == CLEANUP_TRASH_MOVED ? L"recycled" : L"quarantined",
                            e->outcome.recycled_path);
                }
            }
        }
    }
    fflush(stdout);
    PANEL_CHECK(selected == expect_targets);
    PANEL_CHECK(moved > 0 && moved == p->totals.moved);
    GetWindowTextW(p->status, buf, 512);
    {
        wchar_t want[512];
        status_line(p, want, 512);
        PANEL_CHECK(p->status_kind == CL_STATUS_RAN && wcscmp(buf, want) == 0);
    }
    PANEL_CHECK(ui_lang_current() != UI_LANG_KO || wcsncmp(buf, L"정리 완료: ", wcslen(L"정리 완료: ")) == 0);
    for (i = 0; i < p->targets.len; i++) {
        if (p->plan.items[i].ran) {
            wchar_t want[512];
            ListView_GetItemText(p->list, (int)i, COL_STATE, buf, 512);
            state_text(p, i, want, 512);
            PANEL_CHECK(wcscmp(buf, want) == 0);
            PANEL_CHECK(ui_lang_current() != UI_LANG_KO || wcsncmp(buf, L"정리함 ", wcslen(L"정리함 ")) == 0);
            PANEL_CHECK(!can_check(p, i));
        }
    }
    /* 완료 결과 영역: 항목 결과로 센 실제 이동 수와 요약이 같고 판정·제목이 맞다(휴지통 비우기는 하지 않음). */
    {
        wchar_t sentence[256], size[32];
        const CleanPlanTotals *t = &p->totals;
        OpResult want = op_result_classify(moved, t->failed, t->skipped, t->was_cancelled);
        PANEL_CHECK(result_banner_visible(p->banner) && result_banner_result(p->banner) == want);
        PANEL_CHECK((want == OP_RESULT_SUCCESS) == (t->failed + t->skipped == 0 && !t->was_cancelled));
        GetWindowTextW(p->banner, buf, 512);
        PANEL_CHECK(wcscmp(buf, gui_op_result_text(want)) == 0);
        format_size(t->moved_bytes, size, 32);
        swprintf(sentence, 256, ui_text(UI_T_RES_MOVED), (unsigned)moved, size);
        PANEL_CHECK(wcsstr(result_banner_detail(p->banner), sentence) != NULL);
        if (t->quarantined > 0) {
            swprintf(sentence, 256, ui_text(UI_T_RES_WHERE_QUARANTINE), (unsigned)t->quarantined);
            PANEL_CHECK(wcsstr(result_banner_detail(p->banner), sentence) != NULL);
        }
        if (t->skipped > 0) {
            swprintf(sentence, 256, ui_text(UI_T_RES_SKIPPED_N), (unsigned)t->skipped);
            PANEL_CHECK(wcsstr(result_banner_detail(p->banner), sentence) != NULL);
        }
        wprintf(L"result: %ls\n", op_result_name(want));
        fflush(stdout);
    }
    return failures;
}

void cleaner_panel_apply_language(HWND panel, HFONT font)
{
    CleanerPanel *p = panel_of(panel);
    HWND child, h[BUTTON_COUNT];
    UiText t[BUTTON_COUNT];
    int i;

    if (p == NULL) {
        return;
    }
    if (font != NULL) {
        p->font = font;
        for (child = GetWindow(p->hwnd, GW_CHILD); child != NULL; child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SETFONT, (WPARAM)font, FALSE);
        }
    }
    button_texts(p, h, t);
    for (i = 0; i < BUTTON_COUNT; i++) {
        SetWindowTextW(h[i], ui_text(t[i]));
    }
    SetWindowTextW(p->info, info_text(p->kinds));
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        int need = column_width(p, i);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = (wchar_t *)ui_text(k_cols[i].title);
        SendMessageW(p->list, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        /* 새 머리글이 잘리지 않게 넓힌다(사용자가 넓힌 폭은 줄이지 않음). */
        if (ListView_GetColumnWidth(p->list, i) < need) {
            ListView_SetColumnWidth(p->list, i, need);
        }
    }
    measure_buttons(p);
    update_selection(p);
    update_detail(p);
    render_status(p);
    result_banner_set_font(p->banner, p->font);
    if (result_banner_visible(p->banner)) {
        render_result(p); /* 결과 요약·단추를 새 언어로 */
    }
    panel_layout(p);
    /* 목록 칸(대상·분류·상태·제외·증감·위치·근거)은 그릴 때 지금 언어로 만든다(가상 목록). */
    InvalidateRect(p->list, NULL, TRUE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int cleaner_panel_min_width(HWND panel)
{
    CleanerPanel *p = panel_of(panel);
    int a, b, gap;

    if (p == NULL) {
        return 0;
    }
    gap = pscale(p, 6);
    /* 위 단추 둘, 선택 줄 단추 셋(상태·선택 문구는 남는 폭만 쓴다). */
    a = p->w_refresh + p->w_cancel + gap;
    b = p->w_check_all + p->w_check_none + p->w_run + 2 * gap;
    return a > b ? a : b;
}

/* 한글(가-힣)이 들어 있는지. */
static int has_hangul(const wchar_t *s)
{
    for (; *s != L'\0'; s++) {
        if (*s >= 0xAC00 && *s <= 0xD7A3) {
            return 1;
        }
    }
    return 0;
}

int cleaner_panel_verify_lang(HWND panel)
{
    CleanerPanel *p = panel_of(panel);
    wchar_t buf[1600], want[1600];
    HWND h[BUTTON_COUNT];
    UiText t[BUTTON_COUNT];
    RECT r;
    int failures = 0, i, rows, row, other = ui_lang_current() != UI_LANG_KO;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    failures += gui_status_text_verify();
    /* 단추: 지금 언어이고 잘리지 않는다 */
    button_texts(p, h, t);
    for (i = 0; i < BUTTON_COUNT; i++) {
        GetWindowTextW(h[i], buf, 1600);
        PANEL_CHECK(wcscmp(buf, ui_text(t[i])) == 0);
        GetWindowRect(h[i], &r);
        PANEL_CHECK(r.right - r.left >= theme_button_width(p->hwnd, p->font, buf, p->dpi, 0));
    }
    GetWindowTextW(p->info, buf, 1600);
    PANEL_CHECK(wcscmp(buf, info_text(p->kinds)) == 0);
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = buf;
        col.cchTextMax = 1600;
        buf[0] = L'\0';
        SendMessageW(p->list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        PANEL_CHECK(wcscmp(buf, ui_text(k_cols[i].title)) == 0);
        PANEL_CHECK(ListView_GetColumnWidth(p->list, i) > theme_text_width(p->list, p->font, buf));
    }
    /* 상태 줄·선택 줄·상세 설명: 지금 언어로 다시 쓰였다(작업 중에는 부르지 않음) */
    PANEL_CHECK(p->scan == NULL && p->run == NULL);
    status_line(p, want, 1600);
    GetWindowTextW(p->status, buf, 1600);
    PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    if (p->has_plan) {
        selection_text(p, want, 1600);
        GetWindowTextW(p->selection, buf, 1600);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    GetWindowTextW(p->detail, buf, 1600);
    row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED);
    if (row < 0 && p->has_result) {
        PANEL_CHECK(wcscmp(buf, ui_text(UI_T_CL_DETAIL_HINT)) == 0);
        /* 첫 행을 골라 상세 설명도 지금 언어로 나오는지 본다(LVN_ITEMCHANGED → update_detail). */
        if (ListView_GetItemCount(p->list) > 0) {
            ListView_SetItemState(p->list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED);
            GetWindowTextW(p->detail, buf, 1600);
        }
    }
    if (row >= 0 && (size_t)row < p->targets.len) {
        const CleanerTarget *ct = &p->targets.items[row];
        const wchar_t *note = ct->kind == CLEANER_DEV_CACHE ? gui_dev_cache_note(ct->dev_kind)
                                                            : gui_cleaner_kind_note(ct->kind);
        PANEL_CHECK(wcsstr(buf, note) != NULL);
    }
    if (row >= 0 && other) {
        /* 고른 항목의 설명(경로·Windows 오류 설명 밖)에 한글이 없다: 설명 문단으로 확인 */
        const CleanerTarget *ct = &p->targets.items[row];
        const wchar_t *note = ct->kind == CLEANER_DEV_CACHE ? gui_dev_cache_note(ct->dev_kind)
                                                            : gui_cleaner_kind_note(ct->kind);
        PANEL_CHECK(wcsstr(buf, note) != NULL && !has_hangul(note));
    }
    /* 목록의 번역 칸: 대상·분류·상태(실패 아님)·제외·증감·근거 칸에 한글이 없다(경로·이름·Windows 오류 설명 제외) */
    rows = ListView_GetItemCount(p->list);
    for (i = 0; other && i < rows && (size_t)i < p->targets.len; i++) {
        static const int cols[] = {COL_GROUP, COL_EXCLUDED, COL_GROWTH};
        const CleanerTarget *ct = &p->targets.items[i];
        size_t k;
        for (k = 0; k < sizeof(cols) / sizeof(cols[0]); k++) {
            ListView_GetItemText(p->list, i, cols[k], buf, 1600);
            PANEL_CHECK(!has_hangul(buf));
        }
        if (ct->kind != CLEANER_VDISK) { /* 가상 디스크 이름은 배포판 이름 그대로 */
            ListView_GetItemText(p->list, i, COL_TARGET, buf, 1600);
            PANEL_CHECK(!has_hangul(buf));
        }
        if (ct->status != CLEANER_ERROR && !(p->has_run && p->plan.items[i].ran)) {
            ListView_GetItemText(p->list, i, COL_STATE, buf, 1600);
            PANEL_CHECK(!has_hangul(buf));
        }
        if (ct->kind == CLEANER_DEV_CACHE && ct->dev_source == DEV_CACHE_SRC_DEFAULT) {
            ListView_GetItemText(p->list, i, COL_SOURCE, buf, 1600);
            PANEL_CHECK(!has_hangul(buf));
        }
        if (ct->kind == CLEANER_RECYCLE_BIN) {
            ListView_GetItemText(p->list, i, COL_LOCATION, buf, 1600);
            PANEL_CHECK(!has_hangul(buf));
        }
    }
    /* 완료 결과 영역이 보이면 제목이 지금 언어다 */
    if (result_banner_visible(p->banner)) {
        wchar_t title[64];
        GetWindowTextW(p->banner, title, 64);
        PANEL_CHECK(wcscmp(title, gui_op_result_text(result_banner_result(p->banner))) == 0);
    }
    return failures;
}

void cleaner_panel_set_dpi(HWND panel, int dpi)
{
    CleanerPanel *p = panel_of(panel);

    if (p != NULL && dpi > 0) {
        p->dpi = dpi;
        result_banner_set_dpi(p->banner, dpi);
    }
}
