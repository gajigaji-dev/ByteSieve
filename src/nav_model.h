#ifndef BYTESIEVE_NAV_MODEL_H
#define BYTESIEVE_NAV_MODEL_H

#include <stddef.h>

#include "ui_lang.h"

/*
 * GUI 사이드바 메뉴 모델(순수 계산, 창·GDI 를 쓰지 않음). 화면 그리기는 gui_nav 가 한다.
 *
 * 메뉴는 목적별 그룹으로 묶는다. 행(row)은 그룹 머리글과 페이지 항목을 위에서부터 늘어놓은 것이다.
 *   프로젝트 분석   폴더 스캔 · 드라이브
 *   정리            프로젝트 정리 · 시스템 정리
 *   캐시            개발 캐시
 *   격리·복원       격리 폴더
 *   시스템          모니터 · 부스터
 *
 * 계약
 *   - 모든 페이지는 정확히 한 번, 자기 그룹 머리글 아래에 나온다. 첫 행은 머리글이다.
 *   - 머리글은 고를 수 없다. 키 이동·클릭은 항상 페이지 항목에 멈춘다.
 *   - 키 이동은 끝에서 멈춘다(돌아가지 않음).
 */

typedef enum {
    NAV_PAGE_SCAN = 0,   /* 폴더 스캔 */
    NAV_PAGE_DRIVES,     /* 드라이브 */
    NAV_PAGE_CLEANUP,    /* 프로젝트 정리(Git 분석 포함) */
    NAV_PAGE_CLEANER,    /* 시스템 정리(임시 파일·오류 보고서·휴지통·브라우저) */
    NAV_PAGE_CACHE,      /* 개발 캐시(npm·pip·Gradle…·WSL/Docker 가상 디스크) */
    NAV_PAGE_QUARANTINE, /* 격리 폴더(복원·영구 삭제) */
    NAV_PAGE_MONITOR,    /* 모니터(RAM·CPU) */
    NAV_PAGE_BOOSTER,    /* 부스터(시작 프로그램·프로세스) */
    NAV_PAGE_COUNT
} NavPage;

typedef enum {
    NAV_GROUP_ANALYSIS = 0, /* 프로젝트 분석 */
    NAV_GROUP_CLEANUP,      /* 정리 */
    NAV_GROUP_CACHE,        /* 캐시 */
    NAV_GROUP_QUARANTINE,   /* 격리·복원 */
    NAV_GROUP_SYSTEM,       /* 시스템 */
    NAV_GROUP_COUNT
} NavGroup;

typedef enum {
    NAV_ROW_HEADER = 0, /* 그룹 머리글(고를 수 없음) */
    NAV_ROW_PAGE        /* 페이지 항목 */
} NavRowKind;

typedef struct {
    NavRowKind kind;
    NavGroup group;
    NavPage page; /* kind 가 NAV_ROW_PAGE 일 때만 의미가 있다 */
} NavRow;

typedef enum {
    NAV_KEY_UP = 0,
    NAV_KEY_DOWN,
    NAV_KEY_HOME,
    NAV_KEY_END
} NavKey;

/* 행 수와 행. 범위 밖이면 NULL. */
size_t nav_row_count(void);
const NavRow *nav_row(size_t index);

/* 페이지의 행 번호(범위 밖 페이지면 nav_row_count()). */
size_t nav_row_of_page(NavPage page);

/* 페이지가 속한 그룹(범위 밖이면 NAV_GROUP_COUNT). */
NavGroup nav_page_group(NavPage page);

/* 화면 문구 번호(ui_text 로 꺼냄). 페이지 문구는 사이드바 항목과 페이지 제목에 함께 쓴다. */
UiText nav_page_text(NavPage page);
UiText nav_group_text(NavGroup group);

/* 식별자 이름("scan", "cache" 등, 자체 검사 출력용). 범위 밖이면 "unknown". */
const wchar_t *nav_page_name(NavPage page);

/* current 에서 key 로 옮긴 페이지(머리글은 건너뜀, 끝에서 멈춤). current 가 범위 밖이면 첫 페이지. */
NavPage nav_key_move(NavPage current, NavKey key);

/*
 * 세로 배치. 위 여백 top 다음에 행을 차례로 놓는다. 머리글 행 높이 header_h, 항목 행 높이 item_h.
 * 둘째 그룹부터는 머리글 위에 group_gap 만큼 띄운다.
 */
typedef struct {
    int top;
    int header_h;
    int item_h;
    int group_gap;
} NavMetrics;

/* 행의 위·아래 y(아래는 포함하지 않음). 범위 밖이면 0. */
int nav_row_span(const NavMetrics *m, size_t index, int *y0, int *y1);

/* 모든 행을 놓은 뒤의 아래 끝 y. */
int nav_total_height(const NavMetrics *m);

/* y 에 있는 페이지 항목을 *out 에. 머리글·간격·범위 밖이면 0. */
int nav_hit_test(const NavMetrics *m, int y, NavPage *out);

#endif /* BYTESIEVE_NAV_MODEL_H */
