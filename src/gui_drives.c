#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "drive.h"
#include "gui_drives.h"
#include "gui_status_text.h"
#include "gui_theme.h"
#include "size_format.h"
#include "ui_lang.h"

#define DRIVE_PANEL_CLASS L"ByteSieveDrivePanel"
#define WM_APP_DRIVES_DONE (WM_APP + 1)

/* 이 사용률(천분율) 이상이면 막대를 경고 색으로 그린다. */
#define USAGE_WARN_PERMILLE 900

enum { IDC_REFRESH = 100, IDC_SUMMARY, IDC_DRIVE_LIST };

/* 상태 줄에 마지막으로 보인 것(언어를 바꾸면 이것으로 다시 쓴다). */
typedef enum {
    SUMMARY_HINT,          /* 아직 조회하지 않음: 안내 */
    SUMMARY_LOADING,       /* 조회 중 */
    SUMMARY_RESULT,        /* 조회 결과(목록 실패 또는 요약) */
    SUMMARY_START_NOMEM,   /* 새로고침 시작 실패(메모리 부족) */
    SUMMARY_START_NOTHREAD /* 새로고침 시작 실패(스레드 생성 실패) */
} SummaryKind;

/* 목록 열. */
enum {
    COL_ROOT,
    COL_LABEL,
    COL_TYPE,
    COL_FS,
    COL_ENCRYPTION,
    COL_TOTAL,
    COL_USED,
    COL_FREE,
    COL_USAGE,
    COL_COUNT
};

typedef struct {
    HWND hwnd, refresh, summary, list;
    HFONT font;
    int dpi;
    int w_refresh;          /* [새로고침] 폭(문구에 맞춤) */
    SummaryKind summary_kind;
    SYSTEMTIME as_of;       /* 마지막 조회를 마친 시각(요약의 "기준") */

    DriveList drives;
    int loaded;
    int last_rc;            /* 마지막 drive_list_query 결과 */
    unsigned long last_error;
    int busy;
    unsigned generation;    /* 오래된 조회 결과를 버리기 위한 번호 */
} DrivePanel;

/* CreateWindowExW 로 넘기는 생성 인자. */
typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

/* 작업 스레드가 채워서 패널로 넘기는 결과. 받은 쪽이 free 한다. */
typedef struct {
    HWND panel;
    unsigned generation;
    int rc;
    unsigned long error;
    DriveList drives;
} RefreshJob;

