#include "gui_settings.h"

#include <commctrl.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gui_theme.h"
#include "scan_history.h"
#include "version.h"

#define SETTINGS_PANEL_CLASS L"ByteSieveSettingsPanel"

enum {
    IDC_H_APPEARANCE = 100,
    IDC_L_THEME,
    IDC_THEME,
    IDC_THEME_NOTE,
    IDC_H_LANGUAGE,
    IDC_L_LANGUAGE,
    IDC_LANGUAGE,
    IDC_LANG_NOTE,
    IDC_H_HISTORY,
    IDC_AUTOSAVE,
    IDC_HISTORY_DIR,
    IDC_OPEN_DIR,
    IDC_H_ABOUT,
    IDC_VERSION,
    IDC_STATUS
};

/* 96 DPI 기준 치수. */
#define ROW_H 26
#define HEAD_H 28
#define NOTE_H 20
#define SECTION_GAP 18
#define LABEL_MIN 120
#define COMBO_MIN 260
#define GAP 8

typedef struct {
    HWND hwnd;
    HWND h_appearance, l_theme, theme, theme_note;
    HWND h_language, l_language, language, lang_note;
    HWND h_history, autosave, history_dir, open_dir;
    HWND h_about, version;
    HWND status;
    HFONT font, section_font;
    int dpi;
    ThemeMode theme_mode;
    int status_error; /* 상태 줄을 경고 색으로 */
    /* 상태 줄 내용(언어를 바꾸면 새 언어로 다시 쓴다): 문구 번호와 그 %lu 인자. 비었으면 UI_T_COUNT. */
    UiText status_text;
    unsigned long status_arg;
} SettingsPanel;

typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

static SettingsPanel *panel_of(HWND hwnd)
{
    return (SettingsPanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const SettingsPanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

/* 상태 줄을 기억해 둔 문구 번호·인자와 지금 언어로 쓴다. */
static void render_status(SettingsPanel *p)
{
    wchar_t text[256];

    text[0] = L'\0';
    if (p->status_text < UI_T_COUNT) {
        swprintf(text, 256, ui_text(p->status_text), p->status_arg);
    }
    SetWindowTextW(p->status, text);
    InvalidateRect(p->status, NULL, TRUE);
}

/* 상태 줄에 문구 id(인자 %lu 하나 또는 없음)를 보인다. id 가 UI_T_COUNT 면 비운다. */
static void set_status(SettingsPanel *p, UiText id, unsigned long arg, int error)
{
    p->status_error = error;
    p->status_text = id;
    p->status_arg = arg;
    render_status(p);
}

/* ---- 언어 ---- */

/* 콤보 상자 번호: 0 = 시스템 설정 따르기, 1.. = UiLang + 1. */
static int language_index(void)
{
    UiLang lang;

    return settings_load_language(&lang) ? (int)lang + 1 : 0;
}

/* 콤보 상자에서 고른 언어가 실제로 쓰일 언어(시스템 따르기면 Windows 표시 언어). */
static UiLang chosen_language(int index)
{
    if (index >= 1 && index <= UI_LANG_COUNT) {
        return (UiLang)(index - 1);
    }
    return ui_lang_from_langid(GetUserDefaultUILanguage());
}

/* 언어 안내: 환경 변수가 먼저면 그 안내, 고른 언어가 지금 화면 언어와 다르면 다시 시작 안내. */
static void update_lang_note(SettingsPanel *p)
{
    wchar_t text[256];
    UiLang env;
    int index = (int)SendMessageW(p->language, CB_GETCURSEL, 0, 0);

    text[0] = L'\0';
    if (settings_lang_env_override(&env)) {
        swprintf(text, 256, ui_text(UI_T_SET_LANG_ENV), ui_lang_native_name(env));
    } else if (chosen_language(index) != ui_lang_current()) {
        wcsncpy_s(text, 256, ui_text(UI_T_SET_LANG_RESTART), _TRUNCATE);
    }
    SetWindowTextW(p->lang_note, text);
}

static int apply_language(SettingsPanel *p, int index)
{
    unsigned long error = 0;
    UiLang lang;
    int ok;

    if (index < 0 || index > UI_LANG_COUNT) {
        return 0;
    }
    SendMessageW(p->language, CB_SETCURSEL, (WPARAM)index, 0);
    lang = (UiLang)(index - 1);
    ok = settings_save_language(index == 0 ? NULL : &lang, &error) == 0;
    if (ok) {
        set_status(p, UI_T_COUNT, 0, 0);
    } else {
        set_status(p, UI_T_SET_SAVE_FAILED, error, 1);
    }
    update_lang_note(p);
    /* 저장에 실패해도 이번 실행에는 적용한다(부모가 언어를 바꾸고 settings_panel_apply_language 를 부름). */
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), STN_LANGUAGE_CHANGED), (LPARAM)p->hwnd);
    return ok;
}

