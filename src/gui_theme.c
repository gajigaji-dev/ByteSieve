#include "gui_theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <string.h>
#include <uxtheme.h>
#include <wchar.h>

/* DWM 창 속성 번호(Windows 11 SDK). 오래된 SDK 에서도 빌드되게 숫자로 둔다. */
#define THEME_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define THEME_DWMWA_SYSTEMBACKDROP_TYPE 38
#define THEME_DWMWCP_ROUND 2
#define THEME_DWMSBT_MAINWINDOW 2 /* Mica */
/* 어두운 제목 표시줄: Windows 10 20H1 이후 20, 그 전(1809~1909)은 19. */
#define THEME_DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define THEME_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19

/* 목록 머리글 글자색을 바꾸려고 목록에 거는 서브클래스 번호. */
#define THEME_LIST_SUBCLASS_ID 0x42534C56u

/* 단추·체크 상자 여백(96 DPI 기준 픽셀). */
#define THEME_BUTTON_PAD 24
#define THEME_CHECK_BOX 13
#define THEME_CHECK_GAP 6
#define THEME_CHECK_PAD 8

int theme_apply_frame(HWND hwnd)
{
    DWORD corner = THEME_DWMWCP_ROUND, backdrop = THEME_DWMSBT_MAINWINDOW;
    int applied = 0;

    if (hwnd == NULL) {
        return 0;
    }
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, THEME_DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                                        sizeof(corner)))) {
        applied++;
    }
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, THEME_DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                                        sizeof(backdrop)))) {
        applied++;
    }
    return applied;
}

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
    (void)lf;
    (void)tm;
    (void)type;
    *(int *)lp = 1;
    return 0; /* 하나만 찾으면 된다 */
}

