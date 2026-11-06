#ifndef BYTESIEVE_GUI_TREEMAP_H
#define BYTESIEVE_GUI_TREEMAP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>

#include "scan_model.h"

/*
 * 트리맵 보기(자식 창). 스캔 모델의 한 폴더(기준 폴더)를 사각형 하나로 보고, 하위 폴더를
 * 용량(total_bytes, 정밀 스캔이어도 경로마다 센 기본 용량)에 비례하는 사각형으로 2단계까지
 * 나눠 그린다(배치는 treemap.h). 폴더 바로 아래 파일은 "파일" 블록 하나로 묶고, 한 단계의
 * 하위 폴더가 TREEMAP_MAX_ITEMS 개를 넘으면 작은 것들을 "그 밖의 n개" 블록 하나로 묶는다.
 *   - 마우스를 올리면 경로·용량·비율을 도구 설명으로 보여 준다.
 *   - 클릭: 그 폴더를 고른다(파일·묶음 블록이면 그것을 가진 폴더). 부모에게 TMN_SELECT.
 *   - 더블클릭·Enter: 고른 폴더로 확대. Backspace: 상위 폴더로. 부모에게 TMN_ROOT.
 *   - 우클릭 메뉴: 확대, 상위 폴더로, 탐색기에서 열기, 경로 복사, 색 구분.
 * 읽기 전용이며 파일을 바꾸지 않는다. 모델은 부모가 소유하며 바뀌거나 해제되기 전에
 * treemap_view_set_model 로 새 모델(또는 NULL)을 알려야 한다.
 */

/* 한 단계에 따로 그리는 하위 폴더 수의 상한(나머지는 묶음 블록). */
#define TREEMAP_MAX_ITEMS 300

/* 부모에게 WM_COMMAND(MAKEWPARAM(id, code), view) 로 알린다(사용자 조작일 때만). */
#define TMN_SELECT 1 /* 고른 폴더가 바뀜(treemap_view_selected) */
#define TMN_ROOT 2   /* 기준 폴더가 바뀜(treemap_view_root) */

typedef enum {
    TREEMAP_COLOR_DEPTH = 0, /* 단계별 색 */
    TREEMAP_COLOR_TOP,       /* 기준 폴더 바로 아래 폴더마다 다른 색(그 안은 같은 계열) */
    TREEMAP_COLOR_CLEANUP,   /* 이름이 정리 후보(node_modules·빌드·캐시·임시)인 폴더와 그 안을 종류별 색 */
    TREEMAP_COLOR_COUNT
} TreemapColorMode;

/* 메뉴용 이름("깊이" 등). */
const wchar_t *treemap_color_text(TreemapColorMode mode);

/* 식별자 이름("depth" 등). */
const wchar_t *treemap_color_name(TreemapColorMode mode);

/* 창 클래스를 등록한다. 성공 0. */
int treemap_view_register(HINSTANCE inst);

/* 트리맵 창을 만든다(처음에는 숨김). 글꼴은 호출자가 소유하며 창보다 오래 살아야 한다. */
HWND treemap_view_create(HWND parent, int id, HFONT font, int dpi);

/* 모델을 바꾼다(NULL 이면 비움). 기준 폴더는 루트(0), 고른 폴더는 없음으로 돌아간다. */
void treemap_view_set_model(HWND view, const ScanModel *model);

/* 기준 폴더를 바꾼다(범위 밖이면 무시). 부모에게 알리지 않는다. */
void treemap_view_set_root(HWND view, size_t node);
size_t treemap_view_root(HWND view);

/* 고른 폴더를 바꾼다(SCAN_NODE_NONE 이면 없음). 부모에게 알리지 않는다. */
void treemap_view_set_selected(HWND view, size_t node);
size_t treemap_view_selected(HWND view);

void treemap_view_set_color(HWND view, TreemapColorMode mode);
TreemapColorMode treemap_view_color(HWND view);

/*
 * 자체 검사용: node 를 나타내는 1단계 블록에서 클릭하면 그 폴더가 골라지는 점(클라이언트 좌표).
 * 그런 블록이 없거나 너무 작으면 0.
 */
int treemap_view_block_point(HWND view, size_t node, POINT *pt);

/*
 * 자체 검사: 블록이 그리는 영역 안에 있고 1단계 블록끼리 겹치지 않으며, 넓이가 용량에
 * 비례하고, 2단계 블록이 제 1단계 블록 안에 있으며, 묶음·파일 블록을 포함해 용량 합이 기준
 * 폴더와 맞는지, 색 구분 기준마다 그리기와 정리 후보 색이 맞는지 확인한다. 실패 수를 반환한다.
 */
int treemap_view_verify(HWND view);

#endif /* BYTESIEVE_GUI_TREEMAP_H */
