#ifndef BYTESIEVE_GUI_CLEANER_H
#define BYTESIEVE_GUI_CLEANER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>

#include "cleaner.h"

/*
 * Cleaner 패널(자식 창): Windows 임시 파일·오류 보고서·휴지통·브라우저 캐시와
 * 공용 개발 캐시(npm·pip·Gradle·Cargo·NuGet)·가상 디스크를 분석해 대상마다 실제 확보 용량
 * (할당 크기, 하드 링크로 다른 곳과 나누는 파일 제외)·파일 크기·파일 수·상태(정리 가능, 프로그램 실행
 * 중, 최근 항목 제외, 캐시 위치 문제 등)·위치 근거를 체크 목록으로 보여 주고, 사용자가 체크하고
 * 확인한 대상만 정리한다(cleaner.h).
 *   - GUI 는 이 패널을 둘 만든다: [시스템 정리] 페이지(CLEANER_KINDS_SYSTEM)와 [개발 캐시] 페이지
 *     (CLEANER_KINDS_CACHE). 분석 범위만 다르고 선택 정책·안전 장치·확인 창은 같다(cleaner_panel_set_kinds).
 *   - 분석·정리는 작업 스레드에서 한다. 분석은 읽기 전용이다.
 *   - [모두 선택]은 휴지통과 개발 도구 캐시를 고르지 않는다(cleaner_target_manual_only). 휴지통
 *     비우기는 영구 삭제이고, 개발 도구 캐시는 여러 프로젝트가 함께 써서 지우면 다시 내려받아야 하므로
 *     사용자가 행을 직접 체크해야 한다.
 *   - 정리 뒤 오프라인 빌드가 안 되는 개발 캐시(cleaner_target_caution)는 상태 칸에 "정리 가능 · 주의",
 *     설명 칸에 [주의], 선택 요약에 "주의 n개", 확인 창에 [주의]와 경고 문단으로 알린다.
 *   - 정리 전 확인 창(기본 [아니요]). 휴지통이 골라져 있으면 영구 삭제를 따로 한 번 더 묻는다
 *     ([예] 비우고 진행, [아니요] 휴지통은 빼고 진행, [취소] 아무것도 하지 않음, 기본 [아니요]).
 *   - 휴지통 외의 항목은 휴지통(없는 드라이브는 격리 폴더)으로 옮기며 영구 삭제하지 않는다.
 *   - 끝나면 대상마다 결과(옮긴 수·확보 용량·건너뜀·실패)를 보여 주고, 실패한 항목을 알린다.
 */

/* 분석이 끝나면 부모에게 WM_COMMAND(MAKEWPARAM(id, CLN_ANALYZED), panel). */
#define CLN_ANALYZED 1
/* 정리가 끝나면 부모에게 WM_COMMAND(MAKEWPARAM(id, CLN_CLEANED), panel). */
#define CLN_CLEANED 2
/* 완료 결과 영역의 [격리 폴더 보기]를 누르면 부모에게 WM_COMMAND(MAKEWPARAM(id, CLN_OPEN_QUARANTINE), panel). */
#define CLN_OPEN_QUARANTINE 3

int cleaner_panel_register(HINSTANCE inst);
HWND cleaner_panel_create(HWND parent, int id, HFONT font, int dpi);

/*
 * 분석 범위(CleanerOps.kinds)를 정한다. 0 이면 모든 종류(기본값). GUI 는 패널을 둘 만들어
 * [시스템 정리](CLEANER_KINDS_SYSTEM)와 [개발 캐시](CLEANER_KINDS_CACHE) 페이지로 나눈다.
 * 분석·정리 중에는 바꾸지 않는다(무시). 다음 분석부터 적용된다.
 */
void cleaner_panel_set_kinds(HWND panel, unsigned kinds);
unsigned cleaner_panel_kinds(HWND panel);

/* 분석을 시작한다. 시작했으면 1. */
int cleaner_panel_refresh(HWND panel);
/* 한 번이라도 분석을 시작했으면 1. */
int cleaner_panel_loaded(HWND panel);
int cleaner_panel_busy(HWND panel);
void cleaner_panel_cancel(HWND panel);

