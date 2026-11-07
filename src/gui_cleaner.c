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
#include "path.h"
#include "scan_history.h"
#include "size_format.h"
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
    IDC_C_DETAIL
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

typedef struct {
    HWND hwnd;
    HWND refresh, cancel, status, progress, info, check_all, check_none, run_btn, selection, list,
        detail;
    HFONT font;
    int dpi;

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

    CleanPlanRunJob *run;
    int run_interactive;
    ULONGLONG run_started;
    CleanPlanTotals totals; /* 마지막 실행 합계 */
    int has_run;
    int growth_saved;       /* 마지막 분석에서 증가 기록을 저장했음 */

    wchar_t cell[512];
} CleanerPanel;

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

static void error_text(uint32_t code, wchar_t *buf, size_t len)
{
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             code, 0, buf, (DWORD)len, NULL);
    while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L'.' ||
                     buf[n - 1] == L' ')) {
        n--;
    }
    buf[n] = L'\0';
}

/* 대상 이름: 개발 캐시는 도구·캐시 이름, 브라우저는 프로필 수를 덧붙인다. */
static void target_text(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    if (t->kind == CLEANER_DEV_CACHE && t->label != NULL) {
        swprintf(buf, len, L"%ls", t->label);
    } else if (t->label != NULL) {
        swprintf(buf, len, L"%ls (%ls)", kind_of(t)->name, t->label);
    } else {
        swprintf(buf, len, L"%ls", kind_of(t)->name);
    }
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
                swprintf(buf, len, L"비움(영구 삭제) · %ls", size);
            } else {
                error_text(p->totals.permanent_code, why, 160);
                swprintf(buf, len, L"비우지 못함: %ls (0x%08X)", why[0] ? why : L"오류",
                         (unsigned)p->totals.permanent_code);
            }
            return;
        }
        format_size(r->moved_bytes, size, 32);
        swprintf(buf, len, L"정리함 %u개 · %ls%ls%ls", (unsigned)r->moved, size,
                 r->quarantined > 0 ? L" (일부 격리 폴더)" : L"",
                 r->failed + r->skipped + r->cancelled > 0 ? L" ·" : L"");
        if (r->skipped > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, L" 건너뜀 %u", (unsigned)r->skipped);
        }
        if (r->failed > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, L" 실패 %u", (unsigned)r->failed);
        }
        if (r->cancelled > 0) {
            size_t n = wcslen(buf);
            swprintf(buf + n, len - n, L" 취소 %u", (unsigned)r->cancelled);
        }
        return;
    }
    if (t->status == CLEANER_ERROR) {
        swprintf(buf, len, L"%ls (0x%08X)", cleaner_status_text(t->status), (unsigned)t->code);
    } else if (t->kind == CLEANER_DEV_CACHE && t->dev_status != DEV_CACHE_FOUND) {
        swprintf(buf, len, L"%ls", dev_cache_status_text(t->dev_status));
    } else if (t->status == CLEANER_READY && kind_of(t)->permanent) {
        swprintf(buf, len, L"정리 가능 · 영구 삭제");
    } else if (t->status == CLEANER_READY && cleaner_target_caution(t)) {
        swprintf(buf, len, L"정리 가능 · 주의");
    } else {
        swprintf(buf, len, L"%ls", cleaner_status_text(t->status));
    }
}

