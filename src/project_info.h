#ifndef BYTESIEVE_PROJECT_INFO_H
#define BYTESIEVE_PROJECT_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "cleanup_scan.h"

/*
 * Git 기반 프로젝트 분석. 정리 후보(cleanup_scan 결과)를 프로젝트별로 묶고, 프로젝트마다 Git 상태·마지막
 * 활동 시각·휴면 여부를, 후보마다 지운 뒤 복구할 수 있는지를 정한다. 아무것도 바꾸지 않는다.
 *
 * 프로젝트 루트: 후보의 부모 폴더부터 위로 올라가며 ".git"(폴더, 또는 워크트리·서브모듈의 "gitdir:" 파일)이
 * 있는 가장 가까운 폴더(볼륨 루트까지). 없으면 후보가 들어 있는 폴더가 프로젝트다(Git 아님).
 *
 * Git 상태(읽기 전용):
 *   1. git.exe(PATH, 또는 환경 변수 BYTESIEVE_GIT 의 경로. "none" 이면 쓰지 않음)를 실행한다.
 *      명령: rev-parse --abbrev-ref HEAD, log -1 --format=%ct, status --porcelain -z,
 *            for-each-ref refs/remotes, rev-list --count HEAD --not --remotes.
 *      저장소를 바꾸지 않도록 --no-optional-locks(index 를 다시 쓰지 않음)로, 저장소 설정의 외부 프로그램이
 *      실행되지 않도록 -c core.fsmonitor=false 로 실행한다. 창을 띄우지 않고, 명령마다 시간 제한이 있다.
 *   2. git.exe 가 없거나 실패하면(소유자가 달라 거부 등) .git 을 직접 읽는다: HEAD(브랜치), logs/HEAD 의
 *      마지막 기록 시각(마지막 커밋 대신 HEAD 가 마지막으로 움직인 시각), 브랜치와 같은 이름의 원격 ref 비교
 *      (같으면 미푸시 0, 다르면 개수를 모름). 미커밋 변경은 알 수 없다(-1).
 *
 * 마지막 수정: 프로젝트 루트 아래 파일의 가장 늦은 수정 시각. .git, 정리 후보가 될 수 있는 이름의 폴더
 * (node_modules·build 등), 링크, 격리 폴더, 다른 프로젝트 루트는 들어가지 않는다. 너무 크면
 * (PROJECT_WALK_LIMIT 항목) 거기까지만 보고 modified_incomplete 를 세운다.
 *
 * 휴면: 마지막 활동(= 마지막 커밋과 마지막 수정 중 늦은 쪽)이 dormant_days 일보다 오래됐으면 휴면.
 * 둘 다 모르면 휴면이 아니다.
 */

#define PROJECT_DEFAULT_DORMANT_DAYS 90
#define PROJECT_WALK_LIMIT 200000

/* 후보를 지운 뒤 복구할 수 있는지. */
typedef enum {
    PROJECT_RECOVER_NONE = 0, /* 다시 생기지 않음(임시 파일·로그: 필요 없는 파일) */
    PROJECT_RECOVER_REBUILD,  /* 빌드·실행하면 다시 생김(빌드 결과물·캐시) */
    PROJECT_RECOVER_LOCKED,   /* lockfile 이 있어 같은 버전으로 다시 설치할 수 있음(node_modules) */
    PROJECT_RECOVER_UNLOCKED, /* 다시 설치할 수 있지만 lockfile 이 없어 버전이 바뀔 수 있음 */
    PROJECT_RECOVER_COUNT
} ProjectRecovery;

/* 화면용 설명, 식별자 이름("locked" 등). */
const wchar_t *project_recovery_text(ProjectRecovery r);
const wchar_t *project_recovery_name(ProjectRecovery r);

/* Git 상태를 어디서 얻었는지. */
typedef enum {
    PROJECT_GIT_NONE = 0, /* Git 저장소가 아님 */
    PROJECT_GIT_EXE,      /* git.exe 로 확인 */
    PROJECT_GIT_DIRECT,   /* .git 을 직접 읽음(일부만 앎) */
    PROJECT_GIT_SOURCE_COUNT
} ProjectGitSource;

