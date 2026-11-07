#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "booster.h"
#include "gui_booster.h"
#include "gui_theme.h"
#include "size_format.h"
#include "sysmon.h"

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

    SysMonitor mon;
    SysmonSample sample;

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

static void update_summary(BoosterPanel *p)
{
    wchar_t text[320], used[32], total[32], cpu[32];
    size_t i, background = 0;

    sysmon_sample(&p->mon, &p->sample);
    for (i = 0; i < p->procs.len; i++) {
        background += !p->procs.items[i].has_window;
    }
    if (p->sample.cpu.status == SYSMON_OK) {
        swprintf(cpu, 32, L"%u.%u%%", p->sample.cpu.usage_permille / 10,
                 p->sample.cpu.usage_permille % 10);
    } else {
        swprintf(cpu, 32, L"측정 중");
    }
    if (p->sample.memory.status == SYSMON_OK) {
        format_size(p->sample.memory.used_bytes, used, 32);
        format_size(p->sample.memory.total_bytes, total, 32);
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"CPU %ls · 메모리 %ls / %ls (%u%%) · 프로세스 %u개(백그라운드 %u개)", cpu, used,
                 total, p->sample.memory.usage_permille / 10, (unsigned)p->procs.len,
                 (unsigned)background);
    } else {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"CPU %ls · 메모리 측정 실패", cpu);
    }
    SetWindowTextW(p->summary, text);
}

/* ---- 시작 프로그램 ---- */

static void update_startup_info(BoosterPanel *p)
{
    wchar_t text[256];
    size_t i, enabled = 0, checked = 0;

    for (i = 0; i < p->startup.len; i++) {
        enabled += p->startup.items[i].enabled;
        checked += p->srows != NULL && p->srows[i].checked;
    }
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"%u개 중 사용 %u개 · 선택 %u개%ls", (unsigned)p->startup.len, (unsigned)enabled,
             (unsigned)checked,
             p->startup.errors > 0 ? L" · 일부 위치를 읽지 못함" : L"");
    SetWindowTextW(p->start_info, text);
    EnableWindow(p->disable_btn, checked > 0);
    EnableWindow(p->enable_btn, checked > 0);
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
        swprintf(buf, len, L"바꿈");
    } else if (r->applied && r->result == ERROR_ACCESS_DENIED) {
        swprintf(buf, len, L"바꾸지 못함: 관리자 권한 필요");
    } else if (r->applied) {
        swprintf(buf, len, L"바꾸지 못함(오류 %lu)", r->result);
    } else if (booster_source_needs_admin(p->startup.items[i].source)) {
        swprintf(buf, len, L"바꾸려면 관리자 권한 필요");
    }
}

