#ifndef BYTESIEVE_APP_SETTINGS_H
#define BYTESIEVE_APP_SETTINGS_H

#include <wchar.h>

#include "ui_lang.h"

/*
 * GUI 설정(테마·표시 언어)의 저장과 해석. 창·GDI 를 쓰지 않으므로 단위 테스트한다.
 *
 * 저장 위치: 레지스트리 HKCU\<키> 의 REG_SZ 값.
 *   <키>      기본 Software\ByteSieve. 환경 변수 BYTESIEVE_SETTINGS_KEY 가 있으면 그 하위 키(HKCU 기준)를
 *             쓴다(테스트가 사용자의 실제 설정을 건드리지 않게). ui_lang_detect 도 같은 규칙으로 읽는다.
 *   Theme     "system" | "light" | "dark". 없거나 해석할 수 없으면 system.
 *   Language  "ko" | "en" | "ja" | "zh-Hans". 없으면 시스템(Windows 표시 언어)을 따른다.
 *             설치 프로그램이 설치 언어로 기록하는 값과 같다.
 *
 * 계약
 *   - 읽기 함수는 아무것도 쓰지 않는다. 값 하나를 쓰는 것은 레지스트리가 원자적으로 처리한다.
 *   - 언어를 "시스템 설정 따르기"로 저장하면 Language 값을 지운다(키·다른 값은 그대로).
 *   - 표시 언어는 프로그램을 시작할 때 한 번 정한다(ui_lang). 저장한 언어는 다음 실행부터 적용된다.
 *   - 스레드: GUI 스레드에서만 부른다(settings_reg_key 가 정적 버퍼를 쓴다).
 */

typedef enum {
    THEME_MODE_SYSTEM = 0, /* Windows 의 앱 모드(밝게·어둡게)를 따른다 */
    THEME_MODE_LIGHT,
    THEME_MODE_DARK,
    THEME_MODE_COUNT
} ThemeMode;

/* 식별자("system", "light", "dark"). 저장 값·자체 검사 출력에 쓴다. 범위 밖이면 "system". */
const wchar_t *settings_theme_name(ThemeMode mode);
/* 화면 문구 번호(ui_text 로 꺼냄). 범위 밖이면 UI_T_COUNT. */
UiText settings_theme_text(ThemeMode mode);
/* 식별자를 해석한다(대소문자 무시). 해석했으면 *out 에 넣고 1, 아니면 *out 을 건드리지 않고 0. */
int settings_theme_from_name(const wchar_t *name, ThemeMode *out);

/*
 * 실제로 어둡게 그릴지. system_dark 는 Windows 앱 모드가 어둡게이면 1.
 * 고대비 테마가 켜져 있으면(high_contrast) 항상 0(고대비 색과 섞이지 않게).
 */
int settings_theme_is_dark(ThemeMode mode, int system_dark, int high_contrast);

/* 설정 키(HKCU 기준). 환경 변수가 비었거나 너무 길면 기본 키. */
const wchar_t *settings_reg_key(void);

/* 저장된 테마. 없거나 해석할 수 없으면 THEME_MODE_SYSTEM. */
ThemeMode settings_load_theme(void);
/* 테마를 저장한다. 성공 0, 실패 -1(*error 에 Windows 오류, NULL 허용). */
int settings_save_theme(ThemeMode mode, unsigned long *error);

/* 저장된 언어가 있고 해석할 수 있으면 *out 에 넣고 1. 없으면(시스템 따르기) 0. */
int settings_load_language(UiLang *out);
/* 언어를 저장한다. lang 이 NULL 이면 시스템 따르기(값을 지움). 성공 0, 실패 -1. */
int settings_save_language(const UiLang *lang, unsigned long *error);

/* 환경 변수 BYTESIEVE_LANG 이 지원 언어로 설정되어 있으면 *out 에 넣고 1(저장한 언어보다 먼저 적용됨). */
int settings_lang_env_override(UiLang *out);

/*
 * Windows 앱 모드가 어둡게이면 1(HKCU\...\Themes\Personalize 의 AppsUseLightTheme 가 0).
 * 값이 없으면(Windows 10 1809 이전 등) 0. 환경 변수 BYTESIEVE_SYSTEM_THEME 가 "dark"·"light" 면
 * 그 값을 쓴다(테스트용).
 */
int settings_system_dark(void);
/* 고대비 테마가 켜져 있으면 1. */
int settings_high_contrast(void);

/* 설정 키가 환경 변수로 지정한 테스트 키(Software\ByteSieveTest\ 아래)이면 1. 설정을 바꾸는 자체 검사는
 * 이때만 실행한다(사용자의 실제 설정을 바꾸지 않게). */
int settings_key_is_test(void);

/*
 * 테스트 정리: 설정 키가 환경 변수로 지정한 Software\ByteSieveTest\ 아래 키일 때만 그 키를 통째로 지운다.
 * 기본 키(사용자의 실제 설정)는 지우지 않는다. 지웠으면(또는 이미 없으면) 1.
 */
int settings_delete_test_key(void);

#endif /* BYTESIEVE_APP_SETTINGS_H */
