#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "app_settings.h"
#include "ui_lang.h"

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                              \
        }                                                                              \
    } while (0)

/* 이 테스트가 쓰는 레지스트리 키(HKCU, 끝나면 지움). 사용자의 실제 설정 키는 건드리지 않는다. */
static wchar_t g_key[128];

static void set_raw(const wchar_t *name, const wchar_t *value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, g_key, name, REG_SZ, value,
                    (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static int raw_is(const wchar_t *name, const wchar_t *expect)
{
    wchar_t buf[64];
    DWORD size = sizeof(buf);

    return RegGetValueW(HKEY_CURRENT_USER, g_key, name, RRF_RT_REG_SZ, NULL, buf, &size) ==
               ERROR_SUCCESS &&
           wcscmp(buf, expect) == 0;
}

static int raw_missing(const wchar_t *name)
{
    return RegGetValueW(HKEY_CURRENT_USER, g_key, name, RRF_RT_ANY, NULL, NULL, NULL) ==
           ERROR_FILE_NOT_FOUND;
}

/* 식별자·화면 문구. */
static void test_names(void)
{
    ThemeMode mode = THEME_MODE_COUNT;
    int i, k, lang;

    for (i = 0; i < THEME_MODE_COUNT; i++) {
        CHECK(settings_theme_from_name(settings_theme_name((ThemeMode)i), &mode) && mode == i);
    }
    CHECK(wcscmp(settings_theme_name(THEME_MODE_DARK), L"dark") == 0);
    CHECK(wcscmp(settings_theme_name(THEME_MODE_COUNT), L"system") == 0);
    CHECK(settings_theme_from_name(L"DARK", &mode) && mode == THEME_MODE_DARK);
    mode = THEME_MODE_LIGHT;
    CHECK(!settings_theme_from_name(L"purple", &mode) && mode == THEME_MODE_LIGHT);
    CHECK(!settings_theme_from_name(L"", &mode) && mode == THEME_MODE_LIGHT);
    CHECK(!settings_theme_from_name(NULL, &mode));
    CHECK(!settings_theme_from_name(L"dark", NULL));

    /* 화면 문구: 네 언어 모두 있고 서로 다름 */
    CHECK(settings_theme_text(THEME_MODE_COUNT) == UI_T_COUNT);
    for (lang = 0; lang < UI_LANG_COUNT; lang++) {
        for (i = 0; i < THEME_MODE_COUNT; i++) {
            const wchar_t *a = ui_text_in((UiLang)lang, settings_theme_text((ThemeMode)i));
            CHECK(a[0] != L'\0');
            for (k = i + 1; k < THEME_MODE_COUNT; k++) {
                CHECK(wcscmp(a, ui_text_in((UiLang)lang, settings_theme_text((ThemeMode)k))) != 0);
            }
        }
    }
    CHECK(wcscmp(ui_text_in(UI_LANG_KO, settings_theme_text(THEME_MODE_DARK)), L"어둡게") == 0);
}

/* 실제로 어둡게 할지: 고대비면 항상 밝게, 시스템 따르기면 시스템 값. */
static void test_is_dark(void)
{
    CHECK(settings_theme_is_dark(THEME_MODE_DARK, 0, 0) == 1);
    CHECK(settings_theme_is_dark(THEME_MODE_DARK, 1, 0) == 1);
    CHECK(settings_theme_is_dark(THEME_MODE_LIGHT, 1, 0) == 0);
    CHECK(settings_theme_is_dark(THEME_MODE_SYSTEM, 1, 0) == 1);
    CHECK(settings_theme_is_dark(THEME_MODE_SYSTEM, 0, 0) == 0);
    CHECK(settings_theme_is_dark(THEME_MODE_DARK, 1, 1) == 0); /* 고대비 */
    CHECK(settings_theme_is_dark(THEME_MODE_SYSTEM, 1, 1) == 0);
    CHECK(settings_theme_is_dark(THEME_MODE_COUNT, 1, 0) == 1); /* 범위 밖은 시스템 따르기 */
}

/* 설정 키: 환경 변수가 없으면 기본 키, 테스트 키 판정, 기본 키는 지우지 않음. */
static void test_key(void)
{
    SetEnvironmentVariableW(L"BYTESIEVE_SETTINGS_KEY", NULL);
    CHECK(wcscmp(settings_reg_key(), L"Software\\ByteSieve") == 0);
    CHECK(!settings_key_is_test());
    CHECK(!settings_delete_test_key()); /* 사용자의 실제 설정 키는 지우지 않는다 */

    SetEnvironmentVariableW(L"BYTESIEVE_SETTINGS_KEY", L"Software\\Other");
    CHECK(!settings_key_is_test());
    CHECK(!settings_delete_test_key());
    SetEnvironmentVariableW(L"BYTESIEVE_SETTINGS_KEY", L"Software\\ByteSieveTest\\");
    CHECK(!settings_key_is_test()); /* 테스트 키 상위 전체는 안 됨 */

    SetEnvironmentVariableW(L"BYTESIEVE_SETTINGS_KEY", g_key);
    CHECK(wcscmp(settings_reg_key(), g_key) == 0);
    CHECK(settings_key_is_test());
}

/* 테마 저장·읽기(테스트 키). */
static void test_theme_store(void)
{
    unsigned long error = 0;

    CHECK(settings_load_theme() == THEME_MODE_SYSTEM); /* 키가 없음 */
    CHECK(settings_save_theme(THEME_MODE_DARK, &error) == 0);
    CHECK(raw_is(L"Theme", L"dark"));
    CHECK(settings_load_theme() == THEME_MODE_DARK);
    set_raw(L"Theme", L"Light");
    CHECK(settings_load_theme() == THEME_MODE_LIGHT);
    set_raw(L"Theme", L"purple"); /* 해석할 수 없으면 시스템 따르기 */
    CHECK(settings_load_theme() == THEME_MODE_SYSTEM);
    CHECK(settings_save_theme(THEME_MODE_COUNT, &error) == -1 && error == ERROR_INVALID_PARAMETER);
    CHECK(raw_is(L"Theme", L"purple")); /* 실패하면 바꾸지 않음 */
    CHECK(settings_save_theme(THEME_MODE_SYSTEM, NULL) == 0);
    CHECK(settings_load_theme() == THEME_MODE_SYSTEM);
}

/* 언어 저장·읽기와 ui_lang_detect(같은 키 규칙) 연동. */
static void test_language_store(void)
{
    UiLang lang = UI_LANG_KO, bad = UI_LANG_COUNT;
    UiLang windows = ui_lang_from_langid(GetUserDefaultUILanguage());
    unsigned long error = 0;

    SetEnvironmentVariableW(L"BYTESIEVE_LANG", NULL);
    CHECK(!settings_load_language(&lang));
    CHECK(ui_lang_detect() == windows);

    lang = UI_LANG_JA;
    CHECK(settings_save_language(&lang, &error) == 0);
    CHECK(raw_is(L"Language", L"ja"));
    lang = UI_LANG_KO;
    CHECK(settings_load_language(&lang) && lang == UI_LANG_JA);
    CHECK(ui_lang_detect() == UI_LANG_JA);

    lang = UI_LANG_ZH_HANS;
    CHECK(settings_save_language(&lang, NULL) == 0);
    CHECK(raw_is(L"Language", L"zh-Hans"));
    CHECK(ui_lang_detect() == UI_LANG_ZH_HANS);

    /* 시스템 따르기: Language 값만 지우고 다른 값(Theme)은 그대로 */
    CHECK(settings_save_theme(THEME_MODE_DARK, NULL) == 0);
    CHECK(settings_save_language(NULL, &error) == 0);
    CHECK(raw_missing(L"Language"));
    CHECK(!settings_load_language(&lang));
    CHECK(ui_lang_detect() == windows);
    CHECK(settings_load_theme() == THEME_MODE_DARK);
    CHECK(settings_save_language(NULL, &error) == 0); /* 이미 없어도 성공 */

    set_raw(L"Language", L"zh-TW"); /* 지원하지 않는 언어(번체)는 없는 것과 같음 */
    CHECK(!settings_load_language(&lang));
    CHECK(settings_save_language(&bad, &error) == -1 && error == ERROR_INVALID_PARAMETER);
    CHECK(raw_is(L"Language", L"zh-TW"));

    /* 환경 변수 BYTESIEVE_LANG 이 저장 값보다 먼저 */
    lang = UI_LANG_JA;
    CHECK(settings_save_language(&lang, NULL) == 0);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"en-US");
    CHECK(settings_lang_env_override(&lang) && lang == UI_LANG_EN);
    CHECK(ui_lang_detect() == UI_LANG_EN);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"xx");
    lang = UI_LANG_KO;
    CHECK(!settings_lang_env_override(&lang) && lang == UI_LANG_KO);
    CHECK(ui_lang_detect() == UI_LANG_JA);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", NULL);
    CHECK(!settings_lang_env_override(&lang));
}

