#ifndef BYTESIEVE_GUI_BOOSTER_H
#define BYTESIEVE_GUI_BOOSTER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/*
 * Booster 패널(자식 창, booster.h):
 *   - 맨 위: 전체 CPU·메모리 사용률(1초마다), 프로세스 수(백그라운드 수)
 *   - 시작 프로그램: 체크 목록(이름·상태·위치·명령). 체크한 항목만 [사용 안 함]/[사용] 으로 바꾼다.
 *     등록을 지우지 않는 작업 관리자 방식이라 언제든 되돌릴 수 있다. 관리자 권한이 필요한 항목은
 *     실패를 그 행에 알린다.
 *   - 프로세스: 체크 목록(이름·PID·앱/백그라운드·CPU·메모리·상태·경로, 머리글을 눌러 정렬,
 *     기본은 메모리 내림차순, [백그라운드만] 필터). 보호 대상(시스템·Windows 구성 요소·ByteSieve 등)은
 *     체크할 수 없다. 체크한 프로세스만 확인 창(기본 [아니요]) 뒤 끝내고 결과를 행에 남긴다.
 *   - CPU 사용률은 목록을 두 번(1초 간격) 읽어 비교한다.
 */

/* 프로세스 목록(CPU 포함)을 새로 채우면 부모에게 WM_COMMAND(MAKEWPARAM(id, BPN_REFRESHED), panel). */
#define BPN_REFRESHED 1

int booster_panel_register(HINSTANCE inst);
HWND booster_panel_create(HWND parent, int id, HFONT font, int dpi);

/* 처음 부르면 측정 타이머를 켜고 목록을 읽는다(이후에는 아무것도 하지 않음). 시작했으면 1. */
int booster_panel_start(HWND panel);
/* 시작 프로그램·프로세스 목록을 다시 읽는다. 시작했으면 1. */
int booster_panel_refresh(HWND panel);

/*
 * 자체 검사: 목록·요약 화면이 데이터와 맞는지, ByteSieve 자신과 보호 대상은 체크할 수 없는지,
 * [모두 선택] 상당 동작이 끝낼 수 있는 프로세스만 고르는지 확인한다(아무것도 바꾸지 않음). 실패 수.
 */
int booster_panel_verify(HWND panel);

#endif /* BYTESIEVE_GUI_BOOSTER_H */
