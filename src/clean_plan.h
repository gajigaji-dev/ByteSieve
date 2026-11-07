#ifndef BYTESIEVE_CLEAN_PLAN_H
#define BYTESIEVE_CLEAN_PLAN_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "cleanup_scan.h"
#include "cleanup_trash.h"
#include "project_info.h"

/*
 * 정리 계획: 탐지 → 계획 → 실행을 GUI([프로젝트 정리]·[시스템 정리])와 CLI(disk-scanner clean)가
 * 함께 쓰는 공통 구조.
 *
 *   탐지   cleanup_scan(프로젝트 정리 후보) / cleaner_analyze(시스템 정크·공용 개발 캐시)
 *   계획   clean_plan_from_project / cleaner_build_plan 이 결과를 CleanPlan 으로 바꾼다.
 *          대상(target)마다 정리할 수 있는지(ready·deny), 선택 정책(flags), 용량, 실행 항목(entry)을 둔다.
 *          선택·요약(clean_plan_select_*, clean_plan_summarize)은 여기서만 정한다.
 *   실행   clean_plan_run_start 가 고른 대상의 항목을 작업 스레드에서 cleanup_trash_one 으로 옮기고,
 *          clean_plan_run_finish 가 항목별 결과를 계획에 되돌려 쓴다.
 *
 * 안전 규칙(GUI·CLI 공통, 이 모듈이 지킨다):
 *   - 휴지통 비우기 같은 영구 삭제 대상(CLEAN_PLAN_PERMANENT)은 골라져 있어도 ops.allow_permanent 가 1 일
 *     때만 실행한다. 호출자는 사용자가 영구 삭제를 따로 확인한 경우에만 1 로 둔다(CLI 는 언제나 0).
 *     실행하면 다른 항목을 옮기기 **전에** 한 번 비운다(이번에 옮긴 항목을 지우지 않도록).
 *   - 나머지 항목은 항목마다 cleanup_trash_one(시스템 기준 보호 목록)의 안전 장치(경로 판정 → 옮기기 직전
 *     확인 → 옮기는 중 확인)를 그대로 거쳐 휴지통으로, 휴지통이 없으면 격리 폴더로 옮긴다. 영구 삭제하지 않는다.
 *   - 실행 직전 다시 확인: 대상의 app 이 실행 중이면 건너뛴다(CLEAN_SKIP_APP_RUNNING), CHECK_RECENT 대상은
 *     항목이 최근 min_age_hours 안에 바뀌었거나 확인할 수 없으면 건너뛴다(CLEAN_SKIP_RECENT).
 *   - 일부 항목이 실패해도 나머지를 계속하고 항목별 결과를 남긴다.
 *   - [모두 선택](clean_plan_select_all)은 MANUAL 대상(휴지통 비우기·공용 개발 캐시)을 고르지 않는다.
 *     MANUAL 대상은 사용자가 직접(행 체크·CLI 의 --include) 골라야 한다.
 */

typedef enum {
    CLEAN_SOURCE_PROJECT = 0, /* 프로젝트 정리(고른 폴더 안의 정리 후보) */
    CLEAN_SOURCE_SYSTEM       /* 시스템 정리(임시 파일·휴지통·브라우저 캐시·공용 개발 캐시) */
} CleanSource;

/* CleanPlanTarget.flags */
enum {
    CLEAN_PLAN_MANUAL = 1u << 0,       /* [모두 선택]에 넣지 않음(직접 골라야 함) */
    CLEAN_PLAN_CAUTION = 1u << 1,      /* 정리 뒤 오프라인 빌드가 안 되는 등 더 주의할 것 */
    CLEAN_PLAN_PERMANENT = 1u << 2,    /* 영구 삭제(휴지통 비우기). 항목이 없다 */
    CLEAN_PLAN_GUESSED = 1u << 3,      /* 프로젝트 표시 파일 없이 이름으로만 추정한 후보 */
    CLEAN_PLAN_CHECK_RECENT = 1u << 4, /* 실행 직전 최근에 바뀐 항목은 건너뜀(임시 파일·오류 보고서) */
    CLEAN_PLAN_DIRTY = 1u << 5,        /* 프로젝트에 커밋하지 않은 변경이 있음(MANUAL 도 붙는다) */
    CLEAN_PLAN_UNPUSHED = 1u << 6,     /* 프로젝트에 원격에 없는 커밋이 있음(또는 원격과 다름) */
    CLEAN_PLAN_DORMANT = 1u << 7       /* 휴면 프로젝트(마지막 활동이 기준 일수보다 오래됨) */
};

