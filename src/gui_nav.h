#ifndef BYTESIEVE_GUI_NAV_H
#define BYTESIEVE_GUI_NAV_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "nav_model.h"

/*
 * 왼쪽 사이드바(직접 그리는 자식 창). 메뉴 구성·키 이동·위치 판정은 nav_model 이 정하고,
 * 이 창은 그리기와 입력만 맡는다(Windows 11 설정 앱 모양). [설정]은 아래 끝에 붙는다(footer).
 * 색은 gui_theme 의 토큰을 그릴 때마다 읽으므로 다크 모드로 바꾸면 다시 그리기만 하면 된다.
 *   - 그룹 머리글은 보조 글자색, 페이지 항목은 본문 글자색으로 그린다.
 *   - 고른 항목: 흰 표면(둥근 모서리) + 왼쪽 강조색 막대. 마우스를 올린 항목: 옅은 배경.
 *   - 마우스 클릭, ↑·↓·Home·End 로 고른다(Tab 으로 포커스를 받으면 고른 항목에 포커스 테두리).
 *   - 고른 항목이 바뀌면 부모에게 WM_COMMAND(MAKEWPARAM(id, NVN_SELCHANGE), nav) 를 보낸다
 *     (nav_view_select 의 notify 가 0 이면 보내지 않음). 같은 항목을 다시 고르면 보내지 않는다.
 */

#define NVN_SELCHANGE 1

int nav_view_register(HINSTANCE inst);

/* 사이드바를 만든다(보이는 상태, 처음 고른 항목은 NAV_PAGE_SCAN). 글꼴은 호출자가 소유한다. */
HWND nav_view_create(HWND parent, int id, HFONT font, int dpi);

/*
 * 글꼴을 바꾸고(표시 언어를 바꿨을 때) 다시 그린다. 문구는 그릴 때마다 지금 언어로 가져오고 폭도
 * nav_view_width 가 그때 재므로, 부모는 이어서 다시 배치하면 된다. 글꼴은 호출자가 소유한다.
 */
void nav_view_set_font(HWND nav, HFONT font);

/* DPI 가 바뀌면(배율이 다른 모니터로 옮김·배율 변경) 크기 계산 기준을 바꾼다. 글꼴·폭·배치는 이어서
 * 부르는 nav_view_set_font 가 다시 잡는다. */
void nav_view_set_dpi(HWND nav, int dpi);

/* 사이드바 폭: 가장 긴 문구(머리글·항목) + 여백, 최소 200px(96 DPI 기준). */
int nav_view_width(HWND nav);

/* 모든 항목이 겹치지 않고 들어가는 사이드바 높이(아래 여백 포함). 최소 창 높이에 쓴다.
 * 사이드바가 이보다 낮으면(작업 영역이 작음) 세로 스크롤 막대가 생기고 휠·키보드로 옮긴다. */
int nav_view_min_height(HWND nav);

/* 고른 페이지. */
NavPage nav_view_selected(HWND nav);

/* page 를 고른다(바뀌었고 notify 가 1 이면 부모에게 알림). 범위 밖이면 무시. 바뀌었으면 1. */
int nav_view_select(HWND nav, NavPage page, int notify);

/* 자체 검사: page 항목의 사이드바 안 사각형(클라이언트 좌표). 성공 1. */
int nav_view_item_rect(HWND nav, NavPage page, RECT *out);

#endif /* BYTESIEVE_GUI_NAV_H */
