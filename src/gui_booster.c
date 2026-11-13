#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "booster.h"
#include "gui_booster.h"
#include "gui_status_text.h"
#include "gui_theme.h"
#include "size_format.h"
#include "sysmon.h"
#include "ui_lang.h"

#define BOOSTER_PANEL_CLASS L"ByteSieveBoosterPanel"

#define TIMER_SUMMARY 1 /* 1초마다 전체 CPU·메모리 */
#define TIMER_CPU 2     /* 목록을 다시 읽어 프로세스별 CPU 를 계산(한 번) */
#define SUMMARY_INTERVAL_MS 1000
#define CPU_INTERVAL_MS 1000
#define END_WAIT_MS 1000
#define CONFIRM_LIST_MAX 15

#define CHECK_NONE 0
#define CHECK_OFF 1
#define CHECK_ON 2

enum {
    IDC_B_REFRESH = 100,
    IDC_B_SUMMARY,
    IDC_B_START_LABEL,
    IDC_B_DISABLE,
    IDC_B_ENABLE,
    IDC_B_START_INFO,
    IDC_B_START_LIST,
    IDC_B_PROC_LABEL,
    IDC_B_BACKGROUND,
    IDC_B_END,
    IDC_B_PROC_INFO,
    IDC_B_PROC_LIST
};

enum { SCOL_NAME, SCOL_STATE, SCOL_SOURCE, SCOL_COMMAND, SCOL_RESULT, SCOL_COUNT };
enum { PCOL_NAME, PCOL_PID, PCOL_KIND, PCOL_CPU, PCOL_MEM, PCOL_STATE, PCOL_PATH, PCOL_COUNT };

/* 열 머리글과 기본 폭(한국어 화면의 예전 폭). */
typedef struct {
    UiText title;
    int width;
    int fmt;
} ColumnDef;

static const ColumnDef k_scols[SCOL_COUNT] = {
    {UI_T_B_COL_NAME, 180, LVCFMT_LEFT},    {UI_T_B_COL_STATE, 80, LVCFMT_LEFT},
    {UI_T_B_COL_SOURCE, 200, LVCFMT_LEFT},  {UI_T_B_COL_COMMAND, 420, LVCFMT_LEFT},
    {UI_T_B_COL_RESULT, 220, LVCFMT_LEFT},
};
static const ColumnDef k_pcols[PCOL_COUNT] = {
    {UI_T_B_COL_NAME, 180, LVCFMT_LEFT},  {UI_T_B_COL_PID, 60, LVCFMT_RIGHT},
    {UI_T_B_COL_KIND, 80, LVCFMT_LEFT},   {UI_T_B_COL_CPU, 60, LVCFMT_RIGHT},
    {UI_T_B_COL_MEMORY, 90, LVCFMT_RIGHT}, {UI_T_B_COL_STATE, 220, LVCFMT_LEFT},
    {UI_T_B_COL_PATH, 400, LVCFMT_LEFT},
};

typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

typedef struct {
    int checked;
    int applied;          /* 바꾸기를 시도했음 */
    unsigned long result; /* 0 성공, 아니면 Windows 오류 */
} StartupRow;

typedef struct {
    int checked;
    int ended; /* 끝내기를 시도했음 */
    BoosterEndResult result;
} ProcRow;

typedef struct {
    HWND hwnd;
    HWND refresh, summary, start_label, disable_btn, enable_btn, start_info, start_list;
    HWND proc_label, background, end_btn, proc_info, proc_list;
    HFONT font;
    int dpi;
    int started;
    /* 문구에 맞춘 폭(언어를 바꾸면 다시 잰다). */
    int w_refresh, w_disable, w_enable, w_background, w_end, label_w;

    SysMonitor mon;
    SysmonSample sample;
    int has_sample;

    /* 정보 줄에 마지막으로 보인 작업 결과(언어를 바꾸면 이것으로 다시 쓴다). */
    struct {
        int shown;
        int enable;
        unsigned ok;
        int partial;
    } start_note;
    struct {
        int shown;
        unsigned ok, failed;
        uint64_t mem;
    } proc_note;

    BoosterStartupList startup;
    StartupRow *srows;

    BoosterProcessList prev, procs;
    ProcRow *prows;
    size_t *order; /* 보이는 행 → procs 인덱스(필터·정렬) */
    size_t visible;
    int sort_col;
    int sort_desc;
    int measuring; /* 두 번째 목록을 기다리는 중 */
    int has_procs;

    wchar_t cell[512];
} BoosterPanel;

