#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gui_scan.h"
#include "gui_theme.h"
#include "gui_treemap.h"
#include "path.h"
#include "scan_export.h"
#include "scan_history.h"
#include "scan_job.h"
#include "scan_model.h"
#include "size_format.h"
#include "ui_lang.h"

#define SCAN_PANEL_CLASS L"ByteSieveScanPanel"

#define WM_APP_SCAN_PROGRESS (WM_APP + 1)
#define WM_APP_SCAN_DONE (WM_APP + 2)

/* 진행률 알림 간격과 용량 순위 개수. */
#define PROGRESS_INTERVAL_MS 100
#define RANK_COUNT 100
#define PROGRESS_RANGE 1000

enum {
    IDC_PATH = 100,
    IDC_BROWSE,
    IDC_SCAN,
    IDC_CANCEL,
    IDC_PROGRESS,
    IDC_STATUS,
    IDC_TREE,
    IDC_LIST,
    IDC_VIEW,
    IDC_ANALYSIS,
    IDC_AUTOSAVE,
    IDC_CLEAR_HISTORY,
    IDC_EXPORT_CSV,
    IDC_EXPORT_HTML,
    IDC_PRECISE,
    IDC_TREEMAP
};

typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

typedef struct {
    HWND hwnd; /* 패널 */
    HWND path, browse, scan, cancel, progress, status, tree, list, view, analysis;
    HWND precise; /* [정밀 스캔(하드 링크)] 체크 상자 */
    HWND autosave, clear_history, export_csv, export_html;
    HWND treemap; /* 보기가 트리맵일 때 목록 자리에 보인다 */
    int map_syncing; /* 트리맵 조작을 트리에 반영하는 중(트리 선택으로 기준 폴더를 바꾸지 않음) */
    HFONT font;
    int dpi;
    /* 컨트롤 폭: 언어마다 문구 길이가 달라 글자 폭을 재서 정한다(create_controls). */
    int w_browse, w_scan, w_cancel, w_precise, w_view, w_export, w_autosave, w_clear;

    ScanJob *job;
    ScanModel model;
    int has_model;
    int progress_pos;
    ExportInfo scan_info; /* 마지막 스캔의 완료 시각·걸린 시간(보고서용) */

    /* 폴더별 용량 변화(직전 스캔 대비): history_track 결과 */
    HistoryTrackStatus history;
    int history_cleared;     /* 스캔 뒤 [기록 삭제]로 이 대상의 기록을 지웠다(변화 표시도 지움) */
    unsigned long history_error;
    HistoryDiff diff;        /* has_diff 일 때 */
    int has_diff;
    size_t *change_of_node;  /* 노드 → diff 항목 번호(없으면 SCAN_NODE_NONE) */
    int autosave_on;         /* 스캔 기록 자동 저장(설정 파일에 저장된 값과 같게 유지) */
    HistoryClearResult cleared; /* 마지막 [기록 삭제] 결과 */

    /* 자체 검사(scan_panel_verify 인자) */
    ScanVerifyMode self_test;
    const wchar_t *export_prefix;
    int export_enabled_before_scan;
} ScanPage;

/* 패널은 하나뿐이다(scan_panel_create 참고). */
static ScanPage g_scan;

/* 순위 목록 열. */
/* COL_UNIQUE·COL_DUP 는 정밀 스캔 결과일 때만 보인다(기본 스캔이면 너비 0). */
enum {
    COL_RANK,
    COL_PATH,
    COL_SIZE,
    COL_UNIQUE,
    COL_DUP,
    COL_CHANGE,
    COL_ALLOC,
    COL_FILES,
    COL_SHARE,
    COL_COUNT
};

/* 오른쪽 목록 보기(보기 콤보 상자 순서). 트리맵은 목록 대신 트리맵 창을 보인다. */
enum { VIEW_SIZE, VIEW_ALLOC, VIEW_CHANGE, VIEW_UNIQUE, VIEW_DUP, VIEW_TREEMAP, VIEW_COUNT };
static int scale(int px)
{
    return MulDiv(px, g_scan.dpi, 96);
}

