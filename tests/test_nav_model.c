#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "nav_model.h"
#include "ui_lang.h"

static int g_failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                              \
        }                                                                              \
    } while (0)

/* 메뉴 표: 모든 페이지가 정확히 한 번, 자기 그룹 머리글 아래(footer 는 머리글 없이 맨 끝)에 있다. */
static void test_table(void)
{
    int seen[NAV_PAGE_COUNT] = {0}, headers[NAV_GROUP_COUNT] = {0};
    size_t i, n = nav_row_count();
    NavGroup group = NAV_GROUP_COUNT;
    int p, g;

    CHECK(n == NAV_PAGE_COUNT + NAV_GROUP_COUNT - 1); /* footer 는 머리글이 없다 */
    CHECK(nav_row(0) != NULL && nav_row(0)->kind == NAV_ROW_HEADER);
    CHECK(nav_row(n) == NULL);
    for (i = 0; i < n; i++) {
        const NavRow *r = nav_row(i);
        if (r->kind == NAV_ROW_HEADER) {
            CHECK(nav_group_has_header(r->group));
            headers[r->group]++;
            group = r->group;
        } else if (r->group == NAV_GROUP_FOOTER) {
            /* footer 뒤에는 footer 만 온다 */
            CHECK(i + 1 == n || nav_row(i + 1)->group == NAV_GROUP_FOOTER);
            seen[r->page]++;
            CHECK(nav_row_of_page(r->page) == i);
        } else {
            CHECK(r->group == group); /* 바로 위 머리글의 그룹 */
            CHECK((unsigned)r->page < NAV_PAGE_COUNT);
            seen[r->page]++;
            CHECK(nav_row_of_page(r->page) == i);
            CHECK(nav_page_group(r->page) == r->group);
        }
    }
    for (p = 0; p < NAV_PAGE_COUNT; p++) {
        CHECK(seen[p] == 1);
    }
    for (g = 0; g < NAV_GROUP_COUNT; g++) {
        CHECK(headers[g] == (g == NAV_GROUP_FOOTER ? 0 : 1));
    }
    CHECK(!nav_group_has_header(NAV_GROUP_FOOTER) && !nav_group_has_header(NAV_GROUP_COUNT));
    CHECK(nav_row_of_page(NAV_PAGE_COUNT) == n);
    CHECK(nav_page_group(NAV_PAGE_COUNT) == NAV_GROUP_COUNT);

    /* 목적별 그룹 */
    CHECK(nav_page_group(NAV_PAGE_SCAN) == NAV_GROUP_ANALYSIS);
    CHECK(nav_page_group(NAV_PAGE_DRIVES) == NAV_GROUP_ANALYSIS);
    CHECK(nav_page_group(NAV_PAGE_CLEANUP) == NAV_GROUP_CLEANUP);
    CHECK(nav_page_group(NAV_PAGE_CLEANER) == NAV_GROUP_CLEANUP);
    CHECK(nav_page_group(NAV_PAGE_CACHE) == NAV_GROUP_CACHE);
    CHECK(nav_page_group(NAV_PAGE_QUARANTINE) == NAV_GROUP_QUARANTINE);
    CHECK(nav_page_group(NAV_PAGE_MONITOR) == NAV_GROUP_SYSTEM);
    CHECK(nav_page_group(NAV_PAGE_BOOSTER) == NAV_GROUP_SYSTEM);
    CHECK(nav_page_group(NAV_PAGE_SETTINGS) == NAV_GROUP_FOOTER);
    CHECK(nav_row_of_page(NAV_PAGE_SETTINGS) == n - 1);
}

