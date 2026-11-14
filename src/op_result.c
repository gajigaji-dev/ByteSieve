#include "op_result.h"

OpResult op_result_classify(size_t done, size_t failed, size_t skipped, int cancelled)
{
    if (cancelled) {
        return OP_RESULT_CANCELLED;
    }
    if (failed == 0 && skipped == 0) {
        return done > 0 ? OP_RESULT_SUCCESS : OP_RESULT_EMPTY;
    }
    return done > 0 ? OP_RESULT_PARTIAL : OP_RESULT_FAILED;
}

const wchar_t *op_result_text(OpResult result)
{
    switch (result) {
    case OP_RESULT_SUCCESS:
        return L"완료";
    case OP_RESULT_PARTIAL:
        return L"일부 완료";
    case OP_RESULT_FAILED:
        return L"완료하지 못함";
    case OP_RESULT_CANCELLED:
        return L"취소됨";
    case OP_RESULT_EMPTY:
        return L"처리할 항목 없음";
    default:
        return L"";
    }
}

const wchar_t *op_result_name(OpResult result)
{
    switch (result) {
    case OP_RESULT_SUCCESS:
        return L"success";
    case OP_RESULT_PARTIAL:
        return L"partial";
    case OP_RESULT_FAILED:
        return L"failed";
    case OP_RESULT_CANCELLED:
        return L"cancelled";
    case OP_RESULT_EMPTY:
        return L"empty";
    default:
        return L"unknown";
    }
}