static double share_of(uint64_t part, uint64_t whole)
{
    return whole > 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

/* 트리·순위에 덧붙이는 상태 표시. 없으면 빈 문자열. */
static const wchar_t *flag_label(uint32_t flags)
{
    if (flags & SCAN_NODE_FAILED) {
        return ui_text(UI_T_FLAG_FAILED);
    }
    if (flags & SCAN_NODE_CANCELLED) {
        return ui_text(UI_T_FLAG_CANCELLED);
    }
    if (flags & SCAN_NODE_INCOMPLETE) {
        return ui_text(UI_T_FLAG_INCOMPLETE);
    }
    if (flags & SCAN_NODE_REPARSE) {
        return ui_text(UI_T_FLAG_REPARSE);
    }
    if (flags & SCAN_NODE_DEPTH_LIMIT) {
        return ui_text(UI_T_FLAG_DEPTH_LIMIT);
    }
    return L"";
}
/* ---- 레이아웃 ---- */

/* 경로 칸·진행 막대를 뺀 첫째·둘째 줄의 고정 폭(컨트롤 폭과 간격). */
static int scan_row_fixed(int gap)
{
    return gap + g_scan.w_browse + gap + g_scan.w_scan + gap + g_scan.w_cancel + 2 * gap +
           g_scan.w_precise + gap + g_scan.w_view;
}

static int progress_row_fixed(int gap)
{
    return gap + g_scan.w_export + gap + g_scan.w_export + 2 * gap + g_scan.w_autosave + gap +
           g_scan.w_clear;
}

/* 패널 안 컨트롤이 잘리지 않는 최소 폭: 경로 칸 80, 진행 막대 40 은 남긴다. */
static int min_panel_width(void)
{
    int m = scale(8), gap = scale(6);
    int row1 = scale(80) + scan_row_fixed(gap), row2 = scale(40) + progress_row_fixed(gap);

    if (g_scan.w_scan == 0) {
        return 0; /* 컨트롤을 만들기 전 */
    }
    return 2 * m + (row1 > row2 ? row1 : row2);
}

static void layout(void)
{
    RECT rc;
    int m = scale(8), h = scale(24), gap = scale(6);
    int x, w, y, cx, path_w, bar_w, body_h, tree_w;

    /* 패널의 클라이언트 영역 전체에 놓는다(바깥 여백·페이지 제목은 메인 창이 둔다). */
    GetClientRect(g_scan.hwnd, &rc);
    /* 아래 계산은 안쪽 여백 m 을 두므로, 사각형을 m 만큼 넓혀 여백이 패널 가장자리와 맞게 한다. */
    InflateRect(&rc, m, m);
    x = rc.left;
    w = rc.right - rc.left;
    if (w < 0) {
        w = 0;
    }

    /* [경로] [찾아보기] [스캔] [취소]  [v] 정밀 스캔(하드 링크) [보기 ▼]
     * 단추 폭은 문구에 맞추고(언어마다 다름) 남는 폭은 경로 칸이 쓴다. */
    y = rc.top + m;
    path_w = w - 2 * m - scan_row_fixed(gap);
    if (path_w < scale(80)) {
        path_w = scale(80);
    }
    cx = x + m;
    MoveWindow(g_scan.path, cx, y, path_w, h, TRUE);
    cx += path_w + gap;
    MoveWindow(g_scan.browse, cx, y, g_scan.w_browse, h, TRUE);
    cx += g_scan.w_browse + gap;
    MoveWindow(g_scan.scan, cx, y, g_scan.w_scan, h, TRUE);
    cx += g_scan.w_scan + gap;
    MoveWindow(g_scan.cancel, cx, y, g_scan.w_cancel, h, TRUE);
    cx += g_scan.w_cancel + 2 * gap;
    MoveWindow(g_scan.precise, cx, y, g_scan.w_precise, h, TRUE);
    cx += g_scan.w_precise + gap;
    /* 콤보 상자 높이는 펼친 목록 높이까지 포함한다. */
    MoveWindow(g_scan.view, cx, y, g_scan.w_view, scale(160), TRUE);

    /* 진행 막대 오른쪽에 내보내기와 기록 설정:
     * [CSV 내보내기...] [HTML 보고서...]  [v] 스캔 기록 자동 저장 [기록 삭제...] */
    y += h + gap;
    bar_w = w - 2 * m - progress_row_fixed(gap);
    if (bar_w < scale(40)) {
        bar_w = scale(40);
    }
    MoveWindow(g_scan.progress, x + m, y + (h - scale(16)) / 2, bar_w, scale(16), TRUE);
    cx = x + m + bar_w + gap;
    MoveWindow(g_scan.export_csv, cx, y, g_scan.w_export, h, TRUE);
    cx += g_scan.w_export + gap;
    MoveWindow(g_scan.export_html, cx, y, g_scan.w_export, h, TRUE);
    cx += g_scan.w_export + 2 * gap;
    MoveWindow(g_scan.autosave, cx, y, g_scan.w_autosave, h, TRUE);
    cx += g_scan.w_autosave + gap;
    MoveWindow(g_scan.clear_history, cx, y, g_scan.w_clear, h, TRUE);
    y += h + gap;
    MoveWindow(g_scan.status, x + m, y, w - 2 * m, scale(20), TRUE);
    y += scale(20);
    MoveWindow(g_scan.analysis, x + m, y, w - 2 * m, scale(20), TRUE);
    y += scale(20) + gap;

    body_h = rc.bottom - y - m;
    if (body_h < 0) {
        body_h = 0;
    }
    tree_w = (w - 2 * m - gap) * 2 / 5;
    MoveWindow(g_scan.tree, x + m, y, tree_w, body_h, TRUE);
    MoveWindow(g_scan.list, x + m + tree_w + gap, y, w - 2 * m - gap - tree_w, body_h, TRUE);
    MoveWindow(g_scan.treemap, x + m + tree_w + gap, y, w - 2 * m - gap - tree_w, body_h, TRUE);
}

/* 오른쪽에는 보기에 따라 순위 목록이나 트리맵 중 하나만 보인다. */
static void show_view_windows(void)
{
    int map = SendMessageW(g_scan.view, CB_GETCURSEL, 0, 0) == VIEW_TREEMAP;

    ShowWindow(g_scan.list, !map ? SW_SHOW : SW_HIDE);
    ShowWindow(g_scan.treemap, map ? SW_SHOW : SW_HIDE);
}
/* ---- 결과 표시 ---- */

/* 지금 결과가 정밀 스캔(하드 링크 보정)으로 집계됐는지. */
static int precise_result(void)
{
    return g_scan.has_model && (g_scan.model.options & SCAN_OPT_HARDLINKS) != 0;
}

/* 하드 링크 중복을 뺀 용량(정밀 스캔이 아니면 경로마다 센 용량과 같다). */
static uint64_t unique_bytes(const ScanNode *n)
{
    return n->total_bytes - n->total_dup_bytes;
}

/* 트리 항목 표시 문자열(malloc, 호출자가 free). 정밀 스캔에서 중복이 있으면 중복 제외 용량을 덧붙인다. */
static wchar_t *tree_item_text(size_t index)
{
    const ScanNode *n = scan_model_node(&g_scan.model, index);
    uint64_t whole = g_scan.model.nodes[0].total_bytes;
    size_t len = wcslen(n->name) + 128, used;
    wchar_t size[32], *buf = malloc(len * sizeof(*buf));

    if (buf == NULL) {
        return NULL;
    }
    format_size(n->total_bytes, size, 32);
    if (index == 0) {
        swprintf(buf, len, L"%ls  —  %ls", n->name, size);
    } else {
        swprintf(buf, len, L"%ls  —  %ls (%.1f%%)", n->name, size,
                 share_of(n->total_bytes, whole));
    }
    used = wcslen(buf);
    if (precise_result() && n->total_dup_bytes > 0) {
        format_size(unique_bytes(n), size, 32);
        swprintf(buf + used, len - used, L" · 중복 제외 %ls", size);
        used = wcslen(buf);
    }
    swprintf(buf + used, len - used, L"%ls", flag_label(n->flags));
    return buf;
}

static HTREEITEM tree_insert(HTREEITEM parent, size_t index)
{
    TVINSERTSTRUCTW ins;
    const ScanNode *n = scan_model_node(&g_scan.model, index);
    wchar_t *text = tree_item_text(index);
    HTREEITEM item;

    if (text == NULL) {
        return NULL;
    }
    memset(&ins, 0, sizeof(ins));
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    ins.item.pszText = text;
    ins.item.lParam = (LPARAM)index;
    /* 자식은 펼칠 때 채운다(지연 로딩). */
    ins.item.cChildren = n->child_count > 0 ? 1 : 0;
    item = (HTREEITEM)SendMessageW(g_scan.tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
    free(text);
    return item;
}

/* item 의 자식을 아직 넣지 않았으면 용량 순으로 넣는다. */
static void tree_populate(HTREEITEM item, size_t index)
{
    size_t count, i, *kids;

    if (TreeView_GetChild(g_scan.tree, item) != NULL) {
        return;
    }
    count = scan_model_children(&g_scan.model, index, SCAN_ORDER_SIZE_DESC, NULL, 0);
    if (count == 0) {
        return;
    }
    kids = malloc(count * sizeof(*kids));
    if (kids == NULL) {
        return;
    }
    count = scan_model_children(&g_scan.model, index, SCAN_ORDER_SIZE_DESC, kids, count);
    SendMessageW(g_scan.tree, WM_SETREDRAW, FALSE, 0);
    for (i = 0; i < count; i++) {
        tree_insert(item, kids[i]);
    }
    SendMessageW(g_scan.tree, WM_SETREDRAW, TRUE, 0);
    free(kids);
}

static size_t tree_item_index(HTREEITEM item)
{
    TVITEMW tv;

    memset(&tv, 0, sizeof(tv));
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!TreeView_GetItem(g_scan.tree, &tv)) {
        return SCAN_NODE_NONE;
    }
    return (size_t)tv.lParam;
}

static void list_set(int row, int col, const wchar_t *text)
{
    LVITEMW lv;

    memset(&lv, 0, sizeof(lv));
    lv.iSubItem = col;
    lv.pszText = (wchar_t *)text;
    SendMessageW(g_scan.list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&lv);
}

static int current_view(void)
{
    LRESULT sel = SendMessageW(g_scan.view, CB_GETCURSEL, 0, 0);

    return sel >= 0 && sel < VIEW_COUNT ? (int)sel : VIEW_SIZE;
}

/* 부호 있는 용량("+1.20 GiB", "-300 B"). */

/* 한 줄을 넣는다. node 가 SCAN_NODE_NONE 이면(삭제된 폴더) 용량 열을 비운다. */
static void insert_row(int index, size_t node, const wchar_t *path, const wchar_t *flags,
                       const HistoryChange *change, uint64_t whole)
{
    wchar_t buf[96];
    LVITEMW lv;
    int row;

    swprintf(buf, 96, L"%u", (unsigned)(index + 1));
    memset(&lv, 0, sizeof(lv));
    lv.mask = LVIF_TEXT | LVIF_PARAM;
    lv.iItem = index;
    lv.pszText = buf;
    lv.lParam = (LPARAM)node;
    row = (int)SendMessageW(g_scan.list, LVM_INSERTITEMW, 0, (LPARAM)&lv);
    if (row < 0) {
        return;
    }
    if (path != NULL) {
        size_t plen = wcslen(path) + wcslen(flags) + 1;
        wchar_t *text = malloc(plen * sizeof(*text));
        if (text != NULL) {
            swprintf(text, plen, L"%ls%ls", path, flags);
            list_set(row, COL_PATH, text);
            free(text);
        }
    }
    if (node != SCAN_NODE_NONE) {
        const ScanNode *n = &g_scan.model.nodes[node];
        format_size(n->total_bytes, buf, 96);
        list_set(row, COL_SIZE, buf);
        format_size(n->total_alloc, buf, 96);
        list_set(row, COL_ALLOC, buf);
        swprintf(buf, 96, L"%llu", (unsigned long long)n->total_files);
        list_set(row, COL_FILES, buf);
        swprintf(buf, 96, L"%.1f%%", share_of(n->total_bytes, whole));
        list_set(row, COL_SHARE, buf);
        /* 정밀 스캔: 중복 제외 용량, 하드 링크로 중복 집계된 용량(기본 스캔 대비 차이) */
        if (precise_result()) {
            format_size(unique_bytes(n), buf, 96);
            list_set(row, COL_UNIQUE, buf);
            if (n->total_dup_bytes > 0 || n->total_dup_files > 0) {
                wchar_t size[32];
                format_size(n->total_dup_bytes, size, 32);
                swprintf(buf, 96, L"-%ls (%llu개)", size, (unsigned long long)n->total_dup_files);
                list_set(row, COL_DUP, buf);
            }
        }
    } else {
        list_set(row, COL_SIZE, L"-");
        list_set(row, COL_ALLOC, L"-");
        list_set(row, COL_FILES, L"-");
        list_set(row, COL_SHARE, L"-");
    }
    if (change != NULL) {
        history_change_text(change, buf, 96);
        list_set(row, COL_CHANGE, buf);
    }
}

/* 보기에 맞는 순위 기준. */
static ScanRankKey view_rank_key(int view)
{
    switch (view) {
    case VIEW_ALLOC:
        return SCAN_RANK_TOTAL_ALLOC;
    case VIEW_UNIQUE:
        return SCAN_RANK_TOTAL_UNIQUE;
    case VIEW_DUP:
        return SCAN_RANK_TOTAL_DUP;
    default:
        return SCAN_RANK_TOTAL_BYTES;
    }
}

/*
 * 오른쪽 목록을 보기에 맞게 채운다.
 *   용량·디스크 사용 순위: 상위 RANK_COUNT 개 폴더(변화 열은 직전 스캔 대비, 없으면 빈칸)
 *   변화 순위: 직전 스캔 대비 변화가 큰 폴더(삭제된 폴더 포함)
 *   중복 제외 용량 순위: 하드 링크 중복을 뺀 용량 순(기본 스캔이면 용량 순위와 같다)
 *   하드 링크 중복 순위: 중복 집계된 용량이 있는 폴더만(기본 스캔이면 비어 있다)
 */
static void fill_rank(void)
{
    size_t top[RANK_COUNT];
    size_t count, i;
    uint64_t whole;
    int view = current_view();

    ListView_DeleteAllItems(g_scan.list);
    if (!g_scan.has_model || g_scan.model.len == 0) {
        return;
    }
    whole = g_scan.model.nodes[0].total_bytes;
    SendMessageW(g_scan.list, WM_SETREDRAW, FALSE, 0);
    if (view == VIEW_CHANGE) {
        count = g_scan.has_diff ? (g_scan.diff.len < RANK_COUNT ? g_scan.diff.len : RANK_COUNT) : 0;
        for (i = 0; i < count; i++) {
            const HistoryChange *c = &g_scan.diff.items[i];
            wchar_t *path = history_full_path(g_scan.model.nodes[0].name, c->rel);
            const wchar_t *flags = c->node != SCAN_NODE_NONE
                                       ? flag_label(g_scan.model.nodes[c->node].flags)
                                       : L"";
            insert_row((int)i, c->node, path, flags, c, whole);
            free(path);
        }
    } else {
        count = scan_model_rank(&g_scan.model, view_rank_key(view), top, RANK_COUNT);
        if (view == VIEW_DUP) {
            /* 중복이 없는 폴더는 빼낸다(순위가 내림차순이므로 앞부분만 남는다). */
            while (count > 0 && g_scan.model.nodes[top[count - 1]].total_dup_files == 0) {
                count--;
            }
        }
        for (i = 0; i < count; i++) {
            const HistoryChange *c = NULL;
            wchar_t *path = scan_model_path(&g_scan.model, top[i]);
            if (g_scan.change_of_node != NULL &&
                g_scan.change_of_node[top[i]] != SCAN_NODE_NONE) {
                c = &g_scan.diff.items[g_scan.change_of_node[top[i]]];
            }
            insert_row((int)i, top[i], path, flag_label(g_scan.model.nodes[top[i]].flags), c,
                       whole);
            free(path);
        }
    }
    SendMessageW(g_scan.list, WM_SETREDRAW, TRUE, 0);
}

/* 디스크 사용량 분석과 직전 스캔 대비 변화를 둘째 상태 줄에 쓴다. */
static void update_analysis(void)
{
    const ScanReport *r = &g_scan.model.report;
    wchar_t text[1024], a[32], b[32], c[32];
    size_t used = 0;
    int n;

#define APPEND(...)                                                                          \
    do {                                                                                     \
        n = swprintf(text + used, sizeof(text) / sizeof(text[0]) - used, __VA_ARGS__);       \
        if (n > 0) {                                                                         \
            used += (size_t)n;                                                               \
        }                                                                                    \
    } while (0)

    text[0] = L'\0';
    if (!g_scan.has_model || g_scan.model.status == SCAN_FATAL) {
        SetWindowTextW(g_scan.analysis, L"");
        return;
    }
    format_size(r->alloc_bytes, a, 32);
    history_format_delta((int64_t)(r->alloc_bytes - r->total_bytes), b, 32);
    APPEND(L"디스크 사용 %ls(논리 크기 대비 %ls)", a, b);
    /* 정밀 스캔: 기본 집계(경로마다)와 하드 링크 중복을 뺀 집계의 차이 */
    if (precise_result() && r->hardlinks_unsupported) {
        APPEND(L" · 정밀 스캔: 하드 링크가 없는 파일 시스템이라 보정할 것이 없음");
    } else if (precise_result()) {
        format_size(r->total_bytes - r->hardlink_dup_bytes, a, 32);
        format_size(r->total_bytes, b, 32);
        format_size(r->hardlink_dup_bytes, c, 32);
        APPEND(L" · 정밀 스캔: 중복 제외 %ls(기본 %ls, 하드 링크 중복 %llu개 -%ls)", a, b,
               (unsigned long long)r->hardlink_dup_files, c);
        format_size(r->alloc_bytes - r->hardlink_dup_alloc, a, 32);
        APPEND(L" · 실제 디스크 사용 %ls", a);
        if (r->hardlink_unknown_files > 0) {
            APPEND(L" · 파일 ID 모름 %llu개(경로마다 셈)",
                   (unsigned long long)r->hardlink_unknown_files);
        }
    }
    if (r->slack_bytes > 0) {
        format_size(r->slack_bytes, a, 32);
        APPEND(L" · 클러스터 낭비 +%ls", a);
    }
    if (r->compressed_saved_bytes > 0) {
        format_size(r->compressed_saved_bytes, a, 32);
        APPEND(L" · 압축 -%ls(%llu개)", a, (unsigned long long)r->compressed_files);
    }
    if (r->sparse_saved_bytes > 0) {
        format_size(r->sparse_saved_bytes, a, 32);
        APPEND(L" · 스파스 -%ls(%llu개)", a, (unsigned long long)r->sparse_files);
    }
    if (r->cloud_saved_bytes > 0) {
        format_size(r->cloud_saved_bytes, a, 32);
        APPEND(L" · 클라우드 전용 -%ls(%llu개)", a, (unsigned long long)r->cloud_files);
    }
    if (r->other_saved_bytes > 0) {
        format_size(r->other_saved_bytes, a, 32);
        APPEND(L" · 기타 -%ls(%llu개)", a, (unsigned long long)r->other_saved_files);
    }
    APPEND(L"   |   ");
    if (g_scan.has_diff) {
        FILETIME utc, local;
        SYSTEMTIME st;
        utc.dwLowDateTime = (DWORD)(g_scan.diff.old_time & 0xFFFFFFFFu);
        utc.dwHighDateTime = (DWORD)(g_scan.diff.old_time >> 32);
        if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &st)) {
            memset(&st, 0, sizeof(st));
        }
        history_format_delta((int64_t)(g_scan.diff.new_total - g_scan.diff.old_total), a, 32);
        format_size(g_scan.diff.old_total, b, 32);
        format_size(g_scan.diff.new_total, c, 32);
        APPEND(L"직전 스캔(%02u-%02u %02u:%02u) 대비 %ls(%ls → %ls) · 늘어남 %u · 줄어듦 %u · "
               L"새 폴더 %u · 삭제 %u",
               (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wHour, (unsigned)st.wMinute,
               a, b, c, (unsigned)g_scan.diff.grown, (unsigned)g_scan.diff.shrunk,
               (unsigned)g_scan.diff.added, (unsigned)g_scan.diff.removed);
    }
    if (g_scan.history_cleared) {
        APPEND(L"저장된 스캔 기록을 삭제했습니다(직전 스캔과 비교한 변화 표시도 지움)");
    }
    switch (g_scan.history_cleared ? HISTORY_TRACK_NONE : g_scan.history) {
    case HISTORY_TRACK_BASELINE:
        APPEND(L"첫 스캔: 기준으로 저장했습니다(다음 스캔부터 변화를 비교)");
        break;
    case HISTORY_TRACK_CANCELLED:
        APPEND(L"%ls취소되어 기록하지 않았습니다", g_scan.has_diff ? L" · " : L"");
        break;
    case HISTORY_TRACK_FAILED:
        APPEND(L"%ls변화 기록을 저장하지 못했습니다(오류 %lu)", g_scan.has_diff ? L" · " : L"",
               g_scan.history_error);
        break;
    case HISTORY_TRACK_DISABLED:
        APPEND(L"%ls자동 저장이 꺼져 있어 이번 스캔은 기록하지 않았습니다",
               g_scan.has_diff ? L" · " : L"");
        break;
    default:
        break;
    }
#undef APPEND
    SetWindowTextW(g_scan.analysis, text);
}