/* 시스템 앱 모드: 테스트용 환경 변수가 먼저. */
static void test_system_dark(void)
{
    int real;

    SetEnvironmentVariableW(L"BYTESIEVE_SYSTEM_THEME", L"dark");
    CHECK(settings_system_dark() == 1);
    SetEnvironmentVariableW(L"BYTESIEVE_SYSTEM_THEME", L"LIGHT");
    CHECK(settings_system_dark() == 0);
    SetEnvironmentVariableW(L"BYTESIEVE_SYSTEM_THEME", L"neon"); /* 해석할 수 없으면 실제 값 */
    real = settings_system_dark();
    SetEnvironmentVariableW(L"BYTESIEVE_SYSTEM_THEME", NULL);
    CHECK(settings_system_dark() == real);
    CHECK(real == 0 || real == 1);
    CHECK(settings_high_contrast() == 0 || settings_high_contrast() == 1);
}

/* 정리: 테스트 키만 지운다. */
static void test_cleanup(void)
{
    HKEY key;

    CHECK(settings_delete_test_key());
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, g_key, 0, KEY_READ, &key) == ERROR_FILE_NOT_FOUND);
    CHECK(settings_delete_test_key()); /* 이미 없어도 성공 */
}

int main(void)
{
    swprintf(g_key, sizeof(g_key) / sizeof(g_key[0]), L"Software\\ByteSieveTest\\app_settings_%lu",
             (unsigned long)GetCurrentProcessId());
    RegDeleteTreeW(HKEY_CURRENT_USER, g_key);
    RegDeleteKeyW(HKEY_CURRENT_USER, g_key);

    test_names();
    test_is_dark();
    test_key();
    test_theme_store();
    test_language_store();
    test_system_dark();
    test_cleanup();
    SetEnvironmentVariableW(L"BYTESIEVE_SETTINGS_KEY", NULL);
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("app_settings: all checks passed\n");
    return 0;
}
