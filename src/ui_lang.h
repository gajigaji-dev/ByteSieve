#ifndef BYTESIEVE_UI_LANG_H
#define BYTESIEVE_UI_LANG_H

#include <wchar.h>

/*
 * 화면 표시 언어와 번역 문자열.
 *
 * GUI 의 문자열을 언어별 표에서 꺼내 쓴다(외부 라이브러리·리소스 DLL 없음).
 * - 지원 언어: 한국어(기본 원문), 영어, 일본어, 중국어 간체.
 * - 언어 결정 순서(ui_lang_detect): 환경 변수 BYTESIEVE_LANG → 레지스트리
 *   HKCU\Software\ByteSieve 의 Language 값(설치 프로그램이 설치 언어로 기록) → Windows 표시 언어.
 *   지원하지 않는 언어(중국어 번체 포함)는 영어로 한다.
 * - 언어는 프로그램 시작 때 한 번 정한다(ui_lang_set). 실행 중에 바꾸면 이미 만든 창의 글자는 그대로다.
 * - 모든 번역 문자열은 비어 있지 않고, 같은 항목의 서식 지정자(%ls, %u 등)가 언어마다 같은 순서로
 *   들어 있다(test_ui_lang 이 확인). 그래서 어떤 언어의 문자열이든 같은 인자로 swprintf 할 수 있다.
 * - 스레드: ui_lang_set 은 창을 만들기 전에 GUI 스레드에서만 부른다. 이후의 조회는 어느 스레드에서든 안전하다.
 */

typedef enum {
    UI_LANG_KO = 0, /* 한국어(번역 원문) */
    UI_LANG_EN,     /* 영어(지원하지 않는 언어의 대체 언어) */
    UI_LANG_JA,     /* 일본어 */
    UI_LANG_ZH_HANS, /* 중국어 간체 */
    UI_LANG_COUNT
} UiLang;

/* 번역 문자열 번호. 새 항목은 ui_lang.c 의 표에 네 언어를 모두 채운다. */
typedef enum {
    /* 탐색(사이드바 메뉴·페이지 제목) */
    UI_T_NAV_SCAN,
    UI_T_NAV_DRIVES,
    UI_T_NAV_CLEANUP,
    UI_T_NAV_QUARANTINE,
    UI_T_NAV_MONITOR,
    UI_T_NAV_CLEANER,
    UI_T_NAV_BOOSTER,
    UI_T_NAV_CACHE,

    /* 사이드바 그룹 머리글(목적별 메뉴) */
    UI_T_GROUP_ANALYSIS,
    UI_T_GROUP_CLEANUP,
    UI_T_GROUP_CACHE,
    UI_T_GROUP_QUARANTINE,
    UI_T_GROUP_SYSTEM,

    /* 폴더 스캔: 버튼·체크 상자·입력 칸 */
    UI_T_SCAN_BROWSE,
    UI_T_SCAN_START,
    UI_T_SCAN_CANCEL,
    UI_T_SCAN_PRECISE,
    UI_T_SCAN_EXPORT_CSV,
    UI_T_SCAN_EXPORT_HTML,
    UI_T_SCAN_AUTOSAVE,
    UI_T_SCAN_CLEAR_HISTORY,
    UI_T_SCAN_PATH_CUE,
    UI_T_SCAN_READY,

    /* 폴더 스캔: 보기 콤보 상자(VIEW_* 순서) */
    UI_T_VIEW_SIZE,
    UI_T_VIEW_ALLOC,
    UI_T_VIEW_CHANGE,
    UI_T_VIEW_UNIQUE,
    UI_T_VIEW_DUP,
    UI_T_VIEW_TREEMAP,

    /* 폴더 스캔: 순위 목록 열(COL_* 순서) */
    UI_T_COL_RANK,
    UI_T_COL_FOLDER,
    UI_T_COL_SIZE,
    UI_T_COL_UNIQUE,
    UI_T_COL_DUP,
    UI_T_COL_CHANGE,
    UI_T_COL_ALLOC,
    UI_T_COL_FILES,
    UI_T_COL_SHARE,

    /* 트리·순위에 덧붙이는 폴더 상태 표시(앞에 공백 포함) */
    UI_T_FLAG_FAILED,
    UI_T_FLAG_CANCELLED,
    UI_T_FLAG_INCOMPLETE,
    UI_T_FLAG_REPARSE,
    UI_T_FLAG_DEPTH_LIMIT,

    UI_T_COUNT
} UiText;

/* 현재 언어를 정한다. 범위 밖 값은 무시한다. 처음 값은 UI_LANG_KO. */
void ui_lang_set(UiLang lang);
UiLang ui_lang_current(void);

/* 현재 언어의 문자열. 범위 밖 번호는 빈 문자열(L"")을 돌려준다(NULL 아님). */
const wchar_t *ui_text(UiText id);
/* 지정한 언어의 문자열(검사·언어 선택 화면용). 범위 밖이면 빈 문자열. */
const wchar_t *ui_text_in(UiLang lang, UiText id);

/* 언어 식별자(BCP 47: "ko", "en", "ja", "zh-Hans"). 설정 저장·환경 변수·설치 프로그램 연동에 쓴다.
 * 범위 밖이면 "en". */
const wchar_t *ui_lang_name(UiLang lang);
/* 언어 선택 화면에 보일 그 언어 자체의 이름("한국어", "English", "日本語", "简体中文"). */
const wchar_t *ui_lang_native_name(UiLang lang);

/* 언어 태그를 해석한다. 대소문자를 가리지 않고 지역 표기를 허용한다
 * ("ko", "ko-KR", "en-US", "ja_JP", "zh-Hans", "zh-CN", "zh-SG"; "zh" 만 있으면 간체).
 * 지원 언어면 *out 에 넣고 1, 아니면(번체 "zh-TW"·"zh-Hant" 포함) *out 을 건드리지 않고 0. */
int ui_lang_from_tag(const wchar_t *tag, UiLang *out);
/* Windows 언어 번호(LANGID)를 지원 언어로 바꾼다. 지원하지 않으면 UI_LANG_EN. */
UiLang ui_lang_from_langid(unsigned short langid);

/* 위 결정 순서대로 표시 언어를 정한다(값을 읽기만 하고 아무것도 쓰지 않는다).
 * 환경 변수·레지스트리 값이 없거나 해석할 수 없으면 다음 단계로 넘어간다. */
UiLang ui_lang_detect(void);

#endif