/*
 * 직전 스냅숏과 비교하고, 취소되지 않았고 자동 저장이 켜져 있으면 새 스냅숏을 저장한다
 * (CLI --track 과 같은 history_track).
 * 기록 위치: BYTESIEVE_HISTORY_DIR 또는 %LOCALAPPDATA%\ByteSieve\history.
 * 비교했으면 순위 목록의 변화 열을 위해 노드 → 변화 항목 번호 표를 만든다.
 */
static void track_history(void)
{
    HistoryTrackResult r;
    size_t i;

    g_scan.history_cleared = 0;
    history_track(&g_scan.model, NULL, NULL, g_scan.autosave_on, &g_scan.diff, &r);
    g_scan.history = r.status;
    g_scan.history_error = r.error;
    g_scan.has_diff = r.has_diff;
    history_track_result_free(&r);
    if (g_scan.has_diff) {
        g_scan.change_of_node = malloc(g_scan.model.len * sizeof(*g_scan.change_of_node));
        if (g_scan.change_of_node != NULL) {
            for (i = 0; i < g_scan.model.len; i++) {
                g_scan.change_of_node[i] = SCAN_NODE_NONE;
            }
            for (i = 0; i < g_scan.diff.len; i++) {
                if (g_scan.diff.items[i].node < g_scan.model.len) {
                    g_scan.change_of_node[g_scan.diff.items[i].node] = i;
                }
            }
        }
    }
}

static void clear_results(void)
{
    /* 트리맵은 모델을 가리키므로 해제하기 전에 떼어 낸다. */
    treemap_view_set_model(g_scan.treemap, NULL);
    TreeView_DeleteAllItems(g_scan.tree);
    ListView_DeleteAllItems(g_scan.list);
    scan_model_free(&g_scan.model);
    g_scan.has_model = 0;
    history_diff_free(&g_scan.diff);
    g_scan.has_diff = 0;
    free(g_scan.change_of_node);
    g_scan.change_of_node = NULL;
    g_scan.history = HISTORY_TRACK_NONE;
    g_scan.history_cleared = 0;
    SetWindowTextW(g_scan.analysis, L"");
    EnableWindow(g_scan.export_csv, FALSE);
    EnableWindow(g_scan.export_html, FALSE);
}

/* 정밀 스캔 열(중복 제외·하드 링크 중복)은 정밀 스캔 결과일 때만 보인다. */
/* 순위 목록 열 머리글 문구(COL_* 순서). */
static const UiText k_col_text[COL_COUNT] = {
    UI_T_COL_RANK,   UI_T_COL_FOLDER, UI_T_COL_SIZE,  UI_T_COL_UNIQUE, UI_T_COL_DUP,
    UI_T_COL_CHANGE, UI_T_COL_ALLOC,  UI_T_COL_FILES, UI_T_COL_SHARE};

/* 열 폭: 기본 폭(96 DPI 기준)과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(int col, int base)
{
    int need = theme_text_width(g_scan.list, g_scan.font, ui_text(k_col_text[col])) + scale(16);
    return scale(base) > need ? scale(base) : need;
}

static void update_precise_columns(void)
{
    int on = precise_result();

    ListView_SetColumnWidth(g_scan.list, COL_UNIQUE, on ? column_width(COL_UNIQUE, 90) : 0);
    ListView_SetColumnWidth(g_scan.list, COL_DUP, on ? column_width(COL_DUP, 120) : 0);
}

static void show_results(void)
{
    HTREEITEM root;

    update_precise_columns();
    if (g_scan.model.len == 0) {
        return;
    }
    treemap_view_set_model(g_scan.treemap, &g_scan.model);
    root = tree_insert(TVI_ROOT, 0);
    if (root != NULL) {
        tree_populate(root, 0);
        TreeView_Expand(g_scan.tree, root, TVE_EXPAND);
        TreeView_SelectItem(g_scan.tree, root);
    }
    fill_rank();
}

/* 순위 목록·트리맵에서 고른 폴더를 트리에서 펼쳐 보여 준다. focus 면 트리로 포커스를 옮긴다. */
static void reveal_in_tree(size_t index, int focus)
{
    const ScanNode *n = scan_model_node(&g_scan.model, index);
    size_t *chain, depth, i;
    HTREEITEM item;

    if (n == NULL) {
        return;
    }
    depth = n->depth;
    chain = malloc((depth + 1) * sizeof(*chain));
    if (chain == NULL) {
        return;
    }
    for (i = depth + 1; i-- > 0;) {
        chain[i] = index;
        index = g_scan.model.nodes[index].parent;
    }

    item = TreeView_GetRoot(g_scan.tree);
    for (i = 1; item != NULL && i <= depth; i++) {
        HTREEITEM child;
        tree_populate(item, chain[i - 1]);
        TreeView_Expand(g_scan.tree, item, TVE_EXPAND);
        for (child = TreeView_GetChild(g_scan.tree, item); child != NULL;
             child = TreeView_GetNextSibling(g_scan.tree, child)) {
            if (tree_item_index(child) == chain[i]) {
                break;
            }
        }
        item = child;
    }
    free(chain);

    if (item != NULL) {
        TreeView_SelectItem(g_scan.tree, item);
        TreeView_EnsureVisible(g_scan.tree, item);
        if (focus) {
            SetFocus(g_scan.tree);
        }
    }
}

/* 트리맵에서 고르거나 확대·축소한 폴더를 트리에도 고른다(트리맵 기준 폴더는 그대로). */
static void sync_tree_from_map(size_t index)
{
    if (!g_scan.has_model || index >= g_scan.model.len) {
        return;
    }
    g_scan.map_syncing = 1;
    reveal_in_tree(index, 0);
    g_scan.map_syncing = 0;
}

/* ---- 스캔 ---- */

/* [기록 삭제]는 스캔 중이 아니고 저장된 기록이 있을 때만 누를 수 있다. */
static void update_clear_button(void)
{
    wchar_t dir[MAX_PATH * 2];
    size_t count = 0;

    if (g_scan.job == NULL && history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) == 0) {
        count = history_count(dir);
    }
    EnableWindow(g_scan.clear_history, count > 0);
}

/* 내보낼 스캔 결과가 있는지(스캔 중이 아니고, 결과가 있고, 대상 폴더를 열지 못한 것이 아님). */
static int can_export(void)
{
    return g_scan.job == NULL && g_scan.has_model && g_scan.model.len > 0 &&
           g_scan.model.status != SCAN_FATAL;
}

static void set_scanning(int scanning)
{
    EnableWindow(g_scan.scan, !scanning);
    EnableWindow(g_scan.browse, !scanning);
    EnableWindow(g_scan.path, !scanning);
    EnableWindow(g_scan.cancel, scanning);
    EnableWindow(g_scan.precise, !scanning);
    EnableWindow(g_scan.export_csv, can_export());
    EnableWindow(g_scan.export_html, can_export());
    update_clear_button();
}

static void set_status(const wchar_t *text)
{
    SetWindowTextW(g_scan.status, text);
}

/* ---- 기록 설정 ---- */

/*
 * 자동 저장 설정을 바꾸고 기록 폴더의 설정 파일에 저장한다(다음 실행에도 유지).
 * 저장하지 못하면 체크 상태를 이전 값으로 되돌리고 상태 줄에 알린다. 성공하면 1.
 */
