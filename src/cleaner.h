#ifndef BYTESIEVE_CLEANER_H
#define BYTESIEVE_CLEANER_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "clean_plan.h"
#include "cleanup_scan.h"
#include "cleanup_trash.h"
#include "dev_cache.h"
#include "devenv_history.h"
#include "vdisk.h"

/*
 * Cleaner: 프로젝트 밖의 시스템 정크(Windows 임시 파일·휴지통·브라우저 캐시·개발 도구 캐시)를
 * 분석하고, 사용자가 고른 대상만 정리한다.
 *
 * 분석(cleaner_analyze, 읽기 전용, 아무것도 바꾸지 않음):
 *   USER_TEMP    %TEMP%(없으면 %TMP%, GetTempPathW) 바로 아래 항목
 *   WER          %LOCALAPPDATA%\Microsoft\Windows\WER\ReportArchive·ReportQueue 바로 아래 보고서
 *   RECYCLE_BIN  모든 드라이브의 휴지통 용량·항목 수(SHQueryRecycleBinW)
 *   CHROME/EDGE  %LOCALAPPDATA%\(Google\Chrome|Microsoft\Edge)\User Data 의 프로필(Preferences 가 있는
 *                폴더)마다 Cache·Code Cache·GPUCache 폴더
 *   FIREFOX      %LOCALAPPDATA%\Mozilla\Firefox\Profiles\<프로필>\cache2
 *   DEV_CACHE    공용 개발 캐시(dev_cache: npm·pip·Gradle·Cargo·NuGet)에서 찾은 것마다 한 대상.
 *                설정한 위치가 링크·폴더가 아님·캐시 구조가 아님이면 그 이유(dev_status)와 함께 정리할 수
 *                없는 대상으로 남긴다(설치하지 않아 없는 캐시는 넣지 않음). 위치 근거(dev_source·dev_origin)도 둔다.
 *
 * 실제 확보 가능 용량(reclaim_bytes): 항목마다 reclaim 으로 할당 크기와 하드 링크를 확인해 지웠을 때
 * 실제로 비는 용량을 센다(휴지통·격리 폴더로 옮긴 뒤에는 그곳을 비워야 빈다). 휴지통은 조회한 용량.
 *
 * 안전 규칙:
 *   - 휴지통을 뺀 모든 대상은 항목(entry)마다 cleanup_trash 의 안전 장치(경로 판정 → 옮기기 직전 확인 →
 *     옮기는 중 확인)를 그대로 거쳐 휴지통으로, 휴지통이 없는 드라이브는 격리 폴더로 옮긴다.
 *     영구 삭제하지 않는다. 분석 때도 cleanup_guard_check 를 통과한 항목만 정리 대상이다.
 *   - 임시 파일·오류 보고서는 최근 24시간 안에 바뀐(만들어진) 것이 하나라도 들어 있는 항목을 뺀다
 *     (설치 프로그램 등이 지금 쓰는 파일 보호). 실행할 때 다시 확인한다.
 *   - 브라우저 캐시는 그 브라우저가 실행 중이면 분석·실행 모두에서 정리하지 않는다(APP_RUNNING).
 *   - 링크(재분석 지점)인 항목, 다 읽지 못한 항목은 정리 대상이 아니다.
 *   - 휴지통 비우기는 **영구 삭제**다. 사용자가 그 대상을 직접 고르고 확인한 경우에만 실행하며
 *     (GUI 는 따로 한 번 더 확인), 다른 항목을 휴지통으로 옮기기 **전에** 비운다(방금 옮긴 항목을
 *     지우지 않도록). 비우기가 실패해도 나머지 정리는 계속한다.
 *   - 일부 항목이 실패해도(사용 중·권한 없음) 나머지를 계속 처리하고 항목별 결과를 남긴다.
 *   - 휴지통 비우기와 개발 도구 캐시는 [모두 선택]에 들어가지 않는다(cleaner_target_manual_only).
 */

typedef enum {
    CLEANER_USER_TEMP = 0, /* 사용자 임시 파일 */
    CLEANER_WER,           /* Windows 오류 보고서(사용자) */
    CLEANER_RECYCLE_BIN,   /* 휴지통(비우기 = 영구 삭제) */
    CLEANER_CHROME,        /* Chrome 캐시 */
    CLEANER_EDGE,          /* Edge 캐시 */
    CLEANER_FIREFOX,       /* Firefox 캐시 */
    CLEANER_DEV_CACHE,     /* 공용 개발 캐시 하나 */
    CLEANER_VDISK,         /* WSL2·Docker 가상 디스크(.vhdx) 하나: 분석만, 정리하지 않음 */
    CLEANER_KIND_COUNT
} CleanerKind;

