#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "clean_plan.h"
#include "cleanup_job.h"
#include "cleanup_scan.h"
#include "cleanup_trash.h"
#include "gui_cleanup.h"
#include "gui_progress.h"
#include "gui_status_text.h"
#include "gui_theme.h"
#include "quarantine.h"
#include "size_format.h"
#include "ui_lang.h"

#define CLEANUP_PANEL_CLASS L"ByteSieveCleanupPanel"
#define WM_APP_CLEANUP_PROGRESS (WM_APP + 1)
#define WM_APP_CLEANUP_DONE (WM_APP + 2)
#define WM_APP_TRASH_PROGRESS (WM_APP + 3)
#define WM_APP_TRASH_DONE (WM_APP + 4)

#define PROGRESS_INTERVAL_MS 100
#define PROGRESS_RANGE 1000

/* 확인 창·실패 요약 창에 나열하는 최대 항목 수(나머지는 "외 n개"). */
#define CONFIRM_LIST_MAX 12
#define FAILURE_LIST_MAX 10

/* 체크 상자 상태 그림 번호(0 이면 체크 상자 없음). */
#define CHECK_NONE 0
#define CHECK_OFF 1
#define CHECK_ON 2

enum {
    IDC_C_PATH = 100,
    IDC_C_BROWSE,
    IDC_C_START,
    IDC_C_CANCEL,
    IDC_C_PROGRESS,
    IDC_C_STATUS,
    IDC_C_WARN,
    IDC_C_FILTER_LABEL,
    IDC_C_FILTER,
    IDC_C_SUMMARY,
    IDC_C_LIST,
    IDC_C_CHECK_ALL,
    IDC_C_CHECK_NONE,
    IDC_C_TRASH,
    IDC_C_SELECTION,
    IDC_C_ECO_LABEL,
    IDC_C_ECO
};

/* 목록 열. 첫 열(용량)에 체크 상자가 붙는다. */
/* COL_RECLAIM: 실제 확보 가능(할당 크기, 하드 링크로 항목 밖과 나누는 파일 제외). 정렬은 파일 크기 순. */
/* COL_PROJECT·COL_ACTIVITY·COL_RECOVERY: 프로젝트 분석(project_info.h) — 프로젝트(Git 브랜치), 마지막 활동·휴면,
 * 지운 뒤 복구할 수 있는지. */
enum {
    COL_RECLAIM,
    COL_SIZE,
    COL_TYPE,
    COL_ECO,
    COL_PATH,
    COL_FILES,
    COL_NOTE,
    COL_RECOVERY,
    COL_PROJECT,
    COL_ACTIVITY,
    COL_CLEAN,
    COL_COUNT
};

/*
 * 필터 콤보 상자의 항목 순서와 비트. 이름은 번역 문구(text)이거나, 제품·도구 이름처럼 번역하지 않는
 * 고정 글자(fixed, text 가 UI_T_COUNT 일 때)다.
 */
typedef struct {
    UiText text;
    const wchar_t *fixed;
    unsigned mask;
} FilterItem;

static const FilterItem g_filters[] = {
    {UI_T_C_ALL, NULL, CLEANUP_MASK_ALL},
    {UI_T_COUNT, L"Node.js (node_modules)", CLEANUP_MASK(CLEANUP_NODE_MODULES)},
    {UI_T_C_FILTER_BUILD, NULL, CLEANUP_MASK(CLEANUP_BUILD)},
    {UI_T_C_FILTER_CACHE, NULL, CLEANUP_MASK(CLEANUP_CACHE)},
    {UI_T_C_FILTER_TEMP, NULL, CLEANUP_MASK(CLEANUP_TEMP)},
    {UI_T_C_FILTER_LOG, NULL, CLEANUP_MASK(CLEANUP_LOG)},
};
#define FILTER_COUNT (sizeof(g_filters) / sizeof(g_filters[0]))

/* 개발 환경 필터 콤보 상자의 항목 순서와 비트. */
static const FilterItem g_eco_filters[] = {
    {UI_T_C_ALL, NULL, CLEANUP_ECO_MASK_ALL},
    {UI_T_COUNT, L"Node.js", CLEANUP_ECO_BIT(CLEANUP_ECO_NODE)},
    {UI_T_COUNT, L"Python", CLEANUP_ECO_BIT(CLEANUP_ECO_PYTHON)},
    {UI_T_COUNT, L"Rust", CLEANUP_ECO_BIT(CLEANUP_ECO_RUST)},
    {UI_T_COUNT, L"Java·Kotlin", CLEANUP_ECO_BIT(CLEANUP_ECO_JAVA)},
    {UI_T_COUNT, L".NET·Visual Studio", CLEANUP_ECO_BIT(CLEANUP_ECO_DOTNET)},
    {UI_T_COUNT, L"C/C++", CLEANUP_ECO_BIT(CLEANUP_ECO_CPP)},
    {UI_T_COUNT, L"Go", CLEANUP_ECO_BIT(CLEANUP_ECO_GO)},
    {UI_T_C_ECO_GENERIC, NULL, CLEANUP_ECO_BIT(CLEANUP_ECO_GENERIC)},
};
#define ECO_FILTER_COUNT (sizeof(g_eco_filters) / sizeof(g_eco_filters[0]))

static const wchar_t *filter_label(const FilterItem *f)
{
    return f->text < UI_T_COUNT ? ui_text(f->text) : f->fixed;
}

/* 상태 줄에 보인 내용의 종류(언어를 바꾸면 render_status 가 새 언어로 다시 쓴다). */
typedef enum {
    C_STATUS_TEXT,      /* 인자 없는 문구(status_text) */
    C_STATUS_DETECTING, /* 탐지·프로젝트 분석 진행(작업에서 읽음) */
    C_STATUS_DETECTED,  /* 탐지 결과(결과·detected·detect_ms) */
    C_STATUS_TRASHING,  /* 휴지통 이동 진행(작업에서 읽음) */
    C_STATUS_TRASHED    /* 휴지통 이동 결과(t_*) */
} CStatusKind;

/* CreateWindowExW 로 넘기는 생성 인자. */
typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

typedef struct {
    HWND hwnd;
    HWND path, browse, start, cancel, progress, status, warn, filter_label, filter, summary,
        list, check_all, check_none, trash_btn, selection, eco_label, eco;
    HFONT font;
    int dpi;

    CleanupJob *job;
    CleanupResult result;
    int has_result;
    int progress_pos;

    size_t *view; /* 현재 필터의 항목 인덱스(용량 내림차순) */
    size_t view_len;
    uint64_t view_bytes;

    CleanupGuard guard;
    /* 정리 계획(clean_plan.h): result.items 와 같은 순서. 경로 판정·선택·이동 결과를 담는다. */
    CleanPlan plan;
    int has_plan;
    ProjectList projects; /* 프로젝트 분석 결과(탐지 작업이 함께 만든다) */
    size_t checked_count;
    uint64_t checked_bytes;
    uint64_t checked_reclaim; /* 선택한 항목의 실제 확보 가능 합 */
    size_t moved_count;       /* 휴지통 또는 격리 폴더로 옮긴 수 */
    uint64_t moved_bytes;
    size_t quarantined_count; /* moved_count 중 격리 폴더로 옮긴 수 */

    CleanPlanRunJob *trash;
    size_t trash_count; /* 마지막 이동에 들어간 항목 수 */
    int trash_interactive; /* 사용자가 시작했으면 실패 요약 창을 띄운다 */
    ULONGLONG trash_started;

    /* 단추·이름표 폭(지금 글꼴로 잰 문구 폭, 예전 고정 폭이 최소) */
    int w_browse, w_start, w_cancel, w_check_all, w_check_none, w_trash, w_label;

    /* 상태 줄(render_status) */
    CStatusKind status_kind;
    UiText status_text;
    ScanStatus detected;     /* C_STATUS_DETECTED */
    uint64_t detect_ms;
    size_t t_moved, t_failed, t_cancelled, t_quarantined; /* C_STATUS_TRASHED */
    uint64_t t_bytes, t_elapsed_ms;

    wchar_t cell[512]; /* LVN_GETDISPINFO 용 */
} CleanupPanel;

static CleanupPanel *panel_of(HWND hwnd)
{
    return (CleanupPanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const CleanupPanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

static const wchar_t *category_label(CleanupCategory category)
{
    switch (category) {
    case CLEANUP_NODE_MODULES:
        return L"Node.js";
    case CLEANUP_BUILD:
        return ui_text(UI_T_C_CAT_BUILD);
    case CLEANUP_CACHE:
        return ui_text(UI_T_C_CAT_CACHE);
    case CLEANUP_TEMP:
        return ui_text(UI_T_C_CAT_TEMP);
    case CLEANUP_LOG:
        return ui_text(UI_T_C_CAT_LOG);
    default:
        return L"";
    }
}

static const wchar_t *eco_label(CleanupEco eco)
{
    switch (eco) {
    case CLEANUP_ECO_NODE:
        return L"Node.js";
    case CLEANUP_ECO_PYTHON:
        return L"Python";
    case CLEANUP_ECO_RUST:
        return L"Rust";
    case CLEANUP_ECO_JAVA:
        return L"Java·Kotlin";
    case CLEANUP_ECO_DOTNET:
        return L".NET";
    case CLEANUP_ECO_CPP:
        return L"C/C++";
    case CLEANUP_ECO_GO:
        return L"Go";
    default:
        return ui_text(UI_T_C_ECO_GENERIC);
    }
}

/* 비고 열: 신뢰도, 일부 누락, 시스템 경로. */
static void note_text(const CleanupItem *item, wchar_t *buf, size_t len)
{
    size_t used = 0;

    buf[0] = L'\0';
    if (item->flags & CLEANUP_ITEM_PROTECTED) {
        used += (size_t)swprintf(buf + used, len - used, L"%ls · ", ui_text(UI_T_C_NOTE_SYSTEM));
    }
    if ((item->flags & CLEANUP_ITEM_DIR) && cleanup_needs_marker(item->category)) {
        used += (size_t)swprintf(buf + used, len - used, L"%ls · ",
                                 ui_text((item->flags & CLEANUP_ITEM_CONFIRMED) ? UI_T_C_NOTE_CONFIRMED
                                                                                : UI_T_C_NOTE_GUESSED));
    }
    if (item->flags & CLEANUP_ITEM_INCOMPLETE) {
        used += (size_t)swprintf(buf + used, len - used, L"%ls · ", ui_text(UI_T_C_NOTE_INCOMPLETE));
    }
    if ((item->flags & CLEANUP_ITEM_MEASURED) && item->shared_bytes > 0) {
        used += (size_t)swprintf(buf + used, len - used, L"%ls · ", ui_text(UI_T_C_NOTE_SHARED));
    }
    /* 끝의 " · " 제거 */
    if (used >= 3) {
        buf[used - 3] = L'\0';
    }
}

/* ---- 정리 상태 ---- */

/* 실패 코드(HRESULT 또는 Windows 오류)의 시스템 설명. 앱 언어로 먼저 찾고, 그 언어가 Windows 에 없으면
 * Windows 기본 언어로. 없으면 빈 문자열. */
static void error_text(uint32_t code, wchar_t *buf, size_t len)
{
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                             code, ui_lang_langid(ui_lang_current()), buf, (DWORD)len, NULL);

    if (n == 0) {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0,
                           buf, (DWORD)len, NULL);
    }

    /* 끝의 마침표·줄바꿈 제거 */
    while (n > 0 && (buf[n - 1] == L'\r' || buf[n - 1] == L'\n' || buf[n - 1] == L'.' ||
                     buf[n - 1] == L' ')) {
        n--;
    }
    buf[n] = L'\0';
}

