#ifndef BYTESIEVE_CLI_CLEAN_H
#define BYTESIEVE_CLI_CLEAN_H

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include "clean_plan.h"
#include "cli.h"

/*
 * disk-scanner clean: GUI 의 [프로젝트 정리]·[시스템 정리]와 같은 탐지·계획·실행(clean_plan.h)을 쓰는 CLI.
 *
 *   disk-scanner clean project <dir> [--category LIST] [--eco LIST] [--apply [--yes]] [--jsonl]
 *   disk-scanner clean system [--include LIST] [--exclude LIST] [--apply [--yes]] [--jsonl]
 *   disk-scanner clean -h | --help
 *
 * 안전 규칙:
 *   - 기본은 미리보기(계획만 출력, 아무것도 옮기지 않음). --apply 일 때만 옮긴다.
 *   - --apply 는 계획을 보여 준 뒤 확인([y/N])을 받는다. --yes 면 묻지 않는다. 입력이 대화형이 아니면
 *     --yes 없이 실행하지 않는다(호출자 main 이 confirm 콜백으로 처리).
 *   - 옮기기는 GUI 와 같은 안전 장치(cleanup_trash_one)로 휴지통, 휴지통이 없으면 격리 폴더로. 영구 삭제 없음.
 *   - 휴지통 비우기(영구 삭제)는 CLI 에서 지원하지 않는다(recycle_bin 을 고르면 인자 오류).
 *   - 선택: GUI [모두 선택]과 같다(clean_plan_select_all). 공용 개발 캐시(MANUAL)는 --include 로 이름을
 *     직접 지정해야 들어간다(dev_cache 는 공용 개발 캐시 전체). --category·--eco 는 프로젝트 후보를 거른다.
 */

#define CLI_CLEAN_MAX_IDS 32

typedef enum {
    CLI_CLEAN_PROJECT = 0,
    CLI_CLEAN_SYSTEM
} CliCleanCommand;

typedef struct {
    CliCleanCommand command;
    const wchar_t *target;  /* project 의 폴더(argv 안의 문자열) */
    unsigned categories;    /* CLEANUP_MASK 비트(0 이면 모두) */
    unsigned ecos;          /* CLEANUP_ECO_BIT 비트(0 이면 모두) */
    const wchar_t *include[CLI_CLEAN_MAX_IDS];
    size_t include_count;
    const wchar_t *exclude[CLI_CLEAN_MAX_IDS];
    size_t exclude_count;
    int apply;
    int yes;
    int jsonl;
    /* include·exclude 의 쉼표 목록을 잘라 담는 버퍼(argv 를 바꾸지 않으려고 복사) */
    wchar_t ids[1024];
} CliCleanOptions;

/*
 * argv[0] 이 "clean" 인 인자를 해석한다. CLI_PARSE_ERROR 이면 err 에 메시지.
 * 알 수 없는 이름(유형·환경·대상)과 recycle_bin 은 오류다.
 */
CliParseResult cli_clean_parse_args(int argc, wchar_t **argv, CliCleanOptions *opts,
                                    wchar_t *err, size_t err_len);

void cli_clean_print_usage(FILE *out);

/* 계획에 CLI 선택을 적용한다(공통 선택 정책 + 필터·--include·--exclude). 고른 대상 수. */
size_t cli_clean_select(CleanPlan *plan, const CliCleanOptions *opts);

/*
 * --apply 일 때 실행 직전에 부르는 확인 콜백. 1 이면 진행, 0 이면 취소. NULL 이면 --yes 가 있어야 한다.
 * summary 는 고른 대상의 요약이다.
 */
typedef int (*cli_clean_confirm_fn)(const CleanPlanSummary *summary, void *ctx);

/*
 * 탐지 → 계획 → (미리보기 | 확인 후 실행)을 하고 종료 코드를 돌려준다.
 *   EXIT_COMPLETE  미리보기 완료, 또는 고른 항목을 모두 옮김(고른 것이 없거나 취소한 경우 포함)
 *   EXIT_PARTIAL   탐지 일부를 읽지 못했거나, 실행에서 실패·건너뜀·취소가 있었음
 *   EXIT_FATAL     탐지 불가(폴더를 열지 못함)·메모리 부족·출력 실패, 확인할 수 없는데 --yes 가 없음
 * out 은 텍스트 모드면 wide 출력, jsonl 이면 UTF-8 바이트(fwrite)로 쓴다. err 는 경고·오류(wide).
 */
int cli_clean_run(const CliCleanOptions *opts, FILE *out, FILE *err,
                  cli_clean_confirm_fn confirm, void *ctx);

#endif /* BYTESIEVE_CLI_CLEAN_H */