/* 실행할 때 항목을 옮기지 않고 건너뛴 이유. */
typedef enum {
    CLEAN_SKIP_NONE = 0,    /* 건너뛰지 않음(outcome 참고) */
    CLEAN_SKIP_RECENT,      /* 분석 뒤 최근에 바뀜 */
    CLEAN_SKIP_APP_RUNNING, /* 관련 프로그램이 실행 중 */
    CLEAN_SKIP_COUNT
} CleanSkip;

/* 화면용 설명, 식별자 이름("recent", "app_running" 등). */
const wchar_t *clean_skip_text(CleanSkip skip);
const wchar_t *clean_skip_name(CleanSkip skip);

/* 실행 항목 하나(휴지통·격리 폴더로 옮길 파일·폴더). */
typedef struct {
    CleanupItem item;            /* item.path 소유 */
    wchar_t *root;               /* 안전 판정의 탐지 경로(소유) */
    CleanSkip skip;              /* 마지막 실행에서 건너뛴 이유 */
    CleanupTrashOutcome outcome; /* 마지막 실행 결과(status PENDING 이면 아직 시도 안 함) */
} CleanPlanEntry;

/* 정리 대상 하나(선택 단위). 프로젝트 후보는 항목 하나, 시스템 대상은 항목 여럿(휴지통은 0). */
typedef struct {
    CleanSource source;
    wchar_t *id;       /* 식별자(소유): 프로젝트는 유형("node_modules"), 시스템은 종류("user_temp", "npm") */
    wchar_t *label;    /* 화면용 이름(소유) */
    wchar_t *location; /* 표시 위치(소유, NULL 가능) */
    wchar_t *app;      /* 실행 중이면 건너뛸 프로그램 실행 파일 이름(소유, NULL 가능) */
    const wchar_t *state; /* 탐지 상태 식별자(정적 문자열: "ready", "deny_protected", "app_running" 등) */
    CleanupCategory category; /* 프로젝트 후보의 유형 */
    CleanupEco eco;           /* 프로젝트 후보의 개발 환경 */
    size_t origin;     /* 원래 탐지 결과의 번호(CleanupResult.items / CleanerList.items) */
    size_t project;    /* 프로젝트 정리: ProjectList 의 프로젝트 번호(모르면 (size_t)-1) */
    int recovery;      /* 프로젝트 정리: ProjectRecovery(모르면 -1) */
    unsigned flags;    /* CLEAN_PLAN_* */
    int ready;         /* 탐지 결과상 정리할 수 있음(시스템: READY, 프로젝트: 경로 판정 통과) */
    CleanupTrashStatus deny; /* 경로 판정에서 거부된 이유(PENDING 이면 통과) */
    uint64_t bytes;    /* 논리 크기 합(휴지통은 휴지통 용량) */
    uint64_t reclaim;  /* 실제 확보 가능 합(reclaim.h, 휴지통은 bytes) */
    uint64_t shared;   /* 하드 링크로 대상 밖과 나눠 지워도 비지 않는 점유 */
    uint64_t files;
    CleanPlanEntry *entries;
    size_t entry_count;
    size_t entry_cap;
    int selected;
    /* 마지막 실행 결과 */
    int ran;            /* 실행에 포함됐음 */
    size_t moved;       /* 휴지통 또는 격리 폴더로 옮긴 항목 */
    size_t quarantined; /* moved 중 격리 폴더 */
    size_t failed;      /* 옮기지 못함(사용 중·권한·거부) */
    size_t skipped;     /* 최근에 바뀜·프로그램 실행 중 */
    size_t cancelled;
    uint64_t moved_bytes;
} CleanPlanTarget;

typedef struct {
    CleanPlanTarget *items;
    size_t len;
    size_t cap;
} CleanPlan;