static BoosterPanel *panel_of(HWND hwnd)
{
    return (BoosterPanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const BoosterPanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

static int proc_can_check(const BoosterPanel *p, size_t i)
{
    return p->prows != NULL && !p->prows[i].ended &&
           p->procs.items[i].protect == BOOSTER_PROTECT_NONE;
}

/* ---- 요약 ---- */

/* 요약 줄 문구(마지막 측정값을 지금 언어로). 아직 측정하지 않았으면 빈 문자열. */
static void summary_text(const BoosterPanel *p, wchar_t *text, size_t len)
{
    wchar_t used[32], total[32], cpu[32];
    size_t i, background = 0;

    text[0] = L'\0';
    if (!p->has_sample) {
        return;
    }
    for (i = 0; i < p->procs.len; i++) {
        background += !p->procs.items[i].has_window;
    }
    if (p->sample.cpu.status == SYSMON_OK) {
        swprintf(cpu, 32, L"%u.%u%%", p->sample.cpu.usage_permille / 10,
                 p->sample.cpu.usage_permille % 10);
    } else {
        swprintf(cpu, 32, L"%ls", ui_text(UI_T_B_MEASURING));
    }
    if (p->sample.memory.status == SYSMON_OK) {
        format_size(p->sample.memory.used_bytes, used, 32);
        format_size(p->sample.memory.total_bytes, total, 32);
        swprintf(text, len, ui_text(UI_T_B_SUMMARY), cpu, used, total,
                 p->sample.memory.usage_permille / 10, (unsigned)p->procs.len, (unsigned)background);
    } else {
        swprintf(text, len, ui_text(UI_T_B_SUMMARY_MEM_FAIL), cpu);
    }
}

static void render_summary(BoosterPanel *p)
{
    wchar_t text[320];

    summary_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->summary, text);
}

/* 새로 측정해 요약 줄을 쓴다. */
static void update_summary(BoosterPanel *p)
{
    sysmon_sample(&p->mon, &p->sample);
    p->has_sample = 1;
    render_summary(p);
}

/* ---- 시작 프로그램 ---- */

/* 시작 프로그램 정보 줄: 마지막 바꾸기 결과, 없으면 개수·선택 수. */
static void start_info_text(const BoosterPanel *p, wchar_t *text, size_t len)
{
    size_t i, enabled = 0, checked = 0;

    if (p->start_note.shown) {
        swprintf(text, len, ui_text(p->start_note.enable ? UI_T_B_APPLIED_ON : UI_T_B_APPLIED_OFF),
                 p->start_note.ok, p->start_note.partial ? ui_text(UI_T_B_PARTIAL_FAIL) : L"");
        return;
    }
    for (i = 0; i < p->startup.len; i++) {
        enabled += p->startup.items[i].enabled;
        checked += p->srows != NULL && p->srows[i].checked;
    }
    swprintf(text, len, ui_text(UI_T_B_START_INFO), (unsigned)p->startup.len, (unsigned)enabled,
             (unsigned)checked, p->startup.errors > 0 ? ui_text(UI_T_B_START_READ_ERRORS) : L"");
}

static void render_start_info(BoosterPanel *p)
{
    wchar_t text[256];
    size_t i, checked = 0;

    for (i = 0; p->srows != NULL && i < p->startup.len; i++) {
        checked += p->srows[i].checked;
    }
    start_info_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->start_info, text);
    EnableWindow(p->disable_btn, checked > 0);
    EnableWindow(p->enable_btn, checked > 0);
}

/* 선택이 바뀌면 결과 대신 개수·선택 수를 보인다. */
static void update_startup_info(BoosterPanel *p)
{
    p->start_note.shown = 0;
    render_start_info(p);
}

static void load_startup(BoosterPanel *p)
{
    ListView_SetItemCountEx(p->start_list, 0, 0);
    free(p->srows);
    p->srows = NULL;
    booster_startup_list(&p->startup, NULL);
    p->srows = calloc(p->startup.len > 0 ? p->startup.len : 1, sizeof(*p->srows));
    ListView_SetItemCountEx(p->start_list, p->srows != NULL ? (int)p->startup.len : 0, 0);
    update_startup_info(p);
}

static void startup_result_text(const BoosterPanel *p, size_t i, wchar_t *buf, size_t len)
{
    const StartupRow *r = &p->srows[i];

    buf[0] = L'\0';
    if (r->applied && r->result == 0) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_B_RES_CHANGED));
    } else if (r->applied && r->result == ERROR_ACCESS_DENIED) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_B_RES_DENIED));
    } else if (r->applied) {
        swprintf(buf, len, ui_text(UI_T_B_RES_FAILED), r->result);
    } else if (booster_source_needs_admin(p->startup.items[i].source)) {
        swprintf(buf, len, L"%ls", ui_text(UI_T_B_RES_NEEDS_ADMIN));
    }
}

/* 체크한 항목을 켜거나 끈다(등록은 지우지 않음). */
static void apply_startup(BoosterPanel *p, int enable)
{
    size_t i, ok = 0, failed = 0;

    for (i = 0; p->srows != NULL && i < p->startup.len; i++) {
        BoosterStartupItem *it = &p->startup.items[i];
        if (!p->srows[i].checked) {
            continue;
        }
        p->srows[i].checked = 0;
        if (it->enabled == enable) {
            continue;
        }
        p->srows[i].applied = 1;
        p->srows[i].result = booster_startup_set(it, enable, NULL);
        if (p->srows[i].result == 0) {
            it->enabled = enable;
            ok++;
        } else {
            failed++;
        }
    }
    p->start_note.shown = 1;
    p->start_note.enable = enable;
    p->start_note.ok = (unsigned)ok;
    p->start_note.partial = failed > 0;
    render_start_info(p);
    InvalidateRect(p->start_list, NULL, TRUE);
}

/* ---- 프로세스 ---- */

static int cmp_u64(uint64_t a, uint64_t b)
{
    return a < b ? -1 : a > b;
}

static int compare_proc(const BoosterPanel *p, size_t a, size_t b)
{
    const BoosterProcess *x = &p->procs.items[a], *y = &p->procs.items[b];

    switch (p->sort_col) {
    case PCOL_NAME:
        return _wcsicmp(x->name, y->name);
    case PCOL_PID:
        return cmp_u64(x->pid, y->pid);
    case PCOL_KIND:
        return x->has_window - y->has_window;
    case PCOL_CPU:
        return cmp_u64(x->cpu_permille, y->cpu_permille);
    case PCOL_STATE:
        return (int)x->protect - (int)y->protect;
    case PCOL_PATH:
        return _wcsicmp(x->path != NULL ? x->path : L"", y->path != NULL ? y->path : L"");
    default:
        return cmp_u64(x->working_set, y->working_set);
    }
}

