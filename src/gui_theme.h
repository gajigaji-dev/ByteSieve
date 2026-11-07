#ifndef BYTESIEVE_GUI_THEME_H
#define BYTESIEVE_GUI_THEME_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ui_lang.h"

/*
 * GUI 공통 모양(Windows 11 Fluent 방향): 창 외곽, 밝게·어둡게 색 토큰과 다크 모드 적용, 언어별 UI 글꼴,
 * 글자 폭에 맞춘 컨트롤 크기.
 *
 * - 외부 라이브러리 없이 DWM(dwmapi)과 GDI 만 쓴다.
 * - Windows 11 이 아니어서 DWM 속성을 지원하지 않으면 조용히 무시하고 기존 모양으로 둔다
 *   (실패해도 창 동작에는 영향이 없다).
 * - 크기 함수는 실제 글꼴로 글자 폭을 재므로, 언어에 따라 길이가 다른 버튼 문구가 잘리지 않는다.
 *   돌려주는 값은 최소 폭(min_px) 이상이다. 그래서 기존 한국어 화면의 배치는 그대로 유지된다.
 */

/* 창 외곽을 Windows 11 모양으로: 둥근 모서리, 제목 표시줄 Mica 배경(어두운 제목 표시줄은 theme_apply_window).
 * 지원하는 속성 수(0~2)를 돌려준다. hwnd 가 NULL 이면 0. */
int theme_apply_frame(HWND hwnd);

/* 언어별 UI 글꼴 이름. 설치되어 있지 않으면 NULL(시스템 메시지 글꼴을 그대로 씀).
 * 한국어 Malgun Gothic, 일본어 Yu Gothic UI, 중국어 간체 Microsoft YaHei UI,
 * 영어 Segoe UI Variable Text(Windows 11) → Segoe UI.
 * 한자는 언어마다 글자 모양이 달라(한자 통합) 언어에 맞는 글꼴을 골라야 한다. */
const wchar_t *theme_font_face(UiLang lang);

/* 시스템 메시지 글꼴과 같은 크기·굵기로, 글꼴 이름만 언어에 맞춘 UI 글꼴을 만든다.
 * 호출한 쪽이 DeleteObject 로 지운다. 실패하면 NULL. */
HFONT theme_create_ui_font(UiLang lang);

/* font 로 그린 text 의 폭(픽셀). ref 는 DC 를 얻을 창(숨긴 창도 됨). 실패하면 0. */
int theme_text_width(HWND ref, HFONT font, const wchar_t *text);

/* 누름 단추: 글자 폭 + 좌우 여백. min_px 보다 작으면 min_px. */
int theme_button_width(HWND ref, HFONT font, const wchar_t *text, int dpi, int min_px);

/* 체크 상자: 상자 + 간격 + 글자 폭 + 여백. min_px 보다 작으면 min_px. */
int theme_check_width(HWND ref, HFONT font, const wchar_t *text, int dpi, int min_px);

/*
 * 색 토큰(docs/ui-design.md 의 "디자인 토큰"). 밝은·어두운 값이 있고, 지금 모드(theme_set_dark)의 값을 돌려준다.
 *   BG             창·패널 배경(Mica 가 비치는 자리)
 *   SURFACE        카드·목록·트리·그래프 바탕, 입력 칸, 사이드바 선택 항목
 *   TEXT·TEXT_2ND  본문·보조 문구(사이드바 그룹 머리글, 눈금 숫자)
 *   STROKE         구분선
 *   SUBTLE         사이드바 마우스를 올린 항목 배경
 *   DANGER         경고 글자(보호 경로 등)·사용률 경고
 *   FACE·FRAME     막대 바탕·트리맵 안내 줄, 막대·그래프 테두리(밝게는 시스템 색 BTNFACE·BTNSHADOW)
 */
typedef enum {
    THEME_TOKEN_BG = 0,
    THEME_TOKEN_SURFACE,
    THEME_TOKEN_TEXT,
    THEME_TOKEN_TEXT_2ND,
    THEME_TOKEN_STROKE,
    THEME_TOKEN_SUBTLE,
    THEME_TOKEN_DANGER,
    THEME_TOKEN_FACE,
    THEME_TOKEN_FRAME,
    THEME_TOKEN_COUNT
} ThemeToken;

/* 지금 모드의 토큰 색. 범위 밖이면 TEXT. */
COLORREF theme_color(ThemeToken token);
/* 모드를 정해 토큰 색을 꺼낸다(자체 검사·문서용). */
COLORREF theme_color_in(int dark, ThemeToken token);

