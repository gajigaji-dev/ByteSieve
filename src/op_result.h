#ifndef BYTESIEVE_OP_RESULT_H
#define BYTESIEVE_OP_RESULT_H

#include <stddef.h>
#include <wchar.h>

/*
 * 끝난 작업(휴지통 이동·정리 실행·격리 항목 복원·영구 삭제)의 결과 판정. 화면(GUI 완료 결과 영역)이 이 판정으로
 * 제목과 색을 정하므로, 실제 처리 결과 수만으로 정하고 끝나지 않은 작업을 "완료"로 보이지 않게 한다.
 *
 *   CANCELLED  사용자가 취소했다(일부를 처리했어도). 남은 항목은 처리하지 않았다
 *   EMPTY      처리할 항목이 없었다(처리·실패·건너뜀 모두 0)
 *   SUCCESS    모든 항목을 처리했다(실패·건너뜀 0, 처리 1 이상)
 *   PARTIAL    일부만 처리했다(처리 1 이상, 실패 또는 건너뜀 1 이상)
 *   FAILED     하나도 처리하지 못했다(처리 0, 실패 또는 건너뜀 1 이상)
 *
 * 건너뜀(최근에 바뀜·프로그램 실행 중·같은 이름 등)도 처리하지 못한 것이므로 SUCCESS 가 되지 않는다.
 */
typedef enum {
    OP_RESULT_SUCCESS = 0,
    OP_RESULT_PARTIAL,
    OP_RESULT_FAILED,
    OP_RESULT_CANCELLED,
    OP_RESULT_EMPTY,
    OP_RESULT_COUNT
} OpResult;

/* done: 처리한 수, failed: 실패 수, skipped: 건너뛴 수, cancelled: 취소했으면 0 이 아님. */
OpResult op_result_classify(size_t done, size_t failed, size_t skipped, int cancelled);

/* 화면용 문구(한국어, CLI·로그용. GUI 는 gui_op_result_text 로 지금 언어 문구)와 식별자. 범위 밖이면 ""·"unknown". */
const wchar_t *op_result_text(OpResult result);
const wchar_t *op_result_name(OpResult result);

#endif /* BYTESIEVE_OP_RESULT_H */