static int font_installed(const wchar_t *face)
{
    LOGFONTW lf;
    HDC dc;
    int found = 0;

    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
    dc = GetDC(NULL);
    if (dc == NULL) {
        return 0;
    }
    EnumFontFamiliesExW(dc, &lf, font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

const wchar_t *theme_font_face(UiLang lang)
{
    /* 언어마다 앞에서부터 설치된 첫 글꼴을 쓴다. 결과는 처음 물을 때 정해 둔다(GUI 스레드 전용). */
    static const wchar_t *const candidates[UI_LANG_COUNT][2] = {
        {L"Malgun Gothic", NULL},
        {L"Segoe UI Variable Text", L"Segoe UI"},
        {L"Yu Gothic UI", L"Meiryo UI"},
        {L"Microsoft YaHei UI", NULL},
    };
    static int resolved[UI_LANG_COUNT];
    static const wchar_t *face[UI_LANG_COUNT];
    int i;

    if ((int)lang < 0 || lang >= UI_LANG_COUNT) {
        return NULL;
    }
    if (!resolved[lang]) {
        for (i = 0; i < 2 && candidates[lang][i] != NULL; i++) {
            if (font_installed(candidates[lang][i])) {
                face[lang] = candidates[lang][i];
                break;
            }
        }
        resolved[lang] = 1;
    }
    return face[lang];
}

HFONT theme_create_ui_font(UiLang lang)
{
    NONCLIENTMETRICSW ncm;
    const wchar_t *face = theme_font_face(lang);

    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return NULL;
    }
    /* 크기·굵기는 사용자의 시스템 설정(텍스트 크기 포함)을 따르고 글꼴 이름만 바꾼다. */
    if (face != NULL) {
        wcsncpy_s(ncm.lfMessageFont.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
    }
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

int theme_text_width(HWND ref, HFONT font, const wchar_t *text)
{
    HDC dc;
    HGDIOBJ old;
    SIZE size = {0, 0};

    if (text == NULL || text[0] == L'\0') {
        return 0;
    }
    dc = GetDC(ref);
    if (dc == NULL) {
        return 0;
    }
    old = SelectObject(dc, font != NULL ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    if (!GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size)) {
        size.cx = 0;
    }
    SelectObject(dc, old);
    ReleaseDC(ref, dc);
    return (int)size.cx;
}

int theme_text_wrap_height(HWND ref, HFONT font, const wchar_t *text, int width)
{
    HDC dc;
    HGDIOBJ old;
    RECT r = {0, 0, 0, 0};

    if (text == NULL || text[0] == L'\0' || width <= 0) {
        return 0;
    }
    dc = GetDC(ref);
    if (dc == NULL) {
        return 0;
    }
    r.right = width;
    old = SelectObject(dc, font != NULL ? (HGDIOBJ)font : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc, text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(ref, dc);
    return (int)(r.bottom - r.top);
}

int theme_button_width(HWND ref, HFONT font, const wchar_t *text, int dpi, int min_px)
{
    int w = theme_text_width(ref, font, text) + MulDiv(THEME_BUTTON_PAD, dpi, 96);
    return w > min_px ? w : min_px;
}

int theme_check_width(HWND ref, HFONT font, const wchar_t *text, int dpi, int min_px)
{
    int w = theme_text_width(ref, font, text) +
            MulDiv(THEME_CHECK_BOX + THEME_CHECK_GAP + THEME_CHECK_PAD, dpi, 96);
    return w > min_px ? w : min_px;
}

COLORREF theme_accent_color(void)
{
    DWORD argb = 0;
    BOOL opaque = FALSE;

    /* DWM 색은 0xAARRGGBB. 너무 밝으면(흰색에 가까우면) 선택 막대가 안 보이므로 시스템 강조색을 쓴다. */
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque))) {
        BYTE r = (BYTE)((argb >> 16) & 0xFF), g = (BYTE)((argb >> 8) & 0xFF);
        BYTE b = (BYTE)(argb & 0xFF);
        if ((int)r + (int)g + (int)b < 3 * 0xD0) {
            return RGB(r, g, b);
        }
    }
    return GetSysColor(COLOR_HIGHLIGHT);
}

/* ---- 색 토큰과 다크 모드 ---- */

static int g_dark;

COLORREF theme_color_in(int dark, ThemeToken token)
{
    switch (token) {
    case THEME_TOKEN_BG:
        return dark ? RGB(0x20, 0x20, 0x20) : RGB(0xF3, 0xF3, 0xF3);
    case THEME_TOKEN_SURFACE:
        return dark ? RGB(0x2B, 0x2B, 0x2B) : RGB(0xFF, 0xFF, 0xFF);
    case THEME_TOKEN_TEXT_2ND:
        return dark ? RGB(0xC5, 0xC5, 0xC5) : RGB(0x5D, 0x5D, 0x5D);
    case THEME_TOKEN_STROKE:
        return dark ? RGB(0x3A, 0x3A, 0x3A) : RGB(0xE5, 0xE5, 0xE5);
    case THEME_TOKEN_SUBTLE:
        return dark ? RGB(0x27, 0x27, 0x27) : RGB(0xEA, 0xEA, 0xEA);
    case THEME_TOKEN_DANGER:
        return dark ? RGB(0xFF, 0x99, 0xA4) : RGB(0xC4, 0x2B, 0x1C);
    case THEME_TOKEN_FACE:
        return dark ? RGB(0x3A, 0x3A, 0x3A) : GetSysColor(COLOR_BTNFACE);
    case THEME_TOKEN_FRAME:
        return dark ? RGB(0x5C, 0x5C, 0x5C) : GetSysColor(COLOR_BTNSHADOW);
    case THEME_TOKEN_TEXT:
    default:
        return dark ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x1A, 0x1A, 0x1A);
    }
}

COLORREF theme_color(ThemeToken token)
{
    return theme_color_in(g_dark, token);
}

void theme_set_dark(int dark)
{
    g_dark = dark != 0;
}

int theme_is_dark(void)
{
    return g_dark;
}

