/* op_result: 끝난 작업의 결과 판정(완료·일부 완료·실패·취소·없음)과 문구·식별자. */
#include <stdio.h>
#include <wchar.h>

#include "op_result.h"

static int g_failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

int main(void)
{
    int r;

    /* 모두 처리 */
    CHECK(op_result_classify(3, 0, 0, 0) == OP_RESULT_SUCCESS);
    /* 일부 실패·일부 건너뜀은 일부 완료(완료로 보이지 않음) */
    CHECK(op_result_classify(3, 1, 0, 0) == OP_RESULT_PARTIAL);
    CHECK(op_result_classify(3, 0, 2, 0) == OP_RESULT_PARTIAL);
    /* 하나도 처리하지 못함 */
    CHECK(op_result_classify(0, 2, 0, 0) == OP_RESULT_FAILED);
    CHECK(op_result_classify(0, 0, 1, 0) == OP_RESULT_FAILED);
    /* 취소는 처리한 것이 있어도·실패가 있어도 취소 */
    CHECK(op_result_classify(5, 0, 0, 1) == OP_RESULT_CANCELLED);
    CHECK(op_result_classify(0, 0, 0, 1) == OP_RESULT_CANCELLED);
    CHECK(op_result_classify(2, 1, 1, 1) == OP_RESULT_CANCELLED);
    /* 처리할 것이 없었음 */
    CHECK(op_result_classify(0, 0, 0, 0) == OP_RESULT_EMPTY);

    /* 상태마다 화면 문구·식별자가 있고 서로 다르다 */
    for (r = 0; r < OP_RESULT_COUNT; r++) {
        int k;
        CHECK(op_result_text((OpResult)r)[0] != L'\0');
        CHECK(wcscmp(op_result_name((OpResult)r), L"unknown") != 0);
        for (k = 0; k < r; k++) {
            CHECK(wcscmp(op_result_name((OpResult)r), op_result_name((OpResult)k)) != 0);
        }
    }
    CHECK(wcscmp(op_result_name(OP_RESULT_SUCCESS), L"success") == 0);
    CHECK(wcscmp(op_result_name(OP_RESULT_CANCELLED), L"cancelled") == 0);
    CHECK(op_result_text(OP_RESULT_COUNT)[0] == L'\0');
    CHECK(wcscmp(op_result_name(OP_RESULT_COUNT), L"unknown") == 0);

    if (g_failures != 0) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("op_result: ok\n");
    return 0;
}
