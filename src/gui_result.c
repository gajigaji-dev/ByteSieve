/* 완료 결과 영역(gui_result.h). */
#include "gui_result.h"

#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gui_status_text.h"
#include "gui_theme.h"

#define RESULT_BANNER_CLASS L"ByteSieveResultBanner"

/* 96 DPI 기준 치수. */
#define PAD 10      /* 안쪽 여백 */
#define ACCENT_W 4  /* 왼쪽 색 막대 폭 */
#define LINE_GAP 4  /* 제목과 요약 사이 */
#define BUTTON_H 26 /* 단추 높이 */
#define BUTTON_GAP 6

enum { IDC_R_TITLE = 10, IDC_R_DETAIL, IDC_R_CLOSE, IDC_R_ACTION0 };

typedef struct {
    HWND hwnd;
    HWND title, detail, close;
    HWND buttons[RESULT_BANNER_MAX_ACTIONS];
    ResultAction actions[RESULT_BANNER_MAX_ACTIONS];
    int action_count;
    HFONT font, bold;
    int dpi;
    OpResult result;
    wchar_t *detail_text;
    HBRUSH brush;     /* 배경(SURFACE) 붓 */
    COLORREF brush_color;
} ResultBanner;

typedef struct {
    HFONT font;
    int dpi;
} BannerParams;

static ResultBanner *banner_of(HWND hwnd)
{
    return hwnd != NULL ? (ResultBanner *)GetWindowLongPtrW(hwnd, GWLP_USERDATA) : NULL;
}

static int bscale(const ResultBanner *b, int px)
{
    return MulDiv(px, b->dpi, 96);
}

/* 결과별 색: 완료 초록, 일부 완료·취소 주황, 실패 빨강, 없음 회색(밝게·어둡게 각각 대비가 나는 값). */
static COLORREF accent_color(OpResult result)
{
    int dark = theme_is_dark();

    switch (result) {
    case OP_RESULT_SUCCESS:
        return dark ? RGB(108, 203, 95) : RGB(16, 124, 16);
    case OP_RESULT_PARTIAL:
    case OP_RESULT_CANCELLED:
        return dark ? RGB(252, 185, 0) : RGB(157, 93, 0);
    case OP_RESULT_FAILED:
        return THEME_COLOR_DANGER;
    default:
        return THEME_COLOR_TEXT_2ND;
    }
}

static HBRUSH surface_brush(ResultBanner *b)
{
    COLORREF c = THEME_COLOR_SURFACE;

    if (b->brush == NULL || b->brush_color != c) {
        if (b->brush != NULL) {
            DeleteObject(b->brush);
        }
        b->brush = CreateSolidBrush(c);
        b->brush_color = c;
    }
    return b->brush;
}