/* 체크한 항목을 켜거나 끈다(등록은 지우지 않음). */
static void apply_startup(BoosterPanel *p, int enable)
{
    size_t i, ok = 0, failed = 0;
    wchar_t text[160];

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
    swprintf(text, 160, L"시작 프로그램 %u개를 %ls으로 바꿈%ls", (unsigned)ok,
             enable ? L"사용" : L"사용 안 함", failed > 0 ? L" · 일부 실패(행의 결과 참고)" : L"");
    update_startup_info(p);
    SetWindowTextW(p->start_info, text);
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

static void update_proc_info(BoosterPanel *p)
{
    wchar_t text[200], size[32];
    size_t i, checked = 0;
    uint64_t mem = 0;

    for (i = 0; p->prows != NULL && i < p->procs.len; i++) {
        if (p->prows[i].checked) {
            checked++;
            mem += p->procs.items[i].working_set;
        }
    }
    format_size(mem, size, 32);
    swprintf(text, 200, L"선택 %u개 · 확보 예상 메모리 %ls", (unsigned)checked, size);
    SetWindowTextW(p->proc_info, p->measuring ? L"프로세스 목록과 CPU 사용률을 읽는 중..." : text);
    EnableWindow(p->end_btn, !p->measuring && checked > 0);
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
    APPEND(L"선택한 프로세스 %u개를 끝냅니다(확보 예상 메모리 %ls).\n\n", (unsigned)checked, size);
    for (i = 0; i < p->procs.len && shown < CONFIRM_LIST_MAX; i++) {
        const BoosterProcess *pr = &p->procs.items[i];
        if (!p->prows[i].checked) {
            continue;
        }
        format_size(pr->working_set, size, 32);
        APPEND(L"■ %ls (PID %lu, %ls, %ls)\n", pr->name, (unsigned long)pr->pid, size,
               pr->has_window ? L"앱" : L"백그라운드");
        shown++;
    }
    if (checked > shown) {
        APPEND(L"... 외 %u개\n", (unsigned)(checked - shown));
    }
    APPEND(L"\n프로세스를 바로 끝내므로 저장하지 않은 작업은 사라질 수 있습니다. 끝낸 프로그램은 "
           L"다시 실행할 수 있으며, 시작 프로그램이면 다음 로그인 때 다시 실행됩니다.\n"
           L"시스템·Windows 구성 요소·ByteSieve 는 고를 수 없고, 목록을 읽은 뒤 다른 프로세스로 "
           L"바뀐 경우(같은 PID 재사용)에는 끝내지 않습니다.\n\n계속할까요?");
#undef APPEND
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, L"Booster - 프로세스 끝내기",
                         MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    free(text);
    return answer == IDYES;
}

static void end_processes(BoosterPanel *p)
{
    size_t i, ok = 0, failed = 0;
    uint64_t mem = 0;
    wchar_t text[200], size[32];

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
    format_size(mem, size, 32);
    swprintf(text, 200, L"끝냄 %u개(메모리 약 %ls) · 끝내지 못함 %u개(행의 상태 참고)",
             (unsigned)ok, size, (unsigned)failed);
    InvalidateRect(p->proc_list, NULL, TRUE);
    update_proc_info(p);
    SetWindowTextW(p->proc_info, text);
}

static void proc_state_text(const BoosterPanel *p, size_t i, wchar_t *buf, size_t len)
{
    const BoosterProcess *pr = &p->procs.items[i];
    const ProcRow *r = &p->prows[i];

    if (r->ended) {
        if (r->result.status == BOOSTER_END_PROTECTED) {
            swprintf(buf, len, L"%ls: %ls", booster_end_text(r->result.status),
                     booster_protect_text(r->result.protect));
        } else if (r->result.code != 0) {
            swprintf(buf, len, L"%ls (오류 %lu)", booster_end_text(r->result.status),
                     r->result.code);
        } else {
            swprintf(buf, len, L"%ls", booster_end_text(r->result.status));
        }
    } else if (pr->protect != BOOSTER_PROTECT_NONE) {
        swprintf(buf, len, L"보호: %ls", booster_protect_text(pr->protect));
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
        di->item.pszText = it->enabled ? (wchar_t *)L"사용" : (wchar_t *)L"사용 안 함";
        break;
    case SCOL_SOURCE:
        di->item.pszText = (wchar_t *)booster_source_text(it->source);
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
        di->item.pszText = pr->has_window ? (wchar_t *)L"앱" : (wchar_t *)L"백그라운드";
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
    int h = pscale(p, 24), gap = pscale(p, 6), bw = pscale(p, 100), label_w = pscale(p, 110);
    int w, y, x, start_h, proc_h, rest;

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    y = 0;
    MoveWindow(p->refresh, 0, y, bw, h, TRUE);
    MoveWindow(p->summary, bw + 2 * gap, y + pscale(p, 4), w - bw - 2 * gap, pscale(p, 20), TRUE);
    y += h + gap;
    rest = rc.bottom - y - 2 * (h + gap) - gap;
    start_h = rest * 2 / 5;
    proc_h = rest - start_h;

    MoveWindow(p->start_label, 0, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    x = label_w;
    MoveWindow(p->disable_btn, x, y, bw, h, TRUE);
    x += bw + gap;
    MoveWindow(p->enable_btn, x, y, bw, h, TRUE);
    x += bw + 2 * gap;
    MoveWindow(p->start_info, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->start_list, 0, y, w, start_h > 0 ? start_h : 0, TRUE);
    y += (start_h > 0 ? start_h : 0) + gap;

    MoveWindow(p->proc_label, 0, y + pscale(p, 4), label_w, pscale(p, 20), TRUE);
    x = label_w;
    MoveWindow(p->background, x, y, pscale(p, 130), h, TRUE);
    x += pscale(p, 130) + gap;
    MoveWindow(p->end_btn, x, y, pscale(p, 170), h, TRUE);
    x += pscale(p, 170) + 2 * gap;
    MoveWindow(p->proc_info, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->proc_list, 0, y, w, proc_h > 0 ? proc_h : 0, TRUE);
}

static HWND panel_child(BoosterPanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)p->font, FALSE);
    return h;
}

static void add_columns(BoosterPanel *p, HWND list, const wchar_t *const *titles,
                        const int *widths, const int *fmts, int count)
{
    int i;

    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                LVS_EX_CHECKBOXES | LVS_EX_INFOTIP);
    ListView_SetCallbackMask(list, LVIS_STATEIMAGEMASK);
    for (i = 0; i < count; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)titles[i];
        col.cx = pscale(p, widths[i]);
        col.fmt = fmts[i];
        SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
}

