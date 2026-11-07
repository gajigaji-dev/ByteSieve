#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleanup_rules.h"
#include "cli.h"
#include "cli_clean.h"
#include "cli_devenv.h"
#include "jsonl.h"
#include "scan_history.h"
#include "scan_model.h"
#include "scanner.h"
#include "size_format.h"

/* JSON Lines 모드에서 progress 이벤트를 내는 항목 간격. */
#define JSONL_PROGRESS_INTERVAL 1000

/*
 * 이후 wide 출력 함수만 사용한다(printf와 혼용 금지).
 * 콘솔은 UTF-16 으로 써야 한글이 깨지지 않고, 파이프·파일로 넘길 때는 UTF-8 로 쓴다.
 */
static void set_wide_mode(FILE *stream)
{
    int fd = _fileno(stream);
    _setmode(fd, _isatty(fd) ? _O_U16TEXT : _O_U8TEXT);
}

/*
 * --track: 직전 스냅숏과 비교하고 새 스냅숏을 저장한다. 비교 결과는 diff 에(직전 스냅숏이 없으면
 * has_previous 0). 저장 위치는 *file 에(malloc). 스캔이 중단·취소됐으면 저장하지 않는다.
 * 기록 관련 실패는 경고만 하고 스캔 결과(종료 코드)에는 영향을 주지 않는다.
 */
static void track_history(const CliOptions *opts, const ScanModel *model, HistoryDiff *diff,
                          int *has_previous, wchar_t **file, int *saved)
{
    HistoryTrackResult r;

    *has_previous = 0;
    *saved = 0;
    *file = NULL;
    if (opts->history_dir != NULL && wcslen(opts->history_dir) >= MAX_PATH * 2) {
        fwprintf(stderr, L"warning: --history-dir is too long; history not saved\n");
        return;
    }
    /* GUI 폴더 스캔과 같은 추적 함수(scan_history). --track 은 명시적 요청이라 항상 저장한다. */
    history_track(model, opts->history_dir, opts->target, 1, diff, &r);
    *has_previous = r.has_diff;
    switch (r.status) {
    case HISTORY_TRACK_CANCELLED:
        fwprintf(stderr, L"warning: scan was cancelled; history not saved\n");
        break;
    case HISTORY_TRACK_FAILED:
        if (r.file == NULL) {
            fwprintf(stderr, L"warning: cannot determine history folder; history not saved\n");
        } else {
            fwprintf(stderr, L"warning: cannot save history to %ls (Windows error %lu)\n",
                     r.file, r.error);
        }
        break;
    case HISTORY_TRACK_BASELINE:
    case HISTORY_TRACK_COMPARED:
        *saved = 1;
        break;
    default:
        break;
    }
    *file = r.file; /* 소유권을 넘긴다 */
    r.file = NULL;
    history_track_result_free(&r);
}

/* --track 텍스트 모드: 폴더 트리를 만들어야 하므로 모델로 스캔한다. */
static int run_scan_tracked(const CliOptions *opts)
{
    ScanModel model;
    ScanStatus status;
    HistoryDiff diff;
    wchar_t *file;
    int has_previous, saved;

    scan_model_init(&model);
    history_diff_init(&diff);
    status = scan_model_build(&model, opts->target, opts->max_depth, NULL, NULL);
    cli_print_report(stdout, stderr, opts->target, opts->max_depth, &model.report, status);
    track_history(opts, &model, &diff, &has_previous, &file, &saved);
    if (status != SCAN_FATAL) {
        if (has_previous) {
            cli_print_changes(stdout, &diff, CLI_MAX_LISTED_CHANGES);
        } else {
            fwprintf(stdout, L"Changes:        no previous snapshot (baseline)\n");
        }
        if (saved) {
            fwprintf(stdout, L"History:        saved to %ls\n", file);
        }
    }
    free(file);
    history_diff_free(&diff);
    scan_model_free(&model);
    return cli_exit_code(status);
}

/*
 * 정리한 항목을 옮겨 둔 격리 폴더(CLEANUP_QUARANTINE_DIR)는 사용 중인 공간으로 세지 않는다
 * (GUI 폴더 스캔·--track 의 scan_model 과 같은 기준).
 */