/* 모드별 공용 브러시(BG·SURFACE). 한 번 만들면 프로세스가 끝날 때까지 쓴다. */
static HBRUSH mode_brush(int dark, ThemeToken token)
{
    static HBRUSH brushes[2][2];
    int t = token == THEME_TOKEN_SURFACE ? 1 : 0;

    dark = dark != 0;
    if (brushes[dark][t] == NULL) {
        brushes[dark][t] = CreateSolidBrush(theme_color_in(dark, token));
        if (brushes[dark][t] == NULL) {
            return GetSysColorBrush(t ? COLOR_WINDOW : COLOR_BTNFACE);
        }
    }
    return brushes[dark][t];
}

void theme_fill_rect(HDC dc, const RECT *r, COLORREF color)
{
    COLORREF old = SetBkColor(dc, color);

    /* 브러시를 만들지 않고 단색으로 채운다. */
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, r, NULL, 0, NULL);
    SetBkColor(dc, old);
}

void theme_frame_rect(HDC dc, const RECT *r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);

    if (brush != NULL) {
        FrameRect(dc, r, brush);
        DeleteObject(brush);
    }
}

HBRUSH theme_bg_brush(void)
{
    return mode_brush(g_dark, THEME_TOKEN_BG);
}

LRESULT theme_ctlcolor_static(HDC dc)
{
    SetBkColor(dc, THEME_COLOR_BG);
    SetTextColor(dc, g_dark ? THEME_COLOR_TEXT : GetSysColor(COLOR_WINDOWTEXT));
    return (LRESULT)theme_bg_brush();
}

LRESULT theme_ctlcolor(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    HDC dc = (HDC)wp;

    if (msg == WM_CTLCOLORSTATIC) {
        return theme_ctlcolor_static(dc);
    }
    if (!g_dark) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_CTLCOLORBTN) {
        return theme_ctlcolor_static(dc);
    }
    SetBkColor(dc, THEME_COLOR_SURFACE);
    SetTextColor(dc, THEME_COLOR_TEXT);
    return (LRESULT)mode_brush(1, THEME_TOKEN_SURFACE);
}

