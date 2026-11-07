#include "gui_nav.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gui_theme.h"
#include "ui_lang.h"

#define NAV_CLASS L"ByteSieveNav"

/* 96 DPI 기준 치수. */
#define NAV_TOP 8
#define NAV_HEADER_H 30
#define NAV_ITEM_H 36
#define NAV_GROUP_GAP 6
#define NAV_PAD_X 12  /* 사이드바 안쪽 좌우 여백 */
#define NAV_TEXT_X 16 /* 항목 사각형 안 글자 시작 */
#define NAV_MIN_W 200
#define NAV_BAR_W 3
#define NAV_BAR_H 16
#define NAV_RADIUS 4

typedef struct {
    HWND hwnd;
    HFONT font;
    int dpi;
    NavPage selected;
    NavPage hover; /* NAV_PAGE_COUNT 이면 없음 */
    int tracking;  /* WM_MOUSELEAVE 를 기다리는 중 */
} NavView;

typedef struct {
    HFONT font;
    int dpi;
} NavParams;

static NavView *nav_of(HWND hwnd)
{
    return (NavView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int nscale(const NavView *v, int px)
{
    return MulDiv(px, v->dpi, 96);
}

static NavMetrics metrics(const NavView *v)
{
    NavMetrics m;
    RECT client;

    m.top = nscale(v, NAV_TOP);
    m.header_h = nscale(v, NAV_HEADER_H);
    m.item_h = nscale(v, NAV_ITEM_H);
    m.group_gap = nscale(v, NAV_GROUP_GAP);
    /* [설정](footer)은 사이드바 아래 끝에 붙인다. */
    GetClientRect(v->hwnd, &client);
    m.bottom = client.bottom - nscale(v, NAV_TOP);
    return m;
}

/* 행 사각형(항목은 좌우 여백 안쪽). */
static int row_rect(const NavView *v, size_t row, RECT *out)
{
    NavMetrics m = metrics(v);
    RECT client;
    int y0, y1;

    if (!nav_row_span(&m, row, &y0, &y1)) {
        return 0;
    }
    GetClientRect(v->hwnd, &client);
    out->left = client.left + nscale(v, NAV_PAD_X) / 2;
    out->right = client.right - nscale(v, NAV_PAD_X) / 2;
    out->top = y0 + nscale(v, 2);
    out->bottom = y1 - nscale(v, 2);
    return 1;
}

static void invalidate_page(NavView *v, NavPage page)
{
    RECT r;

    if (page < NAV_PAGE_COUNT && row_rect(v, nav_row_of_page(page), &r)) {
        InvalidateRect(v->hwnd, &r, FALSE);
    }
}

static void fill_round(HDC dc, const RECT *r, COLORREF color, int radius)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, pen);

    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius * 2, radius * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void paint(NavView *v, HDC dc)
{
    RECT client, r;
    size_t i, n = nav_row_count();
    HGDIOBJ old;
    int focused = GetFocus() == v->hwnd;

    GetClientRect(v->hwnd, &client);
    FillRect(dc, &client, theme_bg_brush());
    old = SelectObject(dc, v->font);
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < n; i++) {
        const NavRow *row = nav_row(i);
        RECT text;
        if (!row_rect(v, i, &r)) {
            continue;
        }
        text = r;
        text.left += nscale(v, NAV_TEXT_X);
        text.right -= nscale(v, 4);
        if (row->kind == NAV_ROW_HEADER) {
            /* 머리글은 아래쪽에 붙여 그 아래 항목과 묶여 보이게 한다. */
            text.left = r.left + nscale(v, 8);
            SetTextColor(dc, THEME_COLOR_TEXT_2ND);
            DrawTextW(dc, ui_text(nav_group_text(row->group)), -1, &text,
                      DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            continue;
        }
        if (row->page == v->selected) {
            RECT bar;
            fill_round(dc, &r, THEME_COLOR_SURFACE, nscale(v, NAV_RADIUS));
            bar.left = r.left + nscale(v, 1);
            bar.right = bar.left + nscale(v, NAV_BAR_W);
            bar.top = (r.top + r.bottom - nscale(v, NAV_BAR_H)) / 2;
            bar.bottom = bar.top + nscale(v, NAV_BAR_H);
            fill_round(dc, &bar, theme_accent_color(), nscale(v, 2));
        } else if (row->page == v->hover) {
            fill_round(dc, &r, THEME_COLOR_SUBTLE, nscale(v, NAV_RADIUS));
        }
        SetTextColor(dc, THEME_COLOR_TEXT);
        DrawTextW(dc, ui_text(nav_page_text(row->page)), -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (focused && row->page == v->selected) {
            RECT f = r;
            InflateRect(&f, -nscale(v, 2), -nscale(v, 2));
            DrawFocusRect(dc, &f);
        }
    }
    SelectObject(dc, old);
}

static int select_page(NavView *v, NavPage page, int notify)
{
    if ((unsigned)page >= (unsigned)NAV_PAGE_COUNT || page == v->selected) {
        return 0;
    }
    invalidate_page(v, v->selected);
    v->selected = page;
    invalidate_page(v, v->selected);
    if (notify) {
        SendMessageW(GetParent(v->hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(v->hwnd), NVN_SELCHANGE), (LPARAM)v->hwnd);
    }
    return 1;
}

static void set_hover(NavView *v, NavPage page)
{
    if (page != v->hover) {
        invalidate_page(v, v->hover);
        v->hover = page;
        invalidate_page(v, v->hover);
    }
}

static LRESULT CALLBACK nav_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NavView *v = nav_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        const NavParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        v = calloc(1, sizeof(*v));
        if (v == NULL) {
            return FALSE;
        }
        v->hwnd = hwnd;
        v->font = params->font;
        v->dpi = params->dpi;
        v->selected = NAV_PAGE_SCAN;
        v->hover = NAV_PAGE_COUNT;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        break;
    }
    case WM_ERASEBKGND:
        return 1; /* paint 가 배경까지 그린다 */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (v != NULL) {
            /* 깜박임이 없게 메모리 DC 에 그린 뒤 옮긴다. */
            RECT client;
            HDC mem;
            HBITMAP bmp;
            GetClientRect(hwnd, &client);
            mem = CreateCompatibleDC(dc);
            bmp = CreateCompatibleBitmap(dc, client.right > 0 ? client.right : 1,
                                         client.bottom > 0 ? client.bottom : 1);
            if (mem != NULL && bmp != NULL) {
                HGDIOBJ ob = SelectObject(mem, bmp);
                paint(v, mem);
                BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, ob);
            } else {
                paint(v, dc);
            }
            if (bmp != NULL) {
                DeleteObject(bmp);
            }
            if (mem != NULL) {
                DeleteDC(mem);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (v != NULL) {
            invalidate_page(v, v->selected);
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        if (v != NULL) {
            NavKey key;
            switch (wp) {
            case VK_UP:
                key = NAV_KEY_UP;
                break;
            case VK_DOWN:
                key = NAV_KEY_DOWN;
                break;
            case VK_HOME:
                key = NAV_KEY_HOME;
                break;
            case VK_END:
                key = NAV_KEY_END;
                break;
            default:
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            select_page(v, nav_key_move(v->selected, key), 1);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (v != NULL) {
            NavMetrics m = metrics(v);
            NavPage page;
            SetFocus(hwnd);
            if (nav_hit_test(&m, (short)HIWORD(lp), &page)) {
                select_page(v, page, 1);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (v != NULL) {
            NavMetrics m = metrics(v);
            NavPage page = NAV_PAGE_COUNT;
            if (!v->tracking) {
                TRACKMOUSEEVENT tme;
                memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                v->tracking = TrackMouseEvent(&tme) != 0;
            }
            if (!nav_hit_test(&m, (short)HIWORD(lp), &page)) {
                page = NAV_PAGE_COUNT;
            }
            set_hover(v, page);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (v != NULL) {
            v->tracking = 0;
            set_hover(v, NAV_PAGE_COUNT);
        }
        return 0;
    case WM_SETCURSOR:
        if (v != NULL && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            NavMetrics m = metrics(v);
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(NULL, nav_hit_test(&m, pt.y, NULL) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        free(v);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int nav_view_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = nav_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = NAV_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND nav_view_create(HWND parent, int id, HFONT font, int dpi)
{
    NavParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(0, NAV_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                           parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

int nav_view_min_height(HWND nav)
{
    NavView *v = nav_of(nav);
    NavMetrics m;

    if (v == NULL) {
        return 0;
    }
    m = metrics(v);
    return nav_total_height(&m) + nscale(v, NAV_TOP);
}

int nav_view_width(HWND nav)
{
    NavView *v = nav_of(nav);
    int w = 0, i;

    if (v == NULL) {
        return 0;
    }
    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        int tw = theme_text_width(nav, v->font, ui_text(nav_page_text((NavPage)i)));
        if (tw > w) {
            w = tw;
        }
    }
    for (i = 0; i < NAV_GROUP_COUNT; i++) {
        int tw = theme_text_width(nav, v->font, ui_text(nav_group_text((NavGroup)i)));
        if (tw > w) {
            w = tw;
        }
    }
    w += nscale(v, NAV_PAD_X + NAV_TEXT_X + 16);
    return w > nscale(v, NAV_MIN_W) ? w : nscale(v, NAV_MIN_W);
}

NavPage nav_view_selected(HWND nav)
{
    NavView *v = nav_of(nav);

    return v != NULL ? v->selected : NAV_PAGE_SCAN;
}

int nav_view_select(HWND nav, NavPage page, int notify)
{
    NavView *v = nav_of(nav);

    return v != NULL ? select_page(v, page, notify) : 0;
}

int nav_view_item_rect(HWND nav, NavPage page, RECT *out)
{
    NavView *v = nav_of(nav);
    size_t row = nav_row_of_page(page);

    return v != NULL && out != NULL && row < nav_row_count() && row_rect(v, row, out);
}
