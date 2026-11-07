#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleanup_rules.h"
#include "gui_theme.h"
#include "gui_treemap.h"
#include "size_format.h"
#include "treemap.h"

#define TREEMAP_CLASS L"ByteSieveTreemap"
#define NO_BLOCK ((size_t)-1)

/* 우클릭 메뉴 */
enum {
    IDM_ZOOM = 1,
    IDM_UP,
    IDM_OPEN,
    IDM_COPY,
    IDM_COLOR_BASE /* + TreemapColorMode */
};

typedef enum { BLOCK_DIR, BLOCK_FILES, BLOCK_OTHER } BlockKind;

/*
 * 화면의 사각형 하나. 1단계 블록을 모두 넣은 뒤 그 안의 2단계 블록을 덧붙이므로, 뒤에 있는
 * 블록이 안쪽이다(그리기·찾기 순서).
 */
typedef struct {
    TreemapRect area; /* 배치 결과(실수) */
    RECT rc;          /* 화면 좌표(반올림) */
    BlockKind kind;
    size_t node;      /* DIR: 그 폴더, FILES·OTHER: 그것을 가진 폴더 */
    int level;        /* 1 또는 2 */
    size_t top;       /* 속한 1단계 블록 번호(1단계는 자기 자신) */
    uint64_t bytes;
    uint64_t count;   /* FILES: 파일 수, OTHER: 묶은 폴더 수 */
    int nested;       /* 1단계 블록 안에 2단계를 그렸으면 1(header 에 제목) */
    RECT header;
} Block;

typedef struct {
    uint64_t weight;
    BlockKind kind;
    size_t node;
    uint64_t count;
} Item;

typedef struct {
    HFONT font;
    int dpi;
} ViewParams;

typedef struct {
    HWND hwnd, tip;
    HFONT font, bold;
    int dpi;
    int line_h; /* 글자 줄 높이 */

    const ScanModel *model;
    size_t root, selected;
    wchar_t *root_path; /* 안내 줄용 */
    TreemapColorMode color;

    Block *blocks;
    size_t len, cap;
    RECT strip; /* 위쪽 안내 줄 */
    RECT map;   /* 블록을 그리는 영역 */

    size_t hover; /* 블록 번호 */
    int tracking;
    wchar_t *tip_text;
} TreemapView;