/* 항목(후보 하나 = 계획 대상 하나, 실행 항목 하나)의 마지막 이동 결과. */
static const CleanupTrashOutcome *last_outcome(const CleanPlanTarget *t)
{
    static const CleanupTrashOutcome none = {CLEANUP_TRASH_PENDING, 0, NULL};
    return t != NULL && t->entry_count > 0 ? &t->entries[0].outcome : &none;
}

/* 정리 열: 보호 사유, 이동 결과, 실패 사유. 옮길 수 있는 항목은 빈 문자열. */
static void clean_text(const CleanPlanTarget *t, wchar_t *buf, size_t len)
{
    const CleanupTrashOutcome *o = last_outcome(t);
    wchar_t why[160];

    buf[0] = L'\0';
    if (o->status == CLEANUP_TRASH_FAILED) {
        error_text(o->code, why, 160);
        swprintf(buf, len, ui_text(UI_T_C_CLEAN_FAILED), why[0] ? why : ui_text(UI_T_C_CLEAN_MOVE_FAILED),
                 (unsigned)o->code);
    } else if (o->status == CLEANUP_TRASH_QUARANTINE_FAILED) {
        /* 휴지통을 쓸 수 없어 격리 폴더로 옮기려다 실패(권한 부족·사용 중·공간 부족 등). */
        error_text(o->code, why, 160);
        swprintf(buf, len, ui_text(UI_T_C_CLEAN_QFAILED),
                 gui_trash_status_text(CLEANUP_TRASH_QUARANTINE_FAILED),
                 why[0] ? why : ui_text(UI_T_C_UNKNOWN_ERROR), (unsigned)o->code);
    } else if (cleanup_trash_is_denied(o->status) || o->status == CLEANUP_TRASH_NO_RECYCLE) {
        swprintf(buf, len, ui_text(UI_T_C_CLEAN_SKIPPED), gui_trash_status_text(o->status));
    } else if (o->status != CLEANUP_TRASH_PENDING) {
        swprintf(buf, len, L"%ls", gui_trash_status_text(o->status));
    } else if (t->deny != CLEANUP_TRASH_PENDING) {
        swprintf(buf, len, ui_text(UI_T_C_CLEAN_PROTECTED), gui_trash_status_text(t->deny));
    }
}

static CleanPlanTarget *plan_at(CleanupPanel *p, size_t index)
{
    return p->has_plan && index < p->plan.len ? &p->plan.items[index] : NULL;
}

/* 선택할 수 있는지: 경로 판정을 통과했고 아직 옮기지 않았으며 다시 시도해 볼 만한 상태. */
static int can_check(CleanupPanel *p, size_t index)
{
    return clean_plan_selectable(plan_at(p, index));
}

static int is_checked(CleanupPanel *p, size_t index)
{
    const CleanPlanTarget *t = plan_at(p, index);
    return t != NULL && t->selected;
}

static void free_states(CleanupPanel *p)
{
    clean_plan_free(&p->plan);
    p->has_plan = 0;
    p->checked_count = 0;
    p->checked_bytes = 0;
    p->checked_reclaim = 0;
    p->moved_count = 0;
    p->moved_bytes = 0;
    p->quarantined_count = 0;
}

/* 탐지 결과를 정리 계획으로 바꾼다(항목마다 경로 판정). 메모리가 없으면 선택 기능만 꺼진다. */
static void build_states(CleanupPanel *p)
{
    free_states(p);
    if (!p->has_result || p->result.len == 0) {
        return;
    }
    p->has_plan = clean_plan_from_project(&p->plan, &p->result, &p->guard) == 0;
    if (p->has_plan) {
        /* 미커밋 변경이 있는 프로젝트의 후보는 [모두 선택]에서 빠진다(MANUAL). */
        clean_plan_apply_projects(&p->plan, &p->projects);
    }
}

/* 프로젝트 칸: "폴더 이름 (브랜치)" 또는 "폴더 이름". */
static void project_text(CleanupPanel *p, const CleanPlanTarget *t, wchar_t *buf, size_t len)
{
    const ProjectInfo *pi;
    const wchar_t *name;

    buf[0] = L'\0';
    if (t == NULL || t->project >= p->projects.len) {
        return;
    }
    pi = &p->projects.items[t->project];
    name = wcsrchr(pi->root, L'\\');
    name = name != NULL && name[1] != L'\0' ? name + 1 : pi->root;
    if (pi->is_git) {
        swprintf(buf, len, L"%ls (Git %ls)", name, pi->branch != NULL ? pi->branch : L"?");
    } else {
        swprintf(buf, len, L"%ls", name);
    }
}

/* 마지막 활동 칸: "2020-01-15 · 2487일 전 · 휴면". */
static void activity_text(CleanupPanel *p, const CleanPlanTarget *t, wchar_t *buf, size_t len)
{
    const ProjectInfo *pi;
    FILETIME u, l;
    SYSTEMTIME st;

    buf[0] = L'\0';
    if (t == NULL || t->project >= p->projects.len) {
        return;
    }
    pi = &p->projects.items[t->project];
    if (pi->last_activity == 0) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_C_ACTIVITY_UNKNOWN));
        return;
    }
    u.dwLowDateTime = (DWORD)pi->last_activity;
    u.dwHighDateTime = (DWORD)(pi->last_activity >> 32);
    if (!FileTimeToLocalFileTime(&u, &l) || !FileTimeToSystemTime(&l, &st)) {
        return;
    }
    {
        wchar_t dormant[64];
        dormant[0] = L'\0';
        if (pi->dormant) {
            swprintf(dormant, 64, L" · %ls", ui_text(UI_T_C_NOTE_DORMANT));
        }
        swprintf(buf, len, ui_text(UI_T_C_ACTIVITY), st.wYear, st.wMonth, st.wDay, pi->age_days,
                 dormant);
    }
}

static void recount_checked(CleanupPanel *p)
{
    CleanPlanSummary s;

    clean_plan_summarize(p->has_plan ? &p->plan : NULL, &s);
    p->checked_count = s.targets;
    p->checked_bytes = s.bytes;
    p->checked_reclaim = s.reclaim;
}

/* 선택 줄 문구: 선택 수·실제 확보(옮긴 것이 있으면 옮긴 수·용량, 격리 폴더 수). */
static void selection_text(const CleanupPanel *p, wchar_t *text, size_t len)
{
    wchar_t size[32], moved[32];

    format_size(p->checked_reclaim, size, 32);
    if (p->moved_count > 0 && p->quarantined_count > 0) {
        format_size(p->moved_bytes, moved, 32);
        swprintf(text, len, ui_text(UI_T_C_SEL_MOVED_Q), (unsigned)p->checked_count, size,
                 (unsigned)p->moved_count, moved, (unsigned)p->quarantined_count);
    } else if (p->moved_count > 0) {
        format_size(p->moved_bytes, moved, 32);
        swprintf(text, len, ui_text(UI_T_C_SEL_MOVED), (unsigned)p->checked_count, size,
                 (unsigned)p->moved_count, moved);
    } else {
        swprintf(text, len, ui_text(UI_T_C_SEL), (unsigned)p->checked_count, size);
    }
}

static void update_selection(CleanupPanel *p)
{
    wchar_t text[256];
    int busy = p->job != NULL || p->trash != NULL;

    selection_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->selection, p->has_plan ? text : L"");
    EnableWindow(p->check_all, !busy && p->has_plan);
    EnableWindow(p->check_none, !busy && p->checked_count > 0);
    EnableWindow(p->trash_btn, !busy && p->checked_count > 0);
}

static void set_checked(CleanupPanel *p, size_t index, int on)
{
    CleanPlanTarget *t = plan_at(p, index);

    if (t == NULL) {
        return;
    }
    on = on && clean_plan_selectable(t);
    if (t->selected == on) {
        return;
    }
    clean_plan_set_selected(&p->plan, index, on);
    if (on) {
        p->checked_count++;
        p->checked_bytes += p->result.items[index].bytes;
        p->checked_reclaim += cleanup_item_reclaim(&p->result.items[index]);
    } else {
        p->checked_count--;
        p->checked_bytes -= p->result.items[index].bytes;
        p->checked_reclaim -= cleanup_item_reclaim(&p->result.items[index]);
    }
}

/* ---- 표시 ---- */

static unsigned current_mask(const CleanupPanel *p)
{
    LRESULT sel = SendMessageW(p->filter, CB_GETCURSEL, 0, 0);

    if (sel < 0 || (size_t)sel >= FILTER_COUNT) {
        sel = 0;
    }
    return g_filters[sel].mask;
}

static unsigned current_eco_mask(const CleanupPanel *p)
{
    LRESULT sel = SendMessageW(p->eco, CB_GETCURSEL, 0, 0);

    if (sel < 0 || (size_t)sel >= ECO_FILTER_COUNT) {
        sel = 0;
    }
    return g_eco_filters[sel].mask;
}