static int apply_autosave(int on)
{
    wchar_t dir[MAX_PATH * 2], text[256];
    unsigned long error = ERROR_PATH_NOT_FOUND;

    on = on != 0;
    if (history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) == 0 &&
        history_set_autosave(dir, on, &error) == 0) {
        g_scan.autosave_on = on;
        SendMessageW(g_scan.autosave, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        set_status(on ? L"스캔 기록 자동 저장을 켰습니다. 다음 스캔부터 결과를 기록합니다."
                      : L"스캔 기록 자동 저장을 껐습니다. 이후 스캔 결과는 저장하지 않습니다"
                        L"(이미 저장된 기록은 그대로 둡니다).");
        return 1;
    }
    SendMessageW(g_scan.autosave, BM_SETCHECK, g_scan.autosave_on ? BST_CHECKED : BST_UNCHECKED, 0);
    swprintf(text, 256, L"자동 저장 설정을 저장하지 못했습니다(오류 %lu). 설정은 바뀌지 않았습니다.",
             error);
    set_status(text);
    return 0;
}

/*
 * 저장된 스캔 기록을 모두 삭제한다. confirm 이 1 이면 개수·위치·되돌릴 수 없다는 안내로 먼저
 * 확인한다. 지금 보이는 변화가 지운 기록과 비교한 것이면 변화 열·변화 순위·분석 줄에서 지운다.
 * 일부 파일을 지우지 못해도 나머지는 지우고 결과를 상태 줄에 쓴다.
 * 삭제를 실행했으면 1, 기록이 없거나 스캔 중이거나 취소했으면 0.
 */
static int clear_history(int confirm)
{
    wchar_t dir[MAX_PATH * 2], text[MAX_PATH * 2 + 512], *file;
    size_t count;
    int rc;

    if (g_scan.job != NULL) {
        return 0;
    }
    if (history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0) {
        set_status(L"기록 폴더를 정하지 못해 기록을 삭제할 수 없습니다.");
        return 0;
    }
    count = history_count(dir);
    if (count == 0) {
        set_status(L"삭제할 스캔 기록이 없습니다.");
        update_clear_button();
        return 0;
    }
    if (confirm) {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"저장된 스캔 기록 %u개를 삭제할까요?\n\n기록 폴더: %ls\n\n"
                 L"삭제하면 직전 스캔과 비교한 변화를 더 볼 수 없고, 다음 스캔은 새 기준이 됩니다. "
                 L"같은 폴더를 쓰는 CLI --track 기록도 함께 삭제됩니다.\n"
                 L"스캔한 폴더와 파일은 건드리지 않으며, 자동 저장 설정은 그대로 둡니다.\n\n"
                 L"삭제한 기록은 되돌릴 수 없습니다.",
                 (unsigned)count, dir);
        if (MessageBoxW(GetAncestor(g_scan.hwnd, GA_ROOT), text, L"스캔 기록 삭제",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            set_status(L"기록 삭제를 취소했습니다. 아무것도 바뀌지 않았습니다.");
            return 0;
        }
    }

    rc = history_clear(dir, &g_scan.cleared);
    /* 지금 대상의 기록이 사라졌으면 그 기록과 비교한 변화 표시도 지운다. */
    if (g_scan.has_model && g_scan.model.len > 0 &&
        (file = history_file_for(dir, g_scan.model.nodes[0].name)) != NULL) {
        if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
            history_diff_free(&g_scan.diff);
            g_scan.has_diff = 0;
            free(g_scan.change_of_node);
            g_scan.change_of_node = NULL;
            g_scan.history_cleared = 1;
        }
        free(file);
    }
    if (rc == 0) {
        swprintf(text, sizeof(text) / sizeof(text[0]), L"스캔 기록 %u개를 삭제했습니다.",
                 (unsigned)g_scan.cleared.removed);
    } else if (g_scan.cleared.failed == 0) {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"기록 폴더를 읽지 못해 기록을 삭제하지 못했습니다(오류 %lu).",
                 g_scan.cleared.error);
    } else {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"스캔 기록 %u개를 삭제했고 %u개는 삭제하지 못했습니다(오류 %lu: 사용 중이거나 "
                 L"권한이 없을 수 있음). 다시 시도하세요.",
                 (unsigned)g_scan.cleared.removed, (unsigned)g_scan.cleared.failed,
                 g_scan.cleared.error);
    }
    set_status(text);
    update_analysis();
    fill_rank();
    update_clear_button();
    return 1;
}

/*
 * 지금 스캔 결과를 kind 형식으로 file 에 쓴다. 결과는 상태 줄에 쓰고, 실패했고 show_error 가 1 이면
 * 원인을 창으로도 알린다(기존 파일은 바뀌지 않음). 성공하면 1.
 */
static int export_to(ExportKind kind, const wchar_t *file, int show_error)
{
    const wchar_t *what = kind == EXPORT_KIND_CSV ? L"CSV" : L"HTML 보고서";
    unsigned long error = ERROR_INVALID_PARAMETER;
    size_t len = wcslen(file) + 256;
    wchar_t *text;
    int ok = 0;

    if (can_export()) {
        ok = (kind == EXPORT_KIND_CSV
                  ? export_csv(&g_scan.model, file, &error)
                  : export_html(&g_scan.model, &g_scan.scan_info, file, &error)) == 0;
    }
    text = malloc(len * sizeof(*text));
    if (text == NULL) {
        return ok;
    }
    if (ok) {
        swprintf(text, len, L"%ls(으)로 내보냈습니다(폴더 %u개): %ls", what,
                 (unsigned)g_scan.model.len, file);
        set_status(text);
    } else {
        swprintf(text, len, L"%ls 내보내기 실패(오류 %lu): %ls", what, error,
                 export_error_text(error));
        set_status(text);
        if (show_error) {
            swprintf(text, len,
                     L"%ls(으)로 내보내지 못했습니다.\n\n%ls\n(오류 %lu)\n\n파일: %ls\n\n"
                     L"이미 있던 파일은 바뀌지 않았습니다.",
                     what, export_error_text(error), error, file);
            MessageBoxW(GetAncestor(g_scan.hwnd, GA_ROOT), text, L"내보내기 실패",
                        MB_OK | MB_ICONWARNING);
        }
    }
    free(text);
    return ok;
}

/* 저장 위치를 고르게 한 뒤(덮어쓰기는 대화 상자가 확인) 내보낸다. */
static void export_results(ExportKind kind)
{
    OPENFILENAMEW ofn;
    wchar_t file[MAX_PATH * 4];
    DWORD dlg_error;

    if (!can_export()) {
        set_status(L"내보낼 스캔 결과가 없습니다. 먼저 폴더를 스캔하세요.");
        return;
    }
    export_default_name(g_scan.model.nodes[0].name, kind, NULL, file,
                        sizeof(file) / sizeof(file[0]));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetAncestor(g_scan.hwnd, GA_ROOT);
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    if (kind == EXPORT_KIND_CSV) {
        ofn.lpstrFilter = L"CSV 파일 (*.csv)\0*.csv\0모든 파일 (*.*)\0*.*\0";
        ofn.lpstrDefExt = L"csv";
        ofn.lpstrTitle = L"스캔 결과를 CSV로 내보내기";
    } else {
        ofn.lpstrFilter = L"HTML 보고서 (*.html)\0*.html;*.htm\0모든 파일 (*.*)\0*.*\0";
        ofn.lpstrDefExt = L"html";
        ofn.lpstrTitle = L"스캔 결과를 HTML 보고서로 저장";
    }
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) {
        dlg_error = CommDlgExtendedError();
        if (dlg_error != 0) {
            wchar_t text[128];
            swprintf(text, 128, L"저장 위치 대화 상자를 열지 못했습니다(오류 0x%lx).",
                     (unsigned long)dlg_error);
            set_status(text);
        }
        return; /* 취소 */
    }
    export_to(kind, file, 1);
}

/* 스캔 스레드에서 호출된다. 창이 이미 없으면 PostMessage 가 실패할 뿐이다. */
static void on_job_notify(int finished, void *ctx)
{
    HWND hwnd = ctx;

    PostMessageW(hwnd, finished ? WM_APP_SCAN_DONE : WM_APP_SCAN_PROGRESS, 0, 0);
}

static void start_scan(void)
{
    wchar_t *target;
    int len, precise;

    if (g_scan.job != NULL) {
        return;
    }
    len = GetWindowTextLengthW(g_scan.path);
    target = malloc(((size_t)len + 1) * sizeof(*target));
    if (target == NULL) {
        return;
    }
    GetWindowTextW(g_scan.path, target, len + 1);
    if (target[0] == L'\0') {
        free(target);
        set_status(L"스캔할 폴더를 입력하거나 [찾아보기]로 선택하세요.");
        return;
    }

    clear_results();
    g_scan.progress_pos = 0;
    SendMessageW(g_scan.progress, PBM_SETPOS, 0, 0);
    precise = SendMessageW(g_scan.precise, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_scan.job = scan_job_start_ex(target, SCAN_DEPTH_UNLIMITED,
                                  precise ? (unsigned)SCAN_OPT_HARDLINKS : 0u,
                                  PROGRESS_INTERVAL_MS, on_job_notify, g_scan.hwnd);
    free(target);
    if (g_scan.job == NULL) {
        set_status(L"스캔을 시작하지 못했습니다(메모리 또는 스레드 생성 실패).");
        return;
    }
    set_scanning(1);
    set_status(precise ? L"정밀 스캔 중(하드 링크 확인)..." : L"스캔 중...");
}

static void cancel_scan(void)
{
    if (g_scan.job != NULL) {
        scan_job_cancel(g_scan.job);
        EnableWindow(g_scan.cancel, FALSE);
        set_status(L"취소하는 중...");
    }
}

static void update_progress(void)
{
    ScanJobSnapshot snap;
    wchar_t size[32], text[SCAN_JOB_DIR_LEN + 160];
    int pos;

    if (g_scan.job == NULL) {
        return;
    }
    scan_job_progress(g_scan.job, &snap);
    if (snap.cancel_requested) {
        return;
    }
    /* 남은 폴더 수가 늘면 비율이 줄 수 있으므로 막대는 뒤로 가지 않게 한다. */
    pos = (int)(scan_progress_fraction(&snap.progress) * PROGRESS_RANGE);
    if (pos > g_scan.progress_pos) {
        g_scan.progress_pos = pos;
        SendMessageW(g_scan.progress, PBM_SETPOS, (WPARAM)pos, 0);
    }
    format_size(snap.progress.bytes, size, 32);
    if (snap.progress.precise) {
        /* 정밀 스캔: 지금까지 찾은 하드 링크 중복도 보여 준다. */
        wchar_t dup[32];
        format_size(snap.progress.dup_bytes, dup, 32);
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"정밀 스캔 중... 파일 %llu · 폴더 %llu · %ls · 하드 링크 중복 %llu개(-%ls) · "
                 L"오류 %llu  |  %ls",
                 (unsigned long long)snap.progress.files, (unsigned long long)snap.progress.dirs,
                 size, (unsigned long long)snap.progress.dup_files, dup,
                 (unsigned long long)snap.progress.errors, snap.current_dir);
    } else {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"스캔 중... 파일 %llu · 폴더 %llu · %ls · 오류 %llu  |  %ls",
                 (unsigned long long)snap.progress.files, (unsigned long long)snap.progress.dirs,
                 size, (unsigned long long)snap.progress.errors, snap.current_dir);
    }
    set_status(text);
}

