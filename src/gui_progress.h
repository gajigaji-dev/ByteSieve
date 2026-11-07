#ifndef BYTESIEVE_GUI_PROGRESS_H
#define BYTESIEVE_GUI_PROGRESS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/*
 * 진행 막대(공용 컨트롤 msctls_progress32)의 상태 표시. 폴더 스캔·프로젝트 정리·시스템 정리(개발 캐시)·
 * 격리 폴더 패널이 같은 규칙으로 쓴다. 막대만 보고도 끝났는지·멈췄는지·실패했는지 알 수 있게 한다.
 *
 *   IDLE       비움(작업 전)
 *   RUNNING    pos 만큼 채움(전체량을 알 때)
 *   BUSY       움직이는 막대(marquee). 전체량을 아직 모르거나 비율로 나타낼 수 없는 단계
 *              (분석 시작 직후, Git 분석, 휴지통 비우기)
 *   DONE       가득, 기본 색
 *   CANCELLED  멈춘 위치 그대로, 노란색(일시 중지 상태). 위치가 0 이면 가득 채워 보이게 한다
 *   FAILED     가득, 빨간색(오류 상태). 일부 항목만 실패해도 이 상태로 둔다
 *
 * 예전에는 실패·취소하면 막대를 0 으로 되돌려 막대가 사라진 것처럼 보였다.
 * 상태는 막대마다(GWLP_USERDATA) 저장하므로 막대가 여러 개여도 서로 섞이지 않는다. GUI 스레드에서만 부른다.
 */

typedef enum {
    PROGRESS_IDLE = 0,
    PROGRESS_RUNNING,
    PROGRESS_BUSY,
    PROGRESS_DONE,
    PROGRESS_CANCELLED,
    PROGRESS_FAILED,
    PROGRESS_STATE_COUNT
} ProgressState;

/* 상태를 바꾼다. range 는 막대의 최대값(PBM_SETRANGE32 로 정한 값), pos 는 RUNNING 일 때만 쓴다. */
void progress_set(HWND bar, ProgressState state, int pos, int range);

/* 마지막으로 정한 상태(정한 적이 없으면 IDLE). */
ProgressState progress_state(HWND bar);

/* 화면용 문구("완료" 등)와 식별자("done" 등). 범위 밖이면 빈 문자열·"unknown". */
const wchar_t *progress_state_text(ProgressState state);
const wchar_t *progress_state_name(ProgressState state);

#endif /* BYTESIEVE_GUI_PROGRESS_H */