/*
 * 정리할 수 있는 대상을 모두 체크하거나(check=1, 휴지통·개발 도구 캐시는 건드리지 않음) 모두 해제한다
 * (check=0, 직접 체크한 대상 포함). 체크한 대상 수를 반환한다.
 */
size_t cleaner_panel_check_all(HWND panel, int check);

/* kind 인 대상 행을 사용자가 직접 체크·해제한 것처럼 바꾼다(정리할 수 없는 행은 체크되지 않음).
 * 체크한 대상 수(전체)를 반환한다. */
size_t cleaner_panel_check_kind(HWND panel, CleanerKind kind, int check);

/* 체크한 대상의 정리를 시작한다. confirm 이 1 이면 확인 창을 띄운다. 시작했으면 1. */
int cleaner_panel_run(HWND panel, int confirm);

/*
 * 자체 검사: 분석 결과 화면이 목록과 맞는지, 분석 범위 밖 종류의 행이 없는지, (시스템 범위면) 휴지통
 * 행이 하나 있는지, 휴지통·개발 도구 캐시가 [모두 선택]으로 체크되지 않는지, 주의 대상의 상태 칸·선택 요약이 맞는지(주의 대상마다 stdout 에 "caution: <이름>"),
 * 파일 시스템 대상(임시 파일·브라우저·개발 캐시)의 위치가 모두 expect_root 아래인지 확인한다
 * (아니면 실제 사용자 파일을 볼 수 있으므로 정리하면 안 된다). 실패 수를 반환한다.
 */
int cleaner_panel_verify(HWND panel, const wchar_t *expect_root);

/* 자체 검사: 정리 뒤 옮긴 항목은 원래 자리에 없고 결과 표시가 맞는지, 휴지통은 비우지 않았는지. */
int cleaner_panel_verify_run(HWND panel, size_t expect_targets);

/*
 * 표시 언어를 바꾼 뒤(ui_lang_set) 부른다. 단추·안내·열 머리글·선택 줄·상세 설명·상태 줄을 지금 언어로 다시
 * 쓰고, 글꼴을 font 로 바꿔(NULL 이면 그대로) 문구 폭에 맞게 다시 배치한다. 목록 칸(대상 이름·분류·상태·
 * 제외·증감·위치·근거)은 그릴 때 지금 언어로 만들므로 다시 그리기만 한다. core 의 대상 이름·설명·상태는
 * gui_status_text 로 지금 언어 문구를 쓴다. 분석 결과·선택은 그대로다. 분석·정리 중에 불러도 된다(진행 문구는
 * 다음 알림부터 새 언어). 글꼴은 호출자가 소유한다.
 */
void cleaner_panel_apply_language(HWND panel, HFONT font);

/* DPI 가 바뀌면(배율이 다른 모니터로 옮김·배율 변경) 크기 계산 기준을 바꾼다. 글꼴·폭·배치는 이어서
 * 부르는 cleaner_panel_apply_language 가 다시 잡는다. */
void cleaner_panel_set_dpi(HWND panel, int dpi);

/* 단추 줄이 잘리지 않는 최소 폭(최소 창 크기에 쓴다). */
int cleaner_panel_min_width(HWND panel);
/* 목록이 접히지 않는 높이(픽셀, 마지막 배치의 폭 기준: 안내 문단·완료 결과 영역 포함). 0 이면 아직 배치 전. */
int cleaner_panel_min_height(HWND panel);

/*
 * 자체 검사: 단추·안내·열 머리글이 지금 언어이고 잘리지 않는지, 상태 줄·선택 줄·상세 설명과 목록의 번역 칸이
 * 지금 언어로 쓰였는지(한국어가 아니면 한글이 없는지, 경로·배포판 이름·Windows 오류 설명은 제외), core 문구
 * 연결(gui_status_text_verify, 한국어는 core 문구와 같음)이 맞는지 확인한다. 분석·정리 중에는 부르지 않는다.
 * 실패 수.
 */
int cleaner_panel_verify_lang(HWND panel);

#endif /* BYTESIEVE_GUI_CLEANER_H */