static void update_summary(CleanupPanel *p)
{
    wchar_t text[256], shown[32], total[32], reclaim[32];

    if (!p->has_result) {
        SetWindowTextW(p->summary, L"");
        return;
    }
    format_size(p->view_bytes, shown, 32);
    format_size(p->result.total_bytes, total, 32);
    format_size(p->result.total_reclaim, reclaim, 32);
    swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_C_SUMMARY), (unsigned)p->view_len,
             shown, (unsigned)p->result.len, reclaim, total);
    SetWindowTextW(p->summary, text);
}

/* 필터를 적용해 목록을 다시 만든다(가상 목록이라 개수만 바꾼다). */
static void apply_filter(CleanupPanel *p)
{
    unsigned mask = current_mask(p), eco_mask = current_eco_mask(p);
    size_t count;

    free(p->view);
    p->view = NULL;
    p->view_len = 0;
    p->view_bytes = 0;
    if (p->has_result) {
        count = cleanup_select_ex(&p->result, mask, eco_mask, NULL, 0, NULL);
        if (count > 0) {
            p->view = malloc(count * sizeof(*p->view));
        }
        if (p->view != NULL) {
            p->view_len =
                cleanup_select_ex(&p->result, mask, eco_mask, p->view, count, &p->view_bytes);
        }
    }
    ListView_SetItemCountEx(p->list, (int)p->view_len, 0);
    InvalidateRect(p->list, NULL, TRUE);
    update_summary(p);
}

static void set_warning(CleanupPanel *p)
{
    SetWindowTextW(p->warn, ui_text(p->has_result && p->result.root_protected ? UI_T_C_WARN_PROTECTED
                                                                              : UI_T_C_WARN_INFO));
    InvalidateRect(p->warn, NULL, TRUE);
}

/* ---- 상태 줄 ---- */

/* 탐지·프로젝트 분석 진행 문구(C_STATUS_DETECTING). 작업이 없으면 빈 문자열. */
static void detect_progress_text(CleanupPanel *p, wchar_t *buf, size_t len)
{
    CleanupJobSnapshot snap;
    wchar_t size[32];

    buf[0] = L'\0';
    if (p->job == NULL) {
        return;
    }
    cleanup_job_progress(p->job, &snap);
    if (snap.analyzing_projects) {
        swprintf(buf, len, ui_text(UI_T_C_GIT_PROGRESS), (unsigned)snap.projects_done,
                 (unsigned)snap.projects_total, (unsigned)snap.progress.items);
        return;
    }
    format_size(snap.progress.item_bytes, size, 32);
    swprintf(buf, len, ui_text(UI_T_C_PROGRESS), (unsigned long long)snap.progress.dirs,
             (unsigned long long)snap.progress.files, (unsigned)snap.progress.items, size,
             (unsigned long long)snap.progress.errors, snap.current_dir);
}

/* 탐지 결과 문구(C_STATUS_DETECTED). */
static void detected_text(CleanupPanel *p, wchar_t *buf, size_t len)
{
    const ScanReport *r = &p->result.report;
    UiText head;

    if (r->cancelled) {
        head = UI_T_C_HEAD_CANCELLED;
    } else if (p->detected == SCAN_COMPLETE) {
        head = UI_T_SCAN_HEAD_DONE;
    } else if (p->detected == SCAN_PARTIAL) {
        head = UI_T_SCAN_HEAD_PARTIAL;
    } else {
        head = UI_T_C_HEAD_FAILED;
    }
    if (p->detected == SCAN_FATAL) {
        const wchar_t *kind = r->errors_len > 0 ? scan_error_kind_name(r->errors[0].kind)
                                                : L"other";
        swprintf(buf, len, ui_text(UI_T_SCAN_FATAL), ui_text(head), kind);
        return;
    }
    swprintf(buf, len, ui_text(UI_T_C_FINISHED), ui_text(head), (unsigned long long)r->dir_count,
             (unsigned long long)r->file_count, (unsigned long long)r->error_count,
             (unsigned long long)r->skipped_dir_count, (unsigned long long)r->excluded_dir_count,
             (unsigned long long)r->reparse_count, (double)p->detect_ms / 1000.0);
}

/* 휴지통 이동 진행 문구(C_STATUS_TRASHING). 작업이 없으면 빈 문자열. */
static void trash_progress_text(CleanupPanel *p, wchar_t *buf, size_t len)
{
    CleanPlanSnapshot snap;
    wchar_t size[32];

    buf[0] = L'\0';
    if (p->trash == NULL) {
        return;
    }
    clean_plan_run_progress(p->trash, &snap);
    format_size(snap.moved_bytes, size, 32);
    swprintf(buf, len, ui_text(UI_T_C_TRASH_PROGRESS), (unsigned)snap.done, (unsigned)snap.total,
             (unsigned)snap.moved, size, (unsigned)snap.quarantined, (unsigned)snap.failed);
}

/* 상태 줄 문구를 기억해 둔 종류·값과 지금 언어로 만든다. */
static void status_line(CleanupPanel *p, wchar_t *buf, size_t len)
{
    wchar_t size[32];

    buf[0] = L'\0';
    switch (p->status_kind) {
    case C_STATUS_TEXT:
        swprintf(buf, len, L"%ls", ui_text(p->status_text));
        break;
    case C_STATUS_DETECTING:
        detect_progress_text(p, buf, len);
        break;
    case C_STATUS_DETECTED:
        detected_text(p, buf, len);
        break;
    case C_STATUS_TRASHING:
        trash_progress_text(p, buf, len);
        break;
    case C_STATUS_TRASHED:
        format_size(p->t_bytes, size, 32);
        if (p->t_quarantined > 0) {
            swprintf(buf, len, ui_text(UI_T_C_TRASHED_Q), (unsigned)p->t_moved, size,
                     (unsigned)p->t_failed, (unsigned)p->t_cancelled,
                     (double)p->t_elapsed_ms / 1000.0, (unsigned)p->t_quarantined);
        } else {
            swprintf(buf, len, ui_text(UI_T_C_TRASHED), (unsigned)p->t_moved, size,
                     (unsigned)p->t_failed, (unsigned)p->t_cancelled,
                     (double)p->t_elapsed_ms / 1000.0);
        }
        break;
    }
}

static void render_status(CleanupPanel *p)
{
    wchar_t text[CLEANUP_JOB_DIR_LEN + 512];

    status_line(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->status, text);
}

static void show_status(CleanupPanel *p, CStatusKind kind)
{
    p->status_kind = kind;
    render_status(p);
}

/* 인자 없는 문구를 상태 줄에 보인다. */
static void show_text(CleanupPanel *p, UiText id)
{
    p->status_text = id;
    show_status(p, C_STATUS_TEXT);
}

/* 탐지·휴지통 이동 중에는 다른 작업을 막고 [취소]만 켠다. */
static void set_busy(CleanupPanel *p, int busy)
{
    EnableWindow(p->start, !busy);
    EnableWindow(p->browse, !busy);
    EnableWindow(p->path, !busy);
    EnableWindow(p->cancel, busy);
    update_selection(p);
}

/* ---- 탐지 ---- */

/* 작업 스레드에서 호출된다. 패널이 이미 없으면 PostMessage 가 실패할 뿐이다. */
static void on_job_notify(int finished, void *ctx)
{
    HWND hwnd = ctx;

    PostMessageW(hwnd, finished ? WM_APP_CLEANUP_DONE : WM_APP_CLEANUP_PROGRESS, 0, 0);
}

static void update_progress(CleanupPanel *p)
{
    CleanupJobSnapshot snap;
    uint64_t done, all;
    int pos;

    if (p->job == NULL) {
        return;
    }
    cleanup_job_progress(p->job, &snap);
    if (snap.cancel_requested) {
        return;
    }
    if (snap.analyzing_projects) {
        /* Git 분석은 탐지와 다른 단계라 비율로 잇지 않고 움직이는 막대로 보인다. */
        if (progress_state(p->progress) != PROGRESS_BUSY) {
            progress_set(p->progress, PROGRESS_BUSY, 0, PROGRESS_RANGE);
        }
        show_status(p, C_STATUS_DETECTING);
        return;
    }
    /* 남은 폴더 수가 늘면 비율이 줄 수 있으므로 막대는 뒤로 가지 않게 한다. */
    done = snap.progress.dirs_scanned;
    all = done + snap.progress.dirs_pending;
    pos = all > 0 ? (int)(done * PROGRESS_RANGE / all) : 0;
    if (pos > p->progress_pos) {
        p->progress_pos = pos;
        progress_set(p->progress, PROGRESS_RUNNING, pos, PROGRESS_RANGE);
    }
    show_status(p, C_STATUS_DETECTING);
}

static void finish_detect(CleanupPanel *p)
{
    CleanupJobSnapshot snap;
    const ScanReport *r;
    ScanStatus status;

    if (p->job == NULL) {
        return;
    }
    cleanup_job_progress(p->job, &snap);
    status = cleanup_job_finish_ex(p->job, &p->result, &p->projects);
    p->job = NULL;
    p->has_result = 1;
    r = &p->result.report;

    if (status == SCAN_FATAL) {
        progress_set(p->progress, PROGRESS_FAILED, 0, PROGRESS_RANGE);
    } else {
        progress_set(p->progress, r->cancelled ? PROGRESS_CANCELLED : PROGRESS_DONE, 0,
                     PROGRESS_RANGE);
    }
    /* 결과 문구는 detected_text 가 지금 언어로 만든다. */
    p->detected = status;
    p->detect_ms = snap.elapsed_ms;
    show_status(p, C_STATUS_DETECTED);
    build_states(p);
    set_busy(p, 0);
    set_warning(p);
    apply_filter(p);
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), CPN_FINISHED), (LPARAM)p->hwnd);
}

/* ---- 휴지통 이동 ---- */

static void on_trash_notify(int finished, void *ctx)
{
    HWND hwnd = ctx;

    PostMessageW(hwnd, finished ? WM_APP_TRASH_DONE : WM_APP_TRASH_PROGRESS, 0, 0);
}

static void update_trash_progress(CleanupPanel *p)
{
    CleanPlanSnapshot snap;

    if (p->trash == NULL) {
        return;
    }
    clean_plan_run_progress(p->trash, &snap);
    if (snap.cancel_requested) {
        return;
    }
    if (snap.total > 0) {
        progress_set(p->progress, PROGRESS_RUNNING, (int)(snap.done * PROGRESS_RANGE / snap.total),
                     PROGRESS_RANGE);
    }
    show_status(p, C_STATUS_TRASHING);
}

