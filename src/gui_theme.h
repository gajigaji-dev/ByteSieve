#ifndef BYTESIEVE_GUI_THEME_H
#define BYTESIEVE_GUI_THEME_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ui_lang.h"

/*
 * GUI 공통 모양(Windows 11 Fluent 방향): 창 외곽, 언어별 UI 글꼴, 글자 폭에 맞춘 컨트롤 크기.
 *
 * - 외부 라이브러리 없이 DWM(dwmapi)과 GDI 만 쓴다.
 * - Windows 11 이 아니어서 DWM 속성을 지원하지 않으면 조용히 무시하고 기존 모양으로 둔다
 *   (실패해도 창 동작에는 영향이 없다).
 * - 크기 함수는 실제 글꼴로 글자 폭을 재므로, 언어에 따라 길이가 다른 버튼 문구가 잘리지 않는다.
 *   돌려주는 값은 최소 폭(min_px) 이상이다. 그래서 기존 한국어 화면의 배치는 그대로 유지된다.
 */

/* 창 외곽을 Windows 11 모양으로: 둥근 모서리, 제목 표시줄 Mica 배경(밝은 테마).
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
 * 색 토큰(밝은 테마, docs/ui-design.md 의 "디자인 토큰"). 다크 모드(4단계)에서 함수로 바꾼다.
 *   BG             창·패널 배경(Mica 가 비치는 자리)
 *   SURFACE        카드·목록·트리·사이드바 선택 항목
 *   TEXT·TEXT_2ND  본문·보조 문구(사이드바 그룹 머리글)
 *   STROKE         구분선
 *   SUBTLE         사이드바 마우스를 올린 항목 배경
 */
#define THEME_COLOR_BG RGB(0xF3, 0xF3, 0xF3)
#define THEME_COLOR_SURFACE RGB(0xFF, 0xFF, 0xFF)
#define THEME_COLOR_TEXT RGB(0x1A, 0x1A, 0x1A)
#define THEME_COLOR_TEXT_2ND RGB(0x5D, 0x5D, 0x5D)
#define THEME_COLOR_STROKE RGB(0xE5, 0xE5, 0xE5)
#define THEME_COLOR_SUBTLE RGB(0xEA, 0xEA, 0xEA)
#define THEME_COLOR_DANGER RGB(0xC4, 0x2B, 0x1C)

/* 시스템 강조색(DWM 색, 없으면 COLOR_HIGHLIGHT). 사이드바 선택 막대·포커스 표시에 쓴다. */
COLORREF theme_accent_color(void);

/* BG 색 브러시. 프로세스가 끝날 때까지 살아 있는 공용 브러시이므로 지우지 않는다(창 클래스 배경에 쓸 수 있음). */
HBRUSH theme_bg_brush(void);

/*
 * 패널의 WM_CTLCOLORSTATIC 처리: 정적 텍스트·체크 상자·읽기 전용 입력 칸의 배경을 BG 로 맞춘다.
 * 창 프로시저에서 `return theme_ctlcolor_static((HDC)wp);` 로 쓴다.
 */
LRESULT theme_ctlcolor_static(HDC dc);

/* 페이지 제목 글꼴: base 와 같은 글꼴 이름으로 크기 20pt(96 DPI 에서 약 27px)·SemiBold. 실패하면 NULL.
 * 호출한 쪽이 DeleteObject 로 지운다. */
HFONT theme_create_title_font(HFONT base, int dpi);

#endif