static void finish_scan(void)
{
    ScanJobSnapshot snap;
    ScanStatus status;
    const ScanReport *r;
    wchar_t size[32], text[512];
    const wchar_t *head;

    if (g_scan.job == NULL) {
        return;
    }
    scan_job_progress(g_scan.job, &snap);
    status = scan_job_finish(g_scan.job, &g_scan.model);
    g_scan.job = NULL;
    g_scan.has_model = 1;
    r = &g_scan.model.report;
    {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        g_scan.scan_info.finished_time = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
        g_scan.scan_info.elapsed_ms = snap.elapsed_ms;
    }

    if (r->cancelled) {
        head = L"취소됨(그때까지 집계)";
    } else if (status == SCAN_COMPLETE) {
        head = L"완료";
    } else if (status == SCAN_PARTIAL) {
        head = L"완료(일부 폴더를 읽지 못함)";
    } else {
        head = L"스캔 실패";
    }

    if (status == SCAN_FATAL) {
        const wchar_t *kind = r->errors_len > 0 ? scan_error_kind_name(r->errors[0].kind)
                                                : L"other";
        swprintf(text, sizeof(text) / sizeof(text[0]), L"%ls: 폴더를 열 수 없습니다 (%ls).",
                 head, kind);
        SendMessageW(g_scan.progress, PBM_SETPOS, 0, 0);
    } else {
        format_size(r->total_bytes, size, 32);
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"%ls%ls: 파일 %llu · 폴더 %llu · %ls · 오류 %llu · 건너뛴 폴더 %llu · %.1f초",
                 r->hardlinks_checked ? L"정밀 스캔 " : L"", head,
                 (unsigned long long)r->file_count, (unsigned long long)r->dir_count, size,
                 (unsigned long long)r->error_count, (unsigned long long)r->skipped_dir_count,
                 (double)snap.elapsed_ms / 1000.0);
        if (!r->cancelled) {
            SendMessageW(g_scan.progress, PBM_SETPOS, PROGRESS_RANGE, 0);
        }
    }
    set_status(text);
    set_scanning(0);
    track_history();
    update_clear_button();
    update_analysis();
    show_results();
}

static void browse_folder(void)
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    wchar_t path[MAX_PATH];

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = GetAncestor(g_scan.hwnd, GA_ROOT);
    bi.lpszTitle = L"스캔할 폴더를 선택하세요.";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl == NULL) {
        return;
    }
    if (SHGetPathFromIDListW(pidl, path)) {
        SetWindowTextW(g_scan.path, path);
    }
    CoTaskMemFree(pidl);
}
/* ---- 자체 검사(--self-test) ---- */

static int count_tree_children(HTREEITEM item)
{
    int n = 0;
    HTREEITEM child;

    for (child = TreeView_GetChild(g_scan.tree, item); child != NULL;
         child = TreeView_GetNextSibling(g_scan.tree, child)) {
        n++;
    }
    return n;
}

#define SELF_CHECK(cond)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            wchar_t msg_[256];                                               \
            swprintf(msg_, 256, L"self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                              \
            OutputDebugStringW(msg_);                                        \
            fwprintf(stderr, L"%ls", msg_);                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* 보기 콤보 상자: 디스크 사용 순위·변화 순위 목록이 모델·비교 결과와 맞는지. 실패 수. */
static int verify_views(void)
{
    int failures = 0, rows;
    size_t top, i, expect;
    LVITEMW lv;
    wchar_t cell[96], want[96];

    SELF_CHECK(current_view() == VIEW_SIZE);

    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_ALLOC, 0);
    fill_rank();
    rows = ListView_GetItemCount(g_scan.list);
    expect = g_scan.model.len > 0 ? g_scan.model.len - 1 : 0;
    SELF_CHECK(rows == (int)(expect < RANK_COUNT ? expect : RANK_COUNT));
    if (rows > 0) {
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = 0;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        SELF_CHECK(scan_model_rank(&g_scan.model, SCAN_RANK_TOTAL_ALLOC, &top, 1) == 1 &&
                   (size_t)lv.lParam == top);
    }

    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_CHANGE, 0);
    fill_rank();
    rows = ListView_GetItemCount(g_scan.list);
    expect = g_scan.has_diff ? (g_scan.diff.len < RANK_COUNT ? g_scan.diff.len : RANK_COUNT) : 0;
    SELF_CHECK(rows == (int)expect);
    for (i = 0; i < expect && (int)i < rows; i++) {
        const HistoryChange *c = &g_scan.diff.items[i];
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = (int)i;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        SELF_CHECK((size_t)lv.lParam == c->node);
        ListView_GetItemText(g_scan.list, (int)i, COL_CHANGE, cell, 96);
        history_change_text(c, want, 96);
        SELF_CHECK(wcscmp(cell, want) == 0);
        if (c->node != SCAN_NODE_NONE) {
            SELF_CHECK(g_scan.change_of_node == NULL || g_scan.change_of_node[c->node] == i);
        }
    }

    /* 중복 제외 용량 순위: 내림차순(기본 스캔이면 용량 순위와 같다) */
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_UNIQUE, 0);
    fill_rank();
    rows = ListView_GetItemCount(g_scan.list);
    expect = g_scan.model.len > 0 ? g_scan.model.len - 1 : 0;
    SELF_CHECK(rows == (int)(expect < RANK_COUNT ? expect : RANK_COUNT));
    for (i = 0; (int)i < rows; i++) {
        size_t prev = 0;
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = (int)i;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        if (i > 0) {
            LVITEMW pv;
            memset(&pv, 0, sizeof(pv));
            pv.mask = LVIF_PARAM;
            pv.iItem = (int)i - 1;
            SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&pv);
            prev = (size_t)pv.lParam;
            SELF_CHECK(unique_bytes(&g_scan.model.nodes[prev]) >=
                       unique_bytes(&g_scan.model.nodes[(size_t)lv.lParam]));
        }
    }

    /* 하드 링크 중복 순위: 중복이 있는 폴더만(기본 스캔이면 없음) */
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_DUP, 0);
    fill_rank();
    rows = ListView_GetItemCount(g_scan.list);
    expect = 0;
    for (i = 1; i < g_scan.model.len; i++) {
        expect += g_scan.model.nodes[i].total_dup_files > 0;
    }
    SELF_CHECK(rows == (int)(expect < RANK_COUNT ? expect : RANK_COUNT));
    SELF_CHECK(precise_result() || rows == 0);
    for (i = 0; (int)i < rows; i++) {
        const ScanNode *n;
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = (int)i;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        n = &g_scan.model.nodes[(size_t)lv.lParam];
        SELF_CHECK(n->total_dup_files > 0);
        ListView_GetItemText(g_scan.list, (int)i, COL_UNIQUE, cell, 96);
        format_size(unique_bytes(n), want, 96);
        SELF_CHECK(wcscmp(cell, want) == 0);
        ListView_GetItemText(g_scan.list, (int)i, COL_DUP, cell, 96);
        SELF_CHECK(cell[0] == L'-');
    }

    /* 정밀 스캔 열은 정밀 스캔 결과일 때만 보이고, 기본 스캔이면 비어 있다 */
    SELF_CHECK((ListView_GetColumnWidth(g_scan.list, COL_UNIQUE) > 0) == precise_result());
    SELF_CHECK((ListView_GetColumnWidth(g_scan.list, COL_DUP) > 0) == precise_result());
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_SIZE, 0);
    fill_rank();
    if (!precise_result() && ListView_GetItemCount(g_scan.list) > 0) {
        ListView_GetItemText(g_scan.list, 0, COL_UNIQUE, cell, 96);
        SELF_CHECK(cell[0] == L'\0');
    }
    return failures;
}

static int is_shown(HWND h);

static size_t tree_selection(void)
{
    return tree_item_index(TreeView_GetSelection(g_scan.tree));
}

static void select_view(int view)
{
    SendMessageW(g_scan.view, CB_SETCURSEL, (WPARAM)view, 0);
    SendMessageW(g_scan.hwnd, WM_COMMAND, MAKEWPARAM(IDC_VIEW, CBN_SELCHANGE), (LPARAM)g_scan.view);
}

/*
 * 트리맵 보기: 고르면 순위 목록 대신 보이고, 트리에서 고른 폴더가 기준 폴더가 되며, 클릭(트리에서
 * 선택)·더블클릭(확대)·Backspace(상위 폴더로)가 트리와 맞는지, 색 구분 기준마다 화면이 맞는지.
 * 실패 수.
 */