/* 문구: 네 언어 모두 비어 있지 않고, 페이지 이름끼리·그룹 이름끼리 겹치지 않는다. */
static void test_text(void)
{
    int lang, a, b;

    for (lang = 0; lang < UI_LANG_COUNT; lang++) {
        for (a = 0; a < NAV_PAGE_COUNT; a++) {
            const wchar_t *ta = ui_text_in((UiLang)lang, nav_page_text((NavPage)a));
            CHECK(ta[0] != L'\0');
            for (b = a + 1; b < NAV_PAGE_COUNT; b++) {
                CHECK(wcscmp(ta, ui_text_in((UiLang)lang, nav_page_text((NavPage)b))) != 0);
            }
        }
        for (a = 0; a < NAV_GROUP_FOOTER; a++) {
            const wchar_t *ta = ui_text_in((UiLang)lang, nav_group_text((NavGroup)a));
            CHECK(ta[0] != L'\0');
            for (b = a + 1; b < NAV_GROUP_FOOTER; b++) {
                CHECK(wcscmp(ta, ui_text_in((UiLang)lang, nav_group_text((NavGroup)b))) != 0);
            }
        }
    }
    CHECK(nav_page_text(NAV_PAGE_COUNT) == UI_T_COUNT);
    CHECK(nav_group_text(NAV_GROUP_COUNT) == UI_T_COUNT);
    CHECK(nav_group_text(NAV_GROUP_FOOTER) == UI_T_COUNT); /* 머리글 없음 */
    CHECK(wcscmp(nav_page_name(NAV_PAGE_SETTINGS), L"settings") == 0);
    CHECK(wcscmp(ui_text_in(UI_LANG_EN, nav_page_text(NAV_PAGE_SETTINGS)), L"Settings") == 0);
    CHECK(wcscmp(nav_page_name(NAV_PAGE_CACHE), L"cache") == 0);
    CHECK(wcscmp(nav_page_name(NAV_PAGE_COUNT), L"unknown") == 0);
    CHECK(wcscmp(ui_text_in(UI_LANG_KO, nav_page_text(NAV_PAGE_CACHE)), L"개발 캐시") == 0);
    CHECK(wcscmp(ui_text_in(UI_LANG_KO, nav_group_text(NAV_GROUP_QUARANTINE)), L"격리·복원") == 0);
}

/* 키 이동: 머리글을 건너뛰고 끝에서 멈춘다. */
static void test_keys(void)
{
    NavPage p;
    int steps;

    CHECK(nav_key_move(NAV_PAGE_SCAN, NAV_KEY_UP) == NAV_PAGE_SCAN);
    CHECK(nav_key_move(NAV_PAGE_SCAN, NAV_KEY_DOWN) == NAV_PAGE_DRIVES);
    CHECK(nav_key_move(NAV_PAGE_DRIVES, NAV_KEY_DOWN) == NAV_PAGE_CLEANUP); /* 머리글 건너뜀 */
    CHECK(nav_key_move(NAV_PAGE_CLEANUP, NAV_KEY_UP) == NAV_PAGE_DRIVES);
    CHECK(nav_key_move(NAV_PAGE_CACHE, NAV_KEY_DOWN) == NAV_PAGE_QUARANTINE);
    CHECK(nav_key_move(NAV_PAGE_BOOSTER, NAV_KEY_DOWN) == NAV_PAGE_SETTINGS); /* footer 로 */
    CHECK(nav_key_move(NAV_PAGE_SETTINGS, NAV_KEY_DOWN) == NAV_PAGE_SETTINGS);
    CHECK(nav_key_move(NAV_PAGE_SETTINGS, NAV_KEY_UP) == NAV_PAGE_BOOSTER);
    CHECK(nav_key_move(NAV_PAGE_CACHE, NAV_KEY_HOME) == NAV_PAGE_SCAN);
    CHECK(nav_key_move(NAV_PAGE_CACHE, NAV_KEY_END) == NAV_PAGE_SETTINGS);
    CHECK(nav_key_move(NAV_PAGE_COUNT, NAV_KEY_DOWN) == NAV_PAGE_SCAN);

    /* 아래로 끝까지 가면 모든 페이지를 한 번씩 지난다 */
    p = NAV_PAGE_SCAN;
    for (steps = 0; steps < 2 * NAV_PAGE_COUNT; steps++) {
        NavPage next = nav_key_move(p, NAV_KEY_DOWN);
        if (next == p) {
            break;
        }
        CHECK(nav_row_of_page(next) > nav_row_of_page(p));
        p = next;
    }
    CHECK(steps == NAV_PAGE_COUNT - 1 && p == NAV_PAGE_SETTINGS);
}

