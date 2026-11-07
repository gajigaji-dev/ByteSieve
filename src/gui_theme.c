#include "gui_theme.h"

#include <dwmapi.h>
#include <string.h>
#include <wchar.h>

/* DWM 창 속성 번호(Windows 11 SDK). 오래된 SDK 에서도 빌드되게 숫자로 둔다. */
#define THEME_DWMWA_WINDOW_CORNER_PREFERENCE 33
#define THEME_DWMWA_SYSTEMBACKDROP_TYPE 38
#define THEME_DWMWCP_ROUND 2
#define THEME_DWMSBT_MAINWINDOW 2 /* Mica */

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

HBRUSH theme_bg_brush(void)
{
    static HBRUSH brush;

    if (brush == NULL) {
        brush = CreateSolidBrush(THEME_COLOR_BG);
        if (brush == NULL) {
            return GetSysColorBrush(COLOR_BTNFACE);
        }
    }
    return brush;
}

LRESULT theme_ctlcolor_static(HDC dc)
{
    SetBkColor(dc, THEME_COLOR_BG);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    return (LRESULT)theme_bg_brush();
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