/*
 * 확인 창: 선택한 항목의 유형·용량·경로와 실제 확보 용량, 주의 사항을 보여 준다.
 * 기본 버튼은 [아니요]이며 [예]일 때만 1.
 */
static int confirm_trash(CleanupPanel *p, const size_t *index, size_t count)
{
    size_t i, guessed, cap, used = 0;
    wchar_t size[32], *text;
    int answer;
    CleanPlanSummary sum;

    clean_plan_summarize(&p->plan, &sum);
    guessed = sum.guessed;
    cap = 2048 + (size_t)CONFIRM_LIST_MAX * (MAX_PATH + 64);
    text = malloc(cap * sizeof(*text));
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
    APPEND(ui_text(UI_T_C_CONFIRM_HEAD), (unsigned)count, size);
    for (i = 0; i < count && i < CONFIRM_LIST_MAX; i++) {
        const CleanupItem *item = &p->result.items[index[i]];
        format_size(item->bytes, size, 32);
        APPEND(L"[%ls] %ls   %ls\n", category_label(item->category), size, item->path);
    }
    if (count > CONFIRM_LIST_MAX) {
        APPEND(ui_text(UI_T_Q_MORE), (unsigned)(count - CONFIRM_LIST_MAX));
        APPEND(L"\n");
    }
    APPEND(L"%ls", ui_text(UI_T_C_CONFIRM_INFO));
    if (guessed > 0) {
        APPEND(ui_text(UI_T_C_CONFIRM_GUESSED), (unsigned)guessed);
    }
    if (sum.dirty > 0) {
        APPEND(ui_text(UI_T_C_CONFIRM_DIRTY), (unsigned)sum.dirty);
    }
    if (sum.unpushed > 0) {
        APPEND(ui_text(UI_T_C_CONFIRM_UNPUSHED), (unsigned)sum.unpushed);
    }
    APPEND(L"%ls", ui_text(UI_T_Q_PURGE_CONFIRM_ASK));
#undef APPEND

    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_C_TRASH_TITLE),
                         MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    free(text);
    return answer == IDYES;
}

/* 실패·건너뛴 항목 요약 창(사용자가 시작한 이동에서 실패가 있을 때만). */
static void show_failures(CleanupPanel *p, size_t failed)
{
    size_t i, shown = 0, cap = 1024 + (size_t)FAILURE_LIST_MAX * (MAX_PATH + 256), used = 0;
    wchar_t *text = malloc(cap * sizeof(*text)), why[256];
    int n;

    if (text == NULL) {
        return;
    }
    n = swprintf(text, cap, ui_text(UI_T_C_FAILURES_HEAD), (unsigned)failed);
    used = n > 0 ? (size_t)n : 0;
    for (i = 0; i < p->plan.len && shown < FAILURE_LIST_MAX; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        CleanupTrashStatus st = last_outcome(t)->status;
        if (!t->ran || st == CLEANUP_TRASH_MOVED || st == CLEANUP_TRASH_QUARANTINED ||
            st == CLEANUP_TRASH_CANCELLED) {
            continue;
        }
        clean_text(t, why, 256);
        n = swprintf(text + used, cap - used, L"%ls\n    %ls\n", p->result.items[i].path, why);
        used += n > 0 ? (size_t)n : 0;
        shown++;
    }
    if (failed > shown) {
        swprintf(text + used, cap - used, ui_text(UI_T_Q_MORE), (unsigned)(failed - shown));
    }
    MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_C_FAILURES_TITLE),
                MB_OK | MB_ICONWARNING);
    free(text);
}

static void finish_trash(CleanupPanel *p)
{
    CleanPlanTotals totals;
    size_t i, moved, quarantined, failed, cancelled;
    uint64_t bytes;
    ULONGLONG elapsed;

    if (p->trash == NULL) {
        return;
    }
    /* 이번 이동에 들어간 항목만 ran 으로 남긴다(실패 요약·자체 검사). */
    for (i = 0; i < p->plan.len; i++) {
        p->plan.items[i].ran = 0;
    }
    clean_plan_run_finish(p->trash, &p->plan, &totals);
    p->trash = NULL;
    elapsed = GetTickCount64() - p->trash_started;
    moved = totals.moved;
    quarantined = totals.quarantined;
    failed = totals.failed + totals.skipped;
    cancelled = totals.cancelled;
    bytes = totals.moved_bytes;
    p->trash_count = totals.targets;
    p->moved_count += moved;
    p->moved_bytes += bytes;
    p->quarantined_count += quarantined;
    recount_checked(p);

    /* 결과 문구는 status_line 이 지금 언어로 만든다. */
    p->t_moved = moved;
    p->t_failed = failed;
    p->t_cancelled = cancelled;
    p->t_quarantined = quarantined;
    p->t_bytes = bytes;
    p->t_elapsed_ms = elapsed;
    show_status(p, C_STATUS_TRASHED);
    /* 일부라도 실패하면 빨간색, 취소하면 멈춘 자리에서 노란색, 모두 옮기면 가득. */
    progress_set(p->progress,
                 failed > 0 ? PROGRESS_FAILED : cancelled > 0 ? PROGRESS_CANCELLED : PROGRESS_DONE, 0,
                 PROGRESS_RANGE);
    set_busy(p, 0);
    InvalidateRect(p->list, NULL, TRUE);
    if (p->trash_interactive && failed > 0) {
        show_failures(p, failed);
    }
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), CPN_TRASHED), (LPARAM)p->hwnd);
}

/* 선택 상자: 클릭한 행, 또는 Space 로 선택한 행들을 바꾼다. */
static void toggle_rows(CleanupPanel *p, int clicked)
{
    int row, on = -1;

    if (!p->has_plan || p->job != NULL || p->trash != NULL) {
        return;
    }
    if (clicked >= 0) {
        if ((size_t)clicked < p->view_len) {
            size_t index = p->view[clicked];
            set_checked(p, index, !is_checked(p, index));
            ListView_RedrawItems(p->list, clicked, clicked);
        }
    } else {
        /* 선택한 행 중 하나라도 해제돼 있으면 모두 체크, 아니면 모두 해제 */
        for (row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED); row >= 0;
             row = ListView_GetNextItem(p->list, row, LVNI_SELECTED)) {
            if ((size_t)row < p->view_len && can_check(p, p->view[row]) &&
                !is_checked(p, p->view[row])) {
                on = 1;
            }
        }
        for (row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED); row >= 0;
             row = ListView_GetNextItem(p->list, row, LVNI_SELECTED)) {
            if ((size_t)row < p->view_len) {
                set_checked(p, p->view[row], on == 1);
            }
        }
        InvalidateRect(p->list, NULL, FALSE);
    }
    update_selection(p);
}

static void browse_folder(CleanupPanel *p)
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    wchar_t path[MAX_PATH];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = GetAncestor(p->hwnd, GA_ROOT);
    bi.lpszTitle = ui_text(UI_T_C_BROWSE_TITLE);
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl == NULL) {
        return;
    }
    if (SHGetPathFromIDListW(pidl, path)) {
        SetWindowTextW(p->path, path);
    }
    CoTaskMemFree(pidl);
}

/* ---- 가상 목록 ---- */