/* 배치와 마우스 위치 판정. */
static void test_layout(void)
{
    NavMetrics m = {10, 20, 30, 8, 0};
    NavPage hit = NAV_PAGE_COUNT;
    size_t i, n = nav_row_count();
    int y0, y1, prev = 10, p;

    /* 첫 머리글 10..30, 폴더 스캔 30..60, 드라이브 60..90, 간격 8, 정리 머리글 98..118 */
    CHECK(nav_row_span(&m, 0, &y0, &y1) && y0 == 10 && y1 == 30);
    CHECK(nav_row_span(&m, 1, &y0, &y1) && y0 == 30 && y1 == 60);
    CHECK(nav_row_span(&m, 3, &y0, &y1) && y0 == 98 && y1 == 118);
    CHECK(!nav_row_span(&m, n, &y0, &y1));
    CHECK(!nav_row_span(NULL, 0, &y0, &y1));
    for (i = 0; i < n; i++) {
        CHECK(nav_row_span(&m, i, &y0, &y1) && y0 >= prev && y1 > y0);
        prev = y1;
    }
    CHECK(nav_total_height(&m) == prev);
    /* 머리글은 footer 를 뺀 그룹 수, 간격은 첫 그룹을 뺀 그룹 수 */
    CHECK(nav_total_height(&m) == 10 + (NAV_GROUP_COUNT - 1) * 20 + NAV_PAGE_COUNT * 30 +
                                      (NAV_GROUP_COUNT - 1) * 8);

    CHECK(nav_hit_test(&m, 30, &hit) && hit == NAV_PAGE_SCAN);
    CHECK(nav_hit_test(&m, 59, &hit) && hit == NAV_PAGE_SCAN);
    CHECK(nav_hit_test(&m, 60, &hit) && hit == NAV_PAGE_DRIVES);
    CHECK(!nav_hit_test(&m, 5, &hit));    /* 위 여백 */
    CHECK(!nav_hit_test(&m, 15, &hit));   /* 머리글 */
    CHECK(!nav_hit_test(&m, 94, &hit));   /* 그룹 간격 */
    CHECK(!nav_hit_test(&m, 100, &hit));  /* 둘째 머리글 */
    CHECK(!nav_hit_test(&m, prev, &hit)); /* 아래 끝 */
    CHECK(!nav_hit_test(NULL, 30, &hit));
    /* 모든 페이지 항목의 가운데를 누르면 그 페이지 */
    for (p = 0; p < NAV_PAGE_COUNT; p++) {
        CHECK(nav_row_span(&m, nav_row_of_page((NavPage)p), &y0, &y1));
        CHECK(nav_hit_test(&m, (y0 + y1) / 2, &hit) && hit == (NavPage)p);
    }
}

/* footer(설정)는 bottom 을 주면 아래 끝에 붙고, 자리가 모자라면 원래 자리에 남는다. */
static void test_footer(void)
{
    NavMetrics m = {10, 20, 30, 8, 1000};
    NavMetrics natural = {10, 20, 30, 8, 0};
    size_t settings = nav_row_of_page(NAV_PAGE_SETTINGS);
    NavPage hit = NAV_PAGE_COUNT;
    int y0, y1, n0, n1, b0, b1;

    CHECK(nav_row_span(&natural, settings, &n0, &n1) && n1 == nav_total_height(&natural));
    CHECK(nav_row_span(&m, settings, &y0, &y1) && y0 == 970 && y1 == 1000);
    CHECK(nav_hit_test(&m, 985, &hit) && hit == NAV_PAGE_SETTINGS);
    CHECK(!nav_hit_test(&m, (n0 + n1) / 2, &hit)); /* 원래 자리는 이제 빈 곳 */
    /* footer 위의 행은 그대로 */
    CHECK(nav_row_span(&m, nav_row_of_page(NAV_PAGE_BOOSTER), &b0, &b1));
    CHECK(nav_row_span(&natural, nav_row_of_page(NAV_PAGE_BOOSTER), &y0, &y1));
    CHECK(b0 == y0 && b1 == y1);
    /* 최소 높이는 bottom 과 상관없다 */
    CHECK(nav_total_height(&m) == nav_total_height(&natural));
    /* 창이 낮아 bottom 이 원래 자리보다 위면 겹치지 않게 원래 자리 */
    m.bottom = 100;
    CHECK(nav_row_span(&m, settings, &y0, &y1) && y0 == n0 && y1 == n1);
}

int main(void)
{
    test_table();
    test_text();
    test_keys();
    test_layout();
    test_footer();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("nav_model: all checks passed\n");
    return 0;
}
