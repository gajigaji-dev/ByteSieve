#ifndef BYTESIEVE_GUI_STATUS_TEXT_H
#define BYTESIEVE_GUI_STATUS_TEXT_H

#include <wchar.h>

#include "cleanup_trash.h"
#include "project_info.h"

/*
 * core 상태 번호 → 화면 문구(지금 표시 언어, ui_lang 표).
 *
 * core 의 *_status_text·*_text 함수는 한국어 문구를 돌려주고 CLI·테스트가 그대로 쓴다. GUI 는 core 를
 * 바꾸지 않고 여기서 같은 분류를 번역 표에 연결한다. 한국어 결과는 core 문구와 글자까지 같다
 * (gui_status_text_verify 가 확인). 범위 밖 값은 빈 문자열(NULL 아님).
 */

/* 휴지통 이동 결과·보호 이유(cleanup_trash_status_text 와 같은 분류). PENDING 은 빈 문자열. */
const wchar_t *gui_trash_status_text(CleanupTrashStatus status);

/* 지운 뒤 복구 가능성(project_recovery_text 와 같은 분류). */
const wchar_t *gui_recovery_text(ProjectRecovery recovery);

/*
 * 자체 검사: 모든 상태에 지금 언어 문구가 있고(PENDING 제외), 지금 언어가 한국어면 core 문구와 같은지.
 * 실패 수(실패마다 stderr 에 한 줄).
 */
int gui_status_text_verify(void);

#endif /* BYTESIEVE_GUI_STATUS_TEXT_H */