typedef enum {
    CLEANER_GROUP_WINDOWS = 0, /* Windows 임시 파일 */
    CLEANER_GROUP_RECYCLE,     /* 휴지통 */
    CLEANER_GROUP_BROWSER,     /* 브라우저 캐시 */
    CLEANER_GROUP_DEV,         /* 개발 도구 캐시 */
    CLEANER_GROUP_VDISK,       /* 가상 디스크(WSL·Docker) */
    CLEANER_GROUP_COUNT
} CleanerGroup;

typedef struct {
    const wchar_t *id;   /* 식별자("user_temp", "chrome" 등) */
    CleanerGroup group;
    const wchar_t *name; /* 화면용 이름 */
    const wchar_t *note; /* 정리하면 어떻게 되는지(한 문단) */
    int permanent;       /* 1 이면 영구 삭제(휴지통 비우기) */
    const wchar_t *app;  /* 실행 중이면 정리하지 않는 프로그램 실행 파일 이름(없으면 NULL) */
} CleanerKindInfo;

const CleanerKindInfo *cleaner_kind_info(CleanerKind kind);
/* 분류 화면 이름("Windows 임시 파일" 등). */
const wchar_t *cleaner_group_text(CleanerGroup group);

typedef enum {
    CLEANER_READY = 0,   /* 정리할 수 있음 */
    CLEANER_EMPTY,       /* 정리할 것이 없음(최근 항목·보호 항목만 있을 수 있음) */
    CLEANER_NOT_FOUND,   /* 위치가 없음(설치하지 않음 등) */
    CLEANER_APP_RUNNING, /* 관련 프로그램이 실행 중이라 정리하지 않음 */
    CLEANER_DENIED,      /* 항목이 모두 안전 판정에서 거부됨(보호 경로·링크·읽기 오류) */
    CLEANER_ERROR,       /* 조회 실패(code 참고) */
    CLEANER_CANCELLED,   /* 분석을 취소해 다 세지 못함 */
    CLEANER_INFO_ONLY,   /* 분석만 하고 정리하지 않음(가상 디스크) */
    CLEANER_STATUS_COUNT
} CleanerStatus;

/* 화면용 설명, 식별자 이름("ready", "app_running" 등). */
const wchar_t *cleaner_status_text(CleanerStatus status);
const wchar_t *cleaner_status_name(CleanerStatus status);

/* 실행할 때 항목을 옮기지 않고 건너뛴 이유. */
typedef enum {
    CLEANER_SKIP_NONE = 0,    /* 건너뛰지 않음(trash 결과 참고) */
    CLEANER_SKIP_RECENT,      /* 분석 뒤 최근에 바뀜 */
    CLEANER_SKIP_APP_RUNNING, /* 관련 프로그램이 실행 중 */
    CLEANER_SKIP_COUNT
} CleanerSkip;

const wchar_t *cleaner_skip_text(CleanerSkip skip);
const wchar_t *cleaner_skip_name(CleanerSkip skip);

/* 정리할 항목 하나(휴지통으로 옮길 파일·폴더). */
typedef struct {
    CleanupItem item; /* item.path 는 이 구조가 소유 */
    wchar_t *root;    /* 안전 판정의 탐지 경로(소유) */
} CleanerEntry;

