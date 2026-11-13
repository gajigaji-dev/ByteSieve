#ifndef BYTESIEVE_GUI_MONITOR_H
#define BYTESIEVE_GUI_MONITOR_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "sysmon.h"

/*
 * RAM·CPU 실시간 모니터링 패널(자식 창).
 * 상태 줄, RAM·CPU 사용량 문구와 사용률 막대, 최근 60초 사용률 그래프로 이루어진다.
 * 측정은 전용 스레드가 1초마다 하고 최신 값만 패널로 넘기므로 UI 가 멈추지 않는다.
 */

/* 측정값을 화면에 반영할 때마다 부모에게 WM_COMMAND(MAKEWPARAM(id, MPN_SAMPLED), panel) 를 보낸다. */
#define MPN_SAMPLED 1

/* 측정 간격. */
#define MONITOR_INTERVAL_MS 1000

/* 창 클래스를 등록한다. 성공 0. */
int monitor_panel_register(HINSTANCE inst);

/* 패널을 만든다(처음에는 숨김, 측정 안 함). 글꼴은 호출자가 소유하며 패널보다 오래 살아야 한다. */
HWND monitor_panel_create(HWND parent, int id, HFONT font, int dpi);

/* 측정 스레드를 시작한다. 이미 돌고 있으면 아무것도 하지 않는다. 돌고 있으면 1. */
int monitor_panel_start(HWND panel);

/* 지금까지 화면에 반영한 측정 횟수. */
unsigned monitor_panel_samples(HWND panel);

/* 자체 검사: 화면이 최근 측정값과 맞는지, 1초 간격으로 갱신되는지, 측정 실패를 안전하게
 * 표시하는지 확인한다. 실패한 항목 수를 반환한다. */
int monitor_panel_verify(HWND panel);

/*
 * 표시 언어를 바꾼 뒤(ui_lang_set) 부른다. 이름표·상태 줄·RAM/CPU 문구를 마지막 측정값으로 지금 언어로
 * 다시 쓰고(상태 줄의 측정 시각은 그대로), 글꼴을 font 로 바꿔(NULL 이면 그대로) 다시 배치한다. 그래프 제목·
 * 범례는 다시 그릴 때 지금 언어로 그린다. 측정은 계속한다. 글꼴은 호출자가 소유한다.
 */
void monitor_panel_apply_language(HWND panel, HFONT font);

/*
 * 자체 검사: 이름표가 지금 언어이고 잘리지 않는지, 상태 줄·RAM/CPU 문구가 지금 언어로 쓰였는지(한국어가
 * 아니면 한글이 없는지), 그래프 문구도 지금 언어인지 확인한다. 실패 수.
 */
int monitor_panel_verify_lang(HWND panel);

#endif /* BYTESIEVE_GUI_MONITOR_H */