/* ---- 테마 ---- */

static void panel_layout(SettingsPanel *p);

static void update_theme_note(SettingsPanel *p)
{
    int show = settings_high_contrast();
    int shown = (GetWindowLongW(p->theme_note, GWL_STYLE) & WS_VISIBLE) != 0;

    if (show != shown) {
        ShowWindow(p->theme_note, show ? SW_SHOW : SW_HIDE);
        panel_layout(p); /* 안내 줄 자리를 넣거나 뺀다 */
    }
}

static int apply_theme(SettingsPanel *p, ThemeMode mode)
{
    unsigned long error = 0;
    int ok;

    if ((unsigned)mode >= (unsigned)THEME_MODE_COUNT) {
        return 0;
    }
    SendMessageW(p->theme, CB_SETCURSEL, (WPARAM)mode, 0);
    p->theme_mode = mode;
    ok = settings_save_theme(mode, &error) == 0;
    if (ok) {
        set_status(p, UI_T_COUNT, 0, 0);
    } else {
        set_status(p, UI_T_SET_SAVE_FAILED, error, 1);
    }
    /* 저장에 실패해도 이번 실행에는 적용한다. */
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), STN_THEME_CHANGED), (LPARAM)p->hwnd);
    return ok;
}

/* ---- 레이아웃·생성 ---- */

static int max_text(SettingsPanel *p, const wchar_t *const *texts, int n)
{
    int i, w = 0;

    for (i = 0; i < n; i++) {
        int t = theme_text_width(p->hwnd, p->font, texts[i]);
        if (t > w) {
            w = t;
        }
    }
    return w;
}

static int label_width(SettingsPanel *p)
{
    const wchar_t *labels[2];
    int w;

    labels[0] = ui_text(UI_T_SET_THEME);
    labels[1] = ui_text(UI_T_SET_LANG_LABEL);
    w = max_text(p, labels, 2) + pscale(p, 16);
    return w > pscale(p, LABEL_MIN) ? w : pscale(p, LABEL_MIN);
}

static int combo_width(SettingsPanel *p)
{
    wchar_t buf[128];
    int i, n, w = 0;

    for (i = 0; i < 2; i++) {
        HWND combo = i == 0 ? p->theme : p->language;
        int k;
        n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
        for (k = 0; k < n; k++) {
            if (SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)k, 0) < 128) {
                SendMessageW(combo, CB_GETLBTEXT, (WPARAM)k, (LPARAM)buf);
                if (theme_text_width(p->hwnd, p->font, buf) > w) {
                    w = theme_text_width(p->hwnd, p->font, buf);
                }
            }
        }
    }
    w += pscale(p, 40); /* 드롭다운 단추·여백 */
    return w > pscale(p, COMBO_MIN) ? w : pscale(p, COMBO_MIN);
}

static int check_width(SettingsPanel *p)
{
    return theme_check_width(p->hwnd, p->font, ui_text(UI_T_SCAN_AUTOSAVE), p->dpi,
                             pscale(p, 160));
}

static int open_width(SettingsPanel *p)
{
    return theme_button_width(p->hwnd, p->font, ui_text(UI_T_SET_OPEN_DIR), p->dpi,
                              pscale(p, 96));
}