static TreemapView *view_of(HWND hwnd)
{
    return (TreemapView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int tscale(const TreemapView *t, int px)
{
    return MulDiv(px, t->dpi, 96);
}

/* ---- 색 구분 ---- */

const wchar_t *treemap_color_text(TreemapColorMode mode)
{
    switch (mode) {
    case TREEMAP_COLOR_DEPTH:
        return L"깊이";
    case TREEMAP_COLOR_TOP:
        return L"최상위 폴더";
    case TREEMAP_COLOR_CLEANUP:
        return L"정리 후보";
    default:
        return L"";
    }
}

const wchar_t *treemap_color_name(TreemapColorMode mode)
{
    switch (mode) {
    case TREEMAP_COLOR_DEPTH:
        return L"depth";
    case TREEMAP_COLOR_TOP:
        return L"top";
    case TREEMAP_COLOR_CLEANUP:
        return L"cleanup";
    default:
        return L"";
    }
}

/* 최상위 폴더 색(밝은 색이라 검은 글자가 잘 보인다). */
static const COLORREF g_palette[] = {
    RGB(141, 182, 226), RGB(246, 178, 107), RGB(147, 205, 140), RGB(232, 140, 140),
    RGB(189, 160, 220), RGB(222, 196, 140), RGB(240, 170, 210), RGB(205, 215, 110),
    RGB(110, 200, 200), RGB(196, 176, 160),
};
#define PALETTE_LEN (sizeof(g_palette) / sizeof(g_palette[0]))

#define COLOR_LEVEL1 RGB(160, 190, 225)
#define COLOR_LEVEL2 RGB(203, 220, 241)
#define COLOR_FILES RGB(226, 226, 226)
#define COLOR_OTHER RGB(208, 208, 208)
#define COLOR_NOT_CANDIDATE RGB(208, 212, 218)
#define COLOR_TEXT RGB(30, 30, 30)
#define COLOR_SELECTED RGB(20, 20, 20)
#define COLOR_HOVER RGB(0, 90, 180)

static COLORREF blend(COLORREF c, COLORREF to, int pct)
{
    return RGB((GetRValue(c) * (100 - pct) + GetRValue(to) * pct) / 100,
               (GetGValue(c) * (100 - pct) + GetGValue(to) * pct) / 100,
               (GetBValue(c) * (100 - pct) + GetBValue(to) * pct) / 100);
}

static COLORREF category_color(CleanupCategory cat)
{
    switch (cat) {
    case CLEANUP_NODE_MODULES:
        return RGB(244, 160, 80);
    case CLEANUP_BUILD:
        return RGB(180, 140, 215);
    case CLEANUP_CACHE:
        return RGB(100, 190, 175);
    case CLEANUP_TEMP:
        return RGB(230, 200, 80);
    default:
        return COLOR_NOT_CANDIDATE;
    }
}

static const wchar_t *category_label(CleanupCategory cat)
{
    switch (cat) {
    case CLEANUP_NODE_MODULES:
        return L"node_modules";
    case CLEANUP_BUILD:
        return L"빌드 결과물";
    case CLEANUP_CACHE:
        return L"캐시";
    case CLEANUP_TEMP:
        return L"임시";
    default:
        return L"";
    }
}

/*
 * node 또는 그 상위 폴더 중 이름이 정리 후보 규칙에 맞는 가장 가까운 것의 유형. 이름만 보므로
 * 실제 정리 대상인지(표시 파일·보호 경로)는 [프로젝트 정리] 탭이 따로 판단한다.
 */
static CleanupCategory category_of(const TreemapView *t, size_t node)
{
    while (node != SCAN_NODE_NONE && node < t->model->len) {
        const ScanNode *n = &t->model->nodes[node];
        /* 루트의 이름은 전체 경로라 규칙에 맞지 않는다(루트 자신은 판단하지 않음). */
        CleanupCategory cat = n->parent != SCAN_NODE_NONE ? cleanup_classify_dir(n->name)
                                                          : CLEANUP_NONE;
        if (cat != CLEANUP_NONE) {
            return cat;
        }
        node = n->parent;
    }
    return CLEANUP_NONE;
}

static COLORREF block_color(const TreemapView *t, const Block *b)
{
    COLORREF base;

    switch (t->color) {
    case TREEMAP_COLOR_TOP:
        base = g_palette[b->top % PALETTE_LEN];
        break;
    case TREEMAP_COLOR_CLEANUP:
        base = category_color(category_of(t, b->node));
        break;
    default:
        if (b->kind == BLOCK_FILES) {
            return COLOR_FILES;
        }
        if (b->kind == BLOCK_OTHER) {
            return COLOR_OTHER;
        }
        return b->level == 1 ? COLOR_LEVEL1 : COLOR_LEVEL2;
    }
    if (b->kind == BLOCK_FILES) {
        return blend(base, RGB(255, 255, 255), 60);
    }
    if (b->kind == BLOCK_OTHER) {
        return blend(base, RGB(160, 160, 160), 45);
    }
    return b->level == 1 ? base : blend(base, RGB(255, 255, 255), 35);
}

/* ---- 배치 ---- */

static RECT to_rect(TreemapRect a)
{
    RECT r;

    r.left = (LONG)floor(a.x + 0.5);
    r.top = (LONG)floor(a.y + 0.5);
    r.right = (LONG)floor(a.x + a.w + 0.5);
    r.bottom = (LONG)floor(a.y + a.h + 0.5);
    return r;
}

static int push_block(TreemapView *t, const Block *b)
{
    if (t->len == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 64;
        Block *grown = realloc(t->blocks, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        t->blocks = grown;
        t->cap = cap;
    }
    t->blocks[t->len++] = *b;
    return 0;
}

static int by_weight_desc(const void *a, const void *b)
{
    const Item *x = a, *y = b;

    if (x->weight != y->weight) {
        return x->weight > y->weight ? -1 : 1;
    }
    if (x->kind != y->kind) {
        return x->kind < y->kind ? -1 : 1;
    }
    return x->node < y->node ? -1 : x->node > y->node ? 1 : 0;
}

/*
 * parent 폴더의 내용(하위 폴더, 묶음, 바로 아래 파일)을 bounds 에 배치해 블록으로 덧붙인다.
 * top 은 2단계일 때 속한 1단계 블록 번호. 실패(메모리 부족) 시 -1.
 */
static int layout_level(TreemapView *t, size_t parent, TreemapRect bounds, int level, size_t top)
{
    const ScanNode *pn = &t->model->nodes[parent];
    size_t n = scan_model_children(t->model, parent, SCAN_ORDER_SIZE_DESC, NULL, 0);
    size_t *kids = n > 0 ? malloc(n * sizeof(*kids)) : NULL;
    size_t keep = n < TREEMAP_MAX_ITEMS ? n : TREEMAP_MAX_ITEMS;
    Item *items = malloc((keep + 2) * sizeof(*items));
    uint64_t *weights = malloc((keep + 2) * sizeof(*weights));
    TreemapRect *rects = malloc((keep + 2) * sizeof(*rects));
    uint64_t other_bytes = 0, other_count = 0;
    size_t count = 0, i;
    int rc = 0;

    if ((n > 0 && kids == NULL) || items == NULL || weights == NULL || rects == NULL) {
        rc = -1;
        goto done;
    }
    if (n > 0) {
        scan_model_children(t->model, parent, SCAN_ORDER_SIZE_DESC, kids, n);
    }
    for (i = 0; i < n; i++) {
        uint64_t bytes = t->model->nodes[kids[i]].total_bytes;
        if (bytes == 0) {
            continue;
        }
        if (count < keep) {
            items[count].weight = bytes;
            items[count].kind = BLOCK_DIR;
            items[count].node = kids[i];
            items[count++].count = 0;
        } else {
            other_bytes += bytes;
            other_count++;
        }
    }
    if (other_count > 0) {
        items[count].weight = other_bytes;
        items[count].kind = BLOCK_OTHER;
        items[count].node = parent;
        items[count++].count = other_count;
    }
    if (pn->own_bytes > 0) {
        items[count].weight = pn->own_bytes;
        items[count].kind = BLOCK_FILES;
        items[count].node = parent;
        items[count++].count = pn->own_files;
    }
    qsort(items, count, sizeof(*items), by_weight_desc);
    for (i = 0; i < count; i++) {
        weights[i] = items[i].weight;
    }
    treemap_layout(weights, count, bounds, rects);
    for (i = 0; i < count; i++) {
        Block b;
        memset(&b, 0, sizeof(b));
        b.area = rects[i];
        b.rc = to_rect(rects[i]);
        b.kind = items[i].kind;
        b.node = items[i].node;
        b.level = level;
        b.top = level == 1 ? t->len : top;
        b.bytes = items[i].weight;
        b.count = items[i].count;
        if (push_block(t, &b) != 0) {
            rc = -1;
            break;
        }
    }
done:
    free(kids);
    free(items);
    free(weights);
    free(rects);
    return rc;
}

static void hide_tip(TreemapView *t)
{
    TOOLINFOW ti;

    if (t->tip == NULL) {
        return;
    }
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = t->hwnd;
    ti.uId = 1;
    SendMessageW(t->tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

/* 창 크기·기준 폴더가 바뀌면 블록을 다시 배치한다. */
static void relayout(TreemapView *t)
{
    RECT rc;
    TreemapRect bounds;
    size_t n1, i;
    int min_w = tscale(t, 56), min_h = t->line_h * 2 + tscale(t, 12);
    int header_h = t->line_h + tscale(t, 4), pad = tscale(t, 3);

    t->len = 0;
    t->hover = NO_BLOCK;
    hide_tip(t);
    GetClientRect(t->hwnd, &rc);
    t->strip = rc;
    t->strip.bottom = rc.top + t->line_h + tscale(t, 8);
    t->map = rc;
    t->map.top = t->strip.bottom + tscale(t, 2);
    if (t->map.bottom < t->map.top) {
        t->map.bottom = t->map.top;
    }
    if (t->model == NULL || t->root >= t->model->len) {
        return;
    }
    bounds.x = t->map.left;
    bounds.y = t->map.top;
    bounds.w = t->map.right - t->map.left;
    bounds.h = t->map.bottom - t->map.top;
    if (layout_level(t, t->root, bounds, 1, 0) != 0) {
        return;
    }
    /* 충분히 큰 1단계 폴더 블록 안에 제목 줄을 두고 그 아래에 2단계를 배치한다. */
    n1 = t->len;
    for (i = 0; i < n1; i++) {
        Block b = t->blocks[i];
        RECT inner;
        TreemapRect area;
        if (b.kind != BLOCK_DIR || t->model->nodes[b.node].child_count == 0 ||
            b.rc.right - b.rc.left < min_w || b.rc.bottom - b.rc.top < min_h) {
            continue;
        }
        inner.left = b.rc.left + pad;
        inner.right = b.rc.right - pad;
        inner.top = b.rc.top + header_h;
        inner.bottom = b.rc.bottom - pad;
        if (inner.right <= inner.left || inner.bottom <= inner.top) {
            continue;
        }
        t->blocks[i].nested = 1;
        t->blocks[i].header = b.rc;
        t->blocks[i].header.bottom = inner.top;
        area.x = inner.left;
        area.y = inner.top;
        area.w = inner.right - inner.left;
        area.h = inner.bottom - inner.top;
        if (layout_level(t, b.node, area, 2, i) != 0) {
            break;
        }
    }
}

static void set_root_path(TreemapView *t)
{
    free(t->root_path);
    t->root_path = t->model != NULL && t->root < t->model->len
                       ? scan_model_path(t->model, t->root)
                       : NULL;
}

/* 점이 있는 가장 안쪽 블록(없으면 NO_BLOCK). */
static size_t hit_block(const TreemapView *t, int x, int y)
{
    POINT pt;
    size_t i;

    pt.x = x;
    pt.y = y;
    for (i = t->len; i-- > 0;) {
        const RECT *r = &t->blocks[i].rc;
        if (r->right > r->left && r->bottom > r->top && PtInRect(r, pt)) {
            return i;
        }
    }
    return NO_BLOCK;
}

/* 고른 폴더를 보여 주는 블록: 그 폴더나 가장 가까운 상위 폴더의 폴더 블록(가장 안쪽). */
static size_t selected_block(const TreemapView *t)
{
    size_t node = t->selected, i;

    while (node != SCAN_NODE_NONE && node < t->model->len && node != t->root) {
        for (i = t->len; i-- > 0;) {
            if (t->blocks[i].kind == BLOCK_DIR && t->blocks[i].node == node) {
                return i;
            }
        }
        node = t->model->nodes[node].parent;
    }
    return NO_BLOCK;
}

/* ---- 그리기 ---- */

static void fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);

    FillRect(dc, r, b);
    DeleteObject(b);
}

/* r 안쪽으로 두께 width 인 테두리. */
static void frame(HDC dc, RECT r, COLORREF c, int width)
{
    HBRUSH b = CreateSolidBrush(c);
    int i;

    for (i = 0; i < width && r.right - r.left > 2 && r.bottom - r.top > 2; i++) {
        FrameRect(dc, &r, b);
        InflateRect(&r, -1, -1);
    }
    DeleteObject(b);
}

static void block_name(const TreemapView *t, const Block *b, wchar_t *buf, size_t len)
{
    switch (b->kind) {
    case BLOCK_FILES:
        swprintf(buf, len, L"파일 %llu개", (unsigned long long)b->count);
        break;
    case BLOCK_OTHER:
        swprintf(buf, len, L"그 밖의 폴더 %llu개", (unsigned long long)b->count);
        break;
    default:
        swprintf(buf, len, L"%ls", t->model->nodes[b->node].name);
        break;
    }
}

static void draw_block(const TreemapView *t, HDC dc, const Block *b)
{
    COLORREF c = block_color(t, b);
    wchar_t name[MAX_PATH + 32], size[32], text[MAX_PATH + 64];
    RECT r = b->rc, tr;
    int w = r.right - r.left, h = r.bottom - r.top, pad = tscale(t, 3);

    if (w <= 0 || h <= 0) {
        return;
    }
    fill(dc, &r, c);
    frame(dc, r, blend(c, RGB(0, 0, 0), 35), 1);
    if (w < tscale(t, 28) || h < t->line_h + 2) {
        return;
    }
    block_name(t, b, name, MAX_PATH + 32);
    format_size(b->bytes, size, 32);
    SetTextColor(dc, COLOR_TEXT);
    if (b->nested) {
        /* 제목 줄: 이름  용량 */
        swprintf(text, MAX_PATH + 64, L"%ls  %ls", name, size);
        tr = b->header;
        InflateRect(&tr, -pad, 0);
        SelectObject(dc, t->bold);
        DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, t->font);
        return;
    }
    tr = r;
    InflateRect(&tr, -pad, -pad / 2);
    SelectObject(dc, b->level == 1 ? t->bold : t->font);
    DrawTextW(dc, name, -1, &tr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, t->font);
    if (h >= t->line_h * 2 + pad) {
        tr.top += t->line_h;
        DrawTextW(dc, size, -1, &tr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

/* 안내 줄: 기준 폴더 경로·용량과 오른쪽에 조작 안내(정리 후보 색이면 범례). */
static void draw_strip(const TreemapView *t, HDC dc)
{
    static const CleanupCategory legend[] = {CLEANUP_NODE_MODULES, CLEANUP_BUILD, CLEANUP_CACHE,
                                             CLEANUP_TEMP};
    wchar_t text[256], size[32];
    RECT r = t->strip, tr;
    int right = r.right - tscale(t, 6), sw = t->line_h - tscale(t, 4);
    size_t i;

    fill(dc, &r, THEME_COLOR_FACE);
    SetTextColor(dc, THEME_COLOR_TEXT);
    if (t->color == TREEMAP_COLOR_CLEANUP) {
        /* 범례는 오른쪽부터 채운다. */
        for (i = sizeof(legend) / sizeof(legend[0]); i-- > 0;) {
            SIZE ext;
            RECT box;
            const wchar_t *label = category_label(legend[i]);
            GetTextExtentPoint32W(dc, label, (int)wcslen(label), &ext);
            tr = r;
            tr.right = right;
            tr.left = right - ext.cx;
            DrawTextW(dc, label, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            box.right = tr.left - tscale(t, 4);
            box.left = box.right - sw;
            box.top = r.top + (r.bottom - r.top - sw) / 2;
            box.bottom = box.top + sw;
            fill(dc, &box, category_color(legend[i]));
            frame(dc, box, RGB(90, 90, 90), 1);
            right = box.left - tscale(t, 12);
        }
    } else {
        const wchar_t *hint = L"더블클릭: 확대 · Backspace: 상위 폴더 · 우클릭: 메뉴";
        SIZE ext;
        GetTextExtentPoint32W(dc, hint, (int)wcslen(hint), &ext);
        tr = r;
        tr.right = right;
        tr.left = right - ext.cx;
        if (tr.left > r.left + tscale(t, 200)) {
            DrawTextW(dc, hint, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            right = tr.left - tscale(t, 12);
        }
    }
    if (t->model == NULL || t->root >= t->model->len) {
        return;
    }
    format_size(t->model->nodes[t->root].total_bytes, size, 32);
    swprintf(text, 256, L"  ·  %ls  ·  하위 폴더 %llu개", size,
             (unsigned long long)t->model->nodes[t->root].child_count);
    {
        SIZE ext;
        RECT pr = r;
        LONG limit;
        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &ext);
        pr.left = r.left + tscale(t, 6);
        pr.right = limit = right - ext.cx;
        if (pr.right > pr.left && t->root_path != NULL) {
            DrawTextW(dc, t->root_path, -1, &pr,
                      DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            /* 경로가 실제로 차지한 폭 뒤에 용량을 붙인다(줄였으면 한도까지). */
            DrawTextW(dc, t->root_path, -1, &pr,
                      DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX | DT_CALCRECT);
            if (pr.right > limit) {
                pr.right = limit;
            }
            tr = r;
            tr.left = pr.right;
            tr.right = right;
            DrawTextW(dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }
}

static void paint(TreemapView *t, HDC dc, const RECT *client)
{
    size_t i, sel;
    int old_mode = SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(dc, t->font);

    fill(dc, client, THEME_COLOR_SURFACE);
    draw_strip(t, dc);
    if (t->model == NULL || t->model->len == 0 || t->len == 0) {
        RECT r = t->map;
        SetTextColor(dc, THEME_COLOR_TEXT_2ND);
        DrawTextW(dc,
                  t->model == NULL || t->model->len == 0
                      ? L"스캔하면 폴더 용량을 사각형 크기로 보여 줍니다."
                      : L"이 폴더에는 용량이 있는 하위 항목이 없습니다.",
                  -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        for (i = 0; i < t->len; i++) {
            draw_block(t, dc, &t->blocks[i]);
        }
        if (t->hover != NO_BLOCK && t->hover < t->len) {
            frame(dc, t->blocks[t->hover].rc, COLOR_HOVER, 2);
        }
        sel = selected_block(t);
        if (sel != NO_BLOCK) {
            RECT r = t->blocks[sel].rc;
            frame(dc, r, COLOR_SELECTED, 3);
            InflateRect(&r, -3, -3);
            frame(dc, r, RGB(255, 255, 255), 1);
        }
    }
    SelectObject(dc, old_font);
    SetBkMode(dc, old_mode);
}

static void on_paint(TreemapView *t)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(t->hwnd, &ps), mem;
    HBITMAP bmp, old;

    GetClientRect(t->hwnd, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right - rc.left > 0 ? rc.right - rc.left : 1,
                                 rc.bottom - rc.top > 0 ? rc.bottom - rc.top : 1);
    if (mem != NULL && bmp != NULL) {
        old = SelectObject(mem, bmp);
        paint(t, mem, &rc);
        BitBlt(dc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    } else {
        paint(t, dc, &rc);
    }
    if (bmp != NULL) {
        DeleteObject(bmp);
    }
    if (mem != NULL) {
        DeleteDC(mem);
    }
    EndPaint(t->hwnd, &ps);
}

/* ---- 도구 설명 ---- */

static double percent(uint64_t part, uint64_t whole)
{
    return whole > 0 ? (double)part * 100.0 / (double)whole : 0.0;
}

static wchar_t *tip_for(const TreemapView *t, const Block *b)
{
    const ScanNode *n = &t->model->nodes[b->node];
    wchar_t *path = scan_model_path(t->model, b->node), *text;
    wchar_t size[32], ratio[96];
    size_t cap;
    int len = 0;

    if (path == NULL) {
        return NULL;
    }
    cap = wcslen(path) + 512;
    text = malloc(cap * sizeof(*text));
    if (text == NULL) {
        free(path);
        return NULL;
    }
    format_size(b->bytes, size, 32);
    swprintf(ratio, 96, L"%ls · 기준 폴더의 %.1f%% · 전체의 %.1f%%", size,
             percent(b->bytes, t->model->nodes[t->root].total_bytes),
             percent(b->bytes, t->model->nodes[0].total_bytes));
    if (b->kind == BLOCK_FILES) {
        len = swprintf(text, cap, L"%ls 바로 아래 파일 %llu개\n%ls", path,
                       (unsigned long long)b->count, ratio);
    } else if (b->kind == BLOCK_OTHER) {
        len = swprintf(text, cap, L"%ls 안의 작은 폴더 %llu개(묶음)\n%ls", path,
                       (unsigned long long)b->count, ratio);
    } else {
        CleanupCategory cat = category_of(t, b->node);
        len = swprintf(text, cap, L"%ls\n%ls\n하위 폴더 %llu개 · 파일 %llu개", path, ratio,
                       (unsigned long long)n->total_dirs, (unsigned long long)n->total_files);
        if (len > 0 && cat != CLEANUP_NONE) {
            int m = swprintf(text + len, cap - (size_t)len,
                             L"\n정리 후보일 수 있음(이름 기준): %ls", category_label(cat));
            len += m > 0 ? m : 0;
        }
        if (len > 0 && (n->flags & (SCAN_NODE_FAILED | SCAN_NODE_INCOMPLETE |
                                    SCAN_NODE_CANCELLED | SCAN_NODE_DEPTH_LIMIT))) {
            swprintf(text + len, cap - (size_t)len, L"\n일부 하위 내용을 읽지 못해 집계에서 빠짐");
        }
    }
    free(path);
    if (len < 0) {
        free(text);
        return NULL;
    }
    return text;
}

static void show_tip(TreemapView *t, size_t index, POINT screen)
{
    TOOLINFOW ti;

    if (t->tip == NULL) {
        return;
    }
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = t->hwnd;
    ti.uId = 1;
    if (index != NO_BLOCK) {
        free(t->tip_text);
        t->tip_text = tip_for(t, &t->blocks[index]);
        ti.lpszText = t->tip_text != NULL ? t->tip_text : L"";
        SendMessageW(t->tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    }
    SendMessageW(t->tip, TTM_TRACKPOSITION, 0,
                 MAKELPARAM(screen.x + tscale(t, 16), screen.y + tscale(t, 20)));
    SendMessageW(t->tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}

static void on_mouse_move(TreemapView *t, int x, int y)
{
    size_t index = hit_block(t, x, y);
    POINT screen;

    if (!t->tracking) {
        TRACKMOUSEEVENT tme;
        memset(&tme, 0, sizeof(tme));
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = t->hwnd;
        t->tracking = TrackMouseEvent(&tme) != 0;
    }
    if (index != t->hover) {
        t->hover = index;
        InvalidateRect(t->hwnd, NULL, FALSE);
        if (index == NO_BLOCK) {
            hide_tip(t);
            return;
        }
        screen.x = x;
        screen.y = y;
        ClientToScreen(t->hwnd, &screen);
        show_tip(t, index, screen);
    } else if (index != NO_BLOCK) {
        screen.x = x;
        screen.y = y;
        ClientToScreen(t->hwnd, &screen);
        SendMessageW(t->tip, TTM_TRACKPOSITION, 0,
                     MAKELPARAM(screen.x + tscale(t, 16), screen.y + tscale(t, 20)));
    }
}

/* ---- 조작 ---- */

static void notify(TreemapView *t, int code)
{
    SendMessageW(GetParent(t->hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(t->hwnd), code),
                 (LPARAM)t->hwnd);
}

/* node 가 기준 폴더 아래(자신 제외)에 있으면 1. */
static int below_root(const TreemapView *t, size_t node)
{
    while (node != SCAN_NODE_NONE && node < t->model->len) {
        node = t->model->nodes[node].parent;
        if (node == t->root) {
            return 1;
        }
    }
    return 0;
}

static void apply_root(TreemapView *t, size_t node)
{
    t->root = node;
    set_root_path(t);
    relayout(t);
    InvalidateRect(t->hwnd, NULL, FALSE);
}

static void select_node(TreemapView *t, size_t node)
{
    t->selected = node;
    InvalidateRect(t->hwnd, NULL, FALSE);
    notify(t, TMN_SELECT);
}

static void zoom_to(TreemapView *t, size_t node)
{
    if (t->model == NULL || !below_root(t, node)) {
        return;
    }
    t->selected = SCAN_NODE_NONE;
    apply_root(t, node);
    notify(t, TMN_ROOT);
}

static void zoom_up(TreemapView *t)
{
    size_t parent;

    if (t->model == NULL || t->root >= t->model->len) {
        return;
    }
    parent = t->model->nodes[t->root].parent;
    if (parent == SCAN_NODE_NONE) {
        return;
    }
    /* 어디에서 올라왔는지 보이게 이전 기준 폴더를 고른 상태로 둔다. */
    t->selected = t->root;
    apply_root(t, parent);
    notify(t, TMN_ROOT);
}

static void copy_text(HWND owner, const wchar_t *text)
{
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem;

    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem != NULL) {
        void *dst = GlobalLock(mem);
        if (dst != NULL) {
            memcpy(dst, text, bytes);
            GlobalUnlock(mem);
            if (SetClipboardData(CF_UNICODETEXT, mem) == NULL) {
                GlobalFree(mem);
            }
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

static void on_context_menu(TreemapView *t, int sx, int sy)
{
    HMENU menu, colors;
    POINT pt;
    size_t index, target;
    int cmd, m;

    if (t->model == NULL || t->root >= t->model->len) {
        return;
    }
    if (sx == -1 && sy == -1) {
        /* 키보드(Shift+F10·메뉴 키): 고른 블록 가운데에 띄운다. */
        index = selected_block(t);
        if (index != NO_BLOCK) {
            pt.x = (t->blocks[index].rc.left + t->blocks[index].rc.right) / 2;
            pt.y = (t->blocks[index].rc.top + t->blocks[index].rc.bottom) / 2;
        } else {
            pt.x = (t->map.left + t->map.right) / 2;
            pt.y = (t->map.top + t->map.bottom) / 2;
        }
        target = index != NO_BLOCK ? t->blocks[index].node : t->root;
        ClientToScreen(t->hwnd, &pt);
    } else {
        pt.x = sx;
        pt.y = sy;
        ScreenToClient(t->hwnd, &pt);
        index = hit_block(t, pt.x, pt.y);
        target = index != NO_BLOCK ? t->blocks[index].node : t->root;
        if (index != NO_BLOCK && target != t->selected) {
            select_node(t, target);
        }
        pt.x = sx;
        pt.y = sy;
    }
    hide_tip(t);

    menu = CreatePopupMenu();
    colors = CreatePopupMenu();
    if (menu == NULL || colors == NULL) {
        if (menu != NULL) {
            DestroyMenu(menu);
        }
        if (colors != NULL) {
            DestroyMenu(colors);
        }
        return;
    }
    AppendMenuW(menu, MF_STRING | (below_root(t, target) ? 0 : MF_GRAYED), IDM_ZOOM,
                L"이 폴더로 확대(&Z)\tEnter");
    AppendMenuW(menu,
                MF_STRING | (t->model->nodes[t->root].parent != SCAN_NODE_NONE ? 0 : MF_GRAYED),
                IDM_UP, L"상위 폴더로(&U)\tBackspace");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"탐색기에서 열기(&O)");
    AppendMenuW(menu, MF_STRING, IDM_COPY, L"경로 복사(&C)");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (m = 0; m < TREEMAP_COLOR_COUNT; m++) {
        AppendMenuW(colors, MF_STRING | ((int)t->color == m ? MF_CHECKED : 0),
                    IDM_COLOR_BASE + m, treemap_color_text((TreemapColorMode)m));
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)colors, L"색 구분(&L)");

    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, t->hwnd, NULL);
    DestroyMenu(menu); /* 하위 메뉴도 함께 해제된다 */

    if (cmd == IDM_ZOOM) {
        zoom_to(t, target);
    } else if (cmd == IDM_UP) {
        zoom_up(t);
    } else if (cmd == IDM_OPEN || cmd == IDM_COPY) {
        wchar_t *path = scan_model_path(t->model, target);
        if (path != NULL) {
            if (cmd == IDM_OPEN) {
                ShellExecuteW(GetAncestor(t->hwnd, GA_ROOT), L"open", path, NULL, NULL,
                              SW_SHOWNORMAL);
            } else {
                copy_text(t->hwnd, path);
            }
            free(path);
        }
    } else if (cmd >= IDM_COLOR_BASE && cmd < IDM_COLOR_BASE + TREEMAP_COLOR_COUNT) {
        treemap_view_set_color(t->hwnd, (TreemapColorMode)(cmd - IDM_COLOR_BASE));
    }
}

/* ---- 창 ---- */

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TreemapView *t = view_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        /* 상태는 창과 수명을 같이 한다(WM_NCDESTROY 에서 해제). */
        const ViewParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        LOGFONTW lf;
        HDC dc;
        TEXTMETRICW tm;
        t = calloc(1, sizeof(*t));
        if (t == NULL) {
            return FALSE;
        }
        t->hwnd = hwnd;
        t->font = params->font;
        t->dpi = params->dpi;
        t->root = 0;
        t->selected = SCAN_NODE_NONE;
        t->hover = NO_BLOCK;
        t->color = TREEMAP_COLOR_DEPTH;
        if (GetObjectW(t->font, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfWeight = FW_BOLD;
            t->bold = CreateFontIndirectW(&lf);
        }
        if (t->bold == NULL) {
            t->bold = t->font;
        }
        dc = GetDC(hwnd);
        SelectObject(dc, t->font);
        t->line_h = GetTextMetricsW(dc, &tm) ? tm.tmHeight : tscale(t, 16);
        ReleaseDC(hwnd, dc);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)t);
        break;
    }
    case WM_CREATE: {
        TOOLINFOW ti;
        t->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL,
                                 GetModuleHandleW(NULL), NULL);
        if (t->tip != NULL) {
            memset(&ti, 0, sizeof(ti));
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = hwnd;
            ti.uId = 1;
            ti.lpszText = L"";
            SendMessageW(t->tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(t->tip, TTM_SETMAXTIPWIDTH, 0, tscale(t, 560));
            SendMessageW(t->tip, WM_SETFONT, (WPARAM)t->font, FALSE);
        }
        relayout(t);
        return 0;
    }
    case WM_SIZE:
        relayout(t);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        on_paint(t);
        return 0;
    case WM_PRINTCLIENT: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        paint(t, (HDC)wp, &rc);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_MOUSEMOVE:
        on_mouse_move(t, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSELEAVE:
        t->tracking = 0;
        t->hover = NO_BLOCK;
        hide_tip(t);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        size_t index = hit_block(t, (short)LOWORD(lp), (short)HIWORD(lp));
        SetFocus(hwnd);
        if (index != NO_BLOCK) {
            select_node(t, t->blocks[index].node);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        size_t index = hit_block(t, (short)LOWORD(lp), (short)HIWORD(lp));
        if (index != NO_BLOCK) {
            zoom_to(t, t->blocks[index].node);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_BACK) {
            zoom_up(t);
            return 0;
        }
        if (wp == VK_RETURN && t->selected != SCAN_NODE_NONE) {
            zoom_to(t, t->selected);
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        on_context_menu(t, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_NCDESTROY:
        if (t != NULL) {
            if (t->bold != NULL && t->bold != t->font) {
                DeleteObject(t->bold);
            }
            free(t->blocks);
            free(t->root_path);
            free(t->tip_text);
            free(t);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int treemap_view_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = view_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = TREEMAP_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND treemap_view_create(HWND parent, int id, HFONT font, int dpi)
{
    ViewParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(WS_EX_CLIENTEDGE, TREEMAP_CLASS, L"",
                           WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

void treemap_view_set_model(HWND view, const ScanModel *model)
{
    TreemapView *t = view_of(view);

    if (t == NULL) {
        return;
    }
    t->model = model != NULL && model->len > 0 ? model : NULL;
    t->root = 0;
    t->selected = SCAN_NODE_NONE;
    apply_root(t, 0);
}

void treemap_view_set_root(HWND view, size_t node)
{
    TreemapView *t = view_of(view);

    if (t == NULL || t->model == NULL || node >= t->model->len || node == t->root) {
        return;
    }
    apply_root(t, node);
}

size_t treemap_view_root(HWND view)
{
    TreemapView *t = view_of(view);

    return t != NULL && t->model != NULL ? t->root : SCAN_NODE_NONE;
}

void treemap_view_set_selected(HWND view, size_t node)
{
    TreemapView *t = view_of(view);

    if (t == NULL) {
        return;
    }
    t->selected = t->model != NULL && node < t->model->len ? node : SCAN_NODE_NONE;
    InvalidateRect(view, NULL, FALSE);
}

size_t treemap_view_selected(HWND view)
{
    TreemapView *t = view_of(view);

    return t != NULL ? t->selected : SCAN_NODE_NONE;
}

void treemap_view_set_color(HWND view, TreemapColorMode mode)
{
    TreemapView *t = view_of(view);

    if (t == NULL || mode < 0 || mode >= TREEMAP_COLOR_COUNT) {
        return;
    }
    t->color = mode;
    InvalidateRect(view, NULL, FALSE);
}

TreemapColorMode treemap_view_color(HWND view)
{
    TreemapView *t = view_of(view);

    return t != NULL ? t->color : TREEMAP_COLOR_DEPTH;
}

int treemap_view_block_point(HWND view, size_t node, POINT *pt)
{
    TreemapView *t = view_of(view);
    size_t i;

    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < t->len; i++) {
        const Block *b = &t->blocks[i];
        RECT r;
        if (b->level != 1 || b->kind != BLOCK_DIR || b->node != node) {
            continue;
        }
        /* 2단계를 그린 블록은 제목 줄(그 안은 2단계 블록이 덮는다). */
        r = b->nested ? b->header : b->rc;
        if (r.right - r.left < 2 || r.bottom - r.top < 2) {
            return 0;
        }
        pt->x = (r.left + r.right) / 2;
        pt->y = (r.top + r.bottom) / 2;
        return 1;
    }
    return 0;
}

#define VIEW_CHECK(cond)                                                            \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fwprintf(stderr, L"treemap self-test failed: %hs (line %d)\n", #cond,   \
                     __LINE__);                                                     \
            failures++;                                                             \
        }                                                                           \
    } while (0)

static int inside(const RECT *inner, const RECT *outer)
{
    return inner->left >= outer->left && inner->top >= outer->top &&
           inner->right <= outer->right && inner->bottom <= outer->bottom;
}

int treemap_view_verify(HWND view)
{
    TreemapView *t = view_of(view);
    const ScanNode *root;
    uint64_t expect = 0, sum = 0, dirs = 0, merged = 0, positive = 0;
    double map_area, laid = 0.0;
    size_t i, j, n1 = 0, kids, *ids;
    int failures = 0, m;

    VIEW_CHECK(t != NULL && t->model != NULL);
    if (t == NULL || t->model == NULL) {
        return failures;
    }
    VIEW_CHECK(t->root < t->model->len);
    if (t->root >= t->model->len) {
        return failures;
    }
    root = &t->model->nodes[t->root];
    map_area = (double)(t->map.right - t->map.left) * (double)(t->map.bottom - t->map.top);
    VIEW_CHECK(map_area > 0.0);

    /* 기준 폴더의 용량 = 바로 아래 파일 + 하위 폴더 합 */
    kids = scan_model_children(t->model, t->root, SCAN_ORDER_SCAN, NULL, 0);
    ids = kids > 0 ? malloc(kids * sizeof(*ids)) : NULL;
    if (ids != NULL) {
        scan_model_children(t->model, t->root, SCAN_ORDER_SCAN, ids, kids);
        for (i = 0; i < kids; i++) {
            expect += t->model->nodes[ids[i]].total_bytes;
            positive += t->model->nodes[ids[i]].total_bytes > 0;
        }
        free(ids);
    }
    expect += root->own_bytes;

    for (i = 0; i < t->len; i++) {
        const Block *b = &t->blocks[i];
        VIEW_CHECK(inside(&b->rc, &t->map));
        VIEW_CHECK(b->bytes > 0);
        if (b->level != 1) {
            /* 2단계는 제 1단계 블록(제목 줄 아래) 안 */
            VIEW_CHECK(b->top < t->len && t->blocks[b->top].level == 1 &&
                       t->blocks[b->top].nested && inside(&b->rc, &t->blocks[b->top].rc) &&
                       b->rc.top >= t->blocks[b->top].header.bottom);
            continue;
        }
        n1++;
        sum += b->bytes;
        laid += b->area.w * b->area.h;
        dirs += b->kind == BLOCK_DIR;
        merged += b->kind == BLOCK_OTHER ? b->count : 0;
        VIEW_CHECK(b->kind == BLOCK_DIR ? t->model->nodes[b->node].parent == t->root
                                        : b->node == t->root);
        /* 넓이가 용량에 비례(실수 배치 기준) */
        if (expect > 0) {
            double want = map_area * (double)b->bytes / (double)expect;
            VIEW_CHECK(fabs(b->area.w * b->area.h - want) <= 1e-6 * (1.0 + want));
        }
        /* 1단계 블록끼리 겹치지 않음(화면 좌표) */
        for (j = 0; j < i; j++) {
            RECT both;
            if (t->blocks[j].level == 1) {
                VIEW_CHECK(!IntersectRect(&both, &b->rc, &t->blocks[j].rc));
            }
        }
    }
    VIEW_CHECK(sum == expect);
    VIEW_CHECK(dirs + merged == positive);
    VIEW_CHECK(n1 == 0 || fabs(laid - map_area) <= 1e-6 * map_area);

    /* 블록을 클릭하면 그 폴더가 골라지는 점이 실제로 그 블록을 가리킨다 */
    for (i = 0; i < t->len; i++) {
        POINT pt;
        if (t->blocks[i].level == 1 && t->blocks[i].kind == BLOCK_DIR &&
            treemap_view_block_point(view, t->blocks[i].node, &pt)) {
            size_t hit = hit_block(t, pt.x, pt.y);
            VIEW_CHECK(hit != NO_BLOCK && t->blocks[hit].node == t->blocks[i].node);
        }
    }

    /* 색 구분: 정리 후보 색은 이름 규칙과 맞고, 기준마다 그리기가 끝까지 된다 */
    for (i = 0; i < t->len; i++) {
        const Block *b = &t->blocks[i];
        TreemapColorMode saved = t->color;
        t->color = TREEMAP_COLOR_CLEANUP;
        if (b->kind == BLOCK_DIR && b->level == 1) {
            VIEW_CHECK((block_color(t, b) != COLOR_NOT_CANDIDATE) ==
                       (category_of(t, b->node) != CLEANUP_NONE));
        }
        t->color = saved;
    }
    {
        HDC screen = GetDC(view), mem = CreateCompatibleDC(screen);
        RECT rc;
        HBITMAP bmp;
        GetClientRect(view, &rc);
        bmp = CreateCompatibleBitmap(screen, rc.right > 0 ? rc.right : 1,
                                     rc.bottom > 0 ? rc.bottom : 1);
        VIEW_CHECK(mem != NULL && bmp != NULL);
        if (mem != NULL && bmp != NULL) {
            HGDIOBJ old = SelectObject(mem, bmp);
            TreemapColorMode saved = t->color;
            for (m = 0; m < TREEMAP_COLOR_COUNT; m++) {
                t->color = (TreemapColorMode)m;
                VIEW_CHECK(treemap_color_name(t->color)[0] != L'\0');
                paint(t, mem, &rc);
            }
            t->color = saved;
            SelectObject(mem, old);
        }
        if (bmp != NULL) {
            DeleteObject(bmp);
        }
        if (mem != NULL) {
            DeleteDC(mem);
        }
        ReleaseDC(view, screen);
    }
    return failures;
}