static int verify_treemap(void)
{
    int failures = 0, m;
    size_t first = SCAN_NODE_NONE;
    POINT pt;

    /* 자체 검사의 창은 숨겨져 WM_SIZE 를 받지 않으므로 직접 배치해 트리맵에 크기를 준다. */
    layout();
    select_view(VIEW_TREEMAP);
    SELF_CHECK(current_view() == VIEW_TREEMAP);
    SELF_CHECK(is_shown(g_scan.treemap) && !is_shown(g_scan.list));
    reveal_in_tree(0, 0);
    SELF_CHECK(treemap_view_root(g_scan.treemap) == 0);
    failures += treemap_view_verify(g_scan.treemap);

    if (scan_model_children(&g_scan.model, 0, SCAN_ORDER_SIZE_DESC, &first, 1) > 0 &&
        g_scan.model.nodes[first].total_bytes > 0) {
        /* 가장 큰 하위 폴더는 클릭할 수 있을 만큼 크게 그려진다 */
        int ok = treemap_view_block_point(g_scan.treemap, first, &pt);
        SELF_CHECK(ok);
        if (ok) {
            LPARAM at = MAKELPARAM(pt.x, pt.y);
            /* 클릭: 트리에서 고르지만 기준 폴더는 그대로 */
            SendMessageW(g_scan.treemap, WM_LBUTTONDOWN, MK_LBUTTON, at);
            SendMessageW(g_scan.treemap, WM_LBUTTONUP, 0, at);
            SELF_CHECK(treemap_view_selected(g_scan.treemap) == first);
            SELF_CHECK(tree_selection() == first);
            SELF_CHECK(treemap_view_root(g_scan.treemap) == 0);
            /* 더블클릭: 그 폴더로 확대, 트리도 그 폴더 */
            SendMessageW(g_scan.treemap, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
            SELF_CHECK(treemap_view_root(g_scan.treemap) == first);
            SELF_CHECK(tree_selection() == first);
            failures += treemap_view_verify(g_scan.treemap);
            /* Backspace: 상위 폴더로, 올라온 폴더를 고른 상태로 둔다 */
            SendMessageW(g_scan.treemap, WM_KEYDOWN, VK_BACK, 0);
            SELF_CHECK(treemap_view_root(g_scan.treemap) == 0);
            SELF_CHECK(tree_selection() == 0);
            SELF_CHECK(treemap_view_selected(g_scan.treemap) == first);
            /* 트리에서 고르면 그 폴더가 기준 폴더 */
            reveal_in_tree(first, 0);
            SELF_CHECK(treemap_view_root(g_scan.treemap) == first);
            reveal_in_tree(0, 0);
            SELF_CHECK(treemap_view_root(g_scan.treemap) == 0);
        }
    }
    for (m = 0; m < TREEMAP_COLOR_COUNT; m++) {
        treemap_view_set_color(g_scan.treemap, (TreemapColorMode)m);
        SELF_CHECK(treemap_view_color(g_scan.treemap) == (TreemapColorMode)m);
        failures += treemap_view_verify(g_scan.treemap);
    }
    treemap_view_set_color(g_scan.treemap, TREEMAP_COLOR_DEPTH);

    select_view(VIEW_SIZE);
    SELF_CHECK(is_shown(g_scan.list) && !is_shown(g_scan.treemap));
    return failures;
}

/* --self-test-changes: 변화 목록을 stdout 에 쓴다(검사 스크립트가 확인). */
static void print_changes(void)
{
    size_t i;
    wchar_t delta[48];

    for (i = 0; i < g_scan.diff.len; i++) {
        const HistoryChange *c = &g_scan.diff.items[i];
        history_format_delta(c->delta, delta, 48);
        wprintf(L"change: %ls %ls %ls\n", history_kind_name(c->kind), delta, c->rel);
    }
    wprintf(L"changes: grown %u shrunk %u added %u removed %u\n", (unsigned)g_scan.diff.grown,
            (unsigned)g_scan.diff.shrunk, (unsigned)g_scan.diff.added,
            (unsigned)g_scan.diff.removed);
    fflush(stdout);
}

/*
 * SCAN_VERIFY_NO_AUTOSAVE: 자동 저장을 끈 채 스캔했으니 저장하지 않았고 설정이 파일에 남았는지.
 * SCAN_VERIFY_CLEAR_HISTORY: 직전 스냅숏과 비교한 뒤 기록을 삭제(확인 창 없이)하면 기록 파일이
 * 없어지고 변화 열·변화 순위·분석 줄·[기록 삭제] 버튼에 반영되는지. 지운 개수를 stdout 에 쓴다.
 * 실패 수.
 */
static int verify_history_settings(void)
{
    int failures = 0, rows;
    wchar_t dir[MAX_PATH * 2], text[512], cell[96];
    size_t before;

    SELF_CHECK(g_scan.model.report.cancelled == 0);
    SELF_CHECK(g_scan.model.status != SCAN_FATAL);
    SELF_CHECK(history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) == 0);
    SELF_CHECK(history_autosave_enabled(dir) == g_scan.autosave_on);
    SELF_CHECK((SendMessageW(g_scan.autosave, BM_GETCHECK, 0, 0) == BST_CHECKED) ==
               (g_scan.autosave_on != 0));
    if (g_scan.self_test == SCAN_VERIFY_NO_AUTOSAVE) {
        SELF_CHECK(!g_scan.autosave_on);
        SELF_CHECK(g_scan.history == HISTORY_TRACK_DISABLED);
        GetWindowTextW(g_scan.analysis, text, 512);
        SELF_CHECK(wcsstr(text, L"자동 저장이 꺼져") != NULL);
        return failures;
    }

    /* 기록 삭제 */
    SELF_CHECK(g_scan.has_diff);
    before = history_count(dir);
    SELF_CHECK(before > 0 && IsWindowEnabled(g_scan.clear_history));
    SELF_CHECK(clear_history(0) == 1);
    SELF_CHECK(g_scan.cleared.failed == 0 && g_scan.cleared.removed == before);
    SELF_CHECK(history_count(dir) == 0);
    SELF_CHECK(history_autosave_enabled(dir) == g_scan.autosave_on);
    SELF_CHECK(!g_scan.has_diff && g_scan.change_of_node == NULL);
    SELF_CHECK(g_scan.history_cleared);
    SELF_CHECK(!IsWindowEnabled(g_scan.clear_history));
    GetWindowTextW(g_scan.analysis, text, 512);
    SELF_CHECK(wcsstr(text, L"기록을 삭제했습니다") != NULL && wcsstr(text, L"직전 스캔(") == NULL);
    GetWindowTextW(g_scan.status, text, 512);
    SELF_CHECK(wcsstr(text, L"삭제했습니다") != NULL);
    rows = ListView_GetItemCount(g_scan.list);
    if (rows > 0) {
        ListView_GetItemText(g_scan.list, 0, COL_CHANGE, cell, 96);
        SELF_CHECK(cell[0] == L'\0');
    }
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_CHANGE, 0);
    fill_rank();
    SELF_CHECK(ListView_GetItemCount(g_scan.list) == 0);
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_SIZE, 0);
    fill_rank();
    /* 더 지울 것이 없으면 아무것도 하지 않는다. */
    SELF_CHECK(clear_history(0) == 0);
    wprintf(L"cleared: %u\n", (unsigned)g_scan.cleared.removed);
    fflush(stdout);
    return failures;
}

/*
 * SCAN_VERIFY_EXPORT: 스캔 전에는 꺼져 있던 내보내기 버튼이 켜졌는지, <prefix>.csv·<prefix>.html 로
 * 내보내지는지, 없는 폴더로 내보내면 실패를 알리고 아무것도 만들지 않는지. 내용은 검사 스크립트가
 * 확인하도록 폴더 수를 stdout 에 쓴다. 실패 수.
 */
static int verify_export(void)
{
    int failures = 0;
    size_t len = wcslen(g_scan.export_prefix) + 32;
    wchar_t *file = malloc(len * sizeof(*file)), text[512];

    SELF_CHECK(!g_scan.export_enabled_before_scan);
    SELF_CHECK(IsWindowEnabled(g_scan.export_csv) && IsWindowEnabled(g_scan.export_html));
    if (file == NULL) {
        return failures + 1;
    }
    swprintf(file, len, L"%ls.csv", g_scan.export_prefix);
    SELF_CHECK(export_to(EXPORT_KIND_CSV, file, 0));
    GetWindowTextW(g_scan.status, text, 512);
    SELF_CHECK(wcsstr(text, L"CSV(으)로 내보냈습니다") == text);
    swprintf(file, len, L"%ls.html", g_scan.export_prefix);
    SELF_CHECK(export_to(EXPORT_KIND_HTML, file, 0));
    GetWindowTextW(g_scan.status, text, 512);
    SELF_CHECK(wcsstr(text, L"HTML 보고서(으)로 내보냈습니다") == text);

    /* 없는 폴더: 실패를 상태 줄에 알리고 아무 파일도 남기지 않는다. */
    swprintf(file, len, L"%ls-missing\\out.csv", g_scan.export_prefix);
    SELF_CHECK(!export_to(EXPORT_KIND_CSV, file, 0));
    GetWindowTextW(g_scan.status, text, 512);
    SELF_CHECK(wcsstr(text, L"내보내기 실패") != NULL);
    SELF_CHECK(GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES);
    free(file);

    wprintf(L"exported folders: %u\n", (unsigned)g_scan.model.len);
    fflush(stdout);
    return failures;
}

/*
 * SCAN_VERIFY_PRECISE: 정밀 스캔 결과가 화면(상태 줄·분석 줄·트리·열)에 맞게 나오는지 확인하고,
 * 검사 스크립트가 확인하도록 기본 집계와 중복 제외 집계를 stdout 에 쓴다. 실패 수.
 */
static int verify_precise(void)
{
    const ScanReport *r = &g_scan.model.report;
    const ScanNode *root = &g_scan.model.nodes[0];
    int failures = 0;
    wchar_t text[1024], want[64], size[32];
    TVITEMW tv;
    HTREEITEM item = TreeView_GetRoot(g_scan.tree);

    SELF_CHECK(r->hardlinks_checked && !r->hardlinks_unsupported);
    SELF_CHECK(!IsWindowEnabled(g_scan.precise) == (g_scan.job != NULL));
    SELF_CHECK(root->total_dup_bytes == r->hardlink_dup_bytes &&
               root->total_dup_files == r->hardlink_dup_files);
    GetWindowTextW(g_scan.status, text, 1024);
    /* "정밀 스캔 완료: …" 또는 "정밀 스캔 완료(일부 폴더를 읽지 못함): …" */
    SELF_CHECK(wcsstr(text, L"정밀 스캔 완료") == text);
    /* 분석 줄: 중복 제외(기본 대비 차이)와 실제 디스크 사용 */
    GetWindowTextW(g_scan.analysis, text, 1024);
    format_size(r->total_bytes - r->hardlink_dup_bytes, size, 32);
    swprintf(want, 64, L"중복 제외 %ls(기본 ", size);
    SELF_CHECK(wcsstr(text, want) != NULL);
    format_size(r->alloc_bytes - r->hardlink_dup_alloc, size, 32);
    swprintf(want, 64, L"실제 디스크 사용 %ls", size);
    SELF_CHECK(wcsstr(text, want) != NULL);
    /* 트리 루트에 중복 제외 용량이 덧붙는다(중복이 있을 때) */
    memset(&tv, 0, sizeof(tv));
    tv.mask = TVIF_TEXT;
    tv.hItem = item;
    tv.pszText = text;
    tv.cchTextMax = 1024;
    SELF_CHECK(item != NULL && TreeView_GetItem(g_scan.tree, &tv));
    SELF_CHECK((wcsstr(text, L" · 중복 제외 ") != NULL) == (root->total_dup_bytes > 0));

    wprintf(L"precise: files %llu bytes %llu dup_files %llu dup_bytes %llu unique %llu "
            L"unknown %llu\n",
            (unsigned long long)r->file_count, (unsigned long long)r->total_bytes,
            (unsigned long long)r->hardlink_dup_files, (unsigned long long)r->hardlink_dup_bytes,
            (unsigned long long)(r->total_bytes - r->hardlink_dup_bytes),
            (unsigned long long)r->hardlink_unknown_files);
    fflush(stdout);
    return failures;
}