static int skip_quarantine(const ScanEntry *entry, const ScanDirInfo *parent, void *ctx)
{
    (void)parent;
    (void)ctx;
    return cleanup_is_quarantine_name(entry->name);
}

static uintptr_t visit_progress(const ScanEntry *entry, uintptr_t parent, int will_descend,
                                void *ctx)
{
    (void)parent;
    (void)will_descend;
    jsonl_progress_visit(entry, ctx);
    return 0;
}

/* 격리 폴더를 빼고 스캔한다. progress 가 NULL 이 아니면 항목마다 JSON Lines 진행을 센다. */
static ScanStatus scan_target(const CliOptions *opts, ScanReport *report, JsonlProgress *progress)
{
    ScanHooks hooks;

    memset(&hooks, 0, sizeof(hooks));
    hooks.should_skip_dir = skip_quarantine;
    if (progress != NULL) {
        hooks.on_entry = visit_progress;
    }
    return scan_tree_ex(opts->target, opts->max_depth, 0, report, &hooks, progress);
}

static int run_scan(const CliOptions *opts)
{
    ScanReport report;
    ScanStatus status;

    scan_report_init(&report);
    status = scan_target(opts, &report, NULL);
    cli_print_report(stdout, stderr, opts->target, opts->max_depth, &report, status);
    scan_report_free(&report);
    return cli_exit_code(status);
}

/* stdout 은 바이너리 모드여야 한다(UTF-8 바이트를 그대로, 줄바꿈은 LF). */
static int run_scan_jsonl(const CliOptions *opts)
{
    ScanReport report;
    ScanStatus status;
    JsonBuf buf;
    JsonlProgress progress;
    int code;

    ScanModel model;
    const ScanReport *final_report = &report;

    scan_report_init(&report);
    scan_model_init(&model);
    jsonl_buf_init(&buf);

    jsonl_start(&buf, opts->target);
    jsonl_buf_flush(&buf, stdout);

    if (opts->track) {
        /* 변화 추적에는 폴더 트리가 필요하다(진행 이벤트는 내지 않는다). */
        status = scan_model_build(&model, opts->target, opts->max_depth, NULL, NULL);
        final_report = &model.report;
    } else {
        progress.buf = &buf;
        progress.report = &report;
        progress.out = stdout;
        progress.interval = JSONL_PROGRESS_INTERVAL;
        progress.seen = 0;
        status = scan_target(opts, &report, &progress);
    }
    code = cli_exit_code(status);

    jsonl_result(&buf, opts->target, final_report);
    if (opts->track && status != SCAN_FATAL) {
        HistoryDiff diff;
        wchar_t *file;
        int has_previous, saved;
        history_diff_init(&diff);
        track_history(opts, &model, &diff, &has_previous, &file, &saved);
        jsonl_changes(&buf, has_previous ? &diff : NULL, saved ? file : NULL,
                      CLI_MAX_LISTED_CHANGES * 5);
        free(file);
        history_diff_free(&diff);
    }
    jsonl_complete(&buf, status, code);
    if (!jsonl_buf_flush(&buf, stdout)) {
        fwprintf(stderr, L"error: failed to write JSON output\n");
        code = EXIT_FATAL;
    }

    jsonl_buf_free(&buf);
    scan_report_free(&report);
    scan_model_free(&model);
    return code;
}

