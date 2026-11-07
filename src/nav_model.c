#include "nav_model.h"

/* 행 표: 그룹 머리글 다음에 그 그룹의 페이지. 순서가 곧 화면 순서다. */
static const NavRow k_rows[] = {
    {NAV_ROW_HEADER, NAV_GROUP_ANALYSIS, NAV_PAGE_COUNT},
    {NAV_ROW_PAGE, NAV_GROUP_ANALYSIS, NAV_PAGE_SCAN},
    {NAV_ROW_PAGE, NAV_GROUP_ANALYSIS, NAV_PAGE_DRIVES},
    {NAV_ROW_HEADER, NAV_GROUP_CLEANUP, NAV_PAGE_COUNT},
    {NAV_ROW_PAGE, NAV_GROUP_CLEANUP, NAV_PAGE_CLEANUP},
    {NAV_ROW_PAGE, NAV_GROUP_CLEANUP, NAV_PAGE_CLEANER},
    {NAV_ROW_HEADER, NAV_GROUP_CACHE, NAV_PAGE_COUNT},
    {NAV_ROW_PAGE, NAV_GROUP_CACHE, NAV_PAGE_CACHE},
    {NAV_ROW_HEADER, NAV_GROUP_QUARANTINE, NAV_PAGE_COUNT},
    {NAV_ROW_PAGE, NAV_GROUP_QUARANTINE, NAV_PAGE_QUARANTINE},
    {NAV_ROW_HEADER, NAV_GROUP_SYSTEM, NAV_PAGE_COUNT},
    {NAV_ROW_PAGE, NAV_GROUP_SYSTEM, NAV_PAGE_MONITOR},
    {NAV_ROW_PAGE, NAV_GROUP_SYSTEM, NAV_PAGE_BOOSTER},
};

#define ROW_COUNT (sizeof(k_rows) / sizeof(k_rows[0]))

static const UiText k_page_text[NAV_PAGE_COUNT] = {
    UI_T_NAV_SCAN,  UI_T_NAV_DRIVES,     UI_T_NAV_CLEANUP, UI_T_NAV_CLEANER,
    UI_T_NAV_CACHE, UI_T_NAV_QUARANTINE, UI_T_NAV_MONITOR, UI_T_NAV_BOOSTER};

static const UiText k_group_text[NAV_GROUP_COUNT] = {
    UI_T_GROUP_ANALYSIS, UI_T_GROUP_CLEANUP, UI_T_GROUP_CACHE, UI_T_GROUP_QUARANTINE,
    UI_T_GROUP_SYSTEM};

static const wchar_t *const k_page_name[NAV_PAGE_COUNT] = {
    L"scan", L"drives", L"cleanup", L"cleaner", L"cache", L"quarantine", L"monitor", L"booster"};

size_t nav_row_count(void)
{
    return ROW_COUNT;
}

const NavRow *nav_row(size_t index)
{
    return index < ROW_COUNT ? &k_rows[index] : NULL;
}

size_t nav_row_of_page(NavPage page)
{
    size_t i;

    for (i = 0; i < ROW_COUNT; i++) {
        if (k_rows[i].kind == NAV_ROW_PAGE && k_rows[i].page == page) {
            return i;
        }
    }
    return ROW_COUNT;
}

NavGroup nav_page_group(NavPage page)
{
    size_t row = nav_row_of_page(page);

    return row < ROW_COUNT ? k_rows[row].group : NAV_GROUP_COUNT;
}

UiText nav_page_text(NavPage page)
{
    return (unsigned)page < (unsigned)NAV_PAGE_COUNT ? k_page_text[page] : UI_T_COUNT;
}

UiText nav_group_text(NavGroup group)
{
    return (unsigned)group < (unsigned)NAV_GROUP_COUNT ? k_group_text[group] : UI_T_COUNT;
}

const wchar_t *nav_page_name(NavPage page)
{
    return (unsigned)page < (unsigned)NAV_PAGE_COUNT ? k_page_name[page] : L"unknown";
}

/* 첫 번째·마지막 페이지 항목 행. */
static size_t first_page_row(void)
{
    size_t i;

    for (i = 0; i < ROW_COUNT && k_rows[i].kind != NAV_ROW_PAGE; i++) {
    }
    return i;
}

static size_t last_page_row(void)
{
    size_t i = ROW_COUNT;

    while (i > 0 && k_rows[i - 1].kind != NAV_ROW_PAGE) {
        i--;
    }
    return i > 0 ? i - 1 : 0;
}

NavPage nav_key_move(NavPage current, NavKey key)
{
    size_t row = nav_row_of_page(current), i;

    if (row >= ROW_COUNT) {
        return k_rows[first_page_row()].page;
    }
    switch (key) {
    case NAV_KEY_HOME:
        return k_rows[first_page_row()].page;
    case NAV_KEY_END:
        return k_rows[last_page_row()].page;
    case NAV_KEY_UP:
        for (i = row; i-- > 0;) {
            if (k_rows[i].kind == NAV_ROW_PAGE) {
                return k_rows[i].page;
            }
        }
        return current;
    case NAV_KEY_DOWN:
        for (i = row + 1; i < ROW_COUNT; i++) {
            if (k_rows[i].kind == NAV_ROW_PAGE) {
                return k_rows[i].page;
            }
        }
        return current;
    default:
        return current;
    }
}

int nav_row_span(const NavMetrics *m, size_t index, int *y0, int *y1)
{
    size_t i;
    int y;

    if (m == NULL || index >= ROW_COUNT) {
        return 0;
    }
    y = m->top;
    for (i = 0; i <= index; i++) {
        int h = k_rows[i].kind == NAV_ROW_HEADER ? m->header_h : m->item_h;
        if (k_rows[i].kind == NAV_ROW_HEADER && i > 0) {
            y += m->group_gap;
        }
        if (i == index) {
            if (y0 != NULL) {
                *y0 = y;
            }
            if (y1 != NULL) {
                *y1 = y + h;
            }
            return 1;
        }
        y += h;
    }
    return 0;
}

int nav_total_height(const NavMetrics *m)
{
    int y1 = m != NULL ? m->top : 0;

    if (m != NULL && ROW_COUNT > 0) {
        nav_row_span(m, ROW_COUNT - 1, NULL, &y1);
    }
    return y1;
}

int nav_hit_test(const NavMetrics *m, int y, NavPage *out)
{
    size_t i;
    int y0, y1;

    for (i = 0; m != NULL && i < ROW_COUNT; i++) {
        if (!nav_row_span(m, i, &y0, &y1)) {
            break;
        }
        if (y >= y0 && y < y1) {
            if (k_rows[i].kind != NAV_ROW_PAGE) {
                return 0;
            }
            if (out != NULL) {
                *out = k_rows[i].page;
            }
            return 1;
        }
    }
    return 0;
}
