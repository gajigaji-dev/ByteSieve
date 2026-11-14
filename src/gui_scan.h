#ifndef BYTESIEVE_GUI_SCAN_H
#define BYTESIEVE_GUI_SCAN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/*
 * [폴더 스캔] 페이지 패널(자식 창). 예전에는 메인 창(gui_main.c)에 직접 붙어 있던 화면을 다른 페이지와
 * 같은 "패널 창 + *_panel_verify" 형태로 옮겼다. 동작은 그대로다.
 *   - 경로 입력·[찾아보기]·[스캔]·[취소]·[정밀 스캔(하드 링크)]·보기 콤보 상자, 진행 막대, 내보내기
 *     (CSV·HTML), [스캔 기록 자동 저장]·[기록 삭제...], 상태 줄·분석 줄, 폴더 트리, 용량 순위 또는 트리맵.
 *   - 스캔은 scan_job 작업 스레드에서 한다. 끝나면 직전 스냅숏과 비교하고 조건이 맞으면 저장한다
 *     (scan_history 의 history_track: CLI --track 과 같은 함수).
 *   - 내보내기 기본 파일 이름·실패 원인 문구도 Core(scan_export)를 쓴다.
 *   - 기록 삭제는 확인 창(기본 [아니요]) 뒤에만 지운다. 스캔한 폴더와 파일은 건드리지 않는다.
 *
 * 한 프로세스에 하나만 만들 수 있다(두 번째 scan_panel_create 는 NULL).
 */

/* 스캔이 끝나면(완료·취소·실패) 부모에게 WM_COMMAND(MAKEWPARAM(id, SPN_FINISHED), panel). */
#define SPN_FINISHED 1

int scan_panel_register(HINSTANCE inst);
HWND scan_panel_create(HWND parent, int id, HFONT font, int dpi);

/*
 * 스캔을 시작한다. target 이 NULL 이 아니면 경로 칸을 그 값으로 바꾸고, precise 가 0 이상이면
 * [정밀 스캔] 체크 상태를 그 값으로 바꾼 뒤(사용자가 고친 것과 같음) 시작한다. 시작했으면 1.
 * 경로 칸이 비었거나 이미 스캔 중이면 0(상태 줄에 이유를 쓴다).
 */
int scan_panel_start(HWND panel, const wchar_t *target, int precise);
void scan_panel_cancel(HWND panel);
int scan_panel_busy(HWND panel);

/*
 * 자체 검사용. 스캔이 끝난 뒤 직전 기록과 비교·저장하는 작업(작업 스레드)을 ms 만큼 늦추고(0 = 늦추지 않음),
 * 그 작업 중인지 알려 주며, 작업 중 상태(바쁨·움직이는 막대·완료로 보이지 않음·다시 스캔/취소/기록 삭제/
 * 내보내기 막힘)를 확인한다(실패 수).
 */
void scan_panel_set_history_delay(HWND panel, unsigned ms);
int scan_panel_history_pending(HWND panel);
int scan_panel_verify_history_pending(HWND panel);

/* 패널 안 컨트롤이 잘리지 않는 최소 폭(문구 길이에 따라 언어마다 다름). */
int scan_panel_min_width(HWND panel);

/* [스캔 기록 자동 저장] 상태와 바꾸기(체크 상자를 누른 것과 같은 경로, 설정 파일에 저장). 성공 1. */
int scan_panel_autosave(HWND panel);
int scan_panel_set_autosave(HWND panel, int on);

/* 내보내기 단추가 하나라도 켜져 있으면 1. */
int scan_panel_export_enabled(HWND panel);

/*
 * 표시 언어를 바꾼 뒤(ui_lang_set) 부른다. 패널의 모든 문구(단추·체크 상자·입력 칸 안내·보기 목록·열 머리글,
 * 트리·순위 목록·트리맵, 분석 줄, 상태 줄)를 지금 언어로 다시 쓰고, 글꼴을 font 로 바꿔(NULL 이면 그대로)
 * 문구 폭에 맞게 다시 배치한다. 스캔 결과·고른 보기·순위 목록에서 고른 줄은 그대로다. 상태 줄은 마지막에
 * 보인 내용(진행률·결과·안내)을 새 언어로 다시 쓴다. 스캔 중에 불러도 된다(진행률은 다음 알림부터 새 언어).
 * 글꼴은 호출자가 소유하며 패널보다 오래 살아야 한다.
 */
void scan_panel_apply_language(HWND panel, HFONT font);

/* DPI 가 바뀌면(배율이 다른 모니터로 옮김·배율 변경) 크기 계산 기준을 바꾼다. 글꼴·폭·배치는 이어서
 * 부르는 scan_panel_apply_language 가 다시 잡는다. */
void scan_panel_set_dpi(HWND panel, int dpi);

/* ---- 자체 검사 ---- */

typedef enum {
    SCAN_VERIFY_SCAN = 0,      /* 완료된 스캔: 트리·순위·보기·트리맵·분석 줄·기록 상태 */
    SCAN_VERIFY_CANCEL,        /* 시작 직후 취소: 부분 결과·기록하지 않음 */
    SCAN_VERIFY_CHANGES,       /* 직전 스냅숏과 비교: 변화 목록을 stdout 에 쓴다 */
    SCAN_VERIFY_NO_AUTOSAVE,   /* 자동 저장 꺼짐: 저장하지 않았는지 */
    SCAN_VERIFY_CLEAR_HISTORY, /* 기록 삭제(확인 창 없이) → 화면 반영, 지운 개수를 stdout 에 */
    SCAN_VERIFY_EXPORT,        /* CSV·HTML 내보내기(대화 상자 없이), 폴더 수를 stdout 에 */
    SCAN_VERIFY_PRECISE        /* 정밀 스캔: 하드 링크 보정 열·분석 줄, 집계를 stdout 에 */
} ScanVerifyMode;

/*
 * 스캔이 끝난 뒤 화면이 모델과 맞는지 확인한다. export_prefix 는 EXPORT 일 때 내보낼 파일 경로
 * (확장자 제외), export_enabled_before 는 스캔 전에 내보내기 단추가 켜져 있었는지. 실패 수.
 */
int scan_panel_verify(HWND panel, ScanVerifyMode mode, const wchar_t *export_prefix,
                      int export_enabled_before);

/*
 * 자체 검사(--self-test-lang, --self-test-lang-switch): 지금 언어의 번역 문구가 컨트롤·보기·열 머리글에
 * 들어갔는지, 상태 줄·분석 줄·트리·변화 열이 지금 언어로 쓰였는지(한국어가 아니면 경로 밖에 한글이 없는지),
 * 단추·체크 상자 문구가 잘리지 않고 패널 안에 있는지 확인한다. 스캔 중에는 부르지 않는다. 실패 수.
 */
int scan_panel_verify_lang(HWND panel);

#endif /* BYTESIEVE_GUI_SCAN_H */
