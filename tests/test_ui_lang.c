#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ui_lang.h"

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                              \
        }                                                                              \
    } while (0)

/* 서식 지정자만 차례로 이어 붙인 문자열("%ls|%u|"). "%%" 는 글자이므로 뺀다. */
static void format_signature(const wchar_t *s, wchar_t *out, size_t len)
{
    size_t used = 0;

    out[0] = L'\0';
    while (*s != L'\0') {
        if (*s == L'%' && s[1] == L'%') {
            s += 2;
            continue;
        }
        if (*s == L'%') {
            /* 변환 문자(서식의 끝)까지 복사한다: 플래그·폭·정밀도·길이 수식자는 그 앞에 온다. */
            while (*s != L'\0' && used + 2 < len) {
                out[used++] = *s;
                if (wcschr(L"diouxXeEfgGaAcCsSpn", *s) != NULL && *s != L'%') {
                    s++;
                    break;
                }
                s++;
            }
            out[used++] = L'|';
            out[used] = L'\0';
            continue;
        }
        s++;
    }
}

static void test_table(void)
{
    int id, lang;

    for (id = 0; id < UI_T_COUNT; id++) {
        wchar_t base[128], sig[128];
        format_signature(ui_text_in(UI_LANG_KO, (UiText)id), base, 128);
        for (lang = 0; lang < UI_LANG_COUNT; lang++) {
            const wchar_t *s = ui_text_in((UiLang)lang, (UiText)id);
            if (s[0] == L'\0') {
                fprintf(stderr, "missing text: id %d, lang %ls\n", id,
                        ui_lang_name((UiLang)lang));
                g_failures++;
            }
            format_signature(s, sig, 128);
            if (wcscmp(sig, base) != 0) {
                fprintf(stderr, "format mismatch: id %d, lang %ls: \"%ls\" vs \"%ls\"\n", id,
                        ui_lang_name((UiLang)lang), sig, base);
                g_failures++;
            }
        }
    }
    /* 번역 누락: 한국어가 아닌 문자열에 한글(가-힣)이 들어 있으면 한국어를 그대로 넣은 것이다. */
    for (id = 0; id < UI_T_COUNT; id++) {
        for (lang = 0; lang < UI_LANG_COUNT; lang++) {
            const wchar_t *s = ui_text_in((UiLang)lang, (UiText)id);
            if (lang == UI_LANG_KO) {
                continue;
            }
            for (; *s != L'\0'; s++) {
                if (*s >= 0xAC00 && *s <= 0xD7A3) {
                    fprintf(stderr, "untranslated (Hangul): id %d, lang %ls\n", id,
                            ui_lang_name((UiLang)lang));
                    g_failures++;
                    break;
                }
            }
        }
    }
    /* 폴더 상태 표시는 이름 뒤에 붙으므로 모든 언어에서 공백으로 시작한다. */
    for (id = UI_T_FLAG_FAILED; id <= UI_T_FLAG_DEPTH_LIMIT; id++) {
        for (lang = 0; lang < UI_LANG_COUNT; lang++) {
            CHECK(ui_text_in((UiLang)lang, (UiText)id)[0] == L' ');
        }
    }
    /* 서식 검사 함수 자체 확인 */
    {
        wchar_t sig[64];
        format_signature(L"%ls: %u개 (100%%) %-8.2f", sig, 64);
        CHECK(wcscmp(sig, L"%ls|%u|%-8.2f|") == 0);
    }
    /* 범위 밖은 빈 문자열(NULL 아님) */
    CHECK(wcscmp(ui_text_in(UI_LANG_COUNT, UI_T_NAV_SCAN), L"") == 0);
    CHECK(wcscmp(ui_text_in(UI_LANG_EN, UI_T_COUNT), L"") == 0);
    CHECK(wcscmp(ui_text_in((UiLang)-1, UI_T_NAV_SCAN), L"") == 0);
}

static void test_current(void)
{
    CHECK(ui_lang_current() == UI_LANG_KO); /* 처음 값 */
    CHECK(wcscmp(ui_text(UI_T_NAV_SCAN), L"폴더 스캔") == 0);
    ui_lang_set(UI_LANG_JA);
    CHECK(ui_lang_current() == UI_LANG_JA);
    CHECK(wcscmp(ui_text(UI_T_SCAN_START), L"スキャン") == 0);
    ui_lang_set(UI_LANG_COUNT); /* 무시 */
    ui_lang_set((UiLang)-1);    /* 무시 */
    CHECK(ui_lang_current() == UI_LANG_JA);
    ui_lang_set(UI_LANG_EN);
    CHECK(wcscmp(ui_text(UI_T_SCAN_START), L"Scan") == 0);
    ui_lang_set(UI_LANG_KO);
}

static void check_tag(const wchar_t *tag, int ok, UiLang expect, int line)
{
    UiLang out = UI_LANG_COUNT;
    int r = ui_lang_from_tag(tag, &out);

    if (r != ok || (ok && out != expect) || (!ok && out != UI_LANG_COUNT)) {
        fprintf(stderr, "%s:%d: tag \"%ls\": got %d/%d\n", __FILE__, line,
                tag != NULL ? tag : L"(null)", r, (int)out);
        g_failures++;
    }
}