static void on_get_dispinfo(CleanupPanel *p, NMLVDISPINFOW *di)
{
    const CleanupItem *item;
    size_t row = (size_t)di->item.iItem;

    if (row >= p->view_len) {
        return;
    }
    /* 체크 상자: 옮길 수 없는 항목(보호 대상·이미 옮김)은 상자를 그리지 않는다. */
    if (di->item.mask & LVIF_STATE) {
        int image = CHECK_NONE;
        if (can_check(p, p->view[row])) {
            image = is_checked(p, p->view[row]) ? CHECK_ON : CHECK_OFF;
        }
        di->item.state = INDEXTOSTATEIMAGEMASK(image);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    }
    if (!(di->item.mask & LVIF_TEXT)) {
        return;
    }
    item = &p->result.items[p->view[row]];
    switch (di->item.iSubItem) {
    case COL_RECLAIM:
        format_size(cleanup_item_reclaim(item), p->cell, 64);
        di->item.pszText = p->cell;
        break;
    case COL_SIZE:
        format_size(item->bytes, p->cell, 64);
        di->item.pszText = p->cell;
        break;
    case COL_TYPE:
        di->item.pszText = (wchar_t *)category_label(item->category);
        break;
    case COL_ECO:
        di->item.pszText = (wchar_t *)eco_label(item->eco);
        break;
    case COL_PATH:
        /* 결과가 바뀌기 전까지 유효한 문자열을 그대로 넘긴다. */
        di->item.pszText = item->path;
        break;
    case COL_FILES:
        swprintf(p->cell, 64, L"%llu", (unsigned long long)item->files);
        di->item.pszText = p->cell;
        break;
    case COL_NOTE: {
        const CleanPlanTarget *t = plan_at(p, p->view[row]);
        note_text(item, p->cell, 256);
        if (t != NULL && (t->flags & (CLEAN_PLAN_DIRTY | CLEAN_PLAN_UNPUSHED | CLEAN_PLAN_DORMANT))) {
            size_t n = wcslen(p->cell);
            swprintf(p->cell + n, 512 - n, L"%ls%ls%ls%ls%ls%ls", n > 0 ? L" · " : L"",
                     (t->flags & CLEAN_PLAN_DIRTY) ? ui_text(UI_T_C_NOTE_DIRTY) : L"",
                     (t->flags & CLEAN_PLAN_DIRTY) ? L" " : L"",
                     (t->flags & CLEAN_PLAN_UNPUSHED) ? ui_text(UI_T_C_NOTE_UNPUSHED) : L"",
                     (t->flags & CLEAN_PLAN_UNPUSHED) ? L" " : L"",
                     (t->flags & CLEAN_PLAN_DORMANT) ? ui_text(UI_T_C_NOTE_DORMANT) : L"");
            n = wcslen(p->cell);
            while (n > 0 && p->cell[n - 1] == L' ') {
                p->cell[--n] = L'\0';
            }
        }
        di->item.pszText = p->cell;
        break;
    }
    case COL_RECOVERY: {
        const CleanPlanTarget *t = plan_at(p, p->view[row]);
        di->item.pszText = (wchar_t *)(t != NULL && t->recovery >= 0
                                           ? gui_recovery_text((ProjectRecovery)t->recovery)
                                           : L"");
        break;
    }
    case COL_PROJECT:
        project_text(p, plan_at(p, p->view[row]), p->cell, 512);
        di->item.pszText = p->cell;
        break;
    case COL_ACTIVITY:
        activity_text(p, plan_at(p, p->view[row]), p->cell, 512);
        di->item.pszText = p->cell;
        break;
    case COL_CLEAN:
        p->cell[0] = L'\0';
        if (plan_at(p, p->view[row]) != NULL) {
            clean_text(plan_at(p, p->view[row]), p->cell, 512);
        }
        di->item.pszText = p->cell;
        break;
    }
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(CleanupPanel *p)
{
    RECT rc;
    int h = pscale(p, 24), gap = pscale(p, 6);
    int w, y, x, path_w, label_w = p->w_label, combo_w = pscale(p, 260), list_h, sum_x;
    int eco_x, eco_w = pscale(p, 150);

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;

    y = 0;
    path_w = w - (p->w_browse + p->w_start + p->w_cancel + 3 * gap);
    if (path_w < pscale(p, 80)) {
        path_w = pscale(p, 80);
    }
    MoveWindow(p->path, 0, y, path_w, h, TRUE);
    x = path_w + gap;
    MoveWindow(p->browse, x, y, p->w_browse, h, TRUE);
    x += p->w_browse + gap;
    MoveWindow(p->start, x, y, p->w_start, h, TRUE);
    x += p->w_start + gap;
    MoveWindow(p->cancel, x, y, p->w_cancel, h, TRUE);

    y += h + gap;
    MoveWindow(p->progress, 0, y, w, pscale(p, 16), TRUE);
    y += pscale(p, 16) + gap;
    MoveWindow(p->status, 0, y, w, pscale(p, 20), TRUE);
    y += pscale(p, 20);
    MoveWindow(p->warn, 0, y, w, pscale(p, 20), TRUE);
    y += pscale(p, 20) + gap;

    MoveWindow(p->filter_label, 0, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    /* 콤보 상자 높이는 펼친 목록 높이까지 포함한다. */
    MoveWindow(p->filter, label_w, y, combo_w, pscale(p, 200), TRUE);
    eco_x = label_w + combo_w + gap * 2;
    MoveWindow(p->eco_label, eco_x, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    MoveWindow(p->eco, eco_x + label_w, y, eco_w, pscale(p, 240), TRUE);
    sum_x = eco_x + label_w + eco_w + gap * 2;
    MoveWindow(p->summary, sum_x, y + pscale(p, 4), w - sum_x > 0 ? w - sum_x : 0,
               pscale(p, 20), TRUE);
    y += h + gap;

    /* 선택 줄: [모두 선택] [선택 해제] [휴지통으로 이동...]  선택 n개 · 실제 확보 */
    MoveWindow(p->check_all, 0, y, p->w_check_all, h, TRUE);
    x = p->w_check_all + gap;
    MoveWindow(p->check_none, x, y, p->w_check_none, h, TRUE);
    x += p->w_check_none + gap;
    MoveWindow(p->trash_btn, x, y, p->w_trash, h, TRUE);
    sum_x = x + p->w_trash + gap * 2;
    MoveWindow(p->selection, sum_x, y + pscale(p, 4), w - sum_x > 0 ? w - sum_x : 0,
               pscale(p, 20), TRUE);
    y += h + gap;

    list_h = rc.bottom - y;
    MoveWindow(p->list, 0, y, w, list_h > 0 ? list_h : 0, TRUE);
}

static HWND panel_child(CleanupPanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
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
    {UI_T_C_COL_RECLAIM, 90, LVCFMT_RIGHT},  {UI_T_C_COL_SIZE, 90, LVCFMT_RIGHT},
    {UI_T_C_COL_TYPE, 90, LVCFMT_LEFT},      {UI_T_C_COL_ECO, 90, LVCFMT_LEFT},
    {UI_T_C_COL_PATH, 440, LVCFMT_LEFT},     {UI_T_COL_FILES, 80, LVCFMT_RIGHT},
    {UI_T_C_COL_NOTE, 200, LVCFMT_LEFT},     {UI_T_C_COL_RECOVERY, 190, LVCFMT_LEFT},
    {UI_T_C_COL_PROJECT, 160, LVCFMT_LEFT},  {UI_T_C_COL_ACTIVITY, 170, LVCFMT_LEFT},
    {UI_T_C_COL_CLEAN, 220, LVCFMT_LEFT},
};

/* 단추·체크 상자가 아닌 이름표와 단추의 문구(왼쪽부터). */
static void label_texts(CleanupPanel *p, HWND *h, UiText *t)
{
    h[0] = p->browse, t[0] = UI_T_SCAN_BROWSE;
    h[1] = p->start, t[1] = UI_T_C_DETECT;
    h[2] = p->cancel, t[2] = UI_T_SCAN_CANCEL;
    h[3] = p->check_all, t[3] = UI_T_C_CHECK_ALL;
    h[4] = p->check_none, t[4] = UI_T_C_CHECK_NONE;
    h[5] = p->trash_btn, t[5] = UI_T_C_TRASH;
    h[6] = p->filter_label, t[6] = UI_T_C_FILTER_LABEL;
    h[7] = p->eco_label, t[7] = UI_T_C_ECO_LABEL;
}
#define LABEL_COUNT 8

/* 열 폭: 기본 폭과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(CleanupPanel *p, int col)
{
    int need = theme_text_width(p->list, p->font, ui_text(k_cols[col].title)) + pscale(p, 16);
    return pscale(p, k_cols[col].width) > need ? pscale(p, k_cols[col].width) : need;
}

/* 단추·이름표 폭: 예전 고정 폭(단추 84·140, 이름표 40)을 최소로 두고(한국어 화면은 그대로), 지금 글꼴로 잰
 * 문구가 더 길면 넓힌다. */
static void measure_controls(CleanupPanel *p)
{
    int a, b;

    p->w_browse = theme_button_width(p->hwnd, p->font, ui_text(UI_T_SCAN_BROWSE), p->dpi, pscale(p, 84));
    p->w_start = theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_DETECT), p->dpi, pscale(p, 84));
    p->w_cancel = theme_button_width(p->hwnd, p->font, ui_text(UI_T_SCAN_CANCEL), p->dpi, pscale(p, 84));
    p->w_check_all = theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_CHECK_ALL), p->dpi, pscale(p, 84));
    p->w_check_none =
        theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_CHECK_NONE), p->dpi, pscale(p, 84));
    p->w_trash = theme_button_width(p->hwnd, p->font, ui_text(UI_T_C_TRASH), p->dpi, pscale(p, 140));
    a = theme_text_width(p->hwnd, p->font, ui_text(UI_T_C_FILTER_LABEL)) + pscale(p, 6);
    b = theme_text_width(p->hwnd, p->font, ui_text(UI_T_C_ECO_LABEL)) + pscale(p, 6);
    p->w_label = a > b ? a : b;
    if (p->w_label < pscale(p, 40)) {
        p->w_label = pscale(p, 40);
    }
}

/* 필터 콤보 상자에 지금 언어의 이름을 넣는다(고른 항목은 그대로). */
static void fill_filters(CleanupPanel *p)
{
    LRESULT sel = SendMessageW(p->filter, CB_GETCURSEL, 0, 0), eco = SendMessageW(p->eco, CB_GETCURSEL, 0, 0);
    size_t i;

    SendMessageW(p->filter, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < FILTER_COUNT; i++) {
        SendMessageW(p->filter, CB_ADDSTRING, 0, (LPARAM)filter_label(&g_filters[i]));
    }
    SendMessageW(p->filter, CB_SETCURSEL, sel >= 0 ? (WPARAM)sel : 0, 0);
    SendMessageW(p->eco, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < ECO_FILTER_COUNT; i++) {
        SendMessageW(p->eco, CB_ADDSTRING, 0, (LPARAM)filter_label(&g_eco_filters[i]));
    }
    SendMessageW(p->eco, CB_SETCURSEL, eco >= 0 ? (WPARAM)eco : 0, 0);
}

