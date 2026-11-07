#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>
#include <wchar.h>

#include "app_settings.h"

#define SETTINGS_DEFAULT_KEY L"Software\\ByteSieve"
#define SETTINGS_KEY_ENV L"BYTESIEVE_SETTINGS_KEY"
#define SETTINGS_TEST_PREFIX L"Software\\ByteSieveTest\\"
#define SETTINGS_SYSTEM_THEME_ENV L"BYTESIEVE_SYSTEM_THEME"
#define SETTINGS_LANG_ENV L"BYTESIEVE_LANG"
#define SETTINGS_THEME_VALUE L"Theme"
#define SETTINGS_LANG_VALUE L"Language"
#define SETTINGS_KEY_MAX 256
#define SETTINGS_VALUE_MAX 32

#define PERSONALIZE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

static const wchar_t *const k_theme_name[THEME_MODE_COUNT] = {L"system", L"light", L"dark"};
static const UiText k_theme_text[THEME_MODE_COUNT] = {UI_T_THEME_SYSTEM, UI_T_THEME_LIGHT,
                                                      UI_T_THEME_DARK};

const wchar_t *settings_theme_name(ThemeMode mode)
{
    return (unsigned)mode < (unsigned)THEME_MODE_COUNT ? k_theme_name[mode]
                                                      : k_theme_name[THEME_MODE_SYSTEM];
}

UiText settings_theme_text(ThemeMode mode)
{
    return (unsigned)mode < (unsigned)THEME_MODE_COUNT ? k_theme_text[mode] : UI_T_COUNT;
}

int settings_theme_from_name(const wchar_t *name, ThemeMode *out)
{
    int i;

    if (name == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < THEME_MODE_COUNT; i++) {
        if (_wcsicmp(name, k_theme_name[i]) == 0) {
            *out = (ThemeMode)i;
            return 1;
        }
    }
    return 0;
}

int settings_theme_is_dark(ThemeMode mode, int system_dark, int high_contrast)
{
    if (high_contrast) {
        return 0;
    }
    switch (mode) {
    case THEME_MODE_DARK:
        return 1;
    case THEME_MODE_LIGHT:
        return 0;
    default:
        return system_dark != 0;
    }
}

const wchar_t *settings_reg_key(void)
{
    static wchar_t key[SETTINGS_KEY_MAX];
    DWORD n = GetEnvironmentVariableW(SETTINGS_KEY_ENV, key, SETTINGS_KEY_MAX);

    return n > 0 && n < SETTINGS_KEY_MAX ? key : SETTINGS_DEFAULT_KEY;
}

/* 설정 키의 REG_SZ 값을 읽는다. 성공 1. */
static int read_value(const wchar_t *name, wchar_t *buf, DWORD chars)
{
    DWORD size = chars * (DWORD)sizeof(wchar_t);

    return RegGetValueW(HKEY_CURRENT_USER, settings_reg_key(), name, RRF_RT_REG_SZ, NULL, buf,
                        &size) == ERROR_SUCCESS;
}

/* 설정 키(없으면 만듦)에 REG_SZ 값을 쓴다. 성공 0. */
static int write_value(const wchar_t *name, const wchar_t *value, unsigned long *error)
{
    HKEY key;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, settings_reg_key(), 0, NULL, 0, KEY_SET_VALUE,
                                 NULL, &key, NULL);

    if (st == ERROR_SUCCESS) {
        st = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                            (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if (st != ERROR_SUCCESS) {
        if (error != NULL) {
            *error = (unsigned long)st;
        }
        return -1;
    }
    return 0;
}

ThemeMode settings_load_theme(void)
{
    wchar_t buf[SETTINGS_VALUE_MAX];
    ThemeMode mode = THEME_MODE_SYSTEM;

    if (read_value(SETTINGS_THEME_VALUE, buf, SETTINGS_VALUE_MAX)) {
        settings_theme_from_name(buf, &mode);
    }
    return mode;
}

int settings_save_theme(ThemeMode mode, unsigned long *error)
{
    if ((unsigned)mode >= (unsigned)THEME_MODE_COUNT) {
        if (error != NULL) {
            *error = ERROR_INVALID_PARAMETER;
        }
        return -1;
    }
    return write_value(SETTINGS_THEME_VALUE, k_theme_name[mode], error);
}

int settings_load_language(UiLang *out)
{
    wchar_t buf[SETTINGS_VALUE_MAX];

    return read_value(SETTINGS_LANG_VALUE, buf, SETTINGS_VALUE_MAX) && ui_lang_from_tag(buf, out);
}

int settings_save_language(const UiLang *lang, unsigned long *error)
{
    LSTATUS st;

    if (lang != NULL) {
        if ((unsigned)*lang >= (unsigned)UI_LANG_COUNT) {
            if (error != NULL) {
                *error = ERROR_INVALID_PARAMETER;
            }
            return -1;
        }
        return write_value(SETTINGS_LANG_VALUE, ui_lang_name(*lang), error);
    }
    /* 시스템 따르기: 값만 지운다. 키나 값이 원래 없으면 이미 그 상태다. */
    st = RegDeleteKeyValueW(HKEY_CURRENT_USER, settings_reg_key(), SETTINGS_LANG_VALUE);
    if (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) {
        return 0;
    }
    if (error != NULL) {
        *error = (unsigned long)st;
    }
    return -1;
}

int settings_lang_env_override(UiLang *out)
{
    wchar_t buf[SETTINGS_VALUE_MAX];
    DWORD n = GetEnvironmentVariableW(SETTINGS_LANG_ENV, buf, SETTINGS_VALUE_MAX);

    return n > 0 && n < SETTINGS_VALUE_MAX && ui_lang_from_tag(buf, out);
}

int settings_system_dark(void)
{
    wchar_t buf[SETTINGS_VALUE_MAX];
    DWORD n = GetEnvironmentVariableW(SETTINGS_SYSTEM_THEME_ENV, buf, SETTINGS_VALUE_MAX);
    DWORD light = 1, size = sizeof(light);

    if (n > 0 && n < SETTINGS_VALUE_MAX) {
        if (_wcsicmp(buf, L"dark") == 0) {
            return 1;
        }
        if (_wcsicmp(buf, L"light") == 0) {
            return 0;
        }
    }
    if (RegGetValueW(HKEY_CURRENT_USER, PERSONALIZE_KEY, L"AppsUseLightTheme", RRF_RT_REG_DWORD,
                     NULL, &light, &size) != ERROR_SUCCESS) {
        return 0;
    }
    return light == 0;
}

int settings_high_contrast(void)
{
    HIGHCONTRASTW hc;

    memset(&hc, 0, sizeof(hc));
    hc.cbSize = sizeof(hc);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
           (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

int settings_key_is_test(void)
{
    const wchar_t *key = settings_reg_key();
    size_t prefix = wcslen(SETTINGS_TEST_PREFIX);

    return _wcsnicmp(key, SETTINGS_TEST_PREFIX, prefix) == 0 && key[prefix] != L'\0';
}

int settings_delete_test_key(void)
{
    const wchar_t *key = settings_reg_key();
    LSTATUS st;

    if (!settings_key_is_test()) {
        return 0;
    }
    st = RegDeleteTreeW(HKEY_CURRENT_USER, key);
    if (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) {
        RegDeleteKeyW(HKEY_CURRENT_USER, key);
        return 1;
    }
    return 0;
}