/* 필터·정렬로 보이는 행 순서를 만든다. */
static void build_order(BoosterPanel *p)
{
    int background_only = SendMessageW(p->background, BM_GETCHECK, 0, 0) == BST_CHECKED;
    size_t i, j, n = 0;

    for (i = 0; p->order != NULL && i < p->procs.len; i++) {
        size_t cur = i;
        if (background_only && p->procs.items[i].has_window) {
            continue;
        }
        for (j = n; j > 0; j--) {
            int r = compare_proc(p, p->order[j - 1], cur);
            if (p->sort_desc) {
                r = -r;
            }
            if (r <= 0) {
                break;
            }
            p->order[j] = p->order[j - 1];
        }
        p->order[j] = cur;
        n++;
    }
    p->visible = n;
    ListView_SetItemCountEx(p->proc_list, (int)n, 0);
    InvalidateRect(p->proc_list, NULL, FALSE);
}

/* 프로세스 정보 줄: 읽는 중, 마지막 끝내기 결과, 없으면 선택 수·확보 예상 메모리. */
static void proc_info_text(const BoosterPanel *p, wchar_t *text, size_t len)
{
    wchar_t size[32];
    size_t i, checked = 0;
    uint64_t mem = 0;

    if (p->measuring) {
        swprintf(text, len, L"%ls", ui_text(UI_T_B_PROC_READING));
        return;
    }
    if (p->proc_note.shown) {
        format_size(p->proc_note.mem, size, 32);
        swprintf(text, len, ui_text(UI_T_B_END_RESULT), p->proc_note.ok, size, p->proc_note.failed);
        return;
    }
    for (i = 0; p->prows != NULL && i < p->procs.len; i++) {
        if (p->prows[i].checked) {
            checked++;
            mem += p->procs.items[i].working_set;
        }
    }
    format_size(mem, size, 32);
    swprintf(text, len, ui_text(UI_T_B_PROC_INFO), (unsigned)checked, size);
}

static void render_proc_info(BoosterPanel *p)
{
    wchar_t text[256];
    size_t i, checked = 0;

    for (i = 0; p->prows != NULL && i < p->procs.len; i++) {
        checked += p->prows[i].checked;
    }
    proc_info_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->proc_info, text);
    EnableWindow(p->end_btn, !p->measuring && checked > 0);
}

/* 선택·목록이 바뀌면 결과 대신 선택 수를 보인다. */
static void update_proc_info(BoosterPanel *p)
{
    p->proc_note.shown = 0;
    render_proc_info(p);
}

static void start_process_refresh(BoosterPanel *p)
{
    booster_process_list(&p->prev);
    p->measuring = 1;
    EnableWindow(p->refresh, FALSE);
    update_proc_info(p);
    SetTimer(p->hwnd, TIMER_CPU, CPU_INTERVAL_MS, NULL);
}

static void finish_process_refresh(BoosterPanel *p)
{
    KillTimer(p->hwnd, TIMER_CPU);
    ListView_SetItemCountEx(p->proc_list, 0, 0);
    free(p->prows);
    free(p->order);
    p->prows = NULL;
    p->order = NULL;
    booster_process_list(&p->procs);
    booster_process_cpu(&p->prev, &p->procs, 0);
    booster_process_list_free(&p->prev);
    p->prows = calloc(p->procs.len > 0 ? p->procs.len : 1, sizeof(*p->prows));
    p->order = malloc((p->procs.len > 0 ? p->procs.len : 1) * sizeof(*p->order));
    if (p->prows == NULL || p->order == NULL) {
        free(p->prows);
        free(p->order);
        p->prows = NULL;
        p->order = NULL;
        booster_process_list_free(&p->procs);
    }
    p->measuring = 0;
    p->has_procs = p->prows != NULL;
    EnableWindow(p->refresh, TRUE);
    build_order(p);
    update_proc_info(p);
    update_summary(p);
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), BPN_REFRESHED), (LPARAM)p->hwnd);
}

static int confirm_end(BoosterPanel *p)
{
    size_t i, shown = 0, cap = 2048 + CONFIRM_LIST_MAX * 200, used = 0, checked = 0;
    wchar_t *text = malloc(cap * sizeof(*text)), size[32];
    uint64_t mem = 0;
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
    for (i = 0; i < p->procs.len; i++) {
        if (p->prows[i].checked) {
            checked++;
            mem += p->procs.items[i].working_set;
        }
    }
    format_size(mem, size, 32);
    APPEND(ui_text(UI_T_B_CONFIRM_HEAD), (unsigned)checked, size);
    for (i = 0; i < p->procs.len && shown < CONFIRM_LIST_MAX; i++) {
        const BoosterProcess *pr = &p->procs.items[i];
        if (!p->prows[i].checked) {
            continue;
        }
        format_size(pr->working_set, size, 32);
        APPEND(L"■ %ls (PID %lu, %ls, %ls)\n", pr->name, (unsigned long)pr->pid, size,
               ui_text(pr->has_window ? UI_T_B_KIND_APP : UI_T_B_KIND_BACKGROUND));
        shown++;
    }
    if (checked > shown) {
        APPEND(ui_text(UI_T_B_CONFIRM_MORE), (unsigned)(checked - shown));
    }
    APPEND(L"%ls", ui_text(UI_T_B_CONFIRM_WARN));
#undef APPEND
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_B_CONFIRM_TITLE),
                         MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    free(text);
    return answer == IDYES;
}

