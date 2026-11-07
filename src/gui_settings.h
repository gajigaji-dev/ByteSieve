#ifndef BYTESIEVE_GUI_SETTINGS_H
#define BYTESIEVE_GUI_SETTINGS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "app_settings.h"
#include "ui_lang.h"

/*
 * [설정] 페이지 패널(자식 창). 저장은 app_settings(레지스트리), 화면 문구는 네 언어로 번역한다
 * (언어를 고르는 화면이므로).
 *   - 모양: 테마(시스템 설정 따르기·밝게·어둡게). 고르면 바로 저장하고 부모에 STN_THEME_CHANGED 를 보낸다
 *     (부모가 창 전체에 적용). 고대비 테마가 켜져 있으면 어둡게 하지 않는다는 안내를 보인다.
 *   - 표시 언어: 시스템 설정 따르기(Windows 표시 언어 이름 표시)·한국어·English·日本語·简体中文.
 *     고르면 바로 저장하고, 다음 실행부터 적용된다는 안내를 보인다(실행 중인 창의 글자는 바꾸지 않음).
 *     환경 변수 BYTESIEVE_LANG 이 있으면 그 값이 먼저라는 안내를 보인다.
 *   - 스캔 기록: [스캔 기록 자동 저장] 체크 상자와 기록 폴더 위치·[폴더 열기]. 체크를 바꾸면 부모에
 *     STN_AUTOSAVE 를 보내고, 부모가 폴더 스캔 패널과 같은 경로(scan_panel_set_autosave)로 저장한 뒤
 *     settings_panel_sync 로 실제 상태를 알려 준다(두 페이지의 체크 상자가 어긋나지 않게).
 *   - 저장에 실패하면 이유를 패널 아래 상태 줄에 쓴다(테마는 이번 실행에만 적용).
 */

#define STN_THEME_CHANGED 1
#define STN_AUTOSAVE 2

int settings_panel_register(HINSTANCE inst);
/* 패널을 만든다(처음에는 숨김). 저장된 테마·언어를 읽어 콤보 상자에 보인다. 글꼴은 호출자가 소유한다. */
HWND settings_panel_create(HWND parent, int id, HFONT font, int dpi);

/* 지금 고른 테마. */
ThemeMode settings_panel_theme(HWND panel);
/* 테마를 고른다(사용자가 콤보 상자에서 고른 것과 같음: 저장 → STN_THEME_CHANGED). 저장했으면 1. */
int settings_panel_set_theme(HWND panel, ThemeMode mode);
/* 언어를 고른다(NULL 이면 시스템 설정 따르기). 저장했으면 1. */
int settings_panel_set_language(HWND panel, const UiLang *lang);

/* 사용자가 체크 상자로 요청한 자동 저장 상태(STN_AUTOSAVE 를 받은 부모가 읽음). */
int settings_panel_autosave_request(HWND panel);
/* 실제 자동 저장 상태를 보이고(ok 가 0 이면 실패 안내), 고대비 안내를 다시 확인한다. 페이지를 열 때도 부른다. */
void settings_panel_sync(HWND panel, int autosave_on, int ok);
/* 자체 검사: [스캔 기록 자동 저장] 체크 상자를 누른다(BM_CLICK). */
void settings_panel_click_autosave(HWND panel);

/* 패널 안 컨트롤이 잘리지 않는 최소 폭. */
int settings_panel_min_width(HWND panel);

/*
 * 자체 검사: 콤보 상자·체크 상자가 저장된 설정(app_settings)·autosave_on 과 맞는지, 언어 안내가 상황에
 * 맞는지 확인한다. 실패 수.
 */
int settings_panel_verify(HWND panel, int autosave_on);
/* 자체 검사(--self-test-lang): 지금 언어의 문구가 들어갔고 잘리지 않는지. 실패 수. */
int settings_panel_verify_lang(HWND panel);

#endif /* BYTESIEVE_GUI_SETTINGS_H */