void clean_plan_init(CleanPlan *plan);
void clean_plan_free(CleanPlan *plan);

/* 대상을 붙인다(문자열은 복사, NULL 가능). 메모리가 부족하면 NULL. */
CleanPlanTarget *clean_plan_add(CleanPlan *plan, CleanSource source, const wchar_t *id,
                                const wchar_t *label, const wchar_t *location);
/* 대상에 실행 항목을 붙인다(item->path·root 는 복사). bytes 등 합계는 바꾸지 않는다. 성공 0. */
int clean_plan_add_entry(CleanPlanTarget *t, const CleanupItem *item, const wchar_t *root);

/*
 * 프로젝트 정리 후보를 계획으로 바꾼다(이전 내용은 지운다). 후보마다 대상 하나, 순서는 r->items 와 같다
 * (plan->items[i].origin == i). guard 로 경로 판정을 해 deny·ready 를 정한다(NULL 이면 시스템 기준).
 * 표시 파일 없이 추정한 폴더는 GUESSED. 메모리가 부족하면 -1.
 */
int clean_plan_from_project(CleanPlan *plan, const CleanupResult *r, const CleanupGuard *guard);

/*
 * 프로젝트 분석(project_info.h) 결과를 프로젝트 정리 계획에 붙인다. plan 은 clean_plan_from_project 로
 * 만든 같은 탐지 결과의 계획이어야 한다(origin 이 후보 번호). 대상마다 project·recovery 를 두고,
 * 미커밋 변경이 있는 프로젝트의 후보는 DIRTY|MANUAL([모두 선택]에서 빠짐, 이미 골랐으면 선택을 푼다),
 * 미푸시 커밋이 있으면 UNPUSHED, 휴면이면 DORMANT 를 붙인다.
 */
void clean_plan_apply_projects(CleanPlan *plan, const ProjectList *projects);

/* 고를 수 있는지: 정리할 수 있고, 옮길 항목이 남아 있음(아직 시도 안 했거나 다시 해 볼 만한 실패·취소). */
int clean_plan_selectable(const CleanPlanTarget *t);

/* 대상 하나를 고르거나 푼다(고를 수 없으면 풀린다). 바뀐 뒤의 선택 상태를 반환한다. */
int clean_plan_set_selected(CleanPlan *plan, size_t index, int on);

/*
 * on 이면 고를 수 있는 대상을 모두 고르되 MANUAL 대상은 건드리지 않는다([모두 선택]).
 * on 이 0 이면 모든 선택을 푼다. 고른 대상 수를 반환한다.
 */
size_t clean_plan_select_all(CleanPlan *plan, int on);

/* id 가 같은(대소문자 무시) 대상을 직접 고르거나 푼다(MANUAL 도). 맞은 대상 수(고를 수 없는 것 포함). */
size_t clean_plan_select_id(CleanPlan *plan, const wchar_t *id, int on);

/* 선택 요약(확인 창·선택 줄·CLI 미리보기). */
typedef struct {
    size_t targets;    /* 고른 대상 수 */
    size_t entries;    /* 고른 대상의 실행 항목 수(옮기지 않은 것만) */
    uint64_t bytes;    /* 고른 대상(영구 삭제 제외)의 논리 크기 합 */
    uint64_t reclaim;  /* 고른 대상(영구 삭제 제외)의 실제 확보 가능 합 */
    size_t caution;    /* 고른 대상 중 CAUTION */
    size_t manual;     /* 고른 대상 중 MANUAL(영구 삭제 제외) */
    size_t guessed;    /* 고른 대상 중 GUESSED */
    size_t permanent;  /* 고른 대상 중 PERMANENT */
    size_t dirty;      /* 고른 대상 중 DIRTY(커밋하지 않은 변경이 있는 프로젝트) */
    size_t unpushed;   /* 고른 대상 중 UNPUSHED */
    uint64_t permanent_bytes;
    size_t selectable; /* 고를 수 있는 대상 수 */
} CleanPlanSummary;

void clean_plan_summarize(const CleanPlan *plan, CleanPlanSummary *out);

/* ---- 실행 ---- */