/* 스캔이 끝난 뒤 화면 상태가 모델과 맞는지 확인하고 종료 코드를 정한다. */
static int verify_screen(void)
{
    int failures = 0;
    HTREEITEM root = TreeView_GetRoot(g_scan.tree);
    size_t expect_rank = g_scan.model.len > 0 ? g_scan.model.len - 1 : 0;
    int rows = ListView_GetItemCount(g_scan.list);
    int pos = (int)SendMessageW(g_scan.progress, PBM_GETPOS, 0, 0);

    if (expect_rank > RANK_COUNT) {
        expect_rank = RANK_COUNT;
    }
    SELF_CHECK(g_scan.job == NULL);
    SELF_CHECK(g_scan.has_model);
    SELF_CHECK(IsWindowEnabled(g_scan.scan) && !IsWindowEnabled(g_scan.cancel));
    SELF_CHECK(g_scan.model.len > 0 && root != NULL);
    SELF_CHECK(rows == (int)expect_rank);

    if (root != NULL && g_scan.model.len > 0) {
        size_t first;
        HTREEITEM child = TreeView_GetChild(g_scan.tree, root);

        SELF_CHECK(tree_item_index(root) == 0);
        SELF_CHECK(count_tree_children(root) == (int)g_scan.model.nodes[0].child_count);
        /* 트리 자식은 용량 내림차순. */
        if (child != NULL &&
            scan_model_children(&g_scan.model, 0, SCAN_ORDER_SIZE_DESC, &first, 1) > 0) {
            SELF_CHECK(tree_item_index(child) == first);
        }
    }
    if (rows > 0) {
        size_t top;
        LVITEMW lv;
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = 0;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        SELF_CHECK(scan_model_rank(&g_scan.model, SCAN_RANK_TOTAL_BYTES, &top, 1) == 1 &&
                   (size_t)lv.lParam == top);

        /* 가장 깊은 순위 항목을 트리에서 찾아 선택할 수 있어야 한다. */
        reveal_in_tree((size_t)lv.lParam, 1);
        SELF_CHECK(tree_item_index(TreeView_GetSelection(g_scan.tree)) == (size_t)lv.lParam);
    }

    if (rows > 0) {
        /* 디스크 사용 열 */
        wchar_t cell[64], expect[64];
        LVITEMW lv;
        memset(&lv, 0, sizeof(lv));
        lv.mask = LVIF_PARAM;
        lv.iItem = 0;
        SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
        ListView_GetItemText(g_scan.list, 0, COL_ALLOC, cell, 64);
        format_size(g_scan.model.nodes[(size_t)lv.lParam].total_alloc, expect, 64);
        SELF_CHECK(wcscmp(cell, expect) == 0);
    }
    failures += verify_views();
    failures += verify_treemap();
    /* 결과가 있으면(취소된 스캔 포함) 내보낼 수 있다. */
    SELF_CHECK(IsWindowEnabled(g_scan.export_csv) == (g_scan.model.status != SCAN_FATAL));
    SELF_CHECK(IsWindowEnabled(g_scan.export_html) == (g_scan.model.status != SCAN_FATAL));

    if (g_scan.self_test == SCAN_VERIFY_SCAN || g_scan.self_test == SCAN_VERIFY_CHANGES ||
        g_scan.self_test == SCAN_VERIFY_PRECISE) {
        wchar_t text[1024];
        SELF_CHECK(g_scan.model.report.cancelled == 0);
        SELF_CHECK(g_scan.model.status != SCAN_FATAL);
        SELF_CHECK(pos == PROGRESS_RANGE);
        /* 직전 스냅숏이 없으면 기준으로, 있으면 비교한 뒤 저장한다. */
        SELF_CHECK(g_scan.history == HISTORY_TRACK_BASELINE ||
                   g_scan.history == HISTORY_TRACK_COMPARED);
        SELF_CHECK((g_scan.history == HISTORY_TRACK_COMPARED) == (g_scan.has_diff != 0));
        GetWindowTextW(g_scan.analysis, text, 1024);
        SELF_CHECK(wcsstr(text, L"디스크 사용 ") == text);
        SELF_CHECK(wcsstr(text, g_scan.has_diff ? L"직전 스캔(" : L"첫 스캔") != NULL);
        /* 정밀 스캔 요약은 정밀 스캔 결과에만 */
        SELF_CHECK((wcsstr(text, L"정밀 스캔: ") != NULL) == precise_result());
        SELF_CHECK(precise_result() == (g_scan.self_test == SCAN_VERIFY_PRECISE));
        if (g_scan.self_test == SCAN_VERIFY_CHANGES) {
            SELF_CHECK(g_scan.has_diff);
            print_changes();
        }
        if (g_scan.self_test == SCAN_VERIFY_PRECISE) {
            failures += verify_precise();
        }
    } else if (g_scan.self_test == SCAN_VERIFY_NO_AUTOSAVE ||
               g_scan.self_test == SCAN_VERIFY_CLEAR_HISTORY) {
        failures += verify_history_settings();
    } else if (g_scan.self_test == SCAN_VERIFY_EXPORT) {
        SELF_CHECK(g_scan.model.report.cancelled == 0 && g_scan.model.status != SCAN_FATAL);
        failures += verify_export();
    } else if (g_scan.model.report.cancelled) {
        SELF_CHECK(g_scan.history == HISTORY_TRACK_CANCELLED);
        size_t i;
        /* 취소 직후라면 열거하지 못한 폴더가 남는다. */
        SELF_CHECK(g_scan.model.status == SCAN_PARTIAL);
        for (i = 1; i < g_scan.model.len; i++) {
            if (g_scan.model.nodes[i].flags & SCAN_NODE_CANCELLED) {
                break;
            }
        }
        SELF_CHECK(g_scan.model.report.skipped_dir_count == 0 || i < g_scan.model.len);
    }
    return failures == 0 ? 0 : 1;
}