static void make_bold(ResultBanner *b)
{
    LOGFONTW lf;

    if (b->bold != NULL) {
        DeleteObject(b->bold);
        b->bold = NULL;
    }
    if (b->font != NULL && GetObjectW(b->font, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        b->bold = CreateFontIndirectW(&lf);
    }
    SendMessageW(b->title, WM_SETFONT, (WPARAM)(b->bold != NULL ? b->bold : b->font), FALSE);
}

/*
 * 자식 배치. 폭 width 에서 필요한 높이를 돌려주고, move 면 실제로 옮긴다.
 * 제목 한 줄, 요약(줄 바꿈), 단추 줄(모자라면 다음 줄로, [닫기]는 마지막 줄 오른쪽 끝).
 */
static int arrange(ResultBanner *b, int width, int move)
{
    int left = bscale(b, ACCENT_W) + bscale(b, PAD), right = width - bscale(b, PAD);
    int inner = right - left, y = bscale(b, PAD), title_h, detail_h, bh = bscale(b, BUTTON_H);
    int gap = bscale(b, BUTTON_GAP), x, i, close_w;
    wchar_t text[256];
    TEXTMETRICW tm;
    HDC dc;
    HGDIOBJ old;

    if (inner < bscale(b, 40)) {
        inner = bscale(b, 40);
    }
    dc = GetDC(b->hwnd);
    old = SelectObject(dc, b->bold != NULL ? b->bold : b->font);
    title_h = GetTextMetricsW(dc, &tm) ? tm.tmHeight : bscale(b, 18);
    SelectObject(dc, old);
    ReleaseDC(b->hwnd, dc);
    if (move) {
        MoveWindow(b->title, left, y, inner, title_h, TRUE);
    }
    y += title_h + bscale(b, LINE_GAP);
    detail_h = theme_text_wrap_height(b->hwnd, b->font, b->detail_text != NULL ? b->detail_text : L"",
                                      inner);
    if (detail_h > 0) {
        if (move) {
            MoveWindow(b->detail, left, y, inner, detail_h, TRUE);
        }
        y += detail_h + bscale(b, PAD) / 2;
    } else if (move) {
        MoveWindow(b->detail, left, y, 0, 0, TRUE);
    }
    GetWindowTextW(b->close, text, 256);
    close_w = theme_button_width(b->hwnd, b->font, text, b->dpi, bscale(b, 72));
    x = left;
    for (i = 0; i < b->action_count; i++) {
        int w;
        GetWindowTextW(b->buttons[i], text, 256);
        w = theme_button_width(b->hwnd, b->font, text, b->dpi, bscale(b, 72));
        if (x > left && x + w > right) {
            x = left;
            y += bh + gap;
        }
        if (move) {
            MoveWindow(b->buttons[i], x, y, w, bh, TRUE);
        }
        x += w + gap;
    }
    /* [닫기]: 마지막 줄 오른쪽 끝. 자리가 없으면 다음 줄 */
    if (x > left && x + close_w > right) {
        y += bh + gap;
    }
    if (move) {
        MoveWindow(b->close, right - close_w > left ? right - close_w : left, y, close_w, bh, TRUE);
    }
    y += bh + bscale(b, PAD);
    return y;
}

/*
 * 보이거나 숨기면 높이가 바뀌므로 다시 배치하게 한다. 패널을 담은 자리(메인 창의 스크롤 자리)가 먼저
 * 패널 크기(최소 높이에 이 영역 포함)를 다시 정하고, 이어서 패널이 자식을 배치한다.
 */
static void relayout_parent(ResultBanner *b)
{
    HWND parent = GetParent(b->hwnd), holder = parent != NULL ? GetParent(parent) : NULL;
    RECT rc;

    if (holder != NULL) {
        GetClientRect(holder, &rc);
        SendMessageW(holder, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
    }
    GetClientRect(parent, &rc);
    SendMessageW(parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
}

static LRESULT CALLBACK banner_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ResultBanner *b = banner_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        const BannerParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        b = calloc(1, sizeof(*b));
        if (b == NULL) {
            return FALSE;
        }
        b->hwnd = hwnd;
        b->font = params->font;
        b->dpi = params->dpi;
        b->result = OP_RESULT_COUNT;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)b);
        break;
    }
    case WM_CREATE: {
        HINSTANCE inst = GetModuleHandleW(NULL);
        int i;
        b->title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP |
                                                          SS_NOPREFIX | SS_ENDELLIPSIS,
                                   0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_R_TITLE, inst, NULL);
        b->detail = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                    0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_R_DETAIL, inst, NULL);
        b->close = CreateWindowExW(0, L"BUTTON", ui_text(UI_T_RES_ACT_CLOSE),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
                                   hwnd, (HMENU)(INT_PTR)IDC_R_CLOSE, inst, NULL);
        for (i = 0; i < RESULT_BANNER_MAX_ACTIONS; i++) {
            b->buttons[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)(IDC_R_ACTION0 + i),
                                            inst, NULL);
            SendMessageW(b->buttons[i], WM_SETFONT, (WPARAM)b->font, FALSE);
        }
        SendMessageW(b->detail, WM_SETFONT, (WPARAM)b->font, FALSE);
        SendMessageW(b->close, WM_SETFONT, (WPARAM)b->font, FALSE);
        make_bold(b);
        return 0;
    }
    case WM_SIZE:
        if (b != NULL) {
            arrange(b, LOWORD(lp), 1);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_ERASEBKGND:
        if (b != NULL) {
            HDC dc = (HDC)wp;
            RECT rc, bar;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, surface_brush(b));
            theme_frame_rect(dc, &rc, THEME_COLOR_STROKE);
            bar = rc;
            bar.right = bar.left + bscale(b, ACCENT_W);
            theme_fill_rect(dc, &bar, accent_color(b->result));
            return 1;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (b != NULL) {
            HDC dc = (HDC)wp;
            SetBkColor(dc, THEME_COLOR_SURFACE);
            SetTextColor(dc, theme_is_dark() ? THEME_COLOR_TEXT : GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)surface_brush(b);
        }
        break;
    case WM_COMMAND:
        if (b != NULL && HIWORD(wp) == BN_CLICKED) {
            int id = LOWORD(wp);
            if (id == IDC_R_CLOSE) {
                result_banner_hide(hwnd);
                return 0;
            }
            if (id >= IDC_R_ACTION0 && id < IDC_R_ACTION0 + b->action_count) {
                SendMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(b->actions[id - IDC_R_ACTION0].id, BN_CLICKED), lp);
                return 0;
            }
        }
        break;
    case WM_NCDESTROY:
        if (b != NULL) {
            free(b->detail_text);
            if (b->bold != NULL) {
                DeleteObject(b->bold);
            }
            if (b->brush != NULL) {
                DeleteObject(b->brush);
            }
            free(b);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int result_banner_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = banner_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = RESULT_BANNER_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND result_banner_create(HWND parent, int id, HFONT font, int dpi)
{
    BannerParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    /* 처음에는 숨긴다(작업이 끝나야 보인다). */
    return CreateWindowExW(WS_EX_CONTROLPARENT, RESULT_BANNER_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

void result_banner_show(HWND banner, OpResult result, const wchar_t *detail,
                        const ResultAction *actions, int count)
{
    ResultBanner *b = banner_of(banner);
    RECT rc;
    int i;

    if (b == NULL) {
        return;
    }
    b->result = result;
    free(b->detail_text);
    b->detail_text = _wcsdup(detail != NULL ? detail : L"");
    SetWindowTextW(banner, gui_op_result_text(result));
    SetWindowTextW(b->title, gui_op_result_text(result));
    SetWindowTextW(b->detail, b->detail_text != NULL ? b->detail_text : L"");
    SetWindowTextW(b->close, ui_text(UI_T_RES_ACT_CLOSE));
    b->action_count = count < 0 ? 0 : count > RESULT_BANNER_MAX_ACTIONS ? RESULT_BANNER_MAX_ACTIONS : count;
    for (i = 0; i < RESULT_BANNER_MAX_ACTIONS; i++) {
        if (i < b->action_count) {
            b->actions[i] = actions[i];
            SetWindowTextW(b->buttons[i], ui_text(actions[i].text));
        }
        ShowWindow(b->buttons[i], i < b->action_count ? SW_SHOWNA : SW_HIDE);
    }
    ShowWindow(banner, SW_SHOWNA);
    GetClientRect(banner, &rc);
    arrange(b, rc.right, 1);
    relayout_parent(b);
    InvalidateRect(banner, NULL, TRUE);
}

void result_banner_hide(HWND banner)
{
    ResultBanner *b = banner_of(banner);

    if (b == NULL || !(GetWindowLongPtrW(banner, GWL_STYLE) & WS_VISIBLE)) {
        return;
    }
    b->result = OP_RESULT_COUNT;
    /* 포커스가 영역 안에 있으면 부모로 옮긴다(숨긴 단추에 포커스가 남지 않게). */
    if (IsChild(banner, GetFocus())) {
        SetFocus(GetParent(banner));
    }
    ShowWindow(banner, SW_HIDE);
    relayout_parent(b);
}

int result_banner_visible(HWND banner)
{
    return banner_of(banner) != NULL && (GetWindowLongPtrW(banner, GWL_STYLE) & WS_VISIBLE) != 0;
}

OpResult result_banner_result(HWND banner)
{
    ResultBanner *b = banner_of(banner);

    return b != NULL && result_banner_visible(banner) ? b->result : OP_RESULT_COUNT;
}

const wchar_t *result_banner_detail(HWND banner)
{
    ResultBanner *b = banner_of(banner);

    return b != NULL && b->detail_text != NULL ? b->detail_text : L"";
}

int result_banner_action_count(HWND banner)
{
    ResultBanner *b = banner_of(banner);

    return b != NULL ? b->action_count : 0;
}

int result_banner_action_id(HWND banner, int i)
{
    ResultBanner *b = banner_of(banner);

    return b != NULL && i >= 0 && i < b->action_count ? b->actions[i].id : 0;
}

int result_banner_height(HWND banner, int width)
{
    ResultBanner *b = banner_of(banner);

    return b != NULL && result_banner_visible(banner) ? arrange(b, width, 0) : 0;
}

void result_banner_set_font(HWND banner, HFONT font)
{
    ResultBanner *b = banner_of(banner);
    int i;

    if (b == NULL || font == NULL) {
        return;
    }
    b->font = font;
    SendMessageW(b->detail, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(b->close, WM_SETFONT, (WPARAM)font, FALSE);
    for (i = 0; i < RESULT_BANNER_MAX_ACTIONS; i++) {
        SendMessageW(b->buttons[i], WM_SETFONT, (WPARAM)font, FALSE);
    }
    make_bold(b);
}

void result_banner_set_dpi(HWND banner, int dpi)
{
    ResultBanner *b = banner_of(banner);

    if (b != NULL && dpi > 0) {
        b->dpi = dpi;
    }
}

void result_text_append(wchar_t *buf, size_t len, UiText fmt, ...)
{
    wchar_t piece[512];
    size_t n = wcslen(buf);
    va_list ap;

    va_start(ap, fmt);
    vswprintf(piece, 512, ui_text(fmt), ap);
    va_end(ap);
    if (n > 0 && n + 1 < len) {
        buf[n++] = L' ';
        buf[n] = L'\0';
    }
    wcsncat_s(buf, len, piece, _TRUNCATE);
}

void result_open_recycle_bin(HWND owner)
{
    ShellExecuteW(owner, L"open", L"shell:RecycleBinFolder", NULL, NULL, SW_SHOWNORMAL);
}
