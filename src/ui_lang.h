#ifndef BYTESIEVE_UI_LANG_H
#define BYTESIEVE_UI_LANG_H

#include <wchar.h>

/*
 * 화면 표시 언어와 번역 문자열.
 *
 * GUI 의 문자열을 언어별 표에서 꺼내 쓴다(외부 라이브러리·리소스 DLL 없음).
 * - 지원 언어: 한국어(기본 원문), 영어, 일본어, 중국어 간체.
 * - 언어 결정 순서(ui_lang_detect): 환경 변수 BYTESIEVE_LANG → 레지스트리
 *   HKCU\Software\ByteSieve 의 Language 값(설치 프로그램이 설치 언어로 기록, GUI [설정] 페이지가 바꿈.
 *   환경 변수 BYTESIEVE_SETTINGS_KEY 가 있으면 그 키) → Windows 표시 언어.
 *   지원하지 않는 언어(중국어 번체 포함)는 영어로 한다.
 * - 언어는 프로그램 시작 때 정하고(ui_lang_set), [설정]에서 바꾸면 실행 중에도 바꾼다. ui_lang_set 은
 *   현재 언어만 바꾸므로, 이미 만든 창의 글자는 그 창이 다시 써야 바뀐다(gui_main 의 언어 적용이
 *   번역된 페이지에 알린다). 문구는 쓸 때마다 ui_text 로 가져오고 저장해 두지 않는다.
 * - 모든 번역 문자열은 비어 있지 않고, 같은 항목의 서식 지정자(%ls, %u 등)가 언어마다 같은 순서로
 *   들어 있다(test_ui_lang 이 확인). 그래서 어떤 언어의 문자열이든 같은 인자로 swprintf 할 수 있다.
 *   한국어가 아닌 문자열에는 한글이 없다(번역 누락으로 한국어를 그대로 넣지 않았는지 test_ui_lang 이 확인).
 * - 스레드: ui_lang_set 은 GUI 스레드에서만 부른다. 조회는 어느 스레드에서든 안전하다(문자열 표는
 *   바뀌지 않고, 언어를 바꾸는 중에 다른 스레드가 조회하면 이전 언어나 새 언어 중 하나를 얻는다).
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
    UI_T_NAV_SETTINGS,

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

    /* 설정 페이지 */
    UI_T_SET_APPEARANCE,
    UI_T_SET_THEME,
    UI_T_THEME_SYSTEM, /* 테마 콤보 상자(ThemeMode 순서) */
    UI_T_THEME_LIGHT,
    UI_T_THEME_DARK,
    UI_T_SET_THEME_HC,
    UI_T_SET_LANGUAGE,
    UI_T_SET_LANG_LABEL,
    UI_T_LANG_SYSTEM, /* %ls: Windows 표시 언어의 이름 */
    UI_T_SET_LANG_RESTART,
    UI_T_SET_LANG_ENV, /* %ls: 환경 변수 값의 언어 이름 */
    UI_T_SET_HISTORY,
    UI_T_SET_HISTORY_DIR, /* %ls: 기록 폴더 */
    UI_T_SET_OPEN_DIR,
    UI_T_SET_SAVE_FAILED, /* %lu: Windows 오류 */
    UI_T_SET_AUTOSAVE_FAILED,
    UI_T_SET_ABOUT,
    UI_T_SET_VERSION, /* %ls: 버전(1.0.0) */

    /* 폴더 스캔: 상태 줄(진행·결과·안내) */
    UI_T_SCAN_NEED_PATH,
    UI_T_SCAN_START_FAILED,
    UI_T_SCAN_STARTING,
    UI_T_SCAN_STARTING_PRECISE,
    UI_T_SCAN_CANCELLING,
    UI_T_SCAN_PROGRESS,         /* %llu 파일, %llu 폴더, %ls 용량, %llu 오류, %ls 지금 폴더 */
    UI_T_SCAN_PROGRESS_PRECISE, /* 위 + %llu 중복 수, %ls 중복 용량(오류 앞) */
    UI_T_SCAN_HEAD_DONE,        /* 결과 머리말(UI_T_SCAN_FINISHED 의 첫 %ls) */
    UI_T_SCAN_HEAD_PARTIAL,
    UI_T_SCAN_HEAD_CANCELLED,
    UI_T_SCAN_HEAD_FAILED,
    UI_T_SCAN_FINISHED,         /* %ls 머리말, %llu 파일, %llu 폴더, %ls 용량, %llu 오류, %llu 건너뜀, %.1f 초 */
    UI_T_SCAN_FINISHED_PRECISE, /* UI_T_SCAN_FINISHED 와 같은 인자(정밀 스캔 결과) */
    UI_T_SCAN_FATAL,            /* %ls 머리말, %ls 오류 종류(식별자) */
    UI_T_SCAN_AUTOSAVE_ON,
    UI_T_SCAN_AUTOSAVE_OFF,
    UI_T_SCAN_AUTOSAVE_SAVE_FAILED, /* %lu: Windows 오류 */
    UI_T_SCAN_HISTORY_NO_DIR,
    UI_T_SCAN_HISTORY_EMPTY,
    UI_T_SCAN_HISTORY_CLEAR_CANCELLED,
    UI_T_SCAN_HISTORY_CLEARED,      /* %u: 지운 수 */
    UI_T_SCAN_HISTORY_READ_FAILED,  /* %lu: Windows 오류 */
    UI_T_SCAN_HISTORY_PARTIAL,      /* %u 지운 수, %u 못 지운 수, %lu Windows 오류 */
    UI_T_SCAN_EXPORT_WHAT_HTML,     /* 내보내기 형식 이름(CSV 는 번역하지 않음) */
    UI_T_SCAN_EXPORTED,             /* %ls 형식, %u 폴더 수, %ls 파일 */
    UI_T_SCAN_EXPORT_FAILED,        /* %ls 형식, %lu Windows 오류, %ls 이유 */
    UI_T_SCAN_NOTHING_TO_EXPORT,
    UI_T_SCAN_DIALOG_FAILED,        /* %lx: 공용 대화 상자 오류 */

    /* 폴더 스캔: 확인·알림 창, 파일·폴더 고르기 대화 상자 */
    UI_T_SCAN_CLEAR_TITLE,
    UI_T_SCAN_CLEAR_CONFIRM,     /* %u 기록 수, %ls 기록 폴더 */
    UI_T_SCAN_EXPORT_FAILED_TITLE,
    UI_T_SCAN_EXPORT_FAILED_MSG, /* %ls 형식, %ls 이유, %lu Windows 오류, %ls 파일 */
    UI_T_SCAN_CSV_FILTER,
    UI_T_SCAN_HTML_FILTER,
    UI_T_SCAN_ALL_FILTER,
    UI_T_SCAN_CSV_TITLE,
    UI_T_SCAN_HTML_TITLE,
    UI_T_SCAN_BROWSE_TITLE,

    /* 내보내기 실패 이유(export_error_text 와 같은 분류) */
    UI_T_EXPORT_ERR_ACCESS,
    UI_T_EXPORT_ERR_IN_USE,
    UI_T_EXPORT_ERR_DISK_FULL,
    UI_T_EXPORT_ERR_PATH,
    UI_T_EXPORT_ERR_MEMORY,
    UI_T_EXPORT_ERR_NO_RESULT,
    UI_T_EXPORT_ERR_OTHER,

    /* 폴더 스캔: 분석 줄(디스크 사용량 분석·직전 스캔 대비 변화). " · " 로 시작하는 것은 앞 항목에 이어 붙는다. */
    UI_T_AN_DISK,         /* %ls 디스크 사용, %ls 논리 크기 대비 차이 */
    UI_T_AN_PRECISE_NONE,
    UI_T_AN_PRECISE,      /* %ls 중복 제외, %ls 기본, %llu 중복 수, %ls 중복 용량 */
    UI_T_AN_REAL_DISK,    /* %ls */
    UI_T_AN_UNKNOWN_ID,   /* %llu */
    UI_T_AN_SLACK,        /* %ls */
    UI_T_AN_COMPRESSED,   /* %ls 절약, %llu 파일 수 */
    UI_T_AN_SPARSE,       /* 위와 같음 */
    UI_T_AN_CLOUD,        /* 위와 같음 */
    UI_T_AN_OTHER,        /* 위와 같음 */
    UI_T_AN_CHANGES,      /* %02u 월, %02u 일, %02u 시, %02u 분, %ls 변화, %ls 이전, %ls 지금, %u×4 늘어남·줄어듦·새·삭제 */
    UI_T_AN_HISTORY_CLEARED,
    UI_T_AN_BASELINE,
    UI_T_AN_CANCELLED,    /* %ls: 앞 항목이 있으면 " · " */
    UI_T_AN_SAVE_FAILED,  /* %ls 위와 같음, %lu Windows 오류 */
    UI_T_AN_AUTOSAVE_OFF, /* %ls 위와 같음 */

    /* 폴더 스캔: 트리·순위 목록 칸 */
    UI_T_TREE_UNIQUE,     /* %ls: 중복 제외 용량(앞에 " · ") */
    UI_T_LIST_DUP,        /* %ls 중복 용량, %llu 중복 수 */
    UI_T_CHANGE_ADDED,    /* %ls 변화량, %ls 불확실 표시(" (?)" 또는 "") */
    UI_T_CHANGE_REMOVED,  /* 위와 같음 */

    /* 폴더 스캔: 트리맵(그림·안내·풍선 도움말·우클릭 메뉴) */
    UI_T_MAP_COLOR_DEPTH, /* 색 구분(TreemapColorMode 순서) */
    UI_T_MAP_COLOR_TOP,
    UI_T_MAP_COLOR_CLEANUP,
    UI_T_MAP_CAT_BUILD,   /* 정리 후보 종류(범례·풍선 도움말) */
    UI_T_MAP_CAT_CACHE,
    UI_T_MAP_CAT_TEMP,
    UI_T_MAP_FILES,       /* %llu: 바로 아래 파일 묶음 */
    UI_T_MAP_OTHER,       /* %llu: 작은 폴더 묶음 */
    UI_T_MAP_HINT,
    UI_T_MAP_ROOT_INFO,   /* %ls 용량, %llu 하위 폴더 수 */
    UI_T_MAP_EMPTY,
    UI_T_MAP_NO_CHILDREN,
    UI_T_MAP_TIP_RATIO,   /* %ls 용량, %.1f 기준 폴더 대비, %.1f 전체 대비 */
    UI_T_MAP_TIP_FILES,   /* %ls 폴더, %llu 파일 수, %ls 비율 줄 */
    UI_T_MAP_TIP_OTHER,   /* %ls 폴더, %llu 폴더 수, %ls 비율 줄 */
    UI_T_MAP_TIP_NODE,    /* %ls 경로, %ls 비율 줄, %llu 하위 폴더, %llu 파일 */
    UI_T_MAP_TIP_CLEANUP, /* %ls: 정리 후보 종류 */
    UI_T_MAP_TIP_INCOMPLETE,
    UI_T_MAP_MENU_ZOOM,
    UI_T_MAP_MENU_UP,
    UI_T_MAP_MENU_OPEN,
    UI_T_MAP_MENU_COPY,
    UI_T_MAP_MENU_COLOR,

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
