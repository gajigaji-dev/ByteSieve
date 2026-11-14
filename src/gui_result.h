#ifndef BYTESIEVE_GUI_RESULT_H
#define BYTESIEVE_GUI_RESULT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "op_result.h"
#include "ui_lang.h"

/*
 * 완료 결과 영역. 정리 작업(프로젝트 정리의 휴지통 이동, 시스템 정리·개발 캐시 정리, 격리 폴더 복원·영구 삭제)이
 * 끝나면 진행 막대 아래에 보인다. 작업 중·작업 전에는 숨긴다(진행 막대·상태 줄이 진행을 보인다).
 *
 *   [색 막대] 제목(완료 · 일부 완료 · 완료하지 못함 · 취소됨 · 처리할 항목 없음, op_result 판정)
 *             결과 요약(처리 수·옮긴 용량·실패·건너뜀·취소, 부르는 쪽이 지금 언어로 만든 문장)
 *             [다음 작업 단추 …]                                              [닫기]
 *
 * - 제목·색은 op_result 판정만으로 정한다. 부르는 쪽은 실제 처리 결과 수로 판정해 넘겨야 한다.
 * - 다음 작업 단추를 누르면 부모에게 WM_COMMAND(MAKEWPARAM(action.id, BN_CLICKED), 단추) 를 보낸다.
 *   [닫기]는 영역을 숨긴다. 보이거나 숨기면 부모에게 WM_SIZE 를 보내 다시 배치하게 한다.
 * - 창 문구(GetWindowText)는 제목이다(화면 읽기 프로그램·자체 검사).
 * - 문구는 보일 때의 언어로 만든다. 언어를 바꾸면 부르는 쪽이 result_banner_show 로 다시 보인다.
 * GUI 스레드에서만 부른다.
 */

#define RESULT_BANNER_MAX_ACTIONS 3

typedef struct {
    int id;      /* 부모가 받을 명령 번호 */
    UiText text; /* 단추 문구 */
} ResultAction;

int result_banner_register(HINSTANCE inst);
HWND result_banner_create(HWND parent, int id, HFONT font, int dpi);

/* 결과를 보인다(detail 은 복사한다, actions 는 앞의 RESULT_BANNER_MAX_ACTIONS 개까지). */
void result_banner_show(HWND banner, OpResult result, const wchar_t *detail,
                        const ResultAction *actions, int count);
void result_banner_hide(HWND banner);

/* 보이고 있으면 1. */
int result_banner_visible(HWND banner);
/* 마지막으로 보인 판정(숨겼으면 OP_RESULT_COUNT). */
OpResult result_banner_result(HWND banner);
/* 마지막으로 보인 결과 요약(없으면 ""). */
const wchar_t *result_banner_detail(HWND banner);
/* 다음 작업 단추 수와 i 번째 단추의 명령 번호(없으면 0). */
int result_banner_action_count(HWND banner);
int result_banner_action_id(HWND banner, int i);

/* width 폭에 배치할 때 필요한 높이(숨겼으면 0). */
int result_banner_height(HWND banner, int width);

/* 글꼴·DPI 를 바꾼다(언어·배율 변경). 문구는 바꾸지 않는다. */
void result_banner_set_font(HWND banner, HFONT font);
void result_banner_set_dpi(HWND banner, int dpi);

/* 결과 요약 문장 잇기: ui_text(fmt) 를 인자로 채워 buf 끝에 붙인다(앞 문장이 있으면 공백 하나). */
void result_text_append(wchar_t *buf, size_t len, UiText fmt, ...);

/* 휴지통 폴더를 탐색기로 연다(다음 작업 [휴지통 열기]). */
void result_open_recycle_bin(HWND owner);

#endif /* BYTESIEVE_GUI_RESULT_H */