static void end_processes(BoosterPanel *p)
{
    size_t i, ok = 0, failed = 0;
    uint64_t mem = 0;

    if (p->prows == NULL || p->measuring || !confirm_end(p)) {
        return;
    }
    for (i = 0; i < p->procs.len; i++) {
        ProcRow *r = &p->prows[i];
        if (!r->checked) {
            continue;
        }
        r->checked = 0;
        r->ended = 1;
        r->result = booster_process_end(&p->procs.items[i], END_WAIT_MS);
        if (r->result.status == BOOSTER_END_OK) {
            ok++;
            mem += p->procs.items[i].working_set;
        } else {
            failed++;
        }
    }
    InvalidateRect(p->proc_list, NULL, TRUE);
    p->proc_note.shown = 1;
    p->proc_note.ok = (unsigned)ok;
    p->proc_note.failed = (unsigned)failed;
    p->proc_note.mem = mem;
    render_proc_info(p);
}

static void proc_state_text(const BoosterPanel *p, size_t i, wchar_t *buf, size_t len)
{
    const BoosterProcess *pr = &p->procs.items[i];
    const ProcRow *r = &p->prows[i];

    if (r->ended) {
        if (r->result.status == BOOSTER_END_PROTECTED) {
            swprintf(buf, len, L"%ls: %ls", gui_booster_end_text(r->result.status),
                     gui_booster_protect_text(r->result.protect));
        } else if (r->result.code != 0) {
            swprintf(buf, len, ui_text(UI_T_B_STATE_ERROR), gui_booster_end_text(r->result.status),
                     r->result.code);
        } else {
            swprintf(buf, len, L"%ls", gui_booster_end_text(r->result.status));
        }
    } else if (pr->protect != BOOSTER_PROTECT_NONE) {
        swprintf(buf, len, ui_text(UI_T_B_PROTECTED), gui_booster_protect_text(pr->protect));
    } else {
        buf[0] = L'\0';
    }
}

/* ---- 목록 표시 ---- */

static void start_dispinfo(BoosterPanel *p, NMLVDISPINFOW *di)
{
    size_t i = (size_t)di->item.iItem;
    const BoosterStartupItem *it;

    if (p->srows == NULL || i >= p->startup.len) {
        return;
    }
    it = &p->startup.items[i];
    if (di->item.mask & LVIF_STATE) {
        di->item.state = INDEXTOSTATEIMAGEMASK(p->srows[i].checked ? CHECK_ON : CHECK_OFF);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    }
    if (!(di->item.mask & LVIF_TEXT)) {
        return;
    }
    p->cell[0] = L'\0';
    di->item.pszText = p->cell;
    switch (di->item.iSubItem) {
    case SCOL_NAME:
        di->item.pszText = it->name;
        break;
    case SCOL_STATE:
        di->item.pszText = (wchar_t *)ui_text(it->enabled ? UI_T_B_STATE_ON : UI_T_B_STATE_OFF);
        break;
    case SCOL_SOURCE:
        di->item.pszText = (wchar_t *)gui_booster_source_text(it->source);
        break;
    case SCOL_COMMAND:
        di->item.pszText = it->command;
        break;
    case SCOL_RESULT:
        startup_result_text(p, i, p->cell, 512);
        break;
    }
}

static void proc_dispinfo(BoosterPanel *p, NMLVDISPINFOW *di)
{
    size_t row = (size_t)di->item.iItem, i;
    const BoosterProcess *pr;

    if (p->order == NULL || row >= p->visible) {
        return;
    }
    i = p->order[row];
    pr = &p->procs.items[i];
    if (di->item.mask & LVIF_STATE) {
        int image = proc_can_check(p, i) ? (p->prows[i].checked ? CHECK_ON : CHECK_OFF) : CHECK_NONE;
        di->item.state = INDEXTOSTATEIMAGEMASK(image);
        di->item.stateMask = LVIS_STATEIMAGEMASK;
    }
    if (!(di->item.mask & LVIF_TEXT)) {
        return;
    }
    p->cell[0] = L'\0';
    di->item.pszText = p->cell;
    switch (di->item.iSubItem) {
    case PCOL_NAME:
        di->item.pszText = pr->name;
        break;
    case PCOL_PID:
        swprintf(p->cell, 32, L"%lu", (unsigned long)pr->pid);
        break;
    case PCOL_KIND:
        di->item.pszText = (wchar_t *)ui_text(pr->has_window ? UI_T_B_KIND_APP : UI_T_B_KIND_BACKGROUND);
        break;
    case PCOL_CPU:
        swprintf(p->cell, 32, L"%u.%u%%", pr->cpu_permille / 10, pr->cpu_permille % 10);
        break;
    case PCOL_MEM:
        if (pr->info) {
            format_size(pr->working_set, p->cell, 64);
        } else {
            swprintf(p->cell, 32, L"-");
        }
        break;
    case PCOL_STATE:
        proc_state_text(p, i, p->cell, 512);
        break;
    case PCOL_PATH:
        di->item.pszText = pr->path != NULL ? pr->path : (wchar_t *)L"";
        break;
    }
}