/* 창 자체의 표시 상태(부모가 숨겨져 있어도 판단할 수 있게 스타일을 본다). */
static int is_shown(HWND h)
{
    return (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

/* 컨트롤 글자가 expect 와 같은지. */
static int text_is(HWND h, const wchar_t *expect)
{
    wchar_t buf[256];
    GetWindowTextW(h, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return wcscmp(buf, expect) == 0;
}

/* ---- 패널 창 ---- */

static HWND make_child(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex,
                       int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                             g_scan.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_scan.font, FALSE);
    return h;
}

static void create_controls(void)
{
    static const struct {
        int width; /* 96 DPI 기준 기본 폭. 머리글 문구가 더 길면 넓힌다(column_width). */
        int fmt;
    } cols[COL_COUNT] = {
        {48, LVCFMT_RIGHT},  /* 순위 */
        {220, LVCFMT_LEFT},  /* 폴더 */
        {80, LVCFMT_RIGHT},  /* 용량 */
        {0, LVCFMT_RIGHT},   /* 중복 제외: 정밀 스캔일 때만 보인다 */
        {0, LVCFMT_RIGHT},   /* 하드 링크 중복: 정밀 스캔일 때만 보인다 */
        {110, LVCFMT_RIGHT}, /* 변화 */
        {84, LVCFMT_RIGHT},  /* 디스크 사용 */
        {64, LVCFMT_RIGHT},  /* 파일 수 */
        {56, LVCFMT_RIGHT},  /* 비율 */
    };
    static const UiText views[VIEW_COUNT] = {UI_T_VIEW_SIZE,   UI_T_VIEW_ALLOC, UI_T_VIEW_CHANGE,
                                             UI_T_VIEW_UNIQUE, UI_T_VIEW_DUP,
                                             UI_T_VIEW_TREEMAP};
    int i, view_text_w = 0;

    g_scan.path = make_child(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                            IDC_PATH);
    g_scan.browse = make_child(L"BUTTON", ui_text(UI_T_SCAN_BROWSE), BS_PUSHBUTTON | WS_TABSTOP,
                              0, IDC_BROWSE);
    g_scan.scan = make_child(L"BUTTON", ui_text(UI_T_SCAN_START), BS_DEFPUSHBUTTON | WS_TABSTOP,
                            0, IDC_SCAN);
    g_scan.cancel = make_child(L"BUTTON", ui_text(UI_T_SCAN_CANCEL), BS_PUSHBUTTON | WS_TABSTOP,
                              0, IDC_CANCEL);
    /* 정밀 스캔: 하드 링크로 같은 실제 파일을 여러 번 세지 않게 보정한다(조금 느리고 메모리를 더 씀). */
    g_scan.precise = make_child(L"BUTTON", ui_text(UI_T_SCAN_PRECISE),
                               BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_PRECISE);
    g_scan.progress = make_child(PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, IDC_PROGRESS);
    g_scan.status = make_child(L"STATIC", ui_text(UI_T_SCAN_READY),
                              SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, 0, IDC_STATUS);
    g_scan.analysis = make_child(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                                IDC_ANALYSIS);
    g_scan.view = make_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0,
                            IDC_VIEW);
    for (i = 0; i < VIEW_COUNT; i++) {
        int tw = theme_text_width(g_scan.view, g_scan.font, ui_text(views[i]));
        if (tw > view_text_w) {
            view_text_w = tw;
        }
        SendMessageW(g_scan.view, CB_ADDSTRING, 0, (LPARAM)ui_text(views[i]));
    }
    SendMessageW(g_scan.view, CB_SETCURSEL, VIEW_SIZE, 0);
    /* 펼친 목록은 가장 긴 보기 이름이 다 보이게(스크롤 막대·여백 포함). */
    SendMessageW(g_scan.view, CB_SETDROPPEDWIDTH,
                 (WPARAM)(view_text_w + scale(32) > scale(240) ? view_text_w + scale(32)
                                                                : scale(240)),
                 0);
    g_scan.tree = make_child(WC_TREEVIEWW, L"",
                            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                                TVS_SHOWSELALWAYS | WS_TABSTOP,
                            WS_EX_CLIENTEDGE, IDC_TREE);
    g_scan.list = make_child(WC_LISTVIEWW, L"",
                            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                            WS_EX_CLIENTEDGE, IDC_LIST);
    g_scan.treemap = treemap_view_create(g_scan.hwnd, IDC_TREEMAP, g_scan.font, g_scan.dpi);

    SendMessageW(g_scan.path, EM_SETCUEBANNER, TRUE, (LPARAM)ui_text(UI_T_SCAN_PATH_CUE));
    SendMessageW(g_scan.progress, PBM_SETRANGE32, 0, PROGRESS_RANGE);
    ListView_SetExtendedListViewStyle(g_scan.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)ui_text(k_col_text[i]);
        col.cx = cols[i].width > 0 ? column_width(i, cols[i].width) : 0;
        col.fmt = cols[i].fmt;
        SendMessageW(g_scan.list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
    /* 탭 순서가 화면 순서와 같도록 왼쪽 것부터 만든다. */
    g_scan.export_csv = make_child(L"BUTTON", ui_text(UI_T_SCAN_EXPORT_CSV),
                                  BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_EXPORT_CSV);
    g_scan.export_html = make_child(L"BUTTON", ui_text(UI_T_SCAN_EXPORT_HTML),
                                   BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_EXPORT_HTML);
    g_scan.autosave = make_child(L"BUTTON", ui_text(UI_T_SCAN_AUTOSAVE),
                                BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_AUTOSAVE);
    g_scan.clear_history = make_child(L"BUTTON", ui_text(UI_T_SCAN_CLEAR_HISTORY),
                                     BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_CLEAR_HISTORY);

    /* 컨트롤 폭: 예전 고정 폭을 최소로 두고(한국어 화면은 그대로), 문구가 더 길면 넓힌다. */
    g_scan.w_browse = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_BROWSE),
                                        g_scan.dpi, scale(84));
    g_scan.w_scan = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_START),
                                      g_scan.dpi, scale(84));
    g_scan.w_cancel = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_CANCEL),
                                        g_scan.dpi, scale(84));
    g_scan.w_clear = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_CLEAR_HISTORY),
                                       g_scan.dpi, scale(84));
    g_scan.w_export = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_EXPORT_CSV),
                                        g_scan.dpi, scale(104));
    g_scan.w_export = theme_button_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_EXPORT_HTML),
                                        g_scan.dpi, g_scan.w_export);
    g_scan.w_precise = theme_check_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_PRECISE),
                                        g_scan.dpi, scale(160));
    g_scan.w_autosave = theme_check_width(g_scan.hwnd, g_scan.font, ui_text(UI_T_SCAN_AUTOSAVE),
                                         g_scan.dpi, scale(150));
    /* 보기 콤보 상자는 고른 보기 이름이 보통 다 보이는 폭(긴 이름은 펼친 목록에서 다 보임). */
    g_scan.w_view = scale(170);
    {
        /* 저장된 설정을 읽는다(없거나 읽을 수 없으면 켬). */
        wchar_t dir[MAX_PATH * 2];
        g_scan.autosave_on = history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0 ||
                            history_autosave_enabled(dir);
        SendMessageW(g_scan.autosave, BM_SETCHECK,
                     g_scan.autosave_on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    set_scanning(0);
}

static LRESULT on_notify(NMHDR *hdr)
{
    /* 트리에서 고른 폴더가 트리맵의 기준 폴더가 된다(트리맵에서 고른 것을 반영할 때는 제외). */
    if (hdr->idFrom == IDC_TREE && hdr->code == TVN_SELCHANGEDW) {
        NMTREEVIEWW *tv = (NMTREEVIEWW *)hdr;
        if (!g_scan.map_syncing && g_scan.has_model && tv->itemNew.hItem != NULL) {
            treemap_view_set_root(g_scan.treemap, (size_t)tv->itemNew.lParam);
        }
        return 0;
    }
    if (hdr->idFrom == IDC_TREE && hdr->code == TVN_ITEMEXPANDINGW) {
        NMTREEVIEWW *tv = (NMTREEVIEWW *)hdr;
        if (tv->action == TVE_EXPAND && g_scan.has_model) {
            tree_populate(tv->itemNew.hItem, (size_t)tv->itemNew.lParam);
        }
        return 0;
    }
    if (hdr->idFrom == IDC_LIST && (hdr->code == NM_DBLCLK || hdr->code == NM_RETURN)) {
        int row = ListView_GetNextItem(g_scan.list, -1, LVNI_SELECTED);
        if (row >= 0 && g_scan.has_model) {
            LVITEMW lv;
            memset(&lv, 0, sizeof(lv));
            lv.mask = LVIF_PARAM;
            lv.iItem = row;
            SendMessageW(g_scan.list, LVM_GETITEMW, 0, (LPARAM)&lv);
            if ((size_t)lv.lParam < g_scan.model.len) {
                reveal_in_tree((size_t)lv.lParam, 1);
            }
        }
        return 0;
    }
    return 0;
}

/* 진행 중인 스캔을 멈추고 스레드가 끝날 때까지 기다린다(창을 닫을 때). */
static void stop_job(void)
{
    if (g_scan.job != NULL) {
        scan_job_cancel(g_scan.job);
        scan_job_finish(g_scan.job, NULL);
        g_scan.job = NULL;
    }
}

static LRESULT on_command(HWND hwnd, WPARAM wp)
{
    switch (LOWORD(wp)) {
    case IDC_BROWSE:
        browse_folder();
        return 0;
    case IDC_SCAN:
        start_scan();
        return 0;
    case IDC_CANCEL:
        cancel_scan();
        return 0;
    case IDC_VIEW:
        if (HIWORD(wp) == CBN_SELCHANGE) {
            fill_rank();
            show_view_windows();
        }
        return 0;
    case IDC_TREEMAP:
        if (HIWORD(wp) == TMN_SELECT) {
            sync_tree_from_map(treemap_view_selected(g_scan.treemap));
        } else if (HIWORD(wp) == TMN_ROOT) {
            sync_tree_from_map(treemap_view_root(g_scan.treemap));
        }
        return 0;
    case IDC_AUTOSAVE:
        /* BS_AUTOCHECKBOX 가 이미 체크 상태를 바꿨다. 저장에 실패하면 되돌린다. */
        if (HIWORD(wp) == BN_CLICKED) {
            apply_autosave(SendMessageW(g_scan.autosave, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        return 0;
    case IDC_CLEAR_HISTORY:
        clear_history(1);
        return 0;
    case IDC_EXPORT_CSV:
        export_results(EXPORT_KIND_CSV);
        return 0;
    case IDC_EXPORT_HTML:
        export_results(EXPORT_KIND_HTML);
        return 0;
    }
    (void)hwnd;
    return 0;
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCCREATE: {
        const PanelParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        if (g_scan.hwnd != NULL) {
            return FALSE; /* 하나만 */
        }
        memset(&g_scan, 0, sizeof(g_scan));
        g_scan.hwnd = hwnd;
        g_scan.font = params->font;
        g_scan.dpi = params->dpi;
        scan_model_init(&g_scan.model);
        history_diff_init(&g_scan.diff);
        break;
    }
    case WM_CREATE:
        create_controls();
        show_view_windows();
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        /* 입력 칸·콤보 상자 목록(어둡게일 때 SURFACE 색). */
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        return theme_ctlcolor_static((HDC)wp);
    case WM_COMMAND:
        return on_command(hwnd, wp);
    case WM_NOTIFY:
        return on_notify((NMHDR *)lp);
    case WM_APP_SCAN_PROGRESS:
        update_progress();
        return 0;
    case WM_APP_SCAN_DONE:
        if (g_scan.job != NULL) {
            finish_scan();
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), SPN_FINISHED), (LPARAM)hwnd);
        }
        return 0;
    case WM_DESTROY:
        stop_job();
        clear_results();
        return 0;
    case WM_NCDESTROY:
        if (g_scan.hwnd == hwnd) {
            g_scan.hwnd = NULL;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int scan_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = SCAN_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND scan_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    /* WS_EX_CONTROLPARENT: 부모의 IsDialogMessage 가 패널 안의 컨트롤로 Tab 이동하게 한다. */
    return CreateWindowExW(WS_EX_CONTROLPARENT, SCAN_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

static int is_panel(HWND panel)
{
    return panel != NULL && panel == g_scan.hwnd;
}

int scan_panel_start(HWND panel, const wchar_t *target, int precise)
{
    if (!is_panel(panel) || g_scan.job != NULL) {
        return 0;
    }
    if (target != NULL) {
        SetWindowTextW(g_scan.path, target);
    }
    if (precise >= 0) {
        SendMessageW(g_scan.precise, BM_SETCHECK, precise ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    start_scan();
    return g_scan.job != NULL;
}

void scan_panel_cancel(HWND panel)
{
    if (is_panel(panel)) {
        cancel_scan();
    }
}

int scan_panel_busy(HWND panel)
{
    return is_panel(panel) && g_scan.job != NULL;
}

int scan_panel_min_width(HWND panel)
{
    return is_panel(panel) ? min_panel_width() : 0;
}

int scan_panel_autosave(HWND panel)
{
    return is_panel(panel) && g_scan.autosave_on;
}

int scan_panel_set_autosave(HWND panel, int on)
{
    if (!is_panel(panel)) {
        return 0;
    }
    SendMessageW(g_scan.autosave, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return apply_autosave(on);
}

int scan_panel_export_enabled(HWND panel)
{
    return is_panel(panel) &&
           (IsWindowEnabled(g_scan.export_csv) || IsWindowEnabled(g_scan.export_html));
}

int scan_panel_verify(HWND panel, ScanVerifyMode mode, const wchar_t *export_prefix,
                      int export_enabled_before)
{
    int failures = 0;

    SELF_CHECK(is_panel(panel));
    if (!is_panel(panel)) {
        return failures;
    }
    g_scan.self_test = mode;
    g_scan.export_prefix = export_prefix;
    g_scan.export_enabled_before_scan = export_enabled_before;
    SELF_CHECK(mode != SCAN_VERIFY_EXPORT || export_prefix != NULL);
    if (mode == SCAN_VERIFY_EXPORT && export_prefix == NULL) {
        return failures;
    }
    return verify_screen();
}

int scan_panel_verify_lang(HWND panel)
{
    static const UiText views[VIEW_COUNT] = {UI_T_VIEW_SIZE,   UI_T_VIEW_ALLOC, UI_T_VIEW_CHANGE,
                                             UI_T_VIEW_UNIQUE, UI_T_VIEW_DUP,
                                             UI_T_VIEW_TREEMAP};
    const struct {
        HWND h;
        UiText text;
        int check; /* 체크 상자면 1 */
    } controls[] = {
        {g_scan.browse, UI_T_SCAN_BROWSE, 0},       {g_scan.scan, UI_T_SCAN_START, 0},
        {g_scan.cancel, UI_T_SCAN_CANCEL, 0},       {g_scan.precise, UI_T_SCAN_PRECISE, 1},
        {g_scan.export_csv, UI_T_SCAN_EXPORT_CSV, 0},
        {g_scan.export_html, UI_T_SCAN_EXPORT_HTML, 0},
        {g_scan.autosave, UI_T_SCAN_AUTOSAVE, 1},
        {g_scan.clear_history, UI_T_SCAN_CLEAR_HISTORY, 0},
    };
    wchar_t buf[256];
    RECT area, r;
    int failures = 0, i;

    SELF_CHECK(is_panel(panel));
    if (!is_panel(panel)) {
        return failures;
    }
    for (i = 0; i < VIEW_COUNT; i++) {
        buf[0] = L'\0';
        if (SendMessageW(g_scan.view, CB_GETLBTEXTLEN, (WPARAM)i, 0) <
            (LRESULT)(sizeof(buf) / sizeof(buf[0]))) {
            SendMessageW(g_scan.view, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf);
        }
        SELF_CHECK(wcscmp(buf, ui_text(views[i])) == 0);
    }
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = buf;
        col.cchTextMax = (int)(sizeof(buf) / sizeof(buf[0]));
        buf[0] = L'\0';
        SendMessageW(g_scan.list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        SELF_CHECK(wcscmp(buf, ui_text(k_col_text[i])) == 0);
        /* 보이는 열은 머리글 문구가 잘리지 않는다(정밀 스캔 열은 스캔 전이라 숨김). */
        if (i != COL_UNIQUE && i != COL_DUP) {
            SELF_CHECK(ListView_GetColumnWidth(g_scan.list, i) >
                       theme_text_width(g_scan.list, g_scan.font, buf));
        }
    }
    SELF_CHECK(text_is(g_scan.status, ui_text(UI_T_SCAN_READY)));

    /* 단추·체크 상자 문구가 잘리지 않고 패널 안에 있다(호출 전에 최소 창 크기로 배치되어 있어야 함). */
    GetClientRect(panel, &area);
    SELF_CHECK(area.right - area.left >= min_panel_width());
    for (i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
        int need = controls[i].check
                       ? theme_check_width(panel, g_scan.font, ui_text(controls[i].text),
                                           g_scan.dpi, 0)
                       : theme_button_width(panel, g_scan.font, ui_text(controls[i].text),
                                            g_scan.dpi, 0);
        SELF_CHECK(text_is(controls[i].h, ui_text(controls[i].text)));
        GetWindowRect(controls[i].h, &r);
        MapWindowPoints(NULL, panel, (POINT *)&r, 2);
        SELF_CHECK(r.right - r.left >= need);
        SELF_CHECK(r.left >= area.left && r.right <= area.right);
    }
    GetWindowRect(g_scan.view, &r);
    MapWindowPoints(NULL, panel, (POINT *)&r, 2);
    SELF_CHECK(r.right <= area.right);
    return failures;
}
