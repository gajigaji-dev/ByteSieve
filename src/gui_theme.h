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

#endif
