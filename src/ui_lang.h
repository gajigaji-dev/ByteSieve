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

    /* 격리 폴더: 단추·열 머리글·안내 */
    UI_T_Q_REFRESH,
    UI_T_Q_RESTORE,
    UI_T_Q_PURGE,
    UI_T_Q_CANCEL,
    UI_T_Q_OPEN,
    UI_T_Q_INFO,
    UI_T_Q_COL_STATE,
    UI_T_Q_COL_SIZE,
    UI_T_Q_COL_ORIGINAL,
    UI_T_Q_COL_TIME,
    UI_T_Q_COL_LOCATION,
    UI_T_Q_COL_RESULT,
    UI_T_Q_STATE_OK,      /* 항목 상태(QuarantineEntryState) */
    UI_T_Q_STATE_MISSING,
    UI_T_Q_STATE_ORPHAN,
    UI_T_Q_UNKNOWN,       /* 원래 위치를 모를 때 */
    UI_T_Q_SUMMARY,       /* %u 격리 항목 수, %ls 용량 */
    UI_T_Q_SUMMARY_CHECK, /* 위 + %u 확인 필요 수 */

    /* 격리 폴더: 상태 줄 */
    UI_T_Q_UNREADABLE,      /* %u: 읽지 못한 드라이브 수 */
    UI_T_Q_SELECT_ITEMS,
    UI_T_Q_EMPTY,
    UI_T_Q_SELECT_RESTORE,
    UI_T_Q_RESTORED,        /* %u 성공, %u 같은 이름 건너뜀, %u 실패, %u 복원할 수 없음 */
    UI_T_Q_SELECT_PURGE,
    UI_T_Q_PURGE_CANCELLED, /* 확인 창에서 취소 */
    UI_T_Q_PURGE_NO_MEMORY,
    UI_T_Q_PURGE_NO_THREAD,
    UI_T_Q_PURGING,         /* %u 지금 항목, %u 전체, %llu 지운 수, %ls 지운 용량, %ls 경로 */
    UI_T_Q_STOPPING,
    UI_T_Q_PURGE_STOPPED,   /* %u 성공, %u 취소, %u 실패 */
    UI_T_Q_PURGED,          /* %u 성공, %u 실패 */

    /* 격리 폴더: 결과 열(항목별 메모) */
    UI_T_Q_HEAD_RESTORE_FAILED, /* 결과 머리말("머리말: 상태 - 시스템 설명") */
    UI_T_Q_HEAD_ALT_FAILED,
    UI_T_Q_HEAD_PURGE_FAILED,
    UI_T_Q_NOTE_NO_ORIGIN,
    UI_T_Q_NOTE_NO_FILE,
    UI_T_Q_NOTE_SKIPPED,
    UI_T_Q_NOTE_CANCEL_PARTIAL,
    UI_T_Q_NOTE_CANCEL_NONE,
    UI_T_Q_ST_OK,           /* 작업 결과(QuarantineStatus 순서, quarantine_status_text 와 같은 분류) */
    UI_T_Q_ST_MISSING,
    UI_T_Q_ST_CONFLICT,
    UI_T_Q_ST_ACCESS_DENIED,
    UI_T_Q_ST_IN_USE,
    UI_T_Q_ST_NO_SPACE,
    UI_T_Q_ST_BAD_STORE,
    UI_T_Q_ST_OTHER_VOLUME,
    UI_T_Q_ST_FAILED,
    UI_T_Q_ST_CANCELLED,

    /* 격리 폴더: 확인·결과 창 */
    UI_T_Q_MORE,               /* %u: 목록에 다 싣지 못한 수 */
    UI_T_Q_CONFLICT_TITLE,
    UI_T_Q_CONFLICT_HEAD,      /* %u: 같은 이름 때문에 복원하지 않은 수 */
    UI_T_Q_CONFLICT_TAIL,      /* %ls: 다른 이름 접미사(파일 이름이라 번역하지 않음, " (복원됨)") */
    UI_T_Q_RESTORE_RESULT_TITLE,
    UI_T_Q_RESTORE_RESULT_HEAD,
    UI_T_Q_PURGE_TITLE,
    UI_T_Q_PURGE_CONFIRM_HEAD,    /* %u 항목 수, %ls 기록된 용량 */
    UI_T_Q_PURGE_CONFIRM_WARN,
    UI_T_Q_PURGE_CONFIRM_ORPHANS, /* %u: 기록 없는 항목 수 */
    UI_T_Q_PURGE_CONFIRM_ASK,
    UI_T_Q_PURGE_FINAL_TITLE,
    UI_T_Q_PURGE_FINAL,           /* %u 항목 수, %ls 용량 */
    UI_T_Q_PURGE_RESULT_TITLE,
    UI_T_Q_PURGE_RESULT_HEAD,

    /* core 상태 문구(gui_status_text 가 상태 번호를 연결): 휴지통 이동 결과(CleanupTrashStatus 순서, PENDING 제외) */
    UI_T_TRASH_MOVED,
    UI_T_TRASH_QUARANTINED,
    UI_T_TRASH_DENY_PATH,
    UI_T_TRASH_DENY_OUTSIDE,
    UI_T_TRASH_DENY_PROTECTED,
    UI_T_TRASH_DENY_CRITICAL,
    UI_T_TRASH_DENY_INCOMPLETE,
    UI_T_TRASH_DENY_DRIVE,
    UI_T_TRASH_MISSING,
    UI_T_TRASH_CHANGED,
    UI_T_TRASH_NO_RECYCLE,
    UI_T_TRASH_FAILED,
    UI_T_TRASH_QUARANTINE_FAILED,
    UI_T_TRASH_CANCELLED,
    /* 지운 뒤 복구 가능성(ProjectRecovery 순서) */
    UI_T_RECOVER_NONE,
    UI_T_RECOVER_REBUILD,
    UI_T_RECOVER_LOCKED,
    UI_T_RECOVER_UNLOCKED,

    /* 프로젝트 정리: 단추·입력 칸·필터·열 머리글 */
    UI_T_C_DETECT,
    UI_T_C_PATH_CUE,
    UI_T_C_READY,
    UI_T_C_FILTER_LABEL,
    UI_T_C_ECO_LABEL,
    UI_T_C_CHECK_ALL,
    UI_T_C_CHECK_NONE,
    UI_T_C_TRASH,
    UI_T_C_ALL,           /* 필터 "전체" */
    UI_T_C_FILTER_BUILD,  /* 유형 필터(설명 포함) */
    UI_T_C_FILTER_CACHE,
    UI_T_C_FILTER_TEMP,
    UI_T_C_FILTER_LOG,
    UI_T_C_CAT_BUILD,     /* 유형 열 */
    UI_T_C_CAT_CACHE,
    UI_T_C_CAT_TEMP,
    UI_T_C_CAT_LOG,
    UI_T_C_ECO_GENERIC,   /* 개발 환경 "일반" */
    UI_T_C_COL_RECLAIM,
    UI_T_C_COL_SIZE,
    UI_T_C_COL_TYPE,
    UI_T_C_COL_ECO,
    UI_T_C_COL_PATH,
    UI_T_C_COL_NOTE,
    UI_T_C_COL_RECOVERY,
    UI_T_C_COL_PROJECT,
    UI_T_C_COL_ACTIVITY,
    UI_T_C_COL_CLEAN,

    /* 프로젝트 정리: 목록 칸(비고·마지막 활동·정리) */
    UI_T_C_NOTE_SYSTEM,
    UI_T_C_NOTE_CONFIRMED,
    UI_T_C_NOTE_GUESSED,
    UI_T_C_NOTE_INCOMPLETE,
    UI_T_C_NOTE_SHARED,
    UI_T_C_NOTE_DIRTY,
    UI_T_C_NOTE_UNPUSHED,
    UI_T_C_NOTE_DORMANT,
    UI_T_C_ACTIVITY_UNKNOWN,
    UI_T_C_ACTIVITY,          /* %04u-%02u-%02u 날짜, %u 며칠 전, %ls " · 휴면" 또는 "" */
    UI_T_C_CLEAN_FAILED,      /* %ls 이유, %08X 오류 */
    UI_T_C_CLEAN_MOVE_FAILED, /* 이유를 모를 때 */
    UI_T_C_CLEAN_QFAILED,     /* %ls 상태, %ls 이유, %08X 오류 */
    UI_T_C_UNKNOWN_ERROR,
    UI_T_C_CLEAN_SKIPPED,     /* %ls 상태 */
    UI_T_C_CLEAN_PROTECTED,   /* %ls 보호 이유 */

    /* 프로젝트 정리: 요약·선택·경고 줄 */
    UI_T_C_SUMMARY,      /* %u 표시, %ls 표시 용량, %u 전체, %ls 실제 확보, %ls 파일 크기 */
    UI_T_C_SEL,          /* %u 선택, %ls 실제 확보 */
    UI_T_C_SEL_MOVED,    /* 위 + %u 옮김, %ls 옮긴 용량 */
    UI_T_C_SEL_MOVED_Q,  /* 위 + %u 격리 폴더 */
    UI_T_C_WARN_PROTECTED,
    UI_T_C_WARN_INFO,

    /* 프로젝트 정리: 상태 줄 */
    UI_T_C_NEED_PATH,
    UI_T_C_START_FAILED,
    UI_T_C_DETECTING,
    UI_T_C_PROGRESS,      /* %llu 폴더, %llu 파일, %u 후보, %ls 용량, %llu 오류, %ls 지금 폴더 */
    UI_T_C_GIT_PROGRESS,  /* %u 끝난 프로젝트, %u 전체, %u 후보 */
    UI_T_C_HEAD_CANCELLED,
    UI_T_C_HEAD_FAILED,
    UI_T_C_FINISHED,      /* %ls 머리말, %llu 폴더, %llu 파일, %llu 오류, %llu 건너뜀, %llu 제외, %llu 링크, %.1f 초 */
    UI_T_C_CANCELLING_TRASH,
    UI_T_C_NOTHING_CHECKED,
    UI_T_C_TRASH_CANCELLED,
    UI_T_C_TRASH_START_FAILED,
    UI_T_C_TRASH_PROGRESS, /* %u 끝난 수, %u 전체, %u 옮김, %ls 용량, %u 격리, %u 실패 */
    UI_T_C_TRASHED,        /* %u 성공, %ls 확보, %u 실패, %u 취소, %.1f 초 */
    UI_T_C_TRASHED_Q,      /* 위 + %u 격리 폴더 */

    /* 프로젝트 정리: 확인·결과 창, 폴더 고르기 */
    UI_T_C_CONFIRM_HEAD,     /* %u 항목 수, %ls 실제 확보 */
    UI_T_C_CONFIRM_INFO,
    UI_T_C_CONFIRM_GUESSED,  /* %u */
    UI_T_C_CONFIRM_DIRTY,    /* %u */
    UI_T_C_CONFIRM_UNPUSHED, /* %u */
    UI_T_C_TRASH_TITLE,
    UI_T_C_FAILURES_HEAD,    /* %u: 옮기지 못한 수 */
    UI_T_C_FAILURES_TITLE,
    UI_T_C_BROWSE_TITLE,

    /* core 상태 문구: Cleaner 대상 종류 이름·설명(CleanerKind 순서).
     * 설명(UI_T_K_NOTE_*·UI_T_D_NOTE_*)은 서식 문자열이 아니라 그대로 보이는 문단이다("%TEMP%" 같은 글자가
     * 들어 있어 swprintf 의 서식으로 쓰면 안 됨, "%ls" 인자로만 쓴다). test_ui_lang 은 이 범위를 서식 비교에서 뺀다. */
    UI_T_K_NAME_USER_TEMP,
    UI_T_K_NAME_WER,
    UI_T_K_NAME_RECYCLE,
    UI_T_K_NAME_CHROME,
    UI_T_K_NAME_EDGE,
    UI_T_K_NAME_FIREFOX,
    UI_T_K_NAME_DEV,
    UI_T_K_NAME_VDISK,
    UI_T_K_NOTE_USER_TEMP,
    UI_T_K_NOTE_WER,
    UI_T_K_NOTE_RECYCLE,
    UI_T_K_NOTE_CHROME,
    UI_T_K_NOTE_EDGE,
    UI_T_K_NOTE_FIREFOX,
    UI_T_K_NOTE_DEV,
    UI_T_K_NOTE_VDISK,
    UI_T_K_GROUP_WINDOWS, /* CleanerGroup 순서 */
    UI_T_K_GROUP_RECYCLE,
    UI_T_K_GROUP_BROWSER,
    UI_T_K_GROUP_DEV,
    UI_T_K_GROUP_VDISK,
    UI_T_K_ST_READY, /* CleanerStatus 순서 */
    UI_T_K_ST_EMPTY,
    UI_T_K_ST_NOT_FOUND,
    UI_T_K_ST_APP_RUNNING,
    UI_T_K_ST_DENIED,
    UI_T_K_ST_ERROR,
    UI_T_K_ST_CANCELLED,
    UI_T_K_ST_INFO_ONLY,
    UI_T_K_PROFILES,   /* %d: 브라우저 프로필 수 */
    UI_T_K_ALL_DRIVES, /* 휴지통 위치 */
    UI_T_SKIP_NONE,    /* CleanSkip 순서 */
    UI_T_SKIP_RECENT,
    UI_T_SKIP_APP_RUNNING,
    /* core 상태 문구: 개발 캐시 이름·설명(DevCacheKind 순서), 위치 상태, 위치 근거 */
    UI_T_D_LABEL_NPM,
    UI_T_D_LABEL_PIP,
    UI_T_D_LABEL_GRADLE,
    UI_T_D_LABEL_GRADLE_WRAPPER,
    UI_T_D_LABEL_CARGO_REGISTRY,
    UI_T_D_LABEL_CARGO_GIT,
    UI_T_D_LABEL_NUGET_PACKAGES,
    UI_T_D_LABEL_NUGET_HTTP,
    UI_T_D_LABEL_PNPM,
    UI_T_D_LABEL_YARN,
    UI_T_D_LABEL_YARN_BERRY,
    UI_T_D_LABEL_MAVEN,
    UI_T_D_LABEL_GO_MOD,
    UI_T_D_LABEL_GO_BUILD,
    UI_T_D_LABEL_CONDA,
    UI_T_D_NOTE_NPM,
    UI_T_D_NOTE_PIP,
    UI_T_D_NOTE_GRADLE,
    UI_T_D_NOTE_GRADLE_WRAPPER,
    UI_T_D_NOTE_CARGO_REGISTRY,
    UI_T_D_NOTE_CARGO_GIT,
    UI_T_D_NOTE_NUGET_PACKAGES,
    UI_T_D_NOTE_NUGET_HTTP,
    UI_T_D_NOTE_PNPM,
    UI_T_D_NOTE_YARN,
    UI_T_D_NOTE_YARN_BERRY,
    UI_T_D_NOTE_MAVEN,
    UI_T_D_NOTE_GO_MOD,
    UI_T_D_NOTE_GO_BUILD,
    UI_T_D_NOTE_CONDA,
    UI_T_D_ST_FOUND, /* DevCacheStatus 순서 */
    UI_T_D_ST_NOT_FOUND,
    UI_T_D_ST_NOT_DIR,
    UI_T_D_ST_LINK,
    UI_T_D_ST_UNRECOGNIZED,
    UI_T_D_ST_NO_PATH,
    UI_T_D_SRC_DEFAULT, /* DevCacheSource 순서 */
    UI_T_D_SRC_ENV,     /* %ls: 환경 변수 이름 */
    UI_T_D_SRC_CONFIG,  /* %ls: 설정 파일 경로 */

    /* 시스템 정리·개발 캐시(Cleaner 패널): 단추·열 머리글·안내 */
    UI_T_CL_REFRESH,
    UI_T_CL_RUN,
    UI_T_CL_COL_TARGET,
    UI_T_CL_COL_GROUP,
    UI_T_CL_COL_GROWTH,
    UI_T_CL_COL_EXCLUDED,
    UI_T_CL_COL_LOCATION,
    UI_T_CL_COL_SOURCE,
    UI_T_CL_INFO_SYSTEM,
    UI_T_CL_INFO_CACHE,
    UI_T_CL_INFO_ALL,

    /* Cleaner: 목록 칸(상태·제외·증감) */
    UI_T_CL_STATE_EMPTIED,      /* %ls: 비운 용량 */
    UI_T_CL_STATE_EMPTY_FAILED, /* %ls 이유, %08X 오류 */
    UI_T_CL_ERROR_WORD,
    UI_T_CL_STATE_DONE,         /* %u 옮김, %ls 용량, %ls 격리 표시, %ls " ·" 또는 "" */
    UI_T_CL_PART_QUARANTINE,
    UI_T_CL_PART_SKIPPED,       /* %u */
    UI_T_CL_PART_FAILED,        /* %u */
    UI_T_CL_PART_CANCELLED,     /* %u */
    UI_T_CL_STATE_PERMANENT,
    UI_T_CL_STATE_CAUTION,
    UI_T_CL_EXCL_RECENT,        /* %u 수, %ls 용량 */
    UI_T_CL_EXCL_DENIED,        /* %ls 앞 구분, %u 수 */
    UI_T_CL_GROWTH_FIRST,
    UI_T_CL_GROWTH_SAME,        /* %llu 며칠 */
    UI_T_CL_GROWTH_DELTA,       /* %lc 부호, %ls 용량, %llu 며칠 */

    /* Cleaner: 선택 줄·상세 설명 */
    UI_T_CL_SELECTION,     /* %u 선택, %ls 실제 확보, %ls 파일 크기, %ls 휴지통 비우기 표시 */
    UI_T_CL_SEL_RECYCLE,
    UI_T_CL_SEL_CAUTION,   /* %u */
    UI_T_CL_DETAIL_HINT,
    UI_T_CL_DETAIL_SOURCE, /* " · 위치 근거: " (뒤에 근거) */
    UI_T_CL_DETAIL_SHARED, /* %ls: 하드 링크로 나누는 용량 */
    UI_T_CL_DETAIL_VDISK,  /* %ls 설명, %ls 위치, %ls 실제 점유, %ls 스파스 표시, %ls 줄이는 방법 */
    UI_T_CL_UNKNOWN,
    UI_T_CL_SPARSE,
    UI_T_CL_DETAIL,        /* %ls 주의 표시, %ls 설명, %ls 위치, %ls 근거, %ls 상태, %ls 제외 머리, %ls 제외, %ls 하드 링크 */
    UI_T_CL_CAUTION_TAG,
    UI_T_CL_EXCL_LABEL,

    /* Cleaner: 상태 줄 */
    UI_T_CL_START_FAILED,
    UI_T_CL_ANALYZING,
    UI_T_CL_SCANNING,       /* %ls 대상, %u 지금, %u 전체, %ls 지금까지 용량 */
    UI_T_CL_NO_MEMORY,
    UI_T_CL_SCANNED,        /* %ls 머리말, %u 대상, %ls 실제 확보, %.1f 초 */
    UI_T_CL_SCAN_DONE_HEAD,
    UI_T_CL_SCAN_CANCELLED_HEAD,
    UI_T_CL_CANCELLING_RUN,
    UI_T_CL_NOTHING_CHECKED,
    UI_T_CL_RUN_CANCELLED,
    UI_T_CL_NOTHING_TO_RUN,
    UI_T_CL_RUN_START_FAILED,
    UI_T_CL_RUNNING_START,
    UI_T_CL_EMPTYING,
    UI_T_CL_RUNNING,        /* %u 끝남, %u 전체, %u 옮김, %ls 용량, %u 건너뜀·실패 */
    UI_T_CL_RAN,            /* %u 옮김, %ls 용량, %ls 휴지통 결과, %u 건너뜀, %u 실패, %ls 취소 표시, %.1f 초 */
    UI_T_CL_RAN_RECYCLE_DONE,   /* %ls */
    UI_T_CL_RAN_RECYCLE_FAILED,
    UI_T_CL_RAN_CANCELLED,

    /* Cleaner: 확인·결과 창 */
    UI_T_CL_CONFIRM_HEAD,        /* %u 대상, %ls 실제 확보, %ls 휴지통 비우기 표시 */
    UI_T_CL_CONFIRM_RECYCLE,
    UI_T_CL_TAG_PERMANENT,
    UI_T_CL_TAG_CAUTION,
    UI_T_CL_CONFIRM_DEV,         /* %u */
    UI_T_CL_CONFIRM_DEV_CAUTION, /* %u */
    UI_T_CL_CONFIRM_INFO,        /* 안내와 "계속할까요?" */
    UI_T_CL_RECYCLE_CONFIRM,     /* %llu 항목 수, %ls 용량 */
    UI_T_CL_RECYCLE_TITLE,
    UI_T_CL_FAILURES_HEAD,       /* %u */
    UI_T_CL_FAILURES_RECYCLE,    /* %ls 이유, %08X 오류 */
    UI_T_CL_MOVE_FAILED,
    UI_T_CL_RESULT_TITLE,

    /* 부스터 페이지: 단추·안내·열 머리글 */
    UI_T_B_REFRESH,
    UI_T_B_START_LABEL,
    UI_T_B_DISABLE,
    UI_T_B_ENABLE,
    UI_T_B_PROC_LABEL,
    UI_T_B_BACKGROUND_ONLY,
    UI_T_B_END,
    UI_T_B_COL_NAME,
    UI_T_B_COL_STATE,
    UI_T_B_COL_SOURCE,
    UI_T_B_COL_COMMAND,
    UI_T_B_COL_RESULT,
    UI_T_B_COL_PID,
    UI_T_B_COL_KIND,
    UI_T_B_COL_CPU,
    UI_T_B_COL_MEMORY,
    UI_T_B_COL_PATH,
    /* 부스터: 요약·안내 줄과 칸 */
    UI_T_B_MEASURING,
    UI_T_B_SUMMARY,          /* %ls CPU, %ls 사용, %ls 전체, %u 사용률, %u 프로세스, %u 백그라운드 */
    UI_T_B_SUMMARY_MEM_FAIL, /* %ls CPU */
    UI_T_B_START_INFO,       /* %u 전체, %u 사용, %u 선택, %ls 읽기 실패 표시 */
    UI_T_B_START_READ_ERRORS,
    UI_T_B_APPLIED_ON,       /* %u, %ls 일부 실패 표시 */
    UI_T_B_APPLIED_OFF,      /* %u, %ls 일부 실패 표시 */
    UI_T_B_PARTIAL_FAIL,
    UI_T_B_RES_CHANGED,
    UI_T_B_RES_DENIED,
    UI_T_B_RES_FAILED,       /* %lu */
    UI_T_B_RES_NEEDS_ADMIN,
    UI_T_B_STATE_ON,
    UI_T_B_STATE_OFF,
    UI_T_B_KIND_APP,
    UI_T_B_KIND_BACKGROUND,
    UI_T_B_PROC_INFO,        /* %u 선택, %ls 메모리 */
    UI_T_B_PROC_READING,
    UI_T_B_END_RESULT,       /* %u 끝냄, %ls 메모리, %u 실패 */
    UI_T_B_STATE_ERROR,      /* %ls 결과, %lu 오류 */
    UI_T_B_PROTECTED,        /* %ls 보호 이유 */
    /* 부스터: 끝내기 확인 창 */
    UI_T_B_CONFIRM_HEAD,     /* %u, %ls 메모리 */
    UI_T_B_CONFIRM_MORE,     /* %u */
    UI_T_B_CONFIRM_WARN,     /* 안내와 "계속할까요?" */
    UI_T_B_CONFIRM_TITLE,
    /* core 상태 문구: 시작 프로그램 위치(BoosterStartupSource 순서) */
    UI_T_B_SRC_RUN_USER,
    UI_T_B_SRC_RUN_MACHINE,
    UI_T_B_SRC_RUN_MACHINE32,
    UI_T_B_SRC_FOLDER_USER,
    UI_T_B_SRC_FOLDER_COMMON,
    /* core 상태 문구: 보호 이유(BoosterProtect 순서) */
    UI_T_B_PROT_NONE,
    UI_T_B_PROT_SYSTEM,
    UI_T_B_PROT_SESSION,
    UI_T_B_PROT_CRITICAL,
    UI_T_B_PROT_WINDOWS,
    UI_T_B_PROT_SELF,
    UI_T_B_PROT_NO_ACCESS,
    /* core 상태 문구: 끝내기 결과(BoosterEndStatus 순서) */
    UI_T_B_END_OK,
    UI_T_B_END_PROTECTED,
    UI_T_B_END_GONE,
    UI_T_B_END_CHANGED,
    UI_T_B_END_DENIED,
    UI_T_B_END_FAILED,

    /* 드라이브 페이지 */
    UI_T_DR_REFRESH,
    UI_T_DR_HINT,
    UI_T_DR_LOADING,
    UI_T_DR_LIST_FAILED,     /* %lu */
    UI_T_DR_SUMMARY,         /* %u 드라이브, %ls 전체, %ls 사용, %ls 여유, %u 실패, %u/%u 암호화, %02u:%02u:%02u */
    UI_T_DR_START_NOMEM,
    UI_T_DR_START_NOTHREAD,
    UI_T_DR_COL_ROOT,
    UI_T_DR_COL_LABEL,
    UI_T_DR_COL_TYPE,
    UI_T_DR_COL_FS,
    UI_T_DR_COL_ENCRYPTION,
    UI_T_DR_COL_TOTAL,
    UI_T_DR_COL_USED,
    UI_T_DR_COL_FREE,
    UI_T_DR_COL_USAGE,
    UI_T_DR_TYPE_REMOVABLE,
    UI_T_DR_TYPE_FIXED,
    UI_T_DR_TYPE_REMOTE,
    UI_T_DR_TYPE_CDROM,
    UI_T_DR_TYPE_RAMDISK,
    UI_T_DR_TYPE_NO_ROOT,
    UI_T_DR_TYPE_UNKNOWN,
    UI_T_DR_ST_OK,
    UI_T_DR_ST_NOT_READY,
    UI_T_DR_ST_ACCESS_DENIED,
    UI_T_DR_ST_MISSING,
    UI_T_DR_ST_DISCONNECTED,
    UI_T_DR_ST_FAILED,
    UI_T_DR_ENC_RAW,         /* %ls 상태, %ld 원래 값 */
    UI_T_DR_LOCKED,
    UI_T_DR_STATUS_ERROR,    /* %ls 상태, %lu 오류 */
    /* core 상태 문구: BitLocker 상태(DriveEncryption 순서) */
    UI_T_DR_ENC_NOT_QUERIED,
    UI_T_DR_ENC_NONE,
    UI_T_DR_ENC_OFF,
    UI_T_DR_ENC_ON,
    UI_T_DR_ENC_LOCKED,
    UI_T_DR_ENC_ENCRYPTING,
    UI_T_DR_ENC_DECRYPTING,
    UI_T_DR_ENC_SUSPENDED,
    UI_T_DR_ENC_PENDING,
    UI_T_DR_ENC_UNKNOWN,
    UI_T_DR_ENC_FAILED,

    /* 모니터 페이지 */
    UI_T_M_HINT,
    UI_T_M_STARTING,
    UI_T_M_SUMMARY,          /* %u 초, %02u:%02u:%02u, %ls 일부 실패 표시 */
    UI_T_M_PARTIAL,
    UI_T_M_START_NOEVENT,
    UI_T_M_START_NOTHREAD,
    UI_T_M_RAM_TITLE,
    UI_T_M_MEM_TEXT,         /* %ls 사용, %ls 전체, %ls 사용 가능, %u.%u 사용률 */
    UI_T_M_FAILED,           /* %lu */
    UI_T_M_MEM_INVALID,
    UI_T_M_CPU_TEXT,         /* %u.%u 사용률, %u 논리 프로세서 */
    UI_T_M_CPU_USAGE,        /* %u.%u */
    UI_T_M_MEASURING,
    UI_T_M_CPU_INVALID,
    UI_T_M_GRAPH_TITLE,
    UI_T_M_LEGEND_MEMORY,

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
/*
 * 지원 언어의 Windows 언어 번호(LANGID): ko-KR, en-US, ja-JP, zh-CN. 범위 밖이면 en-US.
 * Windows 가 만드는 글자(시스템 오류 설명 FormatMessage, 확인 창 단추)를 앱 언어에 맞출 때 쓴다.
 * ui_lang_from_langid(ui_lang_langid(x)) == x.
 */
unsigned short ui_lang_langid(UiLang lang);

/* 위 결정 순서대로 표시 언어를 정한다(값을 읽기만 하고 아무것도 쓰지 않는다).
 * 환경 변수·레지스트리 값이 없거나 해석할 수 없으면 다음 단계로 넘어간다. */
UiLang ui_lang_detect(void);

#endif