typedef struct {
    CleanerKind kind;
    CleanerStatus status;
    uint32_t code;        /* ERROR 일 때 HRESULT 또는 Windows 오류 */
    wchar_t *label;       /* 세부 이름(개발 캐시 "npm 패키지 캐시", 브라우저 프로필 수 등), NULL 가능 */
    int profiles;         /* 브라우저: 찾은 프로필 수(label 의 숫자, 화면이 지금 언어로 다시 쓸 때 씀). 그 밖은 0 */
    wchar_t *location;    /* 표시 위치(폴더, 휴지통은 "모든 드라이브"), NULL 가능 */
    CleanerEntry *entries;
    size_t entry_count;
    size_t entry_cap;
    uint64_t bytes;       /* 정리 대상 항목의 논리 크기 합(휴지통은 휴지통 용량) */
    uint64_t reclaim_bytes; /* 실제 확보 가능 용량 합(cleanup_item_reclaim, 휴지통은 bytes) */
    uint64_t shared_bytes;  /* 하드 링크로 대상 밖과 나눠 지워도 비지 않는 점유 합 */
    uint64_t files;       /* 정리 대상 파일 수(휴지통은 항목 수) */
    size_t recent_count;  /* 최근 24시간 안에 바뀌어 뺀 항목 */
    uint64_t recent_bytes;
    size_t denied_count;  /* 보호 경로·링크·읽기 오류로 뺀 항목 */
    uint64_t denied_bytes;
    DevCacheKind dev_kind; /* DEV_CACHE 일 때 */
    VdiskKind vdisk_kind;  /* VDISK 일 때 */
    uint64_t vdisk_alloc;  /* VDISK 일 때 실제 점유(스파스면 bytes 보다 작다) */
    int vdisk_sparse;
    uint64_t modified;     /* VDISK 일 때 마지막 수정(FILETIME UTC) */
    /* 증가 추적(cleaner_apply_growth): 직전 기록과 비교 */
    int has_growth;        /* 직전 기록이 있어 growth 를 셌음 */
    int64_t growth;        /* 지금 - 직전 기록(바이트) */
    uint64_t growth_since; /* 직전 기록 시각(FILETIME UTC) */
    DevCacheStatus dev_status; /* DEV_CACHE 일 때 위치 상태(FOUND 가 아니면 정리할 수 없는 이유) */
    DevCacheSource dev_source; /* DEV_CACHE 일 때 위치 근거 */
    wchar_t *dev_origin;       /* 환경 변수 이름 또는 설정 파일 경로(소유, NULL 가능) */
} CleanerTarget;

typedef struct {
    CleanerTarget *items;
    size_t len;
    size_t cap;
    int cancelled;
} CleanerList;

void cleaner_list_init(CleanerList *list);
void cleaner_list_free(CleanerList *list);

/* 대상을 실행할 수 있는지(READY). 휴지통은 permanent 이므로 호출자가 따로 확인해야 한다. */
int cleaner_target_runnable(const CleanerTarget *t);

/*
 * [모두 선택]으로 고르지 않고 사용자가 행을 직접 체크해야 하는 대상이면 1.
 *   - 휴지통 비우기(permanent): 영구 삭제
 *   - 개발 도구 캐시: 여러 프로젝트가 함께 쓰므로 지우면 다시 내려받아야 하고, 일부는 인터넷이 없으면
 *     빌드할 수 없다. 임시 파일을 정리하려다 함께 지우는 일을 막는다.
 */
int cleaner_target_manual_only(const CleanerTarget *t);

/* 정리 뒤 오프라인 빌드가 안 되는 등 더 주의할 대상이면 1(개발 캐시의 dev_cache caution). */
int cleaner_target_caution(const CleanerTarget *t);

/*
 * 증가 추적 대상이면 1: 찾은 공용 개발 캐시(용량을 다 셌을 때)와 가상 디스크. 기록하는 용량은
 * cleaner_target_tracked_bytes(개발 캐시는 정리할 수 있는 것과 보호로 뺀 것의 합, 가상 디스크는 파일 크기).
 */
int cleaner_target_tracked(const CleanerTarget *t);
uint64_t cleaner_target_tracked_bytes(const CleanerTarget *t);

/*
 * 추적 대상마다 hist 의 직전 기록(같은 id·위치)과 비교해 has_growth·growth·growth_since 를 채우고,
 * record 가 1 이면 지금 용량을 now 시각으로 hist 에 더한다(파일 저장은 호출자가 devhist_save).
 * 더한 기록 수, 메모리가 부족하면 -1.
 */
int cleaner_apply_growth(CleanerList *list, DevHistory *hist, uint64_t now, int record);

/* 영문 식별자: 개발 캐시는 캐시 종류("npm", "gradle_wrapper"), 그 밖은 대상 종류("user_temp"). CLI --include 에 쓴다. */
const wchar_t *cleaner_target_id(const CleanerTarget *t);

/*
 * 분석 결과를 정리 계획(clean_plan.h)으로 바꾼다(이전 내용은 지운다). 대상마다 계획 대상 하나, 순서는
 * list->items 와 같다(plan->items[i].origin == i). ready 는 cleaner_target_runnable, 플래그는
 * MANUAL(cleaner_target_manual_only)·CAUTION·PERMANENT(휴지통)·CHECK_RECENT(임시 파일·오류 보고서),
 * app 은 실행 중이면 건너뛸 프로그램. 메모리가 부족하면 -1.
 */