static void panel_create_controls(BoosterPanel *p)
{
    static const wchar_t *const stitles[SCOL_COUNT] = {L"이름", L"상태", L"위치", L"명령",
                                                       L"결과"};
    static const int swidths[SCOL_COUNT] = {180, 80, 200, 420, 220};
    static const int sfmts[SCOL_COUNT] = {LVCFMT_LEFT, LVCFMT_LEFT, LVCFMT_LEFT, LVCFMT_LEFT,
                                          LVCFMT_LEFT};
    static const wchar_t *const ptitles[PCOL_COUNT] = {L"이름", L"PID", L"종류", L"CPU",
                                                       L"메모리", L"상태", L"경로"};
    static const int pwidths[PCOL_COUNT] = {180, 60, 80, 60, 90, 220, 400};
    static const int pfmts[PCOL_COUNT] = {LVCFMT_LEFT,  LVCFMT_RIGHT, LVCFMT_LEFT, LVCFMT_RIGHT,
                                          LVCFMT_RIGHT, LVCFMT_LEFT,  LVCFMT_LEFT};
    DWORD list_style = LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_TABSTOP;

    p->refresh = panel_child(p, L"BUTTON", L"새로고침", BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_B_REFRESH);
    p->summary = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                             IDC_B_SUMMARY);
    p->start_label = panel_child(p, L"STATIC", L"시작 프로그램", SS_LEFT, 0, IDC_B_START_LABEL);
    p->disable_btn = panel_child(p, L"BUTTON", L"사용 안 함", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                 IDC_B_DISABLE);
    p->enable_btn = panel_child(p, L"BUTTON", L"사용", BS_PUSHBUTTON | WS_TABSTOP, 0,
                                IDC_B_ENABLE);
    p->start_info = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                                IDC_B_START_INFO);
    p->start_list = panel_child(p, WC_LISTVIEWW, L"", list_style, WS_EX_CLIENTEDGE,
                                IDC_B_START_LIST);
    p->proc_label = panel_child(p, L"STATIC", L"프로세스", SS_LEFT, 0, IDC_B_PROC_LABEL);
    p->background = panel_child(p, L"BUTTON", L"백그라운드만", BS_AUTOCHECKBOX | WS_TABSTOP, 0,
                                IDC_B_BACKGROUND);
    /* 끝내기 단추에는 단축 글자(&)를 두지 않는다(실수로 눌리지 않게, 확인 창 기본은 [아니요]). */
    p->end_btn = panel_child(p, L"BUTTON", L"선택한 프로세스 끝내기...", BS_PUSHBUTTON | WS_TABSTOP,
                             0, IDC_B_END);
    p->proc_info = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                               IDC_B_PROC_INFO);
    p->proc_list = panel_child(p, WC_LISTVIEWW, L"", list_style, WS_EX_CLIENTEDGE,
                               IDC_B_PROC_LIST);
    add_columns(p, p->start_list, stitles, swidths, sfmts, SCOL_COUNT);
    add_columns(p, p->proc_list, ptitles, pwidths, pfmts, PCOL_COUNT);
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
    PANEL_CHECK(wcsncmp(buf, L"CPU ", 4) == 0 && wcsstr(buf, L"프로세스 ") != NULL);

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
            PANEL_CHECK(wcsstr(buf, booster_protect_text(BOOSTER_PROTECT_SELF)) != NULL);
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
        PANEL_CHECK(wcscmp(buf, p->startup.items[i].enabled ? L"사용" : L"사용 안 함") == 0);
    }
    PANEL_CHECK(!IsWindowEnabled(p->end_btn));
    return failures;
}