static void panel_layout(SettingsPanel *p)
{
    RECT rc;
    int lw = label_width(p), cw = combo_width(p), w, y = 0;
    int row = pscale(p, ROW_H), head = pscale(p, HEAD_H), note = pscale(p, NOTE_H);
    int gap = pscale(p, GAP), section = pscale(p, SECTION_GAP);

    GetClientRect(p->hwnd, &rc);
    w = rc.right > 0 ? rc.right : 0;

    MoveWindow(p->h_appearance, 0, y, w, head, TRUE);
    y += head + gap / 2;
    MoveWindow(p->l_theme, 0, y + pscale(p, 4), lw, row - pscale(p, 4), TRUE);
    /* 콤보 상자 높이는 펼친 목록까지 포함한다. */
    MoveWindow(p->theme, lw, y, cw, row + pscale(p, 160), TRUE);
    y += row + gap / 2;
    MoveWindow(p->theme_note, lw, y, w - lw > 0 ? w - lw : 0, note, TRUE);
    /* 고대비 안내는 보일 때만 자리를 차지한다. */
    if (GetWindowLongW(p->theme_note, GWL_STYLE) & WS_VISIBLE) {
        y += note;
    }
    y += section;

    MoveWindow(p->h_language, 0, y, w, head, TRUE);
    y += head + gap / 2;
    MoveWindow(p->l_language, 0, y + pscale(p, 4), lw, row - pscale(p, 4), TRUE);
    MoveWindow(p->language, lw, y, cw, row + pscale(p, 200), TRUE);
    y += row + gap / 2;
    MoveWindow(p->lang_note, lw, y, w - lw > 0 ? w - lw : 0, note, TRUE);
    y += note + section;

    MoveWindow(p->h_history, 0, y, w, head, TRUE);
    y += head + gap / 2;
    MoveWindow(p->autosave, 0, y, check_width(p), row, TRUE);
    y += row + gap / 2;
    MoveWindow(p->history_dir, 0, y + pscale(p, 4), w, row - pscale(p, 4), TRUE);
    y += row + gap / 2;
    MoveWindow(p->open_dir, 0, y, open_width(p), row, TRUE);
    y += row + section;

    MoveWindow(p->h_about, 0, y, w, head, TRUE);
    y += head + gap / 2;
    MoveWindow(p->version, 0, y + pscale(p, 4), w, row - pscale(p, 4), TRUE);
    y += row + section;

    MoveWindow(p->status, 0, y, w, note * 2, TRUE);
}

static HWND panel_child(SettingsPanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        int id, HFONT font)
{
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, FALSE);
    return h;
}

/* 구역 제목 글꼴: 본문 글꼴과 같은 이름·조금 크게·SemiBold. 실패하면 NULL(본문 글꼴을 씀). */
static HFONT create_section_font(HFONT base, int dpi)
{
    LOGFONTW lf;

    memset(&lf, 0, sizeof(lf));
    if (base == NULL || GetObjectW(base, sizeof(lf), &lf) != sizeof(lf)) {
        return NULL;
    }
    lf.lfHeight = -MulDiv(11, dpi > 0 ? dpi : 96, 72);
    lf.lfWidth = 0;
    lf.lfWeight = FW_SEMIBOLD;
    return CreateFontIndirectW(&lf);
}

static void fill_history_dir(SettingsPanel *p)
{
    wchar_t dir[MAX_PATH * 2], text[MAX_PATH * 2 + 64];

    if (history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0) {
        dir[0] = L'\0';
    }
    swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_SET_HISTORY_DIR), dir);
    SetWindowTextW(p->history_dir, text);
}