int cleaner_build_plan(const CleanerList *list, CleanPlan *plan);

/*
 * 시스템 연동 함수(테스트에서 바꿔 끼움). NULL 인 함수는 실제 시스템을 쓴다.
 *   app_running   exe 이름(대소문자 무시)의 프로세스가 실행 중이면 1
 *   query_recycle 모든 드라이브 휴지통의 용량·항목 수. 성공 0, 실패 -1(*code)
 *   empty_recycle 모든 드라이브 휴지통을 확인 창 없이 비운다. 성공 0, 실패 -1(*code)
 *   min_age_hours 임시 파일·오류 보고서에서 이보다 최근에 바뀐 항목을 뺀다(0 이면 24)
 *   kinds         분석할 대상 종류(CLEANER_KIND_BIT 묶음). 0 이면 모든 종류(CLI·예전 동작).
 *                 GUI 는 [시스템 정리](CLEANER_KINDS_SYSTEM)와 [개발 캐시](CLEANER_KINDS_CACHE) 화면으로
 *                 나눠 쓴다. 범위 밖 종류는 분석하지 않아 목록에 행이 생기지 않는다(안전 규칙은 그대로).
 */
typedef struct {
    int (*app_running)(const wchar_t *exe_name, void *ctx);
    int (*query_recycle)(uint64_t *bytes, uint64_t *items, uint32_t *code, void *ctx);
    int (*empty_recycle)(uint32_t *code, void *ctx);
    void *ctx;
    unsigned min_age_hours;
    unsigned kinds;
} CleanerOps;

#define CLEANER_KIND_BIT(kind) (1u << (unsigned)(kind))
/* 모든 종류. */
#define CLEANER_KINDS_ALL ((1u << (unsigned)CLEANER_KIND_COUNT) - 1u)
/* 개발 도구 캐시와 가상 디스크([개발 캐시] 화면). */
#define CLEANER_KINDS_CACHE (CLEANER_KIND_BIT(CLEANER_DEV_CACHE) | CLEANER_KIND_BIT(CLEANER_VDISK))
/* 그 밖의 시스템 정크: 임시 파일·오류 보고서·휴지통·브라우저 캐시([시스템 정리] 화면). */
#define CLEANER_KINDS_SYSTEM (CLEANER_KINDS_ALL & ~CLEANER_KINDS_CACHE)

/* kinds(0 이면 모두)에 kind 가 들어 있으면 1. */
int cleaner_kinds_include(unsigned kinds, CleanerKind kind);

/* 실제 시스템 연동 함수(GUI·테스트가 직접 쓸 수도 있다). */
int cleaner_app_running_system(const wchar_t *exe_name, void *ctx);
int cleaner_query_recycle_system(uint64_t *bytes, uint64_t *items, uint32_t *code, void *ctx);
int cleaner_empty_recycle_system(uint32_t *code, void *ctx);

typedef int (*cleaner_cancel_fn)(void *ctx);
/* 분석 진행: 지금 보는 종류, 마친 대상 종류 수/전체, 지금까지 센 정리 대상 용량. */
typedef void (*cleaner_progress_fn)(CleanerKind kind, size_t done, size_t total, uint64_t bytes,
                                    void *ctx);

/*
 * 분석한다(list 의 이전 내용은 지운다). guard 로 항목마다 안전 판정을 한다(NULL 이면 시스템 기준으로
 * 만든다). ops 가 NULL 이면 모두 실제 시스템. cancel·progress 는 NULL 일 수 있다.
 * 취소하면 남은 대상은 CANCELLED 로 남고 list->cancelled = 1. 메모리가 부족하면 -1, 아니면 0.
 */
int cleaner_analyze(CleanerList *list, const CleanupGuard *guard, const CleanerOps *ops,
                    cleaner_cancel_fn cancel, cleaner_progress_fn progress, void *ctx);

/* ---- 분석 작업 스레드 ---- */

typedef struct CleanerScanJob CleanerScanJob;
typedef void (*cleaner_notify_fn)(int finished, void *ctx);

typedef struct {
    CleanerKind kind; /* 지금 분석하는 종류 */
    size_t done;
    size_t total;
    uint64_t bytes;
    int cancel_requested;
} CleanerScanSnapshot;

