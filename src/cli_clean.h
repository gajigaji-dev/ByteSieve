#ifndef BYTESIEVE_CLI_CLEAN_H
#define BYTESIEVE_CLI_CLEAN_H

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include "clean_plan.h"
#include "cli.h"

/*
 * 정리 CLI: GUI 의 [프로젝트 정리]·[시스템 정리]와 같은 탐지·계획·실행(clean_plan.h)을 쓴다.
 *
 *   disk-scanner find <dir> [--category LIST] [--eco LIST] [--dormant-days N] [--no-git]
 *                     [--json | --jsonl]
 *   disk-scanner clean project <dir> [--category LIST] [--eco LIST] [--include-dirty]
 *                              [--dormant-days N] [--no-git]
 *                              [--dry-run | --execute [--yes]] [--json | --jsonl]
 *   disk-scanner clean system [--include LIST] [--exclude LIST]
 *                             [--dry-run | --execute [--yes]] [--json | --jsonl]
 *   disk-scanner find|clean -h | --help
 *
 * find 는 프로젝트 정리 후보를 찾아 보여 주기만 한다(읽기 전용, 선택 없음). 보호 경로 안의 후보도 이유와
 * 함께 보이며, 순서는 파일 크기 내림차순(GUI 목록과 같다).
 *
 * find·clean project 는 후보를 프로젝트(Git 저장소 등)별로 묶어 보인다(project_info.h): 브랜치·마지막 커밋·
 * 마지막 수정·휴면(--dormant-days, 기본 90일), 미커밋 변경·미푸시 커밋 경고, 후보마다 복구 가능성.
 * --no-git 이면 git.exe 를 실행하지 않고 .git 을 직접 읽는다. 미커밋 변경이 있는 프로젝트의 후보는 기본으로
 * 고르지 않으며 --include-dirty 로만 고른다.
 *
 * clean 안전 규칙:
 *   - 기본은 --dry-run(계획과 예상 결과만 출력, 아무것도 옮기지 않음). --execute 일 때만 옮긴다.
 *     --apply 는 --execute 의 예전 이름(호환용)이다.
 *   - --execute 는 계획을 보여 준 뒤 확인([y/N])을 받는다. --yes 면 묻지 않는다. 입력이 대화형이 아니면
 *     --yes 없이 실행하지 않는다(호출자 main 이 confirm 콜백으로 처리).
 *   - 옮기기는 GUI 와 같은 안전 장치(cleanup_trash_one)로 휴지통, 휴지통이 없으면 격리 폴더로. 영구 삭제 없음.
 *   - 휴지통 비우기(영구 삭제)는 CLI 에서 지원하지 않는다(recycle_bin 을 고르면 인자 오류).
 *   - 선택: GUI [모두 선택]과 같다(clean_plan_select_all). 공용 개발 캐시(MANUAL)는 --include 로 이름을
 *     직접 지정해야 들어간다(dev_cache 는 공용 개발 캐시 전체). --category·--eco 는 프로젝트 후보를 거른다.
 *
 * 출력: 텍스트(기본), --json(UTF-8 JSON 문서 하나, "version":1), --jsonl(UTF-8 JSON Lines 이벤트).
 *
 * 종료 코드(find·clean 공통, JSON 의 "exit_code" 와 같다):
 *   0  EXIT_COMPLETE  완료: 모든 폴더를 읽었고, --execute 면 고른 항목을 모두 옮김
 *                     (고른 것이 없거나 확인에서 취소한 경우도 아무것도 바꾸지 않았으므로 0)
 *   1  EXIT_PARTIAL   일부 폴더를 읽지 못했거나, 실행에서 옮기지 못한 항목(실패·건너뜀·취소)이 있음
 *   2  EXIT_FATAL     인자 오류, 탐지 불가(폴더를 열지 못함), 메모리 부족·출력 실패,
 *                     확인할 수 없는데 --yes 가 없음
 */

#define CLI_CLEAN_MAX_IDS 32

typedef enum {
    CLI_CLEAN_PROJECT = 0, /* clean project */
    CLI_CLEAN_SYSTEM,      /* clean system */
    CLI_CLEAN_FIND         /* find */
} CliCleanCommand;

typedef struct {
    CliCleanCommand command;
    const wchar_t *target;  /* project·find 의 폴더(argv 안의 문자열) */
    unsigned categories;    /* CLEANUP_MASK 비트(0 이면 모두) */
    unsigned ecos;          /* CLEANUP_ECO_BIT 비트(0 이면 모두) */
    const wchar_t *include[CLI_CLEAN_MAX_IDS];
    size_t include_count;
    const wchar_t *exclude[CLI_CLEAN_MAX_IDS];
    size_t exclude_count;
    int apply;   /* --execute(--apply): 고른 대상을 옮긴다 */
    int dry_run; /* --dry-run: 명시한 미리보기(기본과 같다) */
    int yes;
    int json;    /* --json: JSON 문서 하나 */
    int jsonl;   /* --jsonl: JSON Lines 이벤트 */
    int include_dirty;     /* --include-dirty: 미커밋 변경이 있는 프로젝트의 후보도 고른다 */
    unsigned dormant_days; /* --dormant-days N(0 이면 기본 90) */
    int no_git;            /* --no-git: git.exe 를 쓰지 않는다 */
    /* include·exclude 의 쉼표 목록을 잘라 담는 버퍼(argv 를 바꾸지 않으려고 복사) */
    wchar_t ids[1024];
} CliCleanOptions;

/*
 * argv[0] 이 "clean" 또는 "find" 인 인자를 해석한다. CLI_PARSE_ERROR 이면 err 에 메시지.
 * 알 수 없는 이름(유형·환경·대상)과 recycle_bin, 명령에 맞지 않는 옵션, --dry-run 과 --execute 를 함께,
 * --json 과 --jsonl 을 함께, --execute 없는 --yes 는 오류다.
 */
CliParseResult cli_clean_parse_args(int argc, wchar_t **argv, CliCleanOptions *opts,
                                    wchar_t *err, size_t err_len);

/* clean 도움말, find 도움말. */
void cli_clean_print_usage(FILE *out);
void cli_find_print_usage(FILE *out);

/* 계획에 CLI 선택을 적용한다(공통 선택 정책 + 필터·--include·--exclude). 고른 대상 수. */
size_t cli_clean_select(CleanPlan *plan, const CliCleanOptions *opts);

/* find 의 필터(--category·--eco)에 맞는 프로젝트 대상인지. */
int cli_clean_matches(const CleanPlanTarget *t, const CliCleanOptions *opts);

/*
 * --execute 일 때 실행 직전에 부르는 확인 콜백. 1 이면 진행, 0 이면 취소. NULL 이면 --yes 가 있어야 한다.
 * summary 는 고른 대상의 요약이다.
 */
typedef int (*cli_clean_confirm_fn)(const CleanPlanSummary *summary, void *ctx);

/*
 * find: 탐지 → 후보 목록. clean: 탐지 → 계획 → (dry-run | 확인 후 실행). 종료 코드를 돌려준다(위 표).
 * out 은 텍스트 모드면 wide 출력, --json·--jsonl 이면 UTF-8 바이트(fwrite)로 쓴다. err 는 경고·오류(wide).
 * --json 은 끝에 문서 하나를 한 번에 쓴다(실패해도 "exit_code" 를 담은 문서를 쓴다, 출력 실패는 제외).
 */
int cli_clean_run(const CliCleanOptions *opts, FILE *out, FILE *err,
                  cli_clean_confirm_fn confirm, void *ctx);

#endif /* BYTESIEVE_CLI_CLEAN_H */