static void excluded_text(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    wchar_t size[32];
    size_t n = 0;

    buf[0] = L'\0';
    if (t->recent_count > 0) {
        format_size(t->recent_bytes, size, 32);
        swprintf(buf, len, L"최근 24시간 안에 바뀐 %u개(%ls)", (unsigned)t->recent_count, size);
        n = wcslen(buf);
    }
    if (t->denied_count > 0) {
        swprintf(buf + n, len - n, L"%ls보호·사용 중·확인 불가 %u개", n > 0 ? L" · " : L"",
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
}

static void update_selection(CleanerPanel *p)
{
    wchar_t text[256], size[32], reclaim[32];
    int busy = p->scan != NULL || p->run != NULL;
    size_t n;

    format_size(p->checked_bytes, size, 32);
    format_size(p->checked_reclaim, reclaim, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]), L"선택 %u개 · 실제 확보 %ls (파일 크기 %ls)%ls",
             (unsigned)p->checked_count, reclaim, size,
             p->recycle_checked ? L" + 휴지통 비우기(영구 삭제)" : L"");
    if (p->caution_checked > 0) {
        n = wcslen(text);
        swprintf(text + n, sizeof(text) / sizeof(text[0]) - n, L" · 주의 %u개",
                 (unsigned)p->caution_checked);
    }
    SetWindowTextW(p->selection, p->has_plan ? text : L"");
    EnableWindow(p->check_all, !busy && p->has_plan);
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
        swprintf(buf, len, L"첫 기록");
        return;
    }
    GetSystemTimeAsFileTime(&ft);
    now = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (now > t->growth_since) {
        days = (now - t->growth_since) / 864000000000ull;
    }
    if (t->growth == 0) {
        swprintf(buf, len, L"변화 없음 (%llu일 전과 같음)", (unsigned long long)days);
        return;
    }
    format_size((uint64_t)(t->growth < 0 ? -t->growth : t->growth), size, 32);
    swprintf(buf, len, L"%lc%ls (%llu일 전보다)", t->growth > 0 ? L'+' : L'-', size,
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
        SetWindowTextW(p->detail, p->has_result ? L"항목을 고르면 무엇을 정리하는지와 주의 사항이 "
                                                  L"여기 표시됩니다."
                                                : L"");
        return;
    }
    t = &p->targets.items[row];
    note = t->kind == CLEANER_DEV_CACHE ? dev_cache_kind_info(t->dev_kind)->note : kind_of(t)->note;
    excluded_text(t, excl, 256);
    state_text(p, (size_t)row, state, 256);
    source[0] = L'\0';
    if (t->kind == CLEANER_DEV_CACHE) {
        wcscpy_s(source, sizeof(source) / sizeof(source[0]), L" · 위치 근거: ");
        dev_cache_source_text(t->dev_source, t->dev_origin, source + wcslen(source),
                              sizeof(source) / sizeof(source[0]) - wcslen(source));
    }
    shared[0] = L'\0';
    if (t->shared_bytes > 0) {
        format_size(t->shared_bytes, size, 32);
        swprintf(shared, sizeof(shared) / sizeof(shared[0]),
                 L"\r\n하드 링크로 다른 곳과 나누는 %ls는 지워도 비지 않아 실제 확보에서 뺐습니다.",
                 size);
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
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"%ls\r\n위치: %ls · 실제 점유 %ls%ls\r\n줄이는 방법: %ls", note,
                 t->location != NULL ? t->location : L"(모름)", alloc,
                 t->vdisk_sparse ? L"(스파스)" : L"", hint);
        SetWindowTextW(p->detail, text);
        return;
    }
    swprintf(text, sizeof(text) / sizeof(text[0]), L"%ls%ls\r\n위치: %ls%ls · %ls%ls%ls%ls",
             cleaner_target_caution(t) ? L"[주의] " : L"", note,
             t->location != NULL ? t->location : L"(모름)", source, state,
             excl[0] ? L" · 제외: " : L"", excl, shared);
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
                 o->skip != CLEAN_SKIP_NONE ? clean_skip_text(o->skip)
                                            : cleanup_trash_status_text(o->outcome.status));
        used = wcslen(text);
    }
    SetWindowTextW(p->detail, text);
}

/* ---- 분석 ---- */

static void on_scan_notify(int finished, void *ctx)
{
    PostMessageW((HWND)ctx, finished ? WM_APP_SCAN_DONE : WM_APP_SCAN_PROGRESS, 0, 0);
}