/* 목록 머리글(SysHeader32)의 NM_CUSTOMDRAW 는 목록에게 가므로, 목록을 서브클래스해 글자색을 맞춘다. */
static LRESULT CALLBACK list_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR ref)
{
    (void)ref;
    if (msg == WM_NOTIFY && g_dark) {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->code == NM_CUSTOMDRAW && hdr->hwndFrom == ListView_GetHeader(hwnd)) {
            NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lp;
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, THEME_COLOR_TEXT);
                return CDRF_DODEFAULT;
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, list_subclass, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* 어둡게면 dark_name 테마를, 밝게면 기본 테마(연결 해제)를 쓴다. */
static void set_theme(HWND h, const wchar_t *dark_name)
{
    if (g_dark) {
        SetWindowTheme(h, dark_name, NULL);
    } else {
        SetWindowTheme(h, NULL, NULL);
    }
}

static int class_is(HWND h, const wchar_t *name)
{
    wchar_t cls[64];

    return GetClassNameW(h, cls, (int)(sizeof(cls) / sizeof(cls[0]))) > 0 &&
           _wcsicmp(cls, name) == 0;
}

static int is_check_like(HWND h)
{
    LONG type = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK;

    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE ||
           type == BS_AUTO3STATE || type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON ||
           type == BS_GROUPBOX;
}

/* 컨트롤 하나에 지금 모드를 적용한다. */
static void apply_control(HWND h)
{
    HBRUSH old_bg = mode_brush(!g_dark, THEME_TOKEN_BG);

    /* theme_bg_brush 로 등록한 창 클래스는 배경 브러시를 지금 모드로 바꾼다(클래스 전체에 적용). */
    if ((HBRUSH)GetClassLongPtrW(h, GCLP_HBRBACKGROUND) == old_bg) {
        SetClassLongPtrW(h, GCLP_HBRBACKGROUND, (LONG_PTR)theme_bg_brush());
    }
    if (class_is(h, WC_LISTVIEWW)) {
        COLORREF bk = g_dark ? THEME_COLOR_SURFACE : GetSysColor(COLOR_WINDOW);
        COLORREF fg = g_dark ? THEME_COLOR_TEXT : GetSysColor(COLOR_WINDOWTEXT);
        set_theme(h, L"DarkMode_Explorer");
        ListView_SetBkColor(h, bk);
        ListView_SetTextBkColor(h, bk);
        ListView_SetTextColor(h, fg);
        SetWindowSubclass(h, list_subclass, THEME_LIST_SUBCLASS_ID, 0);
    } else if (class_is(h, WC_HEADERW)) {
        set_theme(h, L"DarkMode_ItemsView");
    } else if (class_is(h, WC_TREEVIEWW)) {
        set_theme(h, L"DarkMode_Explorer");
        TreeView_SetBkColor(h, g_dark ? THEME_COLOR_SURFACE : (COLORREF)-1);
        TreeView_SetTextColor(h, g_dark ? THEME_COLOR_TEXT : (COLORREF)-1);
    } else if (class_is(h, WC_BUTTONW)) {
        if (is_check_like(h)) {
            /* 테마를 켠 체크 상자는 WM_CTLCOLORSTATIC 의 글자색을 무시하므로 어둡게일 때는 끈다. */
            if (g_dark) {
                SetWindowTheme(h, L"", L"");
            } else {
                SetWindowTheme(h, NULL, NULL);
            }
        } else {
            set_theme(h, L"DarkMode_Explorer");
        }
    } else if (class_is(h, WC_EDITW)) {
        set_theme(h, L"DarkMode_CFD");
    } else if (class_is(h, WC_COMBOBOXW)) {
        COMBOBOXINFO info;
        set_theme(h, L"DarkMode_CFD");
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        if (GetComboBoxInfo(h, &info) && info.hwndList != NULL) {
            set_theme(info.hwndList, L"DarkMode_Explorer");
        }
    } else if (class_is(h, TOOLTIPS_CLASSW)) {
        set_theme(h, L"DarkMode_Explorer");
    }
}

static BOOL CALLBACK apply_child(HWND h, LPARAM lp)
{
    apply_control(h);
    (*(int *)lp)++;
    return TRUE;
}

/* 이 스레드의 최상위 도구 설명 창(목록·트리·트리맵의 도구 설명). */
static BOOL CALLBACK apply_thread_window(HWND h, LPARAM lp)
{
    if (class_is(h, TOOLTIPS_CLASSW)) {
        apply_control(h);
        (*(int *)lp)++;
    }
    return TRUE;
}

int theme_apply_window(HWND top)
{
    int count = 0;
    BOOL dark = g_dark ? TRUE : FALSE;

    if (top == NULL) {
        return 0;
    }
    if (FAILED(DwmSetWindowAttribute(top, THEME_DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                                     sizeof(dark)))) {
        DwmSetWindowAttribute(top, THEME_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &dark, sizeof(dark));
    }
    apply_control(top);
    count++;
    EnumChildWindows(top, apply_child, (LPARAM)&count);
    EnumThreadWindows(GetCurrentThreadId(), apply_thread_window, (LPARAM)&count);
    RedrawWindow(top, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    return count;
}

HFONT theme_create_title_font(HFONT base, int dpi)
{
    LOGFONTW lf;

    memset(&lf, 0, sizeof(lf));
    if (base == NULL || GetObjectW(base, sizeof(lf), &lf) != sizeof(lf)) {
        return NULL;
    }
    lf.lfHeight = -MulDiv(20, dpi > 0 ? dpi : 96, 72);
    lf.lfWidth = 0;
    lf.lfWeight = FW_SEMIBOLD;
    return CreateFontIndirectW(&lf);
}