const wchar_t *project_git_source_name(ProjectGitSource s);

typedef struct {
    wchar_t *root;            /* 프로젝트 루트(표시용 전체 경로, 소유) */
    int is_git;
    ProjectGitSource git_source;
    wchar_t *branch;          /* 현재 브랜치(소유, 모르면 NULL, 분리된 HEAD 는 "HEAD") */
    uint64_t last_commit;     /* 마지막 커밋 시각(FILETIME UTC, 0 이면 모름·커밋 없음) */
    int changes;              /* 커밋하지 않은 변경(추적 안 하는 새 파일 포함) 수, -1 이면 모름 */
    int unpushed;             /* 어느 원격에도 없는 커밋 수, -1 이면 모름 */
    int has_remote;           /* 원격 ref 가 있으면 1, 없으면 0, 모르면 -1 */
    int remote_differs;       /* 직접 읽기: 브랜치가 같은 이름의 원격 ref 와 다름(개수는 모름) */
    uint32_t git_error;       /* git.exe 가 실패했으면 종료 코드(직접 읽기로 대신함) */
    uint64_t last_modified;   /* 파일 마지막 수정 시각(FILETIME UTC, 0 이면 파일 없음·모름) */
    int modified_incomplete;  /* 다 훑지 못함(너무 많음·읽기 오류·취소) */
    uint64_t last_activity;   /* last_commit 과 last_modified 중 늦은 쪽 */
    int dormant;              /* 휴면 */
    unsigned age_days;        /* 마지막 활동부터 지난 날 수(last_activity 가 0 이면 0) */
    size_t candidates;        /* 이 프로젝트에 속한 후보 수 */
} ProjectInfo;

typedef struct {
    ProjectInfo *items;
    size_t len;
    size_t cap;
    size_t *item_project;         /* 후보(CleanupResult.items) 번호 → 프로젝트 번호 */
    ProjectRecovery *item_recovery; /* 후보마다 복구 가능성 */
    size_t item_count;
    int git_available;            /* git.exe 를 찾았으면 1 */
    unsigned dormant_days;
    uint64_t now;                 /* 판정 기준 시각(FILETIME UTC) */
    int cancelled;
} ProjectList;

void project_list_init(ProjectList *list);
void project_list_free(ProjectList *list);

typedef struct {
    unsigned dormant_days; /* 0 이면 PROJECT_DEFAULT_DORMANT_DAYS */
    int no_git;            /* 1 이면 git.exe 를 쓰지 않고 직접 읽는다 */
    uint64_t now;          /* 판정 기준 시각(0 이면 지금, 테스트용) */
} ProjectOptions;

/* 진행: 마친 프로젝트 수/전체. */
typedef void (*project_progress_fn)(size_t done, size_t total, void *ctx);

/*
 * r 의 후보를 프로젝트별로 묶어 분석한다(out 의 이전 내용은 지운다). opts·cancel·progress 는 NULL 가능.
 * 취소하면 남은 프로젝트는 Git·수정 시각을 모름으로 두고 out->cancelled = 1. 메모리가 부족하면 -1.
 */
int project_analyze(const CleanupResult *r, const ProjectOptions *opts, ProjectList *out,
                    int (*cancel)(void *ctx), project_progress_fn progress, void *ctx);

/* 후보 하나의 복구 가능성(lockfile 은 후보의 부모 폴더에서 찾는다). */
ProjectRecovery project_item_recovery(const CleanupItem *item);

/* 쓸 git.exe 경로를 찾는다(BYTESIEVE_GIT 우선, "none" 이면 없음). 찾으면 1. */
int project_find_git(wchar_t *buf, size_t len);

/* FILETIME(UTC) ↔ 유닉스 초. */
uint64_t project_unix_to_filetime(int64_t seconds);

#endif /* BYTESIEVE_PROJECT_INFO_H */