static void panel_create_controls(SettingsPanel *p)
{
    HFONT head = p->section_font != NULL ? p->section_font : p->font;
    wchar_t text[128];
    int i;

    p->h_appearance =
        panel_child(p, L"STATIC", ui_text(UI_T_SET_APPEARANCE), SS_LEFTNOWORDWRAP, IDC_H_APPEARANCE, head);
    p->l_theme = panel_child(p, L"STATIC", ui_text(UI_T_SET_THEME), SS_LEFTNOWORDWRAP, IDC_L_THEME,
                             p->font);
    p->theme = panel_child(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                           IDC_THEME, p->font);
    for (i = 0; i < THEME_MODE_COUNT; i++) {
        SendMessageW(p->theme, CB_ADDSTRING, 0, (LPARAM)ui_text(settings_theme_text((ThemeMode)i)));
    }
    p->theme_note = panel_child(p, L"STATIC", ui_text(UI_T_SET_THEME_HC),
                                SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, IDC_THEME_NOTE, p->font);

    p->h_language =
        panel_child(p, L"STATIC", ui_text(UI_T_SET_LANGUAGE), SS_LEFTNOWORDWRAP, IDC_H_LANGUAGE, head);
    p->l_language = panel_child(p, L"STATIC", ui_text(UI_T_SET_LANG_LABEL), SS_LEFTNOWORDWRAP,
                                IDC_L_LANGUAGE, p->font);
    p->language = panel_child(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                              IDC_LANGUAGE, p->font);
    swprintf(text, 128, ui_text(UI_T_LANG_SYSTEM),
             ui_lang_native_name(ui_lang_from_langid(GetUserDefaultUILanguage())));
    SendMessageW(p->language, CB_ADDSTRING, 0, (LPARAM)text);
    for (i = 0; i < UI_LANG_COUNT; i++) {
        /* 언어 이름은 그 언어로 쓴다(지금 화면 언어를 못 읽어도 자기 언어를 찾을 수 있게). */
        SendMessageW(p->language, CB_ADDSTRING, 0, (LPARAM)ui_lang_native_name((UiLang)i));
    }
    p->lang_note = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS,
                               IDC_LANG_NOTE, p->font);

    p->h_history =
        panel_child(p, L"STATIC", ui_text(UI_T_SET_HISTORY), SS_LEFTNOWORDWRAP, IDC_H_HISTORY, head);
    p->autosave = panel_child(p, L"BUTTON", ui_text(UI_T_SCAN_AUTOSAVE),
                              BS_AUTOCHECKBOX | WS_TABSTOP, IDC_AUTOSAVE, p->font);
    p->history_dir = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS | SS_NOPREFIX,
                                 IDC_HISTORY_DIR, p->font);
    p->open_dir = panel_child(p, L"BUTTON", ui_text(UI_T_SET_OPEN_DIR), BS_PUSHBUTTON | WS_TABSTOP,
                              IDC_OPEN_DIR, p->font);
    p->h_about = panel_child(p, L"STATIC", ui_text(UI_T_SET_ABOUT), SS_LEFTNOWORDWRAP, IDC_H_ABOUT, head);
    swprintf(text, 128, ui_text(UI_T_SET_VERSION), BYTESIEVE_VERSION_WSTRING);
    p->version = panel_child(p, L"STATIC", text, SS_LEFTNOWORDWRAP | SS_NOPREFIX, IDC_VERSION, p->font);
    p->status = panel_child(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, IDC_STATUS, p->font);

    /* 저장된 설정을 보인다(읽기만 함). */
    p->theme_mode = settings_load_theme();
    SendMessageW(p->theme, CB_SETCURSEL, (WPARAM)p->theme_mode, 0);
    SendMessageW(p->language, CB_SETCURSEL, (WPARAM)language_index(), 0);
    update_lang_note(p);
    update_theme_note(p);
    fill_history_dir(p);
}

