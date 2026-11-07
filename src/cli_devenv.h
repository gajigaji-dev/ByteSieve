#ifndef BYTESIEVE_CLI_DEVENV_H
#define BYTESIEVE_CLI_DEVENV_H

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include "cli.h"

/*
 * disk-scanner devenv: 개발 환경 분석(읽기 전용, 아무것도 지우지 않음).
 *
 *   disk-scanner devenv [--json | --jsonl] [--no-record]
 *
 *   - 공용 개발 캐시(npm·pip·Gradle·Cargo·NuGet·pnpm·Yarn·Maven·Go·Conda)의 위치·근거·용량·실제 확보와
 *     WSL2·Docker 가상 디스크(.vhdx)의 크기·실제 점유·줄이는 방법을 보인다. GUI [시스템 정리]와 같은 분석이다.
 *   - 증가 추적: 기록 폴더(스캔 기록과 같은 곳)의 devenv-history.tsv 와 비교해 캐시·가상 디스크마다 직전 기록보다
 *     늘어난·줄어든 용량을 보이고, 지금 용량을 기록한다. --no-record 이거나 [스캔 기록 자동 저장]이 꺼져 있으면
 *     비교만 하고 기록하지 않는다.
 *   - 종료 코드: 0 완료, 2 인자 오류·분석 실패(기록 저장 실패는 경고만).
 */

typedef struct {
    int json;
    int jsonl;
    int no_record;
} CliDevenvOptions;

/* argv[0] 이 "devenv" 인 인자를 해석한다. */
CliParseResult cli_devenv_parse_args(int argc, wchar_t **argv, CliDevenvOptions *opts,
                                     wchar_t *err, size_t err_len);
void cli_devenv_print_usage(FILE *out);

/* 분석 → 비교·기록 → 출력. out 은 텍스트면 wide, JSON 이면 UTF-8 바이트. 종료 코드를 돌려준다. */
int cli_devenv_run(const CliDevenvOptions *opts, FILE *out, FILE *err);

#endif /* BYTESIEVE_CLI_DEVENV_H */
