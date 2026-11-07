#include "gui_progress.h"

#include <commctrl.h>

/* 상태는 막대의 GWLP_USERDATA 에 ProgressState + 1 로 둔다(0 은 "정한 적 없음"). 앱이 만든 컨트롤의 이 칸은
 * 앱이 쓰는 자리이고 진행 막대 컨트롤은 쓰지 않는다. 창과 함께 사라지므로 따로 지울 필요가 없다. */
/* marquee 한 칸이 움직이는 간격(ms). */
#define PROGRESS_MARQUEE_MS 30

static const wchar_t *const k_text[PROGRESS_STATE_COUNT] = {
    L"대기", L"진행 중", L"진행 중", L"완료", L"취소됨", L"실패"};
static const wchar_t *const k_name[PROGRESS_STATE_COUNT] = {
    L"idle", L"running", L"busy", L"done", L"cancelled", L"failed"};

const wchar_t *progress_state_text(ProgressState state)
{
    return (unsigned)state < (unsigned)PROGRESS_STATE_COUNT ? k_text[state] : L"";
}

const wchar_t *progress_state_name(ProgressState state)
{
    return (unsigned)state < (unsigned)PROGRESS_STATE_COUNT ? k_name[state] : L"unknown";
}

ProgressState progress_state(HWND bar)
{
    LONG_PTR v = GetWindowLongPtrW(bar, GWLP_USERDATA);

    return v > 0 && v <= PROGRESS_STATE_COUNT ? (ProgressState)(v - 1) : PROGRESS_IDLE;
}

/* marquee 를 켜거나 끈다(PBS_MARQUEE 스타일이 있어야 움직인다). */
static void set_marquee(HWND bar, int on)
{
    LONG style = GetWindowLongW(bar, GWL_STYLE);
    int has = (style & PBS_MARQUEE) != 0;

    if (on && !has) {
        SetWindowLongW(bar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessageW(bar, PBM_SETMARQUEE, TRUE, PROGRESS_MARQUEE_MS);
    } else if (!on && has) {
        SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongW(bar, GWL_STYLE, style & ~PBS_MARQUEE);
    }
}

void progress_set(HWND bar, ProgressState state, int pos, int range)
{
    int cur;

    if (bar == NULL || (unsigned)state >= (unsigned)PROGRESS_STATE_COUNT) {
        return;
    }
    if (pos < 0) {
        pos = 0;
    } else if (pos > range) {
        pos = range;
    }
    set_marquee(bar, state == PROGRESS_BUSY);
    switch (state) {
    case PROGRESS_IDLE:
        SendMessageW(bar, PBM_SETSTATE, PBST_NORMAL, 0);
        SendMessageW(bar, PBM_SETPOS, 0, 0);
        break;
    case PROGRESS_RUNNING:
        SendMessageW(bar, PBM_SETSTATE, PBST_NORMAL, 0);
        SendMessageW(bar, PBM_SETPOS, (WPARAM)pos, 0);
        break;
    case PROGRESS_BUSY:
        SendMessageW(bar, PBM_SETSTATE, PBST_NORMAL, 0);
        break;
    case PROGRESS_DONE:
        SendMessageW(bar, PBM_SETSTATE, PBST_NORMAL, 0);
        SendMessageW(bar, PBM_SETPOS, (WPARAM)range, 0);
        break;
    case PROGRESS_CANCELLED:
        /* 멈춘 자리를 보여 준다. 아무것도 못 했으면(0) 비어 사라진 것처럼 보이지 않게 가득 채운다. */
        cur = (int)SendMessageW(bar, PBM_GETPOS, 0, 0);
        SendMessageW(bar, PBM_SETPOS, (WPARAM)(cur > 0 ? cur : range), 0);
        SendMessageW(bar, PBM_SETSTATE, PBST_PAUSED, 0);
        break;
    case PROGRESS_FAILED:
        SendMessageW(bar, PBM_SETPOS, (WPARAM)range, 0);
        SendMessageW(bar, PBM_SETSTATE, PBST_ERROR, 0);
        break;
    default:
        break;
    }
    SetWindowLongPtrW(bar, GWLP_USERDATA, (LONG_PTR)(state + 1));
}