static void update_scan_progress(CleanerPanel *p)
{
    CleanerScanSnapshot s;
    wchar_t text[256], size[32];

    if (p->scan == NULL) {
        return;
    }
    cleaner_scan_progress(p->scan, &s);
    if (s.cancel_requested) {
        return;
    }
    if (s.total > 0) {
        SendMessageW(p->progress, PBM_SETPOS, (WPARAM)(s.done * PROGRESS_RANGE / s.total), 0);
    }
    format_size(s.bytes, size, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]), L"분석하는 중... %ls (%u/%u) · 지금까지 %ls",
             cleaner_kind_info(s.kind)->name, (unsigned)s.done + 1,
             (unsigned)(s.total > 0 ? s.total : 1), size);
    SetWindowTextW(p->status, text);
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
    wchar_t text[256], size[32];

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
        SetWindowTextW(p->status, L"분석 결과를 표시하지 못했습니다(메모리 부족).");
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
    format_size(total, size, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"%ls: 정리할 수 있는 대상 %u개 · 실제 확보 %ls(휴지통 제외) · %.1f초",
             p->targets.cancelled ? L"분석 취소됨(다 세지 못한 대상은 정리할 수 없음)" : L"분석 완료",
             (unsigned)ready, size, (double)(GetTickCount64() - p->started) / 1000.0);
    SetWindowTextW(p->status, text);
    SendMessageW(p->progress, PBM_SETPOS, p->targets.cancelled ? 0 : PROGRESS_RANGE, 0);
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
    wchar_t text[256], size[32];

    if (p->run == NULL) {
        return;
    }
    clean_plan_run_progress(p->run, &s);
    if (s.cancel_requested) {
        return;
    }
    if (s.emptying_recycle) {
        SetWindowTextW(p->status, L"휴지통을 비우는 중...");
        return;
    }
    if (s.total > 0) {
        SendMessageW(p->progress, PBM_SETPOS, (WPARAM)(s.done * PROGRESS_RANGE / s.total), 0);
    }
    format_size(s.moved_bytes, size, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"정리하는 중... %u/%u · 옮김 %u개(%ls) · 건너뜀·실패 %u개", (unsigned)s.done,
             (unsigned)s.total, (unsigned)s.moved, size, (unsigned)s.failed);
    SetWindowTextW(p->status, text);
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
    APPEND(L"선택한 %u개 대상을 정리합니다. 실제 확보 용량: %ls%ls\n\n",
           (unsigned)p->checked_count, size,
           p->recycle_checked ? L" + 휴지통 비우기" : L"");
    for (i = 0; i < p->targets.len; i++) {
        const CleanerTarget *t = &p->targets.items[i];
        if (!is_checked(p, i)) {
            continue;
        }
        target_text(t, name, 160);
        format_size(kind_of(t)->permanent ? t->bytes : t->reclaim_bytes, size, 32);
        APPEND(L"■ %ls — %ls%ls\n", name, size,
               kind_of(t)->permanent      ? L" [영구 삭제]"
               : cleaner_target_caution(t) ? L" [주의]"
                                           : L"");
    }
    if (p->dev_checked > 0) {
        APPEND(L"\n개발 도구 캐시 %u개가 들어 있습니다. 여러 프로젝트가 함께 쓰는 캐시라 정리하면 다음 "
               L"빌드·설치 때 다시 내려받습니다.",
               (unsigned)p->dev_checked);
        if (p->caution_checked > 0) {
            APPEND(L" 이 중 [주의] %u개는 정리하면 인터넷이 없을 때 빌드할 수 없습니다.",
                   (unsigned)p->caution_checked);
        }
        APPEND(L"\n");
    }
    APPEND(L"\n휴지통 비우기를 뺀 항목은 Windows 휴지통(휴지통이 없는 드라이브는 격리 폴더)으로 "
           L"옮기며 영구 삭제하지 않습니다. 공간은 그곳을 비울 때 확보됩니다. 사용 중인 파일과 "
           L"분석 뒤 바뀐 임시 파일은 건너뛰고 항목별로 알려 드립니다. 브라우저 캐시는 그 브라우저가 실행 중이면 정리하지 않습니다.\n"
           L"\n계속할까요?");
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
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"휴지통의 항목 %llu개(%ls)를 영구 삭제합니다.\n\n"
             L"영구 삭제한 파일은 복원할 수 없습니다. ByteSieve가 이전에 휴지통으로 옮긴 항목도 "
             L"함께 지워집니다.\n\n"
             L"[예] 휴지통을 비우고 정리\n[아니요] 휴지통은 그대로 두고 나머지만 정리\n"
             L"[취소] 아무것도 하지 않음",
             (unsigned long long)t->files, size);
    (void)p;
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, L"휴지통 비우기(영구 삭제)",
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
    n = swprintf(text, cap, L"%u개 항목을 정리하지 못했습니다. 이 항목들은 그대로 남아 있습니다.\n"
                            L"사용 중인 프로그램을 닫은 뒤 다시 분석해 정리할 수 있습니다.\n\n",
                 (unsigned)failed);
    used = n > 0 ? (size_t)n : 0;
    if (p->totals.permanent_requested && !p->totals.permanent_done) {
        error_text(p->totals.permanent_code, why, 200);
        n = swprintf(text + used, cap - used, L"휴지통\n    비우지 못함: %ls (0x%08X)\n",
                     why[0] ? why : L"오류", (unsigned)p->totals.permanent_code);
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
                swprintf(why, 200, L"%ls", clean_skip_text(o->skip));
            } else if (o->outcome.status == CLEANUP_TRASH_FAILED ||
                       o->outcome.status == CLEANUP_TRASH_QUARANTINE_FAILED) {
                wchar_t err[160];
                error_text(o->outcome.code, err, 160);
                swprintf(why, 200, L"%ls (0x%08X)", err[0] ? err : L"옮기지 못함",
                         (unsigned)o->outcome.code);
            } else {
                swprintf(why, 200, L"%ls", cleanup_trash_status_text(o->outcome.status));
            }
            n = swprintf(text + used, cap - used, L"%ls\n    %ls\n", o->item.path, why);
            used += n > 0 ? (size_t)n : 0;
            shown++;
        }
    }
    MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, L"Cleaner 결과", MB_OK | MB_ICONWARNING);
    free(text);
}