static DrivePanel *panel_of(HWND hwnd)
{
    return (DrivePanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const DrivePanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

/* 열 머리글과 기본 폭(한국어 화면의 예전 폭). */
static const struct {
    UiText title;
    int width;
    int fmt;
} k_cols[COL_COUNT] = {
    {UI_T_DR_COL_ROOT, 64, LVCFMT_LEFT},        {UI_T_DR_COL_LABEL, 140, LVCFMT_LEFT},
    {UI_T_DR_COL_TYPE, 84, LVCFMT_LEFT},        {UI_T_DR_COL_FS, 80, LVCFMT_LEFT},
    {UI_T_DR_COL_ENCRYPTION, 150, LVCFMT_LEFT}, {UI_T_DR_COL_TOTAL, 84, LVCFMT_RIGHT},
    {UI_T_DR_COL_USED, 84, LVCFMT_RIGHT},       {UI_T_DR_COL_FREE, 84, LVCFMT_RIGHT},
    {UI_T_DR_COL_USAGE, 220, LVCFMT_LEFT},
};

static const wchar_t *type_label(DriveType type)
{
    switch (type) {
    case DRIVE_TYPE_REMOVABLE:
        return ui_text(UI_T_DR_TYPE_REMOVABLE);
    case DRIVE_TYPE_FIXED:
        return ui_text(UI_T_DR_TYPE_FIXED);
    case DRIVE_TYPE_REMOTE:
        return ui_text(UI_T_DR_TYPE_REMOTE);
    case DRIVE_TYPE_CDROM:
        return ui_text(UI_T_DR_TYPE_CDROM);
    case DRIVE_TYPE_RAMDISK:
        return ui_text(UI_T_DR_TYPE_RAMDISK);
    case DRIVE_TYPE_NO_ROOT:
        return ui_text(UI_T_DR_TYPE_NO_ROOT);
    default:
        return ui_text(UI_T_DR_TYPE_UNKNOWN);
    }
}

static const wchar_t *status_label(DriveStatus status)
{
    switch (status) {
    case DRIVE_STATUS_OK:
        return ui_text(UI_T_DR_ST_OK);
    case DRIVE_STATUS_NOT_READY:
        return ui_text(UI_T_DR_ST_NOT_READY);
    case DRIVE_STATUS_ACCESS_DENIED:
        return ui_text(UI_T_DR_ST_ACCESS_DENIED);
    case DRIVE_STATUS_MISSING:
        return ui_text(UI_T_DR_ST_MISSING);
    case DRIVE_STATUS_DISCONNECTED:
        return ui_text(UI_T_DR_ST_DISCONNECTED);
    default:
        return ui_text(UI_T_DR_ST_FAILED);
    }
}

/* ---- 조회(작업 스레드) ---- */

static unsigned __stdcall refresh_thread(void *arg)
{
    RefreshJob *job = arg;

    job->rc = drive_list_query(&job->drives, &job->error);
    /* 패널이 이미 사라졌으면 결과를 받을 곳이 없으므로 여기서 해제한다. */
    if (!PostMessageW(job->panel, WM_APP_DRIVES_DONE, 0, (LPARAM)job)) {
        free(job);
    }
    return 0;
}

/* ---- 표시 ---- */

static void list_set(HWND list, int row, int col, const wchar_t *text)
{
    LVITEMW lv;

    memset(&lv, 0, sizeof(lv));
    lv.iSubItem = col;
    lv.pszText = (wchar_t *)text;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&lv);
}

/* 암호화 열: 뜻을 모르는 값·조회 실패는 원래 값·오류 코드를 덧붙인다. */
static void encryption_text(const DriveInfo *d, wchar_t *buf, size_t len)
{
    if (d->encryption == DRIVE_ENC_UNKNOWN && d->encryption_raw >= 0) {
        swprintf(buf, len, ui_text(UI_T_DR_ENC_RAW), gui_drive_encryption_text((int)d->encryption),
                 d->encryption_raw);
    } else if (d->encryption == DRIVE_ENC_FAILED) {
        swprintf(buf, len, L"%ls(0x%08lX)", gui_drive_encryption_text((int)d->encryption),
                 (unsigned long)d->encryption_error);
    } else {
        swprintf(buf, len, L"%ls", gui_drive_encryption_text((int)d->encryption));
    }
}

static void usage_text(const DriveInfo *d, wchar_t *buf, size_t len)
{
    if (d->status != DRIVE_STATUS_OK && d->encryption == DRIVE_ENC_LOCKED) {
        /* 잠긴 BitLocker 드라이브는 용량을 읽을 수 없다. 오류 코드보다 이유를 보여 준다. */
        swprintf(buf, len, L"%ls", ui_text(UI_T_DR_LOCKED));
    } else if (d->status == DRIVE_STATUS_OK) {
        unsigned pm = drive_usage_permille(d);
        swprintf(buf, len, L"%u.%u%%", pm / 10, pm % 10);
    } else {
        swprintf(buf, len, ui_text(UI_T_DR_STATUS_ERROR), status_label(d->status), d->error);
    }
}

/* 지금 언어로 만드는 칸(종류·암호화·사용률). 언어를 바꾸면 이것만 다시 쓴다. */
static void set_row_texts(DrivePanel *p, int row, const DriveInfo *d)
{
    wchar_t buf[DRIVE_LABEL_LEN + 64];

    list_set(p->list, row, COL_TYPE, type_label(d->type));
    encryption_text(d, buf, sizeof(buf) / sizeof(buf[0]));
    list_set(p->list, row, COL_ENCRYPTION, buf);
    usage_text(d, buf, sizeof(buf) / sizeof(buf[0]));
    list_set(p->list, row, COL_USAGE, buf);
}

static void fill_list(DrivePanel *p)
{
    size_t i;

    SendMessageW(p->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(p->list);
    for (i = 0; i < p->drives.len; i++) {
        const DriveInfo *d = &p->drives.items[i];
        wchar_t buf[DRIVE_LABEL_LEN + 64];
        LVITEMW lv;
        int row;

        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_TEXT | LVIF_PARAM;
        lv.iItem = (int)i;
        lv.pszText = (wchar_t *)d->root;
        lv.lParam = (LPARAM)i;
        row = (int)SendMessageW(p->list, LVM_INSERTITEMW, 0, (LPARAM)&lv);
        if (row < 0) {
            break;
        }
        list_set(p->list, row, COL_LABEL, d->label);
        list_set(p->list, row, COL_FS, d->fs);
        set_row_texts(p, row, d);
        if (d->status == DRIVE_STATUS_OK) {
            format_size(d->total_bytes, buf, 64);
            list_set(p->list, row, COL_TOTAL, buf);
            format_size(drive_used_bytes(d), buf, 64);
            list_set(p->list, row, COL_USED, buf);
            format_size(d->free_bytes, buf, 64);
            list_set(p->list, row, COL_FREE, buf);
        }
    }
    SendMessageW(p->list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(p->list, NULL, TRUE);
}

/* 상태 줄 문구(summary_kind 와 마지막 조회 결과를 지금 언어로). */
static void summary_text(const DrivePanel *p, wchar_t *text, size_t len)
{
    wchar_t total[32], used[32], avail[32];
    uint64_t sum_total = 0, sum_used = 0, sum_free = 0;
    size_t i, failed = 0, enc_known = 0, encrypted = 0;

    switch (p->summary_kind) {
    case SUMMARY_HINT:
        swprintf(text, len, L"%ls", ui_text(UI_T_DR_HINT));
        return;
    case SUMMARY_LOADING:
        swprintf(text, len, L"%ls", ui_text(UI_T_DR_LOADING));
        return;
    case SUMMARY_START_NOMEM:
        swprintf(text, len, L"%ls", ui_text(UI_T_DR_START_NOMEM));
        return;
    case SUMMARY_START_NOTHREAD:
        swprintf(text, len, L"%ls", ui_text(UI_T_DR_START_NOTHREAD));
        return;
    default:
        break;
    }
    if (p->last_rc != 0) {
        swprintf(text, len, ui_text(UI_T_DR_LIST_FAILED), p->last_error);
        return;
    }
    for (i = 0; i < p->drives.len; i++) {
        const DriveInfo *d = &p->drives.items[i];
        if (d->status == DRIVE_STATUS_OK) {
            sum_total += d->total_bytes;
            sum_used += drive_used_bytes(d);
            sum_free += d->free_bytes;
        } else {
            failed++;
        }
        /* 암호화 상태를 알아낸 로컬·이동식 드라이브 중 암호화된 수 */
        if (d->encryption != DRIVE_ENC_NOT_QUERIED && d->encryption != DRIVE_ENC_FAILED) {
            enc_known++;
            encrypted += drive_encryption_is_encrypted(d->encryption) != 0;
        }
    }
    format_size(sum_total, total, 32);
    format_size(sum_used, used, 32);
    format_size(sum_free, avail, 32);
    swprintf(text, len, ui_text(UI_T_DR_SUMMARY), (unsigned)p->drives.len, total, used, avail,
             (unsigned)failed, (unsigned)encrypted, (unsigned)enc_known, (unsigned)p->as_of.wHour,
             (unsigned)p->as_of.wMinute, (unsigned)p->as_of.wSecond);
}

static void render_summary(DrivePanel *p)
{
    wchar_t text[512];

    summary_text(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->summary, text);
}

static void set_summary(DrivePanel *p, SummaryKind kind)
{
    p->summary_kind = kind;
    render_summary(p);
}

/* 사용률 열을 막대로 그린다. 조회에 실패한 드라이브는 기본 그리기(오류 문구)를 쓴다. */
static LRESULT on_custom_draw(DrivePanel *p, NMLVCUSTOMDRAW *cd)
{
    const DriveInfo *d;
    size_t index;
    RECT rc, bar, fill;
    HBRUSH brush;
    wchar_t text[32];
    unsigned pm;
    int inset;

    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        break;
    default:
        return CDRF_DODEFAULT;
    }

    index = (size_t)cd->nmcd.lItemlParam;
    if (cd->iSubItem != COL_USAGE || index >= p->drives.len) {
        return CDRF_DODEFAULT;
    }
    d = &p->drives.items[index];
    if (d->status != DRIVE_STATUS_OK) {
        return CDRF_DODEFAULT;
    }
    if (!ListView_GetSubItemRect(p->list, (int)cd->nmcd.dwItemSpec, COL_USAGE, LVIR_BOUNDS,
                                 &rc)) {
        return CDRF_DODEFAULT;
    }

    pm = drive_usage_permille(d);
    inset = pscale(p, 3);
    theme_fill_rect(cd->nmcd.hdc, &rc, THEME_COLOR_SURFACE);
    bar = rc;
    InflateRect(&bar, -inset, -inset / 2 - 1);
    if (bar.right <= bar.left || bar.bottom <= bar.top) {
        return CDRF_SKIPDEFAULT;
    }
    theme_fill_rect(cd->nmcd.hdc, &bar, THEME_COLOR_FACE);
    fill = bar;
    fill.right = fill.left + MulDiv(bar.right - bar.left, (int)pm, 1000);
    if (fill.right > fill.left) {
        brush = CreateSolidBrush(pm >= USAGE_WARN_PERMILLE ? RGB(218, 38, 38) : RGB(38, 160, 218));
        if (brush != NULL) {
            FillRect(cd->nmcd.hdc, &fill, brush);
            DeleteObject(brush);
        }
    }
    theme_frame_rect(cd->nmcd.hdc, &bar, THEME_COLOR_FRAME);

    usage_text(d, text, 32);
    SetBkMode(cd->nmcd.hdc, TRANSPARENT);
    SetTextColor(cd->nmcd.hdc, theme_is_dark() ? THEME_COLOR_TEXT : GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(cd->nmcd.hdc, text, -1, &bar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return CDRF_SKIPDEFAULT;
}

static void on_refresh_done(DrivePanel *p, RefreshJob *job)
{
    if (job->generation != p->generation) {
        /* 더 최근에 시작한 조회가 있다. */
        free(job);
        return;
    }
    p->busy = 0;
    p->loaded = 1;
    GetLocalTime(&p->as_of);
    p->last_rc = job->rc;
    p->last_error = job->error;
    if (job->rc == 0) {
        p->drives = job->drives;
    } else {
        memset(&p->drives, 0, sizeof(p->drives));
    }
    free(job);

    fill_list(p);
    set_summary(p, SUMMARY_RESULT);
    EnableWindow(p->refresh, TRUE);
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), DPN_REFRESHED), (LPARAM)p->hwnd);
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(DrivePanel *p)
{
    RECT rc;
    int h = pscale(p, 24), bw = p->w_refresh, gap = pscale(p, 6);
    int w, list_h;

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    MoveWindow(p->refresh, 0, 0, bw, h, TRUE);
    MoveWindow(p->summary, bw + gap, pscale(p, 4), w - bw - gap > 0 ? w - bw - gap : 0,
               pscale(p, 20), TRUE);
    list_h = rc.bottom - h - gap;
    MoveWindow(p->list, 0, h + gap, w, list_h > 0 ? list_h : 0, TRUE);
}

static HWND panel_child(DrivePanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)p->font, FALSE);
    return h;
}