/*
 * 시스템 연동(테스트에서 바꿔 끼움). NULL 인 함수는 실제 시스템을 쓴다.
 *   app_running     exe 이름(대소문자 무시)의 프로세스가 실행 중이면 1(확인할 수 없으면 1)
 *   empty_recycle   모든 드라이브 휴지통을 확인 창 없이 비운다. 성공 0, 실패 -1(*code)
 *   min_age_hours   CHECK_RECENT 대상에서 이보다 최근에 바뀐 항목을 건너뛴다(0 이면 24)
 *   allow_permanent 1 이면 고른 PERMANENT 대상을 실행한다(사용자가 영구 삭제를 따로 확인했을 때만)
 */
typedef struct {
    int (*app_running)(const wchar_t *exe_name, void *ctx);
    int (*empty_recycle)(uint32_t *code, void *ctx);
    void *ctx;
    unsigned min_age_hours;
    int allow_permanent;
} CleanPlanOps;

int clean_plan_app_running_system(const wchar_t *exe_name, void *ctx);
int clean_plan_empty_recycle_system(uint32_t *code, void *ctx);

typedef struct CleanPlanRunJob CleanPlanRunJob;
typedef void (*clean_plan_notify_fn)(int finished, void *ctx);

typedef struct {
    int emptying_recycle; /* 휴지통을 비우는 중 */
    size_t total;         /* 옮길 항목 수 */
    size_t done;
    size_t moved;
    size_t quarantined;
    size_t failed;        /* 실패·건너뜀 */
    uint64_t moved_bytes;
    int cancel_requested;
} CleanPlanSnapshot;

/* 실행 한 번의 합계. moved~moved_bytes 는 옮긴 항목만 센다(영구 삭제는 permanent_*). */
typedef struct {
    size_t targets;     /* 실행에 포함한 대상 수(영구 삭제 대상 포함) */
    size_t moved;
    size_t quarantined;
    size_t failed;
    size_t skipped;
    size_t cancelled;
    uint64_t moved_bytes;
    int permanent_requested; /* 영구 삭제 대상을 실행했음 */
    int permanent_done;      /* 비웠음 */
    uint32_t permanent_code; /* 비우지 못했을 때 오류 */
    uint64_t permanent_bytes; /* 비운 용량(분석 때 값) */
    int was_cancelled;
} CleanPlanTotals;

/*
 * 고른(clean_plan_selectable) 대상의 남은 항목을 정리하는 스레드를 시작한다. 경로는 복사하므로 실행 중에
 * plan 을 읽어도 되지만, clean_plan_run_finish 전까지 plan 의 대상 수·순서를 바꾸면 안 된다.
 * 대상은 논리 크기가 큰 것부터 처리한다(PERMANENT 는 맨 먼저). 옮길 것이 없거나 실패하면 NULL.
 * notify 는 항목 하나를 마칠 때마다, 끝나면 반드시 한 번(finished = 1) 작업 스레드에서 호출된다.
 */
CleanPlanRunJob *clean_plan_run_start(const CleanPlan *plan, const CleanPlanOps *ops,
                                      clean_plan_notify_fn notify, void *ctx);
/* 남은 항목을 처리하지 않게 한다(옮기던 항목과 진행 중인 휴지통 비우기는 끝까지 한다). */
void clean_plan_run_cancel(CleanPlanRunJob *job);
void clean_plan_run_progress(CleanPlanRunJob *job, CleanPlanSnapshot *out);
/*
 * 끝날 때까지 기다린 뒤 항목별 결과를 plan 에 쓰고(실행한 대상은 ran = 1, 실행 결과 합계 갱신,
 * 옮긴 대상은 선택을 푼다) job 을 해제한다. totals 는 NULL 가능.
 */
void clean_plan_run_finish(CleanPlanRunJob *job, CleanPlan *plan, CleanPlanTotals *totals);

/* clean_plan_run_start 와 finish 를 이어서 부른다(CLI). 실행할 것이 없으면 -1, 아니면 0. */
int clean_plan_run(CleanPlan *plan, const CleanPlanOps *ops, CleanPlanTotals *totals);

#endif /* BYTESIEVE_CLEAN_PLAN_H */