/* 분석을 새 스레드에서 시작한다. ops 는 복사한다(NULL 이면 실제 시스템). 실패하면 NULL.
 * notify 는 진행이 바뀔 때마다, 끝나면 반드시 한 번(finished = 1) 작업 스레드에서 호출된다. */
CleanerScanJob *cleaner_scan_start(const CleanerOps *ops, cleaner_notify_fn notify, void *ctx);
void cleaner_scan_cancel(CleanerScanJob *job);
void cleaner_scan_progress(CleanerScanJob *job, CleanerScanSnapshot *out);
/* 끝날 때까지 기다린 뒤 결과를 out 으로 옮기고(이전 내용 해제) job 을 해제한다. 반환값은 analyze 와 같다. */
int cleaner_scan_finish(CleanerScanJob *job, CleanerList *out);

/* ---- 실행(정리) ---- */

/* 항목 하나의 실행 결과. */
typedef struct {
    size_t target;              /* list 의 대상 번호 */
    wchar_t *path;              /* 항목 경로(소유) */
    uint64_t bytes;
    CleanerSkip skip;           /* NONE 이 아니면 옮기지 않았다 */
    CleanupTrashOutcome trash;  /* skip 이 NONE 일 때의 휴지통·격리 폴더 이동 결과 */
} CleanerEntryOutcome;

/* 대상 하나의 실행 요약. */
typedef struct {
    int selected;
    size_t moved;       /* 휴지통 또는 격리 폴더로 옮긴 항목 */
    size_t quarantined; /* moved 중 격리 폴더 */
    size_t failed;      /* 옮기지 못함(사용 중·권한·거부) */
    size_t skipped;     /* 최근에 바뀜·프로그램 실행 중 */
    size_t cancelled;
    uint64_t moved_bytes;
} CleanerTargetResult;

typedef struct {
    CleanerEntryOutcome *entries; /* 실행한 항목(실행 순서) */
    size_t entry_count;
    CleanerTargetResult *targets; /* list 와 같은 번호, target_count = list->len */
    size_t target_count;
    int recycle_requested;  /* 휴지통 비우기를 골랐으면 1 */
    int recycle_emptied;    /* 비웠으면 1 */
    uint32_t recycle_code;  /* 비우지 못했을 때 오류 */
    uint64_t recycle_bytes; /* 비운 휴지통 용량(분석 때 값) */
    int cancelled;
} CleanerRunResult;

void cleaner_run_result_free(CleanerRunResult *r);

typedef struct CleanerRunJob CleanerRunJob;

typedef struct {
    int emptying_recycle; /* 휴지통을 비우는 중 */
    size_t total;         /* 옮길 항목 수 */
    size_t done;
    size_t moved;
    size_t failed;        /* 실패·건너뜀 */
    uint64_t moved_bytes;
    int cancel_requested;
} CleanerRunSnapshot;

/*
 * selected[i] 가 0 이 아닌 대상만 정리하는 스레드를 시작한다(실행할 수 없는 대상은 무시).
 * 내부적으로 cleaner_build_plan 으로 계획을 만들어 clean_plan_run_start 로 실행한다(GUI·CLI 와 같은 엔진).
 * 필요한 경로는 복사하므로 호출 뒤 list 를 바꿔도 된다. 휴지통이 골라져 있으면 먼저 비운다
 * (이 함수는 호출자가 영구 삭제를 이미 확인했다고 본다). 휴지통 비우기 외에는 항목마다
 * cleanup_trash_one(시스템 기준 보호 목록)으로 옮긴다. 실행할 항목이 없으면 notify(1) 을 바로 부르고
 * 빈 결과를 내는 작업을 돌려준다. 메모리가 부족하면 NULL.
 */
CleanerRunJob *cleaner_run_start(const CleanerList *list, const int *selected,
                                 const CleanerOps *ops, cleaner_notify_fn notify, void *ctx);
/* 남은 항목을 처리하지 않게 한다(옮기던 항목과 진행 중인 휴지통 비우기는 끝까지 한다). */
void cleaner_run_cancel(CleanerRunJob *job);
void cleaner_run_progress(CleanerRunJob *job, CleanerRunSnapshot *out);
/* 끝날 때까지 기다린 뒤 결과를 out 에 넘기고(이전 내용은 덮어씀, out 은 NULL 가능) job 을 해제한다. */
void cleaner_run_finish(CleanerRunJob *job, CleanerRunResult *out);

#endif /* BYTESIEVE_CLEANER_H */