static void open_history_dir(SettingsPanel *p)
{
    wchar_t dir[MAX_PATH * 2];
    INT_PTR rc;

    if (history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0 ||
        history_ensure_dir(dir) != 0) {
        set_status(p, UI_T_SET_SAVE_FAILED, (unsigned long)GetLastError(), 1);
        return;
    }
    rc = (INT_PTR)ShellExecuteW(p->hwnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
    if (rc <= 32) {
        set_status(p, UI_T_SET_SAVE_FAILED, (unsigned long)rc, 1);
    }
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SettingsPanel *p = panel_of(hwnd);

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
        p->status_text = UI_T_COUNT;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
        break;
    }
    case WM_CREATE:
        p->section_font = create_section_font(p->font, p->dpi);
        panel_create_controls(p);
        return 0;
    case WM_SIZE:
        if (p != NULL) {
            panel_layout(p);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC: {
        /* 안내는 보조 글자색, 실패한 상태 줄은 경고 색. */
        HDC dc = (HDC)wp;
        LRESULT brush = theme_ctlcolor_static(dc);
        if (p != NULL && ((HWND)lp == p->theme_note || (HWND)lp == p->lang_note)) {
            SetTextColor(dc, THEME_COLOR_TEXT_2ND);
        } else if (p != NULL && (HWND)lp == p->status && p->status_error) {
            SetTextColor(dc, THEME_COLOR_DANGER);
        }
        return brush;
    }
    case WM_COMMAND:
        if (p == NULL) {
            break;
        }
        switch (LOWORD(wp)) {
        case IDC_THEME:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                apply_theme(p, (ThemeMode)SendMessageW(p->theme, CB_GETCURSEL, 0, 0));
            }
            return 0;
        case IDC_LANGUAGE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                apply_language(p, (int)SendMessageW(p->language, CB_GETCURSEL, 0, 0));
            }
            return 0;
        case IDC_AUTOSAVE:
            if (HIWORD(wp) == BN_CLICKED) {
                SendMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(hwnd), STN_AUTOSAVE), (LPARAM)hwnd);
            }
            return 0;
        case IDC_OPEN_DIR:
            if (HIWORD(wp) == BN_CLICKED) {
                open_history_dir(p);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_NCDESTROY:
        if (p != NULL && p->section_font != NULL) {
            DeleteObject(p->section_font);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        free(p);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int settings_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = SETTINGS_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND settings_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(WS_EX_CONTROLPARENT, SETTINGS_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

ThemeMode settings_panel_theme(HWND panel)
{
    SettingsPanel *p = panel_of(panel);

    return p != NULL ? p->theme_mode : THEME_MODE_SYSTEM;
}

int settings_panel_set_theme(HWND panel, ThemeMode mode)
{
    SettingsPanel *p = panel_of(panel);

    return p != NULL && apply_theme(p, mode);
}

int settings_panel_set_language(HWND panel, const UiLang *lang)
{
    SettingsPanel *p = panel_of(panel);

    if (p == NULL || (lang != NULL && (unsigned)*lang >= (unsigned)UI_LANG_COUNT)) {
        return 0;
    }
    return apply_language(p, lang == NULL ? 0 : (int)*lang + 1);
}

UiLang settings_panel_language(HWND panel)
{
    SettingsPanel *p = panel_of(panel);

    if (p == NULL) {
        return ui_lang_current();
    }
    return chosen_language((int)SendMessageW(p->language, CB_GETCURSEL, 0, 0));
}

void settings_panel_apply_language(HWND panel, HFONT font)
{
    SettingsPanel *p = panel_of(panel);
    HFONT head;
    HWND child;
    wchar_t text[128];
    LRESULT sel;
    int i;

    if (p == NULL) {
        return;
    }
    if (font != NULL && font != p->font) {
        HFONT section = create_section_font(font, p->dpi);
        p->font = font;
        if (p->section_font != NULL) {
            DeleteObject(p->section_font);
        }
        p->section_font = section;
    }
    head = p->section_font != NULL ? p->section_font : p->font;
    for (child = GetWindow(p->hwnd, GW_CHILD); child != NULL; child = GetWindow(child, GW_HWNDNEXT)) {
        int is_head = child == p->h_appearance || child == p->h_language ||
                      child == p->h_history || child == p->h_about;
        SendMessageW(child, WM_SETFONT, (WPARAM)(is_head ? head : p->font), FALSE);
    }

    SetWindowTextW(p->h_appearance, ui_text(UI_T_SET_APPEARANCE));
    SetWindowTextW(p->l_theme, ui_text(UI_T_SET_THEME));
    sel = SendMessageW(p->theme, CB_GETCURSEL, 0, 0);
    SendMessageW(p->theme, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < THEME_MODE_COUNT; i++) {
        SendMessageW(p->theme, CB_ADDSTRING, 0, (LPARAM)ui_text(settings_theme_text((ThemeMode)i)));
    }
    SendMessageW(p->theme, CB_SETCURSEL, (WPARAM)sel, 0);
    SetWindowTextW(p->theme_note, ui_text(UI_T_SET_THEME_HC));

    SetWindowTextW(p->h_language, ui_text(UI_T_SET_LANGUAGE));
    SetWindowTextW(p->l_language, ui_text(UI_T_SET_LANG_LABEL));
    /* "시스템 설정 따르기(…)" 항목만 지금 언어로 바꾼다(언어 이름은 그 언어 자체로 쓴 것이라 그대로). */
    sel = SendMessageW(p->language, CB_GETCURSEL, 0, 0);
    swprintf(text, 128, ui_text(UI_T_LANG_SYSTEM),
             ui_lang_native_name(ui_lang_from_langid(GetUserDefaultUILanguage())));
    SendMessageW(p->language, CB_DELETESTRING, 0, 0);
    SendMessageW(p->language, CB_INSERTSTRING, 0, (LPARAM)text);
    SendMessageW(p->language, CB_SETCURSEL, (WPARAM)sel, 0);

    SetWindowTextW(p->h_history, ui_text(UI_T_SET_HISTORY));
    SetWindowTextW(p->autosave, ui_text(UI_T_SCAN_AUTOSAVE));
    SetWindowTextW(p->open_dir, ui_text(UI_T_SET_OPEN_DIR));
    fill_history_dir(p);
    SetWindowTextW(p->h_about, ui_text(UI_T_SET_ABOUT));
    swprintf(text, 128, ui_text(UI_T_SET_VERSION), BYTESIEVE_VERSION_WSTRING);
    SetWindowTextW(p->version, text);

    update_lang_note(p);
    render_status(p);
    panel_layout(p);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int settings_panel_autosave_request(HWND panel)
{
    SettingsPanel *p = panel_of(panel);

    return p != NULL && SendMessageW(p->autosave, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void settings_panel_sync(HWND panel, int autosave_on, int ok)
{
    SettingsPanel *p = panel_of(panel);

    if (p == NULL) {
        return;
    }
    SendMessageW(p->autosave, BM_SETCHECK, autosave_on ? BST_CHECKED : BST_UNCHECKED, 0);
    if (!ok) {
        set_status(p, UI_T_SET_AUTOSAVE_FAILED, 0, 1);
    }
    update_theme_note(p);
    fill_history_dir(p);
}

void settings_panel_click_autosave(HWND panel)
{
    SettingsPanel *p = panel_of(panel);

    if (p != NULL) {
        SendMessageW(p->autosave, BM_CLICK, 0, 0);
    }
}

int settings_panel_min_width(HWND panel)
{
    SettingsPanel *p = panel_of(panel);
    int a, b;

    if (p == NULL) {
        return 0;
    }
    a = label_width(p) + combo_width(p);
    b = check_width(p);
    return a > b ? a : b;
}

/* ---- 자체 검사 ---- */

#define SELF_CHECK(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fwprintf(stderr, L"self-test failed: %hs (gui_settings.c line %d)\n", #cond, \
                     __LINE__);                                              \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int text_is(HWND h, const wchar_t *expect)
{
    wchar_t buf[256];

    GetWindowTextW(h, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return wcscmp(buf, expect) == 0;
}

static int text_empty(HWND h)
{
    return GetWindowTextLengthW(h) == 0;
}

int settings_panel_verify(HWND panel, int autosave_on)
{
    SettingsPanel *p = panel_of(panel);
    int failures = 0, index;
    UiLang env;

    SELF_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    SELF_CHECK(SendMessageW(p->theme, CB_GETCOUNT, 0, 0) == THEME_MODE_COUNT);
    SELF_CHECK(SendMessageW(p->language, CB_GETCOUNT, 0, 0) == UI_LANG_COUNT + 1);
    /* 화면 = 저장된 설정 */
    SELF_CHECK(SendMessageW(p->theme, CB_GETCURSEL, 0, 0) == (LRESULT)settings_load_theme());
    SELF_CHECK(p->theme_mode == settings_load_theme());
    index = (int)SendMessageW(p->language, CB_GETCURSEL, 0, 0);
    SELF_CHECK(index == language_index());
    SELF_CHECK((SendMessageW(p->autosave, BM_GETCHECK, 0, 0) == BST_CHECKED) == (autosave_on != 0));
    /* 언어 안내 */
    if (settings_lang_env_override(&env)) {
        SELF_CHECK(!text_empty(p->lang_note));
    } else if (chosen_language(index) != ui_lang_current()) {
        SELF_CHECK(text_is(p->lang_note, ui_text(UI_T_SET_LANG_RESTART)));
    } else {
        SELF_CHECK(text_empty(p->lang_note));
    }
    SELF_CHECK(((GetWindowLongW(p->theme_note, GWL_STYLE) & WS_VISIBLE) != 0) ==
               (settings_high_contrast() != 0));
    SELF_CHECK(!text_empty(p->history_dir));
    return failures;
}

/* 컨트롤 h 의 폭이 지금 글꼴로 그린 글자 폭(+extra) 이상인지. */
static int fits(SettingsPanel *p, HWND h, HFONT font, int extra)
{
    wchar_t buf[256];
    RECT r;

    GetWindowTextW(h, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    GetWindowRect(h, &r);
    return r.right - r.left >= theme_text_width(p->hwnd, font, buf) + extra;
}

int settings_panel_verify_lang(HWND panel)
{
    SettingsPanel *p = panel_of(panel);
    HFONT head;
    RECT panel_rc, r;
    int failures = 0;

    SELF_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    head = p->section_font != NULL ? p->section_font : p->font;
    SELF_CHECK(text_is(p->h_appearance, ui_text(UI_T_SET_APPEARANCE)));
    SELF_CHECK(text_is(p->l_theme, ui_text(UI_T_SET_THEME)));
    SELF_CHECK(text_is(p->h_history, ui_text(UI_T_SET_HISTORY)));
    SELF_CHECK(text_is(p->autosave, ui_text(UI_T_SCAN_AUTOSAVE)));
    SELF_CHECK(text_is(p->open_dir, ui_text(UI_T_SET_OPEN_DIR)));
    SELF_CHECK(text_is(p->h_about, ui_text(UI_T_SET_ABOUT)));
    {
        /* 정보: 지금 빌드의 버전(CMake project VERSION)을 보인다 */
        wchar_t buf[128];
        GetWindowTextW(p->version, buf, 128);
        SELF_CHECK(wcsstr(buf, BYTESIEVE_VERSION_WSTRING) != NULL);
    }
    SELF_CHECK(fits(p, p->version, p->font, 0));
    SELF_CHECK(fits(p, p->h_appearance, head, 0));
    SELF_CHECK(fits(p, p->h_language, head, 0));
    SELF_CHECK(fits(p, p->l_theme, p->font, 0));
    SELF_CHECK(fits(p, p->l_language, p->font, 0));
    SELF_CHECK(fits(p, p->autosave, p->font, pscale(p, 13)));
    SELF_CHECK(fits(p, p->open_dir, p->font, pscale(p, 8)));
    /* 콤보 상자가 패널 안에 있다 */
    GetWindowRect(p->hwnd, &panel_rc);
    GetWindowRect(p->language, &r);
    SELF_CHECK(r.right <= panel_rc.right);
    GetWindowRect(p->theme, &r);
    SELF_CHECK(r.right <= panel_rc.right);
    return failures;
}
