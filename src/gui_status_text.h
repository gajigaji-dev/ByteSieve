#ifndef BYTESIEVE_GUI_STATUS_TEXT_H
#define BYTESIEVE_GUI_STATUS_TEXT_H

#include <wchar.h>

#include "booster.h"
#include "clean_plan.h"
#include "cleaner.h"
#include "cleanup_trash.h"
#include "dev_cache.h"
#include "op_result.h"
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

/* Cleaner 대상 종류의 이름·설명(cleaner_kind_info 의 name·note 와 같은 내용). */
const wchar_t *gui_cleaner_kind_name(CleanerKind kind);
const wchar_t *gui_cleaner_kind_note(CleanerKind kind);
/* Cleaner 분류·상태(cleaner_group_text·cleaner_status_text 와 같은 분류). */
const wchar_t *gui_cleaner_group_text(CleanerGroup group);
const wchar_t *gui_cleaner_status_text(CleanerStatus status);
/* 정리 실행에서 건너뛴 이유(clean_skip_text 와 같은 분류). */
const wchar_t *gui_clean_skip_text(CleanSkip skip);
/* 개발 캐시의 캐시 이름("패키지 캐시" 등)·설명(dev_cache_kind_info 의 label·note 와 같은 내용). */
const wchar_t *gui_dev_cache_label(DevCacheKind kind);
const wchar_t *gui_dev_cache_note(DevCacheKind kind);
/* 개발 캐시 위치 상태(dev_cache_status_text 와 같은 분류). */
const wchar_t *gui_dev_cache_status_text(DevCacheStatus status);

/* 끝난 작업의 결과 판정(op_result) 제목: 완료·일부 완료·완료하지 못함·취소됨·처리할 항목 없음. */
const wchar_t *gui_op_result_text(OpResult result);
/* 개발 캐시 위치 근거("환경 변수 X" 등, dev_cache_source_text 와 같은 형식). */
void gui_dev_cache_source_text(DevCacheSource source, const wchar_t *origin, wchar_t *buf, size_t len);
/* Booster 시작 프로그램 위치·보호 이유·끝내기 결과(booster_source_text·booster_protect_text·
 * booster_end_text 와 같은 분류). */
const wchar_t *gui_booster_source_text(BoosterStartupSource source);
const wchar_t *gui_booster_protect_text(BoosterProtect protect);
const wchar_t *gui_booster_end_text(BoosterEndStatus status);
/*
 * 드라이브 BitLocker 상태(drive_encryption_text 와 같은 분류, 범위 밖은 "알 수 없음"). enc 는 DriveEncryption 값.
 * drive.h 의 DriveType 이 <shlobj.h> 의 DriveType 함수와 이름이 겹쳐 이 헤더에서는 drive.h 를 넣지 않는다.
 */
const wchar_t *gui_drive_encryption_text(int enc);

/*
 * 자체 검사: 모든 상태에 지금 언어 문구가 있고(PENDING 제외), 지금 언어가 한국어면 core 문구와 같은지.
 * 실패 수(실패마다 stderr 에 한 줄).
 */
int gui_status_text_verify(void);

#endif /* BYTESIEVE_GUI_STATUS_TEXT_H */