/* 표준 입력이 콘솔(대화형)이면 1. 파이프·파일·NUL 이면 확인을 받을 수 없다. */
static int stdin_interactive(void)
{
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

/* clean --apply 확인: 고른 대상 수·실제 확보 용량을 보여 주고 y/yes 일 때만 진행한다(기본 아니요). */
static int console_confirm(const CleanPlanSummary *s, void *ctx)
{
    wchar_t line[32], size[32];
    size_t n;

    (void)ctx;
    format_size(s->reclaim, size, 32);
    fwprintf(stderr,
             L"Move %u selected target(s) (reclaim %ls) to the Recycle Bin or quarantine "
             L"folder? [y/N] ",
             (unsigned)s->targets, size);
    fflush(stderr);
    if (fgetws(line, (int)(sizeof(line) / sizeof(line[0])), stdin) == NULL) {
        return 0;
    }
    n = wcslen(line);
    while (n > 0 && (line[n - 1] == L'\n' || line[n - 1] == L'\r' || line[n - 1] == L' ')) {
        line[--n] = L'\0';
    }
    return _wcsicmp(line, L"y") == 0 || _wcsicmp(line, L"yes") == 0;
}

/* disk-scanner clean ... / find ...: GUI 정리 탭과 같은 탐지·계획·실행(cli_clean.h). argv[0] 이 명령. */
static int run_clean(int argc, wchar_t **argv)
{
    CliCleanOptions opts;
    wchar_t err[512];
    int find = wcscmp(argv[0], L"find") == 0;

    switch (cli_clean_parse_args(argc, argv, &opts, err, sizeof(err) / sizeof(err[0]))) {
    case CLI_PARSE_HELP:
        set_wide_mode(stdout);
        if (find) {
            cli_find_print_usage(stdout);
        } else {
            cli_clean_print_usage(stdout);
        }
        return EXIT_COMPLETE;
    case CLI_PARSE_ERROR:
        fwprintf(stderr, L"error: %ls\n\n", err);
        if (find) {
            cli_find_print_usage(stderr);
        } else {
            cli_clean_print_usage(stderr);
        }
        return EXIT_FATAL;
    default:
        break;
    }
    /* JSON 출력의 stdout 은 UTF-8 바이트를 fwrite 로만 쓴다. */
    if (opts.json || opts.jsonl) {
        _setmode(_fileno(stdout), _O_BINARY);
    } else {
        set_wide_mode(stdout);
    }
    return cli_clean_run(&opts, stdout, stderr, stdin_interactive() ? console_confirm : NULL,
                         NULL);
}

/* disk-scanner devenv ...: 개발 캐시·가상 디스크 분석과 증가 기록(cli_devenv.h). */
static int run_devenv(int argc, wchar_t **argv)
{
    CliDevenvOptions opts;
    wchar_t err[512];

    switch (cli_devenv_parse_args(argc, argv, &opts, err, sizeof(err) / sizeof(err[0]))) {
    case CLI_PARSE_HELP:
        set_wide_mode(stdout);
        cli_devenv_print_usage(stdout);
        return EXIT_COMPLETE;
    case CLI_PARSE_ERROR:
        fwprintf(stderr, L"error: %ls\n\n", err);
        cli_devenv_print_usage(stderr);
        return EXIT_FATAL;
    default:
        break;
    }
    if (opts.json || opts.jsonl) {
        _setmode(_fileno(stdout), _O_BINARY);
    } else {
        set_wide_mode(stdout);
    }
    return cli_devenv_run(&opts, stdout, stderr);
}

int wmain(int argc, wchar_t **argv)
{
    CliOptions opts;
    wchar_t err[512];
    CliParseResult parsed;

    set_wide_mode(stderr);
    /* 개발 환경 분석(읽기 전용). */
    if (argc >= 2 && wcscmp(argv[1], L"devenv") == 0) {
        return run_devenv(argc - 1, argv + 1);
    }
    /* 정리 명령. 이름이 clean·find·devenv 인 폴더를 스캔하려면 "disk-scanner -- clean" 또는 ".\clean". */
    if (argc >= 2 && (wcscmp(argv[1], L"clean") == 0 || wcscmp(argv[1], L"find") == 0)) {
        return run_clean(argc - 1, argv + 1);
    }
    parsed = cli_parse_args(argc, argv, &opts, err, sizeof(err) / sizeof(err[0]));
    if (parsed == CLI_PARSE_OK && opts.threads >= 0) {
        scan_set_threads((unsigned)opts.threads);
    }

    /* JSON Lines 모드의 stdout 은 UTF-8 바이트를 fwrite 로만 쓴다. */
    if (parsed == CLI_PARSE_OK && opts.jsonl) {
        _setmode(_fileno(stdout), _O_BINARY);
        return run_scan_jsonl(&opts);
    }
    set_wide_mode(stdout);

    switch (parsed) {
    case CLI_PARSE_HELP:
        cli_print_usage(stdout);
        return EXIT_COMPLETE;
    case CLI_PARSE_ERROR:
        fwprintf(stderr, L"error: %ls\n\n", err);
        cli_print_usage(stderr);
        return EXIT_FATAL;
    default:
        return opts.track ? run_scan_tracked(&opts) : run_scan(&opts);
    }
}