/* 열 폭: 기본 폭과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(DrivePanel *p, int col)
{
    int need = theme_text_width(p->list, p->font, ui_text(k_cols[col].title)) + pscale(p, 16);
    return pscale(p, k_cols[col].width) > need ? pscale(p, k_cols[col].width) : need;
}

/* [새로고침] 폭: 예전 고정 폭(96)을 최소로 두고, 지금 글꼴로 잰 문구가 더 길면 넓힌다. */
static void measure_buttons(DrivePanel *p)
{
    p->w_refresh = theme_button_width(p->hwnd, p->font, ui_text(UI_T_DR_REFRESH), p->dpi, pscale(p, 96));
}

static void panel_create_controls(DrivePanel *p)
{
    int i;

    p->refresh = panel_child(p, L"BUTTON", ui_text(UI_T_DR_REFRESH), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_REFRESH);
    p->summary = panel_child(p, L"STATIC", ui_text(UI_T_DR_HINT), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                             IDC_SUMMARY);
    p->list = panel_child(p, WC_LISTVIEWW, L"",
                          LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                          WS_EX_CLIENTEDGE, IDC_DRIVE_LIST);
    ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)ui_text(k_cols[i].title);
        col.cx = column_width(p, i);
        col.fmt = k_cols[i].fmt;
        SendMessageW(p->list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
    measure_buttons(p);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DrivePanel *p = panel_of(hwnd);

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
        if (LOWORD(wp) == IDC_REFRESH) {
            drive_panel_refresh(hwnd);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (p != NULL && hdr->idFrom == IDC_DRIVE_LIST && hdr->code == NM_CUSTOMDRAW) {
            return on_custom_draw(p, (NMLVCUSTOMDRAW *)lp);
        }
        break;
    }
    case WM_APP_DRIVES_DONE:
        if (p != NULL) {
            on_refresh_done(p, (RefreshJob *)lp);
        } else {
            free((RefreshJob *)lp);
        }
        return 0;
    case WM_NCDESTROY:
        /* 진행 중인 조회 스레드는 PostMessage 가 실패하면 결과를 스스로 해제한다. */
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        free(p);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int drive_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = DRIVE_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND drive_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    /* WS_EX_CONTROLPARENT: 부모의 IsDialogMessage 가 패널 안의 컨트롤로 Tab 이동하게 한다. */
    return CreateWindowExW(WS_EX_CONTROLPARENT, DRIVE_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

void drive_panel_refresh(HWND panel)
{
    DrivePanel *p = panel_of(panel);
    RefreshJob *job;
    HANDLE thread;

    if (p == NULL || p->busy) {
        return;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        set_summary(p, SUMMARY_START_NOMEM);
        return;
    }
    job->panel = panel;
    job->generation = ++p->generation;
    thread = (HANDLE)_beginthreadex(NULL, 0, refresh_thread, job, 0, NULL);
    if (thread == NULL) {
        free(job);
        set_summary(p, SUMMARY_START_NOTHREAD);
        return;
    }
    /* 결과는 메시지로 받으므로 스레드를 기다리지 않는다. */
    CloseHandle(thread);
    p->busy = 1;
    EnableWindow(p->refresh, FALSE);
    set_summary(p, SUMMARY_LOADING);
}

int drive_panel_loaded(HWND panel)
{
    DrivePanel *p = panel_of(panel);

    return p != NULL && p->loaded;
}

void drive_panel_apply_language(HWND panel, HFONT font)
{
    DrivePanel *p = panel_of(panel);
    HWND child;
    int i, rows;

    if (p == NULL) {
        return;
    }
    if (font != NULL) {
        p->font = font;
        for (child = GetWindow(p->hwnd, GW_CHILD); child != NULL; child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SETFONT, (WPARAM)font, FALSE);
        }
    }
    SetWindowTextW(p->refresh, ui_text(UI_T_DR_REFRESH));
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
    /* 목록 행은 조회 결과 순서 그대로다(lParam = 인덱스). 번역 칸만 다시 쓴다. */
    rows = ListView_GetItemCount(p->list);
    for (i = 0; i < rows && (size_t)i < p->drives.len; i++) {
        set_row_texts(p, i, &p->drives.items[i]);
    }
    measure_buttons(p);
    render_summary(p);
    panel_layout(p);
    InvalidateRect(p->list, NULL, TRUE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int drive_panel_min_width(HWND panel)
{
    DrivePanel *p = panel_of(panel);

    /* [새로고침]과 간격(요약은 남는 폭만 쓴다). */
    return p != NULL ? p->w_refresh + pscale(p, 6) : 0;
}

#define PANEL_CHECK(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fwprintf(stderr, L"drive self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                                 \
            failures++;                                                         \
        }                                                                       \
    } while (0)

int drive_panel_verify(HWND panel)
{
    DrivePanel *p = panel_of(panel);
    int failures = 0, rows, sys_found = 0;
    unsigned long mask;
    unsigned mask_count = 0;
    wchar_t windir[MAX_PATH];
    size_t i;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    PANEL_CHECK(p->loaded && !p->busy);
    PANEL_CHECK(p->last_rc == 0);
    PANEL_CHECK(IsWindowEnabled(p->refresh));

    rows = ListView_GetItemCount(p->list);
    PANEL_CHECK(rows == (int)p->drives.len);
    for (mask = GetLogicalDrives(); mask != 0; mask &= mask - 1) {
        mask_count++;
    }
    PANEL_CHECK(mask_count == (unsigned)p->drives.len);

    if (GetWindowsDirectoryW(windir, MAX_PATH) == 0) {
        windir[0] = L'\0';
    }
    for (i = 0; i < p->drives.len && (int)i < rows; i++) {
        const DriveInfo *d = &p->drives.items[i];
        wchar_t cell[DRIVE_LABEL_LEN + 64], expect[DRIVE_LABEL_LEN + 64];
        LVITEMW lv;

        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM | LVIF_TEXT;
        lv.iItem = (int)i;
        lv.pszText = cell;
        lv.cchTextMax = (int)(sizeof(cell) / sizeof(cell[0]));
        SendMessageW(p->list, LVM_GETITEMW, 0, (LPARAM)&lv);
        PANEL_CHECK((size_t)lv.lParam == i);
        PANEL_CHECK(wcscmp(cell, d->root) == 0);

        ListView_GetItemText(p->list, (int)i, COL_USAGE, cell,
                             (int)(sizeof(cell) / sizeof(cell[0])));
        usage_text(d, expect, sizeof(expect) / sizeof(expect[0]));
        PANEL_CHECK(wcscmp(cell, expect) == 0);
        ListView_GetItemText(p->list, (int)i, COL_ENCRYPTION, cell,
                             (int)(sizeof(cell) / sizeof(cell[0])));
        encryption_text(d, expect, sizeof(expect) / sizeof(expect[0]));
        PANEL_CHECK(wcscmp(cell, expect) == 0 && cell[0] != L'\0');
        /* 암호화 상태는 로컬·이동식 드라이브만 조회한다 */
        if (d->type != DRIVE_TYPE_FIXED && d->type != DRIVE_TYPE_REMOVABLE) {
            PANEL_CHECK(d->encryption == DRIVE_ENC_NOT_QUERIED);
        }

        if (d->status == DRIVE_STATUS_OK) {
            PANEL_CHECK(d->free_bytes <= d->total_bytes);
            PANEL_CHECK(drive_used_bytes(d) + d->free_bytes == d->total_bytes);
            PANEL_CHECK(drive_usage_permille(d) <= 1000);
        } else {
            PANEL_CHECK(d->total_bytes == 0 && d->free_bytes == 0);
        }
        if (windir[0] != L'\0' && towupper(windir[0]) == d->root[0]) {
            sys_found = 1;
            PANEL_CHECK(d->status == DRIVE_STATUS_OK && d->total_bytes > 0);
            PANEL_CHECK(d->type == DRIVE_TYPE_FIXED);
            /* 시스템 드라이브는 암호화 여부와 상관없이 셸 속성을 읽을 수 있어야 한다 */
            PANEL_CHECK(d->encryption != DRIVE_ENC_NOT_QUERIED &&
                        d->encryption != DRIVE_ENC_FAILED);
        }
    }
    PANEL_CHECK(windir[0] == L'\0' || sys_found);
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

int drive_panel_verify_lang(HWND panel)
{
    DrivePanel *p = panel_of(panel);
    wchar_t buf[DRIVE_LABEL_LEN + 512], want[DRIVE_LABEL_LEN + 512];
    RECT r;
    int failures = 0, other = ui_lang_current() != UI_LANG_KO, i, rows;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    /* [새로고침]: 지금 언어 문구이고 잘리지 않는다 */
    GetWindowTextW(p->refresh, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, ui_text(UI_T_DR_REFRESH)) == 0);
    GetWindowRect(p->refresh, &r);
    PANEL_CHECK(r.right - r.left >= theme_button_width(p->hwnd, p->font, buf, p->dpi, 0));
    /* 열 머리글: 지금 언어이고 잘리지 않는다 */
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
    /* 상태 줄: 마지막에 보인 내용을 지금 언어로 쓴 것 */
    summary_text(p, want, sizeof(want) / sizeof(want[0]));
    GetWindowTextW(p->summary, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, want) == 0);
    PANEL_CHECK(!other || !has_hangul(buf));
    /* 목록: 종류·암호화·사용률 칸이 지금 언어(이름·파일 시스템은 볼륨 정보라 번역 대상이 아님) */
    rows = ListView_GetItemCount(p->list);
    for (i = 0; i < rows && (size_t)i < p->drives.len; i++) {
        const DriveInfo *d = &p->drives.items[i];
        ListView_GetItemText(p->list, i, COL_TYPE, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(wcscmp(buf, type_label(d->type)) == 0 && (!other || !has_hangul(buf)));
        ListView_GetItemText(p->list, i, COL_ENCRYPTION, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        encryption_text(d, want, sizeof(want) / sizeof(want[0]));
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
        ListView_GetItemText(p->list, i, COL_USAGE, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        usage_text(d, want, sizeof(want) / sizeof(want[0]));
        PANEL_CHECK(wcscmp(buf, want) == 0 && (!other || !has_hangul(buf)));
    }
    return failures;
}

void drive_panel_set_dpi(HWND panel, int dpi)
{
    DrivePanel *p = panel_of(panel);

    if (p != NULL && dpi > 0) {
        p->dpi = dpi;
    }
}