static void finish_run(CleanerPanel *p)
{
    size_t i, moved, failed, skipped;
    uint64_t bytes;
    wchar_t text[400], size[32], recycle[96];

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
    moved = p->totals.moved;
    failed = p->totals.failed;
    skipped = p->totals.skipped;
    bytes = p->totals.moved_bytes; /* 휴지통 비우기는 들어 있지 않다(따로 알린다) */
    recount(p);
    recycle[0] = L'\0';
    if (p->totals.permanent_requested) {
        format_size(p->totals.permanent_bytes, size, 32);
        if (p->totals.permanent_done) {
            swprintf(recycle, 96, L" · 휴지통 비움(%ls)", size);
        } else {
            swprintf(recycle, 96, L" · 휴지통 비우지 못함");
        }
    }
    format_size(bytes, size, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"정리 완료: %u개 옮김(%ls, 휴지통·격리 폴더를 비우면 확보)%ls · 건너뜀 %u개 · 실패 %u개%ls · %.1f초",
             (unsigned)moved, size, recycle, (unsigned)skipped, (unsigned)failed,
             p->totals.was_cancelled ? L" · 취소됨" : L"",
             (double)(GetTickCount64() - p->run_started) / 1000.0);
    SetWindowTextW(p->status, text);
    SendMessageW(p->progress, PBM_SETPOS, p->totals.was_cancelled ? 0 : PROGRESS_RANGE, 0);
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
        di->item.pszText = (wchar_t *)cleaner_group_text(kind_of(t)->group);
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
        di->item.pszText = t->location != NULL ? t->location : (wchar_t *)L"";
        break;
    case COL_SOURCE:
        if (t->kind == CLEANER_DEV_CACHE) {
            dev_cache_source_text(t->dev_source, t->dev_origin, p->cell, 512);
        }
        break;
    }
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(CleanerPanel *p)
{
    RECT rc;
    int h = pscale(p, 24), bw = pscale(p, 84), gap = pscale(p, 6), run_w = pscale(p, 140);
    int w, y, x, list_h, detail_h = pscale(p, 68);

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    y = 0;
    MoveWindow(p->refresh, 0, y, bw, h, TRUE);
    MoveWindow(p->cancel, bw + gap, y, bw, h, TRUE);
    x = 2 * (bw + gap) + gap;
    MoveWindow(p->status, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->progress, 0, y, w, pscale(p, 16), TRUE);
    y += pscale(p, 16) + gap;
    MoveWindow(p->info, 0, y, w, pscale(p, 34), TRUE);
    y += pscale(p, 34) + gap;
    MoveWindow(p->check_all, 0, y, bw, h, TRUE);
    MoveWindow(p->check_none, bw + gap, y, bw, h, TRUE);
    MoveWindow(p->run_btn, 2 * (bw + gap), y, run_w, h, TRUE);
    x = 2 * (bw + gap) + run_w + 2 * gap;
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

static void panel_create_controls(CleanerPanel *p)
{
    static const struct {
        const wchar_t *title;
        int width;
        int fmt;
    } cols[COL_COUNT] = {
        {L"대상", 200, LVCFMT_LEFT},     {L"분류", 110, LVCFMT_LEFT},
        {L"실제 확보", 90, LVCFMT_RIGHT}, {L"파일 크기", 90, LVCFMT_RIGHT},
        {L"증감", 150, LVCFMT_LEFT},
        {L"파일 수", 70, LVCFMT_RIGHT},   {L"상태", 220, LVCFMT_LEFT},
        {L"제외", 220, LVCFMT_LEFT},     {L"위치", 320, LVCFMT_LEFT},
        {L"위치 근거", 200, LVCFMT_LEFT},
    };
    size_t i;

    p->refresh = panel_child(p, L"BUTTON", L"다시 분석", BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_C_REFRESH);
    p->cancel = panel_child(p, L"BUTTON", L"취소", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_C_CANCEL);
    p->status = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                            IDC_C_STATUS);
    p->progress = panel_child(p, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, IDC_C_PROGRESS);
    p->info = panel_child(p, L"STATIC",
                          L"Windows 임시 파일·오류 보고서·휴지통·브라우저 캐시와 npm·pip·Gradle·"
                          L"Cargo·NuGet 공용 캐시를 분석합니다. [실제 확보]는 디스크 할당 크기 기준이며 "
                          L"하드 링크로 다른 곳과 나누는 파일은 뺍니다. 휴지통 비우기를 뺀 항목은 "
                          L"휴지통(없는 드라이브는 격리 폴더)으로 옮기므로 그곳을 비워야 공간이 확보됩니다. "
                          L"휴지통 비우기(영구 삭제)와 개발 도구 캐시는 [모두 선택]에 들어가지 않습니다.",
                          SS_LEFT, 0, IDC_C_INFO);
    p->check_all = panel_child(p, L"BUTTON", L"모두 선택", BS_PUSHBUTTON | WS_TABSTOP, 0,
                               IDC_C_CHECK_ALL);
    p->check_none = panel_child(p, L"BUTTON", L"선택 해제", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                IDC_C_CHECK_NONE);
    /* 정리 단추에는 단축 글자(&)를 두지 않는다: 창이 앞으로 올 때 다른 곳에 치던 글자가 눌러
     * 실행하는 일을 막는다(확인 창도 기본은 [아니요]). */
    p->run_btn = panel_child(p, L"BUTTON", L"선택 항목 정리...", BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_C_RUN);
    p->selection = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                               IDC_C_SELECTION);
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
        col.pszText = (wchar_t *)cols[i].title;
        col.cx = pscale(p, cols[i].width);
        col.fmt = cols[i].fmt;
        SendMessageW(p->list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
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
        cleaner_list_init(&p->targets);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
        break;
    }
    case WM_CREATE:
        panel_create_controls(p);
        return 0;
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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
    SendMessageW(p->progress, PBM_SETPOS, 0, 0);
    update_detail(p);
    p->started = GetTickCount64();
    p->scan = cleaner_scan_start(NULL, on_scan_notify, panel);
    if (p->scan == NULL) {
        SetWindowTextW(p->status, L"분석을 시작하지 못했습니다(메모리 또는 스레드 생성 실패).");
        return 0;
    }
    set_busy(p, 1);
    SetWindowTextW(p->status, L"분석하는 중...");
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
        SetWindowTextW(p->status, L"취소하는 중...");
    } else if (p != NULL && p->run != NULL) {
        clean_plan_run_cancel(p->run);
        EnableWindow(p->cancel, FALSE);
        SetWindowTextW(p->status, L"취소하는 중... 옮기던 항목과 휴지통 비우기는 끝까지 합니다.");
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
        SetWindowTextW(p->status, L"정리할 대상을 목록에서 체크하세요.");
        return 0;
    }
    if (confirm && !confirm_run(p)) {
        SetWindowTextW(p->status, L"정리를 취소했습니다. 아무것도 바뀌지 않았습니다.");
        return 0;
    }
    memset(&ops, 0, sizeof(ops));
    for (i = 0; i < p->plan.len; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        if (t->selected && (t->flags & CLEAN_PLAN_PERMANENT)) {
            /* 영구 삭제는 확인 창에서 직접 [예]를 고른 경우에만(자체 검사는 고르지 않음). */
            int answer = confirm ? confirm_recycle(p, &p->targets.items[i]) : 0;
            if (answer < 0) {
                SetWindowTextW(p->status, L"정리를 취소했습니다. 아무것도 바뀌지 않았습니다.");
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
        SetWindowTextW(p->status, L"정리할 대상이 없습니다. 아무것도 바뀌지 않았습니다.");
        return 0;
    }
    p->run = clean_plan_run_start(&p->plan, &ops, on_run_notify, panel);
    if (p->run == NULL) {
        SetWindowTextW(p->status, L"정리를 시작하지 못했습니다(메모리 또는 스레드 생성 실패).");
        return 0;
    }
    p->run_interactive = confirm;
    p->run_started = GetTickCount64();
    SendMessageW(p->progress, PBM_SETPOS, 0, 0);
    set_busy(p, 1);
    SetWindowTextW(p->status, L"정리하는 중...");
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
    int failures = 0, recycle_rows = 0;
    size_t i, j, ready = 0, manual_ready = 0, dev_ready = 0;
    wchar_t buf[512], want[512];

    PANEL_CHECK(p != NULL && p->has_plan);
    if (p == NULL || !p->has_plan) {
        return failures;
    }
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
    PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->targets.len);
    PANEL_CHECK(p->targets.len >= CLEANER_DEV_CACHE); /* 고정 대상 6개 + 개발 캐시 */
    for (i = 0; i < p->targets.len; i++) {
        const CleanerTarget *t = &p->targets.items[i];
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
            dev_cache_source_text(t->dev_source, t->dev_origin, want, 512);
            PANEL_CHECK(wcscmp(buf, want) == 0);
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
            PANEL_CHECK(wcscmp(buf, dev_cache_status_text(t->dev_status)) == 0);
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
            PANEL_CHECK(wcscmp(buf, L"정리 가능 · 주의") == 0);
            wprintf(L"caution: %ls\n", target_id(t));
        } else {
            PANEL_CHECK(wcscmp(buf, cleaner_status_text(t->status)) == 0 ||
                        t->status == CLEANER_ERROR);
        }
        manual_ready += cleaner_target_runnable(t) && cleaner_target_manual_only(t);
        dev_ready += cleaner_target_runnable(t) && t->kind == CLEANER_DEV_CACHE;
    }
    fflush(stdout);
    PANEL_CHECK(recycle_rows == 1);
    PANEL_CHECK(ready > 0);
    /* [모두 선택]은 휴지통·개발 도구 캐시를 고르지 않는다(직접 체크해야 함) */
    PANEL_CHECK(dev_ready > 0 && dev_ready == manual_ready);
    PANEL_CHECK(cleaner_panel_check_all(panel, 1) == ready - manual_ready);
    PANEL_CHECK(!p->recycle_checked && p->dev_checked == 0 && p->caution_checked == 0);
    for (i = 0; i < p->targets.len; i++) {
        if (cleaner_target_manual_only(&p->targets.items[i])) {
            PANEL_CHECK(!is_checked(p, i));
        }
    }
    GetWindowTextW(p->selection, buf, 512);
    PANEL_CHECK(wcsstr(buf, L"영구 삭제") == NULL && wcsstr(buf, L"주의") == NULL);
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
        PANEL_CHECK(wcsstr(buf, L"실제 확보 ") != NULL && wcsstr(buf, want) != NULL);
    }
    if (p->caution_checked > 0) {
        GetWindowTextW(p->selection, buf, 512);
        PANEL_CHECK(wcsstr(buf, L"주의") != NULL);
    }
    cleaner_panel_check_all(panel, 0);
    PANEL_CHECK(p->checked_count == 0 && p->dev_checked == 0 && p->caution_checked == 0);
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
    PANEL_CHECK(wcsncmp(buf, L"정리 완료: ", wcslen(L"정리 완료: ")) == 0);
    for (i = 0; i < p->targets.len; i++) {
        if (p->plan.items[i].ran) {
            ListView_GetItemText(p->list, (int)i, COL_STATE, buf, 512);
            PANEL_CHECK(wcsncmp(buf, L"정리함 ", wcslen(L"정리함 ")) == 0);
            PANEL_CHECK(!can_check(p, i));
        }
    }
    return failures;
}