/* 체크 상자 클릭·Space. */
static void toggle_start(BoosterPanel *p, int row)
{
    if (p->srows != NULL && row >= 0 && (size_t)row < p->startup.len) {
        p->srows[row].checked = !p->srows[row].checked;
        ListView_RedrawItems(p->start_list, row, row);
        update_startup_info(p);
    }
}

static void toggle_proc(BoosterPanel *p, int row)
{
    if (p->order != NULL && !p->measuring && row >= 0 && (size_t)row < p->visible) {
        size_t i = p->order[row];
        p->prows[i].checked = !p->prows[i].checked && proc_can_check(p, i);
        ListView_RedrawItems(p->proc_list, row, row);
        update_proc_info(p);
    }
}

static void sort_procs(BoosterPanel *p, int col)
{
    HWND header = ListView_GetHeader(p->proc_list);
    int c;

    if (p->sort_col == col) {
        p->sort_desc = !p->sort_desc;
    } else {
        p->sort_col = col;
        p->sort_desc = col == PCOL_CPU || col == PCOL_MEM;
    }
    for (c = 0; header != NULL && c < PCOL_COUNT; c++) {
        HDITEMW hd;
        memset(&hd, 0, sizeof(hd));
        hd.mask = HDI_FORMAT;
        Header_GetItem(header, c, &hd);
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == p->sort_col) {
            hd.fmt |= p->sort_desc ? HDF_SORTDOWN : HDF_SORTUP;
        }
        Header_SetItem(header, c, &hd);
    }
    build_order(p);
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(BoosterPanel *p)
{
    RECT rc;
    int h = pscale(p, 24), gap = pscale(p, 6), label_w = p->label_w;
    int w, y, x, start_h, proc_h, rest;

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    y = 0;
    MoveWindow(p->refresh, 0, y, p->w_refresh, h, TRUE);
    x = p->w_refresh + 2 * gap;
    MoveWindow(p->summary, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    rest = rc.bottom - y - 2 * (h + gap) - gap;
    start_h = rest * 2 / 5;
    proc_h = rest - start_h;

    MoveWindow(p->start_label, 0, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    x = label_w;
    MoveWindow(p->disable_btn, x, y, p->w_disable, h, TRUE);
    x += p->w_disable + gap;
    MoveWindow(p->enable_btn, x, y, p->w_enable, h, TRUE);
    x += p->w_enable + 2 * gap;
    MoveWindow(p->start_info, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->start_list, 0, y, w, start_h > 0 ? start_h : 0, TRUE);
    y += (start_h > 0 ? start_h : 0) + gap;

    MoveWindow(p->proc_label, 0, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    x = label_w;
    MoveWindow(p->background, x, y, p->w_background, h, TRUE);
    x += p->w_background + gap;
    MoveWindow(p->end_btn, x, y, p->w_end, h, TRUE);
    x += p->w_end + 2 * gap;
    MoveWindow(p->proc_info, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->proc_list, 0, y, w, proc_h > 0 ? proc_h : 0, TRUE);
}

/*
 * 단추·체크 상자·이름표 폭: 예전 고정 폭(단추 100, [백그라운드만] 130, 끝내기 170, 이름표 110)을 최소로
 * 두고(한국어 화면은 그대로), 지금 글꼴로 잰 문구가 더 길면 넓힌다.
 */
static void measure_controls(BoosterPanel *p)
{
    int a, b;

    p->w_refresh = theme_button_width(p->hwnd, p->font, ui_text(UI_T_B_REFRESH), p->dpi, pscale(p, 100));
    p->w_disable = theme_button_width(p->hwnd, p->font, ui_text(UI_T_B_DISABLE), p->dpi, pscale(p, 100));
    p->w_enable = theme_button_width(p->hwnd, p->font, ui_text(UI_T_B_ENABLE), p->dpi, pscale(p, 100));
    p->w_background =
        theme_check_width(p->hwnd, p->font, ui_text(UI_T_B_BACKGROUND_ONLY), p->dpi, pscale(p, 130));
    p->w_end = theme_button_width(p->hwnd, p->font, ui_text(UI_T_B_END), p->dpi, pscale(p, 170));
    a = theme_text_width(p->hwnd, p->font, ui_text(UI_T_B_START_LABEL));
    b = theme_text_width(p->hwnd, p->font, ui_text(UI_T_B_PROC_LABEL));
    a = (a > b ? a : b) + pscale(p, 8);
    p->label_w = a > pscale(p, 110) ? a : pscale(p, 110);
}

/* 열 폭: 기본 폭과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(BoosterPanel *p, HWND list, const ColumnDef *def)
{
    int need = theme_text_width(list, p->font, ui_text(def->title)) + pscale(p, 16);
    return pscale(p, def->width) > need ? pscale(p, def->width) : need;
}

static HWND panel_child(BoosterPanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)p->font, FALSE);
    return h;
}

static void add_columns(BoosterPanel *p, HWND list, const ColumnDef *cols, int count)
{
    int i;

    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                LVS_EX_CHECKBOXES | LVS_EX_INFOTIP);
    ListView_SetCallbackMask(list, LVIS_STATEIMAGEMASK);
    for (i = 0; i < count; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)ui_text(cols[i].title);
        col.cx = column_width(p, list, &cols[i]);
        col.fmt = cols[i].fmt;
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
}

/* 언어를 바꾼 뒤 열 머리글을 다시 쓰고, 새 머리글이 잘리지 않게 넓힌다(사용자가 넓힌 폭은 줄이지 않음). */
static void retitle_columns(BoosterPanel *p, HWND list, const ColumnDef *cols, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        LVCOLUMNW col;
        int need = column_width(p, list, &cols[i]);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = (wchar_t *)ui_text(cols[i].title);
        SendMessageW(list, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        if (ListView_GetColumnWidth(list, i) < need) {
            ListView_SetColumnWidth(list, i, need);
        }
    }
}

/* 단추·이름표·체크 상자와 문구(위에서 아래, 왼쪽부터). */
#define CONTROL_TEXTS 7
static void control_texts(BoosterPanel *p, HWND *h, UiText *t)
{
    h[0] = p->refresh, t[0] = UI_T_B_REFRESH;
    h[1] = p->start_label, t[1] = UI_T_B_START_LABEL;
    h[2] = p->disable_btn, t[2] = UI_T_B_DISABLE;
    h[3] = p->enable_btn, t[3] = UI_T_B_ENABLE;
    h[4] = p->proc_label, t[4] = UI_T_B_PROC_LABEL;
    h[5] = p->background, t[5] = UI_T_B_BACKGROUND_ONLY;
    h[6] = p->end_btn, t[6] = UI_T_B_END;
}

static void panel_create_controls(BoosterPanel *p)
{
    DWORD list_style = LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_TABSTOP;

    p->refresh = panel_child(p, L"BUTTON", ui_text(UI_T_B_REFRESH), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_B_REFRESH);
    p->summary = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                             IDC_B_SUMMARY);
    p->start_label = panel_child(p, L"STATIC", ui_text(UI_T_B_START_LABEL), SS_LEFT, 0, IDC_B_START_LABEL);
    p->disable_btn = panel_child(p, L"BUTTON", ui_text(UI_T_B_DISABLE), BS_PUSHBUTTON | WS_TABSTOP, 0,
                                 IDC_B_DISABLE);
    p->enable_btn = panel_child(p, L"BUTTON", ui_text(UI_T_B_ENABLE), BS_PUSHBUTTON | WS_TABSTOP, 0,
                                IDC_B_ENABLE);
    p->start_info = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                                IDC_B_START_INFO);
    p->start_list = panel_child(p, WC_LISTVIEWW, L"", list_style, WS_EX_CLIENTEDGE,
                                IDC_B_START_LIST);
    p->proc_label = panel_child(p, L"STATIC", ui_text(UI_T_B_PROC_LABEL), SS_LEFT, 0, IDC_B_PROC_LABEL);
    p->background = panel_child(p, L"BUTTON", ui_text(UI_T_B_BACKGROUND_ONLY), BS_AUTOCHECKBOX | WS_TABSTOP,
                                0, IDC_B_BACKGROUND);
    /* 끝내기 단추에는 단축 글자(&)를 두지 않는다(실수로 눌리지 않게, 확인 창 기본은 [아니요]). */
    p->end_btn = panel_child(p, L"BUTTON", ui_text(UI_T_B_END), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_B_END);
    p->proc_info = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                               IDC_B_PROC_INFO);
    p->proc_list = panel_child(p, WC_LISTVIEWW, L"", list_style, WS_EX_CLIENTEDGE,
                               IDC_B_PROC_LIST);
    add_columns(p, p->start_list, k_scols, SCOL_COUNT);
    add_columns(p, p->proc_list, k_pcols, PCOL_COUNT);
    measure_controls(p);
    p->sort_col = PCOL_MEM;
    p->sort_desc = 1;
    EnableWindow(p->disable_btn, FALSE);
    EnableWindow(p->enable_btn, FALSE);
    EnableWindow(p->end_btn, FALSE);
}