#define CHECK_TAG(tag, ok, expect) check_tag((tag), (ok), (expect), __LINE__)

static void test_tags(void)
{
    int lang;
    UiLang out;

    CHECK_TAG(L"ko", 1, UI_LANG_KO);
    CHECK_TAG(L"ko-KR", 1, UI_LANG_KO);
    CHECK_TAG(L"KO_kr", 1, UI_LANG_KO);
    CHECK_TAG(L"en", 1, UI_LANG_EN);
    CHECK_TAG(L"en-US", 1, UI_LANG_EN);
    CHECK_TAG(L"en-GB", 1, UI_LANG_EN);
    CHECK_TAG(L"ja", 1, UI_LANG_JA);
    CHECK_TAG(L"ja-JP", 1, UI_LANG_JA);
    CHECK_TAG(L"zh", 1, UI_LANG_ZH_HANS);
    CHECK_TAG(L"zh-Hans", 1, UI_LANG_ZH_HANS);
    CHECK_TAG(L"zh-hans-cn", 1, UI_LANG_ZH_HANS);
    CHECK_TAG(L"zh-CN", 1, UI_LANG_ZH_HANS);
    CHECK_TAG(L"zh_SG", 1, UI_LANG_ZH_HANS);

    /* 번체·지원하지 않는 언어·잘못된 값 */
    CHECK_TAG(L"zh-TW", 0, UI_LANG_COUNT);
    CHECK_TAG(L"zh-Hant", 0, UI_LANG_COUNT);
    CHECK_TAG(L"zh-HK", 0, UI_LANG_COUNT);
    CHECK_TAG(L"fr-FR", 0, UI_LANG_COUNT);
    CHECK_TAG(L"korean", 0, UI_LANG_COUNT);
    CHECK_TAG(L"k", 0, UI_LANG_COUNT);
    CHECK_TAG(L"", 0, UI_LANG_COUNT);
    CHECK_TAG(NULL, 0, UI_LANG_COUNT);
    CHECK_TAG(L"ko-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0, UI_LANG_COUNT);
    CHECK(ui_lang_from_tag(L"ko", NULL) == 0);

    /* 식별자는 다시 해석하면 같은 언어, 표시 이름은 비어 있지 않다. */
    for (lang = 0; lang < UI_LANG_COUNT; lang++) {
        out = UI_LANG_COUNT;
        CHECK(ui_lang_from_tag(ui_lang_name((UiLang)lang), &out) && out == (UiLang)lang);
        CHECK(ui_lang_native_name((UiLang)lang)[0] != L'\0');
    }
    CHECK(wcscmp(ui_lang_name(UI_LANG_ZH_HANS), L"zh-Hans") == 0);
    CHECK(wcscmp(ui_lang_name(UI_LANG_COUNT), L"en") == 0);
}

static void test_langid(void)
{
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)) == UI_LANG_KO);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)) == UI_LANG_JA);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == UI_LANG_EN);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) ==
          UI_LANG_ZH_HANS);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)) ==
          UI_LANG_ZH_HANS);
    /* 번체는 지원하지 않아 영어 */
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) ==
          UI_LANG_EN);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)) == UI_LANG_EN);
    CHECK(ui_lang_from_langid(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)) == UI_LANG_EN);
    /* 앱 언어 → LANGID(Windows 가 만드는 글자를 맞출 때) → 다시 앱 언어 */
    {
        int lang;
        for (lang = 0; lang < UI_LANG_COUNT; lang++) {
            CHECK(ui_lang_from_langid(ui_lang_langid((UiLang)lang)) == (UiLang)lang);
        }
        CHECK(ui_lang_langid(UI_LANG_COUNT) == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    }
    CHECK(ui_lang_from_langid(0) == UI_LANG_EN);
}

/* 환경 변수가 레지스트리·Windows 표시 언어보다 먼저다(레지스트리는 바꾸지 않는다). */
static void test_detect(void)
{
    UiLang fallback;

    SetEnvironmentVariableW(L"BYTESIEVE_LANG", NULL);
    fallback = ui_lang_detect();
    CHECK(fallback >= UI_LANG_KO && fallback < UI_LANG_COUNT);

    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"ja-JP");
    CHECK(ui_lang_detect() == UI_LANG_JA);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"zh-CN");
    CHECK(ui_lang_detect() == UI_LANG_ZH_HANS);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"EN");
    CHECK(ui_lang_detect() == UI_LANG_EN);
    /* 해석할 수 없는 값은 건너뛰고 다음 단계(환경 변수가 없을 때와 같음) */
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", L"fr");
    CHECK(ui_lang_detect() == fallback);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG",
                            L"ko-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(ui_lang_detect() == fallback);
    SetEnvironmentVariableW(L"BYTESIEVE_LANG", NULL);
}

int main(void)
{
    test_table();
    test_current();
    test_tags();
    test_langid();
    test_detect();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("ui_lang: all checks passed\n");
    return 0;
}