static void panel_create_controls(CleanupPanel *p)
{
    size_t i;

    p->path = panel_child(p, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                          IDC_C_PATH);
    p->browse = panel_child(p, L"BUTTON", ui_text(UI_T_SCAN_BROWSE), BS_PUSHBUTTON | WS_TABSTOP, 0,
                            IDC_C_BROWSE);
    p->start = panel_child(p, L"BUTTON", ui_text(UI_T_C_DETECT), BS_PUSHBUTTON | WS_TABSTOP, 0,
                           IDC_C_START);
    p->cancel = panel_child(p, L"BUTTON", ui_text(UI_T_SCAN_CANCEL), BS_PUSHBUTTON | WS_TABSTOP, 0,
                            IDC_C_CANCEL);
    p->progress = panel_child(p, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, IDC_C_PROGRESS);
    p->status = panel_child(p, L"STATIC", ui_text(UI_T_C_READY), SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, 0,
                            IDC_C_STATUS);
    p->warn = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0, IDC_C_WARN);
    p->filter_label = panel_child(p, L"STATIC", ui_text(UI_T_C_FILTER_LABEL), SS_LEFT, 0,
                                  IDC_C_FILTER_LABEL);
    p->filter = panel_child(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                            IDC_C_FILTER);
    p->eco_label = panel_child(p, L"STATIC", ui_text(UI_T_C_ECO_LABEL), SS_LEFT, 0, IDC_C_ECO_LABEL);
    p->eco = panel_child(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                         IDC_C_ECO);
    p->summary = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                             IDC_C_SUMMARY);
    p->check_all = panel_child(p, L"BUTTON", ui_text(UI_T_C_CHECK_ALL), BS_PUSHBUTTON | WS_TABSTOP, 0,
                               IDC_C_CHECK_ALL);
    p->check_none = panel_child(p, L"BUTTON", ui_text(UI_T_C_CHECK_NONE), BS_PUSHBUTTON | WS_TABSTOP,
                                0, IDC_C_CHECK_NONE);
    p->trash_btn = panel_child(p, L"BUTTON", ui_text(UI_T_C_TRASH), BS_PUSHBUTTON | WS_TABSTOP, 0,
                               IDC_C_TRASH);
    p->selection = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                               IDC_C_SELECTION);
    p->list = panel_child(p, WC_LISTVIEWW, L"",
                          LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_TABSTOP,
                          WS_EX_CLIENTEDGE, IDC_C_LIST);

    SendMessageW(p->path, EM_SETCUEBANNER, TRUE, (LPARAM)ui_text(UI_T_C_PATH_CUE));
    SendMessageW(p->progress, PBM_SETRANGE32, 0, PROGRESS_RANGE);
    fill_filters(p);
    /* 가상 목록이라 체크 상태는 패널이 갖고 LVN_GETDISPINFO 로 알려 준다. */
    ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                   LVS_EX_CHECKBOXES);
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
    measure_controls(p);
    set_busy(p, 0);
    set_warning(p);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    CleanupPanel *p = panel_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        /* 상태는 창과 수명을 같이 한다(WM_NCDESTROY 에서 해제). */
        const PanelParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        p = calloc(1, sizeof(*p));
        if (p == NULL) {
            return FALSE;
        }
        p->hwnd = hwnd;
        p->font = params->font;
        p->dpi = params->dpi;
        p->status_kind = C_STATUS_TEXT;
        p->status_text = UI_T_C_READY;
        cleanup_result_init(&p->result);
        cleanup_guard_init(&p->guard);
        cleanup_guard_add_system(&p->guard);
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
        case IDC_C_BROWSE:
            browse_folder(p);
            return 0;
        case IDC_C_START:
            cleanup_panel_start(hwnd, NULL);
            return 0;
        case IDC_C_CANCEL:
            cleanup_panel_cancel(hwnd);
            return 0;
        case IDC_C_CHECK_ALL:
            cleanup_panel_check_all(hwnd, 1);
            return 0;
        case IDC_C_CHECK_NONE:
            cleanup_panel_check_all(hwnd, 0);
            return 0;
        case IDC_C_TRASH:
            cleanup_panel_trash(hwnd, 1);
            return 0;
        case IDC_C_FILTER:
        case IDC_C_ECO:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                apply_filter(p);
            }
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        /* 입력 칸·콤보 상자 목록(어둡게일 때 SURFACE 색). */
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        /* 보호 경로 경고는 빨간 글씨로. */
        if (p != NULL && (HWND)lp == p->warn && p->has_result && p->result.root_protected) {
            HDC dc = (HDC)wp;
            LRESULT brush = theme_ctlcolor_static(dc);
            SetTextColor(dc, THEME_COLOR_DANGER);
            return brush;
        }
        return theme_ctlcolor_static((HDC)wp);
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (p == NULL || hdr->idFrom != IDC_C_LIST) {
            break;
        }
        if (hdr->code == LVN_GETDISPINFOW) {
            on_get_dispinfo(p, (NMLVDISPINFOW *)lp);
            return 0;
        }
        if (hdr->code == NM_CLICK || hdr->code == NM_DBLCLK) {
            /* 체크 상자를 누른 경우만 바꾼다. */
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
    case WM_APP_CLEANUP_PROGRESS:
        if (p != NULL) {
            update_progress(p);
        }
        return 0;
    case WM_APP_CLEANUP_DONE:
        if (p != NULL) {
            finish_detect(p);
        }
        return 0;
    case WM_APP_TRASH_PROGRESS:
        if (p != NULL) {
            update_trash_progress(p);
        }
        return 0;
    case WM_APP_TRASH_DONE:
        if (p != NULL) {
            finish_trash(p);
        }
        return 0;
    case WM_DESTROY:
        /* 진행 중인 탐지는 멈추고 스레드가 끝날 때까지 기다린다. */
        if (p != NULL && p->job != NULL) {
            cleanup_job_cancel(p->job);
            cleanup_job_finish(p->job, NULL);
            p->job = NULL;
        }
        /* 휴지통 이동은 남은 항목을 건너뛰고, 옮기고 있던 항목이 끝날 때까지 기다린다. */
        if (p != NULL && p->trash != NULL) {
            clean_plan_run_cancel(p->trash);
            clean_plan_run_finish(p->trash, NULL, NULL);
            p->trash = NULL;
        }
        return 0;
    case WM_NCDESTROY:
        if (p != NULL) {
            free(p->view);
            free_states(p);
            project_list_free(&p->projects);
            cleanup_guard_free(&p->guard);
            cleanup_result_free(&p->result);
            free(p);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int cleanup_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = CLEANUP_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND cleanup_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    /* WS_EX_CONTROLPARENT: 부모의 IsDialogMessage 가 패널 안의 컨트롤로 Tab 이동하게 한다. */
    return CreateWindowExW(WS_EX_CONTROLPARENT, CLEANUP_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

int cleanup_panel_start(HWND panel, const wchar_t *root)
{
    CleanupPanel *p = panel_of(panel);
    wchar_t *target;
    int len;

    if (p == NULL || p->job != NULL || p->trash != NULL) {
        return 0;
    }
    if (root != NULL) {
        SetWindowTextW(p->path, root);
    }
    len = GetWindowTextLengthW(p->path);
    target = malloc(((size_t)len + 1) * sizeof(*target));
    if (target == NULL) {
        return 0;
    }
    GetWindowTextW(p->path, target, len + 1);
    if (target[0] == L'\0') {
        free(target);
        show_text(p, UI_T_C_NEED_PATH);
        return 0;
    }

    /* 이전 결과를 지운다. 가상 목록이 옛 항목을 읽지 않도록 개수부터 0 으로 만든다. */
    ListView_SetItemCountEx(p->list, 0, 0);
    free(p->view);
    p->view = NULL;
    p->view_len = 0;
    free_states(p);
    cleanup_result_free(&p->result);
    p->has_result = 0;
    update_summary(p);
    set_warning(p);
    p->progress_pos = 0;
    progress_set(p->progress, PROGRESS_BUSY, 0, PROGRESS_RANGE);

    {
        /* 탐지 뒤 같은 스레드에서 프로젝트(Git) 분석까지 한다(휴면 기준은 기본 90일). */
        ProjectOptions po;
        memset(&po, 0, sizeof(po));
        project_list_free(&p->projects);
        p->job = cleanup_job_start_ex(target, PROGRESS_INTERVAL_MS, &po, on_job_notify, panel);
    }
    free(target);
    if (p->job == NULL) {
        show_text(p, UI_T_C_START_FAILED);
        return 0;
    }
    set_busy(p, 1);
    show_text(p, UI_T_C_DETECTING);
    return 1;
}

void cleanup_panel_cancel(HWND panel)
{
    CleanupPanel *p = panel_of(panel);

    if (p != NULL && p->job != NULL) {
        cleanup_job_cancel(p->job);
        EnableWindow(p->cancel, FALSE);
        show_text(p, UI_T_SCAN_CANCELLING);
    } else if (p != NULL && p->trash != NULL) {
        clean_plan_run_cancel(p->trash);
        EnableWindow(p->cancel, FALSE);
        show_text(p, UI_T_C_CANCELLING_TRASH);
    }
}

int cleanup_panel_busy(HWND panel)
{
    CleanupPanel *p = panel_of(panel);

    return p != NULL && (p->job != NULL || p->trash != NULL);
}

size_t cleanup_panel_check_all(HWND panel, int check)
{
    CleanupPanel *p = panel_of(panel);
    size_t i;

    if (p == NULL || !p->has_plan || p->job != NULL || p->trash != NULL) {
        return 0;
    }
    for (i = 0; i < p->view_len; i++) {
        const CleanPlanTarget *t = plan_at(p, p->view[i]);
        /* [모두 선택]은 직접 골라야 하는 대상(미커밋 변경이 있는 프로젝트의 후보)을 고르지 않는다.
         * 해제는 모두 한다. */
        if (check && t != NULL && (t->flags & CLEAN_PLAN_MANUAL)) {
            continue;
        }
        set_checked(p, p->view[i], check);
    }
    InvalidateRect(p->list, NULL, FALSE);
    update_selection(p);
    return p->checked_count;
}

int cleanup_panel_trash(HWND panel, int confirm)
{
    CleanupPanel *p = panel_of(panel);
    CleanPlanOps ops;
    size_t *index, i, n = 0;

    if (p == NULL || !p->has_plan || p->job != NULL || p->trash != NULL) {
        return 0;
    }
    if (p->checked_count == 0) {
        show_text(p, UI_T_C_NOTHING_CHECKED);
        return 0;
    }
    /* 확인 창에는 큰 항목부터 보인다(목록과 같은 순서). 다른 필터에서 체크한 항목도 포함한다.
     * 실제 이동도 계획 실행 엔진이 큰 항목부터 한다. */
    index = malloc(p->checked_count * sizeof(*index));
    if (index == NULL) {
        return 0;
    }
    {
        size_t *order = malloc(p->result.len * sizeof(*order));
        size_t len = order != NULL ? cleanup_select(&p->result, CLEANUP_MASK_ALL, order,
                                                    p->result.len, NULL)
                                   : 0;
        for (i = 0; i < len && n < p->checked_count; i++) {
            if (is_checked(p, order[i])) {
                index[n++] = order[i];
            }
        }
        free(order);
    }
    if (n != p->checked_count) {
        free(index);
        return 0;
    }
    if (confirm && !confirm_trash(p, index, n)) {
        free(index);
        show_text(p, UI_T_C_TRASH_CANCELLED);
        return 0;
    }
    free(index);
    memset(&ops, 0, sizeof(ops)); /* 프로젝트 정리에는 영구 삭제 대상이 없다 */
    p->trash = clean_plan_run_start(&p->plan, &ops, on_trash_notify, panel);
    if (p->trash == NULL) {
        show_text(p, UI_T_C_TRASH_START_FAILED);
        return 0;
    }
    p->trash_count = n;
    p->trash_interactive = confirm;
    p->trash_started = GetTickCount64();
    progress_set(p->progress, PROGRESS_IDLE, 0, PROGRESS_RANGE);
    set_busy(p, 1);
    update_trash_progress(p);
    return 1;
}

void cleanup_panel_apply_language(HWND panel, HFONT font)
{
    CleanupPanel *p = panel_of(panel);
    HWND child, h[LABEL_COUNT];
    UiText t[LABEL_COUNT];
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
    label_texts(p, h, t);
    for (i = 0; i < LABEL_COUNT; i++) {
        SetWindowTextW(h[i], ui_text(t[i]));
    }
    SendMessageW(p->path, EM_SETCUEBANNER, TRUE, (LPARAM)ui_text(UI_T_C_PATH_CUE));
    fill_filters(p);
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
    measure_controls(p);
    set_warning(p);
    update_summary(p);
    update_selection(p);
    render_status(p);
    panel_layout(p);
    /* 목록 칸(유형·비고·복구·마지막 활동·정리)은 그릴 때 지금 언어로 만든다(가상 목록). */
    InvalidateRect(p->list, NULL, TRUE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int cleanup_panel_min_width(HWND panel)
{
    CleanupPanel *p = panel_of(panel);
    int gap, a, b;

    if (p == NULL) {
        return 0;
    }
    gap = pscale(p, 6);
    /* 첫 줄: 경로 칸 최소(80) + 단추 셋. 선택 줄: 단추 셋(선택 문구는 남는 폭만 쓴다). */
    a = pscale(p, 80) + p->w_browse + p->w_start + p->w_cancel + 3 * gap;
    b = p->w_check_all + p->w_check_none + p->w_trash + 2 * gap;
    return a > b ? a : b;
}

#define PANEL_CHECK(cond)                                                         \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fwprintf(stderr, L"cleanup self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                                   \
            failures++;                                                           \
        }                                                                         \
    } while (0)

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

int cleanup_panel_verify_lang(HWND panel)
{
    CleanupPanel *p = panel_of(panel);
    wchar_t buf[CLEANUP_JOB_DIR_LEN + 512], want[CLEANUP_JOB_DIR_LEN + 512];
    HWND h[LABEL_COUNT];
    UiText t[LABEL_COUNT];
    RECT r;
    int failures = 0, i, rows, other = ui_lang_current() != UI_LANG_KO;
    size_t f;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    failures += gui_status_text_verify();
    /* 단추·이름표: 지금 언어이고 잘리지 않는다 */
    label_texts(p, h, t);
    for (i = 0; i < LABEL_COUNT; i++) {
        GetWindowTextW(h[i], buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(wcscmp(buf, ui_text(t[i])) == 0);
        GetWindowRect(h[i], &r);
        PANEL_CHECK(r.right - r.left >= (i < 6 ? theme_button_width(p->hwnd, p->font, buf, p->dpi, 0)
                                               : theme_text_width(p->hwnd, p->font, buf)));
    }
    /* 필터·열 머리글: 지금 언어 */
    for (f = 0; f < FILTER_COUNT; f++) {
        SendMessageW(p->filter, CB_GETLBTEXT, (WPARAM)f, (LPARAM)buf);
        PANEL_CHECK(wcscmp(buf, filter_label(&g_filters[f])) == 0);
    }
    for (f = 0; f < ECO_FILTER_COUNT; f++) {
        SendMessageW(p->eco, CB_GETLBTEXT, (WPARAM)f, (LPARAM)buf);
        PANEL_CHECK(wcscmp(buf, filter_label(&g_eco_filters[f])) == 0);
    }
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = buf;
        col.cchTextMax = (int)(sizeof(buf) / sizeof(buf[0]));
        buf[0] = L'\0';
        SendMessageW(p->list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        PANEL_CHECK(wcscmp(buf, ui_text(k_cols[i].title)) == 0);
        PANEL_CHECK(ListView_GetColumnWidth(p->list, i) > theme_text_width(p->list, p->font, buf));
    }
    /* 안내·요약·선택·상태 줄: 지금 언어로 다시 쓰였다(작업 중에는 부르지 않음) */
    PANEL_CHECK(p->job == NULL && p->trash == NULL);
    GetWindowTextW(p->warn, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, ui_text(p->has_result && p->result.root_protected ? UI_T_C_WARN_PROTECTED
                                                                              : UI_T_C_WARN_INFO)) == 0);
    status_line(p, want, sizeof(want) / sizeof(want[0]));
    GetWindowTextW(p->status, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, want) == 0);
    PANEL_CHECK(!other || !has_hangul(buf));
    if (p->has_plan) {
        selection_text(p, want, sizeof(want) / sizeof(want[0]));
        GetWindowTextW(p->selection, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    if (p->has_result) {
        GetWindowTextW(p->summary, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(!other || !has_hangul(buf));
    }
    /* 목록 칸: 번역 문구 칸(유형·환경·비고·복구·마지막 활동·정리)은 지금 언어(경로·프로젝트 이름·Windows 오류
     * 설명은 번역 대상이 아님) */
    rows = ListView_GetItemCount(p->list);
    for (i = 0; i < rows; i++) {
        static const int cols[] = {COL_TYPE, COL_ECO, COL_NOTE, COL_RECOVERY, COL_ACTIVITY};
        size_t k;
        for (k = 0; k < sizeof(cols) / sizeof(cols[0]); k++) {
            ListView_GetItemText(p->list, i, cols[k], buf, (int)(sizeof(buf) / sizeof(buf[0])));
            PANEL_CHECK(!other || !has_hangul(buf));
        }
        if (p->has_plan && (size_t)i < p->view_len) {
            const CleanPlanTarget *pt = plan_at(p, p->view[i]);
            const CleanupTrashOutcome *o = last_outcome(pt);
            ListView_GetItemText(p->list, i, COL_CLEAN, buf, (int)(sizeof(buf) / sizeof(buf[0])));
            clean_text(pt, want, sizeof(want) / sizeof(want[0]));
            PANEL_CHECK(wcscmp(buf, want) == 0);
            /* 실패 칸에는 Windows 오류 설명이 들어가므로 한글 검사에서 뺀다. */
            if (other && o->status != CLEANUP_TRASH_FAILED && o->status != CLEANUP_TRASH_QUARANTINE_FAILED) {
                PANEL_CHECK(!has_hangul(buf));
            }
        }
    }
    return failures;
}

/* 목록 칸의 글자를 읽는다(가상 목록이라 LVN_GETDISPINFO 를 거친다). */
static void cell_text(const CleanupPanel *p, int row, int col, wchar_t *buf, int len)
{
    buf[0] = L'\0';
    ListView_GetItemText(p->list, row, col, buf, len);
}

int cleanup_panel_verify(HWND panel, int expect_cancelled)
{
    CleanupPanel *p = panel_of(panel);
    int failures = 0, rows, i;
    size_t f;
    wchar_t buf[64];

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    PANEL_CHECK(p->job == NULL && p->has_result);
    PANEL_CHECK(IsWindowEnabled(p->start) && !IsWindowEnabled(p->cancel));
    PANEL_CHECK(SendMessageW(p->filter, CB_GETCURSEL, 0, 0) == 0);
    PANEL_CHECK(SendMessageW(p->eco, CB_GETCURSEL, 0, 0) == 0);

    if (expect_cancelled) {
        if (p->result.report.cancelled) {
            PANEL_CHECK(p->result.status == SCAN_PARTIAL);
            PANEL_CHECK(progress_state(p->progress) == PROGRESS_CANCELLED);
        }
    } else {
        PANEL_CHECK(!p->result.report.cancelled);
        PANEL_CHECK(p->result.status != SCAN_FATAL);
        PANEL_CHECK(SendMessageW(p->progress, PBM_GETPOS, 0, 0) == PROGRESS_RANGE);
        PANEL_CHECK(progress_state(p->progress) == PROGRESS_DONE);
    }
    /* Git 분석 단계의 움직이는 막대는 끝나면 멈추고, 막대가 비어 사라져 보이지 않는다. */
    PANEL_CHECK((GetWindowLongW(p->progress, GWL_STYLE) & PBS_MARQUEE) == 0);
    PANEL_CHECK(SendMessageW(p->progress, PBM_GETPOS, 0, 0) > 0);

    /* 전체 필터: 모든 항목, 파일 크기 내림차순, 표시 합계 = 전체 파일 크기 */
    rows = ListView_GetItemCount(p->list);
    PANEL_CHECK(rows == (int)p->result.len && p->view_len == p->result.len);
    PANEL_CHECK(p->view_bytes == p->result.total_bytes);
    for (i = 1; i < rows; i++) {
        PANEL_CHECK(p->result.items[p->view[i - 1]].bytes >=
                    p->result.items[p->view[i]].bytes);
    }
    if (rows > 0) {
        const CleanupItem *top = &p->result.items[p->view[0]];
        wchar_t expect[64];

        cell_text(p, 0, COL_SIZE, buf, 64);
        format_size(top->bytes, expect, 64);
        PANEL_CHECK(wcscmp(buf, expect) == 0);
        /* 실제 확보: 탐지에서 센 값(할당 크기·하드 링크) */
        PANEL_CHECK(top->flags & CLEANUP_ITEM_MEASURED);
        cell_text(p, 0, COL_RECLAIM, buf, 64);
        format_size(cleanup_item_reclaim(top), expect, 64);
        PANEL_CHECK(wcscmp(buf, expect) == 0);
        cell_text(p, 0, COL_TYPE, buf, 64);
        PANEL_CHECK(wcscmp(buf, category_label(top->category)) == 0);
        cell_text(p, 0, COL_ECO, buf, 64);
        PANEL_CHECK(wcscmp(buf, eco_label(top->eco)) == 0);
    }

    /* 유형별 필터: 개수·합계가 결과의 유형별 집계와 같고, 다른 유형은 없다 */
    for (f = 1; f < FILTER_COUNT; f++) {
        CleanupCategory category = CLEANUP_NONE;
        size_t c, k;

        for (c = 1; c < CLEANUP_CATEGORY_COUNT; c++) {
            if (g_filters[f].mask == CLEANUP_MASK(c)) {
                category = (CleanupCategory)c;
            }
        }
        SendMessageW(p->filter, CB_SETCURSEL, f, 0);
        apply_filter(p);
        PANEL_CHECK(category != CLEANUP_NONE);
        PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->result.category_count[category]);
        PANEL_CHECK(p->view_bytes == p->result.category_bytes[category]);
        for (k = 0; k < p->view_len; k++) {
            PANEL_CHECK(p->result.items[p->view[k]].category == category);
        }
    }
    SendMessageW(p->filter, CB_SETCURSEL, 0, 0);
    apply_filter(p);
    PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->result.len);

    /* 개발 환경 필터: 개수·합계가 결과의 환경별 집계와 같고, 유형 필터와 함께 걸린다 */
    for (f = 1; f < ECO_FILTER_COUNT; f++) {
        CleanupEco eco = CLEANUP_ECO_COUNT;
        size_t e, k;

        for (e = 0; e < CLEANUP_ECO_COUNT; e++) {
            if (g_eco_filters[f].mask == CLEANUP_ECO_BIT(e)) {
                eco = (CleanupEco)e;
            }
        }
        PANEL_CHECK(eco != CLEANUP_ECO_COUNT);
        if (eco == CLEANUP_ECO_COUNT) {
            continue;
        }
        SendMessageW(p->eco, CB_SETCURSEL, f, 0);
        apply_filter(p);
        PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->result.eco_count[eco]);
        PANEL_CHECK(p->view_bytes == p->result.eco_bytes[eco]);
        for (k = 0; k < p->view_len; k++) {
            PANEL_CHECK(p->result.items[p->view[k]].eco == eco);
        }
        /* 유형(빌드 결과물) × 환경 */
        SendMessageW(p->filter, CB_SETCURSEL, 2, 0);
        apply_filter(p);
        for (k = 0; k < p->view_len; k++) {
            PANEL_CHECK(p->result.items[p->view[k]].eco == eco &&
                        p->result.items[p->view[k]].category == CLEANUP_BUILD);
        }
        SendMessageW(p->filter, CB_SETCURSEL, 0, 0);
    }
    SendMessageW(p->eco, CB_SETCURSEL, 0, 0);
    apply_filter(p);
    PANEL_CHECK(ListView_GetItemCount(p->list) == (int)p->result.len);

    /* 프로젝트 분석: 끝까지 탐지했으면 후보마다 프로젝트·복구 가능성이 있고 계획과 맞는다.
     * 미커밋 변경이 있는 프로젝트의 후보는 직접 골라야 한다(MANUAL). */
    if (!expect_cancelled && p->has_plan) {
        size_t k, dirty = 0;
        PANEL_CHECK(p->projects.item_count == p->result.len && p->projects.len > 0);
        for (k = 0; k < p->plan.len && p->projects.item_count == p->result.len; k++) {
            const CleanPlanTarget *t = &p->plan.items[k];
            const ProjectInfo *pi;
            PANEL_CHECK(t->project == p->projects.item_project[t->origin]);
            PANEL_CHECK(t->recovery == (int)p->projects.item_recovery[t->origin]);
            if (t->project >= p->projects.len) {
                continue;
            }
            pi = &p->projects.items[t->project];
            PANEL_CHECK(((t->flags & CLEAN_PLAN_DIRTY) != 0) == (pi->changes > 0));
            PANEL_CHECK(((t->flags & CLEAN_PLAN_DORMANT) != 0) == (pi->dormant != 0));
            if (t->flags & CLEAN_PLAN_DIRTY) {
                PANEL_CHECK(t->flags & CLEAN_PLAN_MANUAL);
                dirty++;
            }
        }
        if (rows > 0) {
            cell_text(p, 0, COL_PROJECT, buf, 64);
            PANEL_CHECK(buf[0] != L'\0');
            cell_text(p, 0, COL_RECOVERY, buf, 64);
            PANEL_CHECK(buf[0] != L'\0');
            cell_text(p, 0, COL_ACTIVITY, buf, 64);
            PANEL_CHECK(buf[0] != L'\0');
        }
        wprintf(L"projects: %u, dirty candidates: %u\n", (unsigned)p->projects.len,
                (unsigned)dirty);
        fflush(stdout);
    }
    return failures;
}

int cleanup_panel_verify_trash(HWND panel, size_t expect_moved)
{
    CleanupPanel *p = panel_of(panel);
    int failures = 0;
    size_t i, allowed = 0, moved = 0;
    wchar_t buf[512], want[512];

    PANEL_CHECK(p != NULL && p->has_plan);
    if (p == NULL || !p->has_plan) {
        return failures;
    }
    PANEL_CHECK(p->trash == NULL && p->job == NULL);
    PANEL_CHECK(IsWindowEnabled(p->start) && !IsWindowEnabled(p->cancel));
    PANEL_CHECK(expect_moved > 0 && p->trash_count == expect_moved);
    PANEL_CHECK(p->moved_count == expect_moved);
    PANEL_CHECK(progress_state(p->progress) == PROGRESS_DONE); /* 모두 옮겼으면 가득·기본 색 */
    PANEL_CHECK(p->checked_count == 0 && p->checked_bytes == 0);
    PANEL_CHECK(!IsWindowEnabled(p->trash_btn));
    GetWindowTextW(p->status, buf, 256);
    PANEL_CHECK(p->status_kind == C_STATUS_TRASHED);
    status_line(p, want, sizeof(want) / sizeof(want[0]));
    PANEL_CHECK(wcscmp(buf, want) == 0);
    if (ui_lang_current() == UI_LANG_KO) {
        PANEL_CHECK(wcsncmp(buf, L"휴지통으로 이동 완료: 성공 ", wcslen(L"휴지통으로 이동 완료: 성공 ")) == 0);
    }

    /* 계획은 탐지 결과와 같은 순서이고, 보호 대상은 선택되지 않았다(옮긴 목록에 없다). */
    PANEL_CHECK(p->plan.len == p->result.len);
    for (i = 0; i < p->plan.len; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        PANEL_CHECK(t->origin == i && t->source == CLEAN_SOURCE_PROJECT && t->entry_count == 1);
        PANEL_CHECK(!t->ran || t->deny == CLEANUP_TRASH_PENDING);
    }
    for (i = 0; i < p->result.len; i++) {
        const CleanPlanTarget *t = &p->plan.items[i];
        const CleanupTrashOutcome *o = last_outcome(t);
        const CleanupItem *item = &p->result.items[i];

        if (t->deny == CLEANUP_TRASH_PENDING) {
            allowed++;
        }
        if (o->status == CLEANUP_TRASH_MOVED || o->status == CLEANUP_TRASH_QUARANTINED) {
            moved++;
            PANEL_CHECK(GetFileAttributesW(item->path) == INVALID_FILE_ATTRIBUTES);
            PANEL_CHECK(o->recycled_path != NULL &&
                        GetFileAttributesW(o->recycled_path) != INVALID_FILE_ATTRIBUTES);
            PANEL_CHECK(!can_check(p, i) && !t->selected);
            if (o->recycled_path != NULL) {
                wprintf(L"recycled: %ls\n", o->recycled_path);
            }
        } else {
            if (t->deny == CLEANUP_TRASH_PENDING) {
                /* 옮기려던 항목이 실패했다: 원인을 알 수 있게 상태와 오류 코드를 쓴다. */
                fwprintf(stderr, L"trash item: %ls code=0x%08lx %ls\n",
                         cleanup_trash_status_name(o->status), (unsigned long)o->code, item->path);
            }
            /* 옮기지 않은 항목(보호 대상)은 그대로 남아 있고 체크할 수 없다. */
            PANEL_CHECK(t->deny != CLEANUP_TRASH_PENDING && o->status == CLEANUP_TRASH_PENDING);
            PANEL_CHECK(GetFileAttributesW(item->path) != INVALID_FILE_ATTRIBUTES);
            set_checked(p, i, 1);
            PANEL_CHECK(!t->selected);
        }
    }
    fflush(stdout);
    PANEL_CHECK(moved == expect_moved && allowed == expect_moved);
    PANEL_CHECK(p->checked_count == 0);

    /* 정리 열: 옮긴 항목은 "휴지통으로 이동됨"·"격리 폴더로 이동됨", 보호 대상은 "보호: ..." */
    for (i = 0; i < p->view_len; i++) {
        const CleanPlanTarget *t = &p->plan.items[p->view[i]];
        CleanupTrashStatus st = last_outcome(t)->status;
        ListView_GetItemText(p->list, (int)i, COL_CLEAN, buf, 512);
        if (st == CLEANUP_TRASH_MOVED || st == CLEANUP_TRASH_QUARANTINED) {
            PANEL_CHECK(wcscmp(buf, gui_trash_status_text(st)) == 0);
            /* 한국어 화면은 core 문구와 같다. */
            PANEL_CHECK(ui_lang_current() != UI_LANG_KO || wcscmp(buf, cleanup_trash_status_text(st)) == 0);
        } else {
            swprintf(want, 512, ui_text(UI_T_C_CLEAN_PROTECTED), gui_trash_status_text(t->deny));
            PANEL_CHECK(wcscmp(buf, want) == 0);
        }
    }
    GetWindowTextW(p->selection, buf, 512);
    selection_text(p, want, 512);
    PANEL_CHECK(p->moved_count > 0 && wcscmp(buf, want) == 0);
    return failures;
}

size_t cleanup_panel_quarantined(HWND panel)
{
    CleanupPanel *p = panel_of(panel);
    size_t i, n = 0;

    for (i = 0; p != NULL && p->has_plan && i < p->plan.len; i++) {
        n += last_outcome(&p->plan.items[i])->status == CLEANUP_TRASH_QUARANTINED;
    }
    return n;
}

size_t cleanup_panel_restore_quarantined(HWND panel)
{
    CleanupPanel *p = panel_of(panel);
    size_t i, k, restored = 0;

    for (i = 0; p != NULL && p->has_plan && i < p->plan.len && i < p->result.len; i++) {
        const CleanupTrashOutcome *o = last_outcome(&p->plan.items[i]);
        const CleanupItem *item = &p->result.items[i];
        QuarantineStatus st = QUARANTINE_FAILED;
        QuarantineList list;
        wchar_t *volume;
        uint32_t code = 0;

        if (o->status != CLEANUP_TRASH_QUARANTINED || o->recycled_path == NULL) {
            continue;
        }
        /* 이번 정리가 격리한 바로 그 항목(격리 위치가 같은 것)만 복원한다. */
        volume = quarantine_volume_root(item->path);
        quarantine_list_init(&list);
        if (volume != NULL && quarantine_list_volume(volume, &list) == QUARANTINE_OK) {
            for (k = 0; k < list.len; k++) {
                const QuarantineEntry *e = &list.items[k];
                if (e->state == QUARANTINE_ENTRY_OK && e->item != NULL &&
                    _wcsicmp(e->item, o->recycled_path) == 0) {
                    st = quarantine_restore(e, NULL, &code);
                    break;
                }
            }
        }
        if (st == QUARANTINE_OK) {
            restored++;
            wprintf(L"restored: %ls\n", item->path);
        } else {
            wprintf(L"restore failed: %ls code=%lu %ls\n", quarantine_status_name(st),
                    (unsigned long)code, item->path);
        }
        quarantine_list_free(&list);
        free(volume);
    }
    fflush(stdout);
    return restored;
}