#define THEME_COLOR_BG theme_color(THEME_TOKEN_BG)
#define THEME_COLOR_SURFACE theme_color(THEME_TOKEN_SURFACE)
#define THEME_COLOR_TEXT theme_color(THEME_TOKEN_TEXT)
#define THEME_COLOR_TEXT_2ND theme_color(THEME_TOKEN_TEXT_2ND)
#define THEME_COLOR_STROKE theme_color(THEME_TOKEN_STROKE)
#define THEME_COLOR_SUBTLE theme_color(THEME_TOKEN_SUBTLE)
#define THEME_COLOR_DANGER theme_color(THEME_TOKEN_DANGER)
#define THEME_COLOR_FACE theme_color(THEME_TOKEN_FACE)
#define THEME_COLOR_FRAME theme_color(THEME_TOKEN_FRAME)

/*
 * 다크 모드. 처음은 밝게. 바꾼 뒤 theme_apply_window 로 이미 만든 창에 적용한다.
 * GUI 스레드에서만 부른다. 실제로 어둡게 할지(시스템 설정·고대비)는 app_settings 가 정한다.
 */
void theme_set_dark(int dark);
int theme_is_dark(void);

/*
 * 지금 모드를 top(최상위 창)과 그 모든 자식 창, 이 스레드의 도구 설명 창에 적용하고 다시 그린다.
 *   - 제목 표시줄: DWM 어두운 모드 속성(Windows 10 20H1+ / 11. 지원하지 않으면 그대로).
 *   - 목록·트리: 배경·글자 색과 Explorer 다크 테마(스크롤 막대·선택 표시), 목록 머리글은 어두운 머리글.
 *   - 누름 단추: 다크 테마. 체크 상자: 어둡게일 때는 비주얼 스타일을 끄고 패널의 WM_CTLCOLORSTATIC 색을
 *     따른다(테마를 켠 체크 상자는 글자 색을 바꿀 수 없음). 입력 칸·콤보 상자: 다크 테마 + theme_ctlcolor.
 *   - 창 클래스 배경 브러시(theme_bg_brush 로 등록한 것)를 지금 모드의 브러시로 바꾼다.
 *   - 밝게로 되돌리면 위 변경을 모두 되돌린다(기본 테마·시스템 색).
 * 확인 창(MessageBox)·오른쪽 클릭 메뉴는 Windows 가 그리므로 바뀌지 않는다.
 * 적용한 창 수.
 */
int theme_apply_window(HWND top);

/* 사각형을 color 로 채운다 / 1px 테두리를 그린다(직접 그리는 막대·그래프용). */
void theme_fill_rect(HDC dc, const RECT *r, COLORREF color);
void theme_frame_rect(HDC dc, const RECT *r, COLORREF color);

/* 시스템 강조색(DWM 색, 없으면 COLOR_HIGHLIGHT). 사이드바 선택 막대·포커스 표시에 쓴다. */
COLORREF theme_accent_color(void);

/* 지금 모드의 BG 색 브러시. 프로세스가 끝날 때까지 살아 있는 공용 브러시이므로 지우지 않는다
 * (창 클래스 배경에 쓸 수 있음. 모드를 바꾸면 theme_apply_window 가 클래스 배경도 바꾼다). */
HBRUSH theme_bg_brush(void);

/*
 * 패널의 WM_CTLCOLORSTATIC 처리: 정적 텍스트·체크 상자·읽기 전용 입력 칸의 배경을 BG 로, 글자를 TEXT 로 맞춘다.
 * 창 프로시저에서 `return theme_ctlcolor_static((HDC)wp);` 로 쓴다.
 */
LRESULT theme_ctlcolor_static(HDC dc);

/*
 * 패널의 WM_CTLCOLOREDIT·WM_CTLCOLORLISTBOX·WM_CTLCOLORBTN 처리. 어둡게일 때는 입력 칸·콤보 상자 목록을
 * SURFACE 바탕에 TEXT 글자로, 밝게일 때는 기본 처리(DefWindowProc)를 한다. WM_CTLCOLORSTATIC 이면
 * theme_ctlcolor_static 과 같다. `return theme_ctlcolor(hwnd, msg, wp, lp);`
 */
LRESULT theme_ctlcolor(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* 페이지 제목 글꼴: base 와 같은 글꼴 이름으로 크기 20pt(96 DPI 에서 약 27px)·SemiBold. 실패하면 NULL.
 * 호출한 쪽이 DeleteObject 로 지운다. */
HFONT theme_create_title_font(HFONT base, int dpi);

#endif