static LRESULT on_list_notify(BoosterPanel *p, NMHDR *hdr)
{
    int start = hdr->idFrom == IDC_B_START_LIST;

    if (hdr->code == LVN_GETDISPINFOW) {
        if (start) {
            start_dispinfo(p, (NMLVDISPINFOW *)hdr);
        } else {
            proc_dispinfo(p, (NMLVDISPINFOW *)hdr);
        }
        return 0;
    }
    if (hdr->code == NM_CLICK || hdr->code == NM_DBLCLK) {
        LVHITTESTINFO ht;
        memset(&ht, 0, sizeof(ht));
        ht.pt = ((NMITEMACTIVATE *)hdr)->ptAction;
        if (ListView_HitTest(hdr->hwndFrom, &ht) >= 0 && (ht.flags & LVHT_ONITEMSTATEICON)) {
            if (start) {
                toggle_start(p, ht.iItem);
            } else {
                toggle_proc(p, ht.iItem);
            }
        }
        return 0;
    }
    if (hdr->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)hdr)->wVKey == VK_SPACE) {
        int row = ListView_GetNextItem(hdr->hwndFrom, -1, LVNI_FOCUSED);
        if (start) {
            toggle_start(p, row);
        } else {
            toggle_proc(p, row);
        }
        return 0;
    }
    if (hdr->code == LVN_COLUMNCLICK && !start && p->order != NULL) {
        sort_procs(p, ((NMLISTVIEW *)hdr)->iSubItem);
        return 0;
    }
    return 0;
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    BoosterPanel *p = panel_of(hwnd);

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
        sysmon_init(&p->mon, NULL, NULL, NULL);
        booster_startup_list_init(&p->startup);
        booster_process_list_init(&p->prev);
        booster_process_list_init(&p->procs);
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
    case WM_TIMER:
        if (wp == TIMER_SUMMARY) {
            update_summary(p);
        } else if (wp == TIMER_CPU) {
            finish_process_refresh(p);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_B_REFRESH:
            booster_panel_refresh(hwnd);
            return 0;
        case IDC_B_DISABLE:
            apply_startup(p, 0);
            return 0;
        case IDC_B_ENABLE:
            apply_startup(p, 1);
            return 0;
        case IDC_B_BACKGROUND:
            build_order(p);
            return 0;
        case IDC_B_END:
            end_processes(p);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (p != NULL && (hdr->idFrom == IDC_B_START_LIST || hdr->idFrom == IDC_B_PROC_LIST)) {
            return on_list_notify(p, hdr);
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_SUMMARY);
        KillTimer(hwnd, TIMER_CPU);
        return 0;
    case WM_NCDESTROY:
        if (p != NULL) {
            free(p->srows);
            free(p->prows);
            free(p->order);
            booster_startup_list_free(&p->startup);
            booster_process_list_free(&p->prev);
            booster_process_list_free(&p->procs);
            free(p);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int booster_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = BOOSTER_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND booster_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(WS_EX_CONTROLPARENT, BOOSTER_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

int booster_panel_refresh(HWND panel)
{
    BoosterPanel *p = panel_of(panel);

    if (p == NULL || p->measuring) {
        return 0;
    }
    load_startup(p);
    start_process_refresh(p);
    update_summary(p);
    return 1;
}

void booster_panel_apply_language(HWND panel, HFONT font)
{
    BoosterPanel *p = panel_of(panel);
    HWND child, h[CONTROL_TEXTS];
    UiText t[CONTROL_TEXTS];
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
    control_texts(p, h, t);
    for (i = 0; i < CONTROL_TEXTS; i++) {
        SetWindowTextW(h[i], ui_text(t[i]));
    }
    retitle_columns(p, p->start_list, k_scols, SCOL_COUNT);
    retitle_columns(p, p->proc_list, k_pcols, PCOL_COUNT);
    measure_controls(p);
    /* 요약·정보 줄은 마지막 측정값·결과를 새 언어로(다시 측정하지 않음), 목록 칸은 그릴 때 지금 언어로. */
    render_summary(p);
    if (p->started) {
        render_start_info(p);
        render_proc_info(p);
    }
    panel_layout(p);
    InvalidateRect(p->start_list, NULL, TRUE);
    InvalidateRect(p->proc_list, NULL, TRUE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int booster_panel_min_width(HWND panel)
{
    BoosterPanel *p = panel_of(panel);
    int gap, a, b;

    if (p == NULL) {
        return 0;
    }
    /* 단추 줄 둘(이름표 + 단추 둘, 이름표 + 체크 상자 + 끝내기)과 간격(정보 줄은 남는 폭만 쓴다). */
    gap = pscale(p, 6);
    a = p->label_w + p->w_disable + gap + p->w_enable + 2 * gap;
    b = p->label_w + p->w_background + gap + p->w_end + 2 * gap;
    return a > b ? a : b;
}

int booster_panel_start(HWND panel)
{
    BoosterPanel *p = panel_of(panel);

    if (p == NULL || p->started) {
        return p != NULL;
    }
    p->started = 1;
    SetTimer(panel, TIMER_SUMMARY, SUMMARY_INTERVAL_MS, NULL);
    return booster_panel_refresh(panel);
}

#define PANEL_CHECK(cond)                                                         \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fwprintf(stderr, L"booster self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                                   \
            failures++;                                                           \
        }                                                                         \
    } while (0)

int booster_panel_verify(HWND panel)
{
    BoosterPanel *p = panel_of(panel);
    int failures = 0, found_self = 0;
    size_t i, can = 0, row;
    wchar_t buf[512];

    PANEL_CHECK(p != NULL && p->has_procs && !p->measuring);
    if (p == NULL || !p->has_procs) {
        return failures;
    }
    PANEL_CHECK(p->procs.len > 10);
    PANEL_CHECK(p->visible == p->procs.len); /* 필터 없음 */
    PANEL_CHECK(ListView_GetItemCount(p->proc_list) == (int)p->visible);
    PANEL_CHECK(ListView_GetItemCount(p->start_list) == (int)p->startup.len);
    GetWindowTextW(p->summary, buf, 512);
    {
        wchar_t want[512];
        summary_text(p, want, 512);
        PANEL_CHECK(wcsncmp(buf, L"CPU ", 4) == 0 && wcscmp(buf, want) == 0);
    }

    /* 기본 정렬: 메모리 내림차순 */
    for (row = 1; row < p->visible; row++) {
        PANEL_CHECK(p->procs.items[p->order[row - 1]].working_set >=
                    p->procs.items[p->order[row]].working_set);
    }
    for (row = 0; row < p->visible; row++) {
        size_t idx = p->order[row];
        const BoosterProcess *pr = &p->procs.items[idx];
        if (pr->pid == GetCurrentProcessId()) {
            found_self = 1;
            PANEL_CHECK(pr->protect == BOOSTER_PROTECT_SELF && !proc_can_check(p, idx));
            ListView_GetItemText(p->proc_list, (int)row, PCOL_STATE, buf, 512);
            PANEL_CHECK(wcsstr(buf, gui_booster_protect_text(BOOSTER_PROTECT_SELF)) != NULL);
            ListView_GetItemText(p->proc_list, (int)row, PCOL_NAME, buf, 512);
            PANEL_CHECK(_wcsicmp(buf, pr->name) == 0);
        }
        if (pr->protect != BOOSTER_PROTECT_NONE) {
            /* 보호 대상은 체크해도 체크되지 않는다 */
            toggle_proc(p, (int)row);
            PANEL_CHECK(!p->prows[idx].checked);
        } else {
            can++;
        }
    }
    PANEL_CHECK(found_self);
    PANEL_CHECK(can < p->procs.len); /* 보호 대상이 반드시 있다(시스템·자신) */
    /* 백그라운드 필터 */
    SendMessageW(p->background, BM_SETCHECK, BST_CHECKED, 0);
    build_order(p);
    for (row = 0; row < p->visible; row++) {
        PANEL_CHECK(!p->procs.items[p->order[row]].has_window);
    }
    SendMessageW(p->background, BM_SETCHECK, BST_UNCHECKED, 0);
    build_order(p);
    PANEL_CHECK(p->visible == p->procs.len);
    /* 시작 프로그램 행 내용 */
    for (i = 0; i < p->startup.len; i++) {
        ListView_GetItemText(p->start_list, (int)i, SCOL_NAME, buf, 512);
        PANEL_CHECK(wcscmp(buf, p->startup.items[i].name) == 0);
        ListView_GetItemText(p->start_list, (int)i, SCOL_STATE, buf, 512);
        PANEL_CHECK(wcscmp(buf, ui_text(p->startup.items[i].enabled ? UI_T_B_STATE_ON : UI_T_B_STATE_OFF)) ==
                    0);
    }
    PANEL_CHECK(!IsWindowEnabled(p->end_btn));
    return failures;
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

/* 열 머리글이 cols 의 지금 언어 문구이고 잘리지 않는지. 실패 수. */
static int verify_columns(BoosterPanel *p, HWND list, const ColumnDef *cols, int count)
{
    wchar_t buf[128];
    int failures = 0, i;

    for (i = 0; i < count; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = buf;
        col.cchTextMax = 128;
        buf[0] = L'\0';
        SendMessageW(list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        PANEL_CHECK(wcscmp(buf, ui_text(cols[i].title)) == 0);
        PANEL_CHECK(ListView_GetColumnWidth(list, i) > theme_text_width(list, p->font, buf));
    }
    return failures;
}

int booster_panel_verify_lang(HWND panel)
{
    BoosterPanel *p = panel_of(panel);
    wchar_t buf[512], want[512];
    HWND h[CONTROL_TEXTS];
    UiText t[CONTROL_TEXTS];
    RECT r;
    size_t i, row;
    int failures = 0, other = ui_lang_current() != UI_LANG_KO;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    /* 단추·이름표·체크 상자: 지금 언어 문구이고 잘리지 않는다 */
    control_texts(p, h, t);
    for (i = 0; i < CONTROL_TEXTS; i++) {
        int need;
        GetWindowTextW(h[i], buf, 512);
        PANEL_CHECK(wcscmp(buf, ui_text(t[i])) == 0);
        if (h[i] == p->start_label || h[i] == p->proc_label) {
            need = theme_text_width(p->hwnd, p->font, buf);
        } else if (h[i] == p->background) {
            need = theme_check_width(p->hwnd, p->font, buf, p->dpi, 0);
        } else {
            need = theme_button_width(p->hwnd, p->font, buf, p->dpi, 0);
        }
        GetWindowRect(h[i], &r);
        PANEL_CHECK(r.right - r.left >= need);
    }
    failures += verify_columns(p, p->start_list, k_scols, SCOL_COUNT);
    failures += verify_columns(p, p->proc_list, k_pcols, PCOL_COUNT);
    /* 요약·정보 줄: 마지막 측정값·결과를 지금 언어로 쓴 것 */
    summary_text(p, want, 512);
    GetWindowTextW(p->summary, buf, 512);
    PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    if (p->started) {
        start_info_text(p, want, 512);
        GetWindowTextW(p->start_info, buf, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
        proc_info_text(p, want, 512);
        GetWindowTextW(p->proc_info, buf, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    /* 목록: 시작 프로그램의 상태·위치·결과, 프로세스의 종류·상태 칸(이름·명령·경로는 번역 대상이 아님) */
    for (i = 0; p->srows != NULL && i < p->startup.len; i++) {
        ListView_GetItemText(p->start_list, (int)i, SCOL_STATE, buf, 512);
        PANEL_CHECK(wcscmp(buf, ui_text(p->startup.items[i].enabled ? UI_T_B_STATE_ON : UI_T_B_STATE_OFF)) == 0);
        PANEL_CHECK(!other || !has_hangul(buf));
        ListView_GetItemText(p->start_list, (int)i, SCOL_SOURCE, buf, 512);
        PANEL_CHECK(wcscmp(buf, gui_booster_source_text(p->startup.items[i].source)) == 0);
        PANEL_CHECK(!other || !has_hangul(buf));
        ListView_GetItemText(p->start_list, (int)i, SCOL_RESULT, buf, 512);
        startup_result_text(p, i, want, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    for (row = 0; p->order != NULL && row < p->visible; row++) {
        size_t idx = p->order[row];
        ListView_GetItemText(p->proc_list, (int)row, PCOL_KIND, buf, 512);
        PANEL_CHECK(wcscmp(buf, ui_text(p->procs.items[idx].has_window ? UI_T_B_KIND_APP
                                                                        : UI_T_B_KIND_BACKGROUND)) == 0);
        PANEL_CHECK(!other || !has_hangul(buf));
        ListView_GetItemText(p->proc_list, (int)row, PCOL_STATE, buf, 512);
        proc_state_text(p, idx, want, 512);
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    return failures;
}
