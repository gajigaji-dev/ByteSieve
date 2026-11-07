#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "cleanup_job.h"
#include "cli_clean.h"
#include "jsonl.h"
#include "size_format.h"

/* ---- 인자 ---- */

void cli_clean_print_usage(FILE *out)
{
    fwprintf(out,
             L"Usage:\n"
             L"  disk-scanner clean project <directory> [--category LIST] [--eco LIST]\n"
             L"                             [--apply [--yes]] [--jsonl]\n"
             L"  disk-scanner clean system [--include LIST] [--exclude LIST]\n"
             L"                            [--apply [--yes]] [--jsonl]\n"
             L"  disk-scanner clean -h | --help\n"
             L"\n"
             L"Finds cleanup targets the same way as the GUI ([Project cleanup] / [System cleanup])\n"
             L"and prints a plan. Nothing is moved unless --apply is given.\n"
             L"\n"
             L"  project <directory>  node_modules, build output, caches, temp files and logs in the folder\n"
             L"  system               user temp files, Windows error reports, browser caches,\n"
             L"                       shared developer caches (npm, pip, Gradle, Cargo, NuGet)\n"
             L"\n"
             L"Options:\n"
             L"  --category LIST   project: only these types (node_modules,build,cache,temp,log)\n"
             L"  --eco LIST        project: only these ecosystems (node,python,rust,java,dotnet,cpp,go,generic)\n"
             L"  --include LIST    system: also select these targets. Shared developer caches are never\n"
             L"                    selected by default: name them (npm,pip,gradle,gradle_wrapper,\n"
             L"                    cargo_registry,cargo_git,nuget_packages,nuget_http) or use dev_cache\n"
             L"  --exclude LIST    system: do not select these targets (user_temp,wer,chrome,edge,firefox,...)\n"
             L"  --apply           move the selected targets to the Recycle Bin (quarantine folder on\n"
             L"                    drives without one) after confirmation. Never deletes permanently.\n"
             L"  --yes             do not ask for confirmation (required when input is not interactive)\n"
             L"  --jsonl           write the plan and results as JSON Lines (UTF-8) to stdout\n"
             L"\n"
             L"Emptying the Recycle Bin is permanent and is only available in the GUI.\n"
             L"Exit codes: 0 done, 1 some folders unreadable or some items not moved, 2 error.\n");
}

static int is_opt(const wchar_t *arg, const wchar_t *name, const wchar_t **value)
{
    size_t n = wcslen(name);

    if (wcsncmp(arg, name, n) != 0) {
        return 0;
    }
    if (arg[n] == L'\0') {
        *value = NULL;
        return 1;
    }
    if (arg[n] == L'=') {
        *value = arg + n + 1;
        return 1;
    }
    return 0;
}

static int known_system_id(const wchar_t *id)
{
    int k;

    for (k = 0; k < CLEANER_KIND_COUNT; k++) {
        if (_wcsicmp(cleaner_kind_info((CleanerKind)k)->id, id) == 0) {
            return 1;
        }
    }
    for (k = 0; k < DEV_CACHE_KIND_COUNT; k++) {
        if (_wcsicmp(dev_cache_kind_info((DevCacheKind)k)->id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * 쉼표 목록을 opts->ids 에 복사해 자르고 이름마다 check 한다. kind: 0 유형, 1 환경, 2 시스템 대상.
 * 유형·환경은 비트로, 시스템 대상은 list 에 담는다. 실패하면 -1(err).
 */
static int parse_list(CliCleanOptions *opts, size_t *used, const wchar_t *value, int kind,
                      const wchar_t **list, size_t *count, wchar_t *err, size_t err_len)
{
    size_t n = wcslen(value), cap = sizeof(opts->ids) / sizeof(opts->ids[0]);
    wchar_t *copy, *ctx = NULL, *tok;

    if (n == 0) {
        swprintf(err, err_len, L"empty list");
        return -1;
    }
    if (*used + n + 1 > cap) {
        swprintf(err, err_len, L"too many names");
        return -1;
    }
    copy = opts->ids + *used;
    wcscpy_s(copy, cap - *used, value);
    *used += n + 1;
    for (tok = wcstok_s(copy, L",", &ctx); tok != NULL; tok = wcstok_s(NULL, L",", &ctx)) {
        int c, found = 0;
        while (*tok == L' ') {
            tok++;
        }
        if (*tok == L'\0') {
            continue;
        }
        if (kind == 0) {
            for (c = CLEANUP_NONE + 1; c < CLEANUP_CATEGORY_COUNT; c++) {
                if (_wcsicmp(cleanup_category_name((CleanupCategory)c), tok) == 0) {
                    opts->categories |= CLEANUP_MASK(c);
                    found = 1;
                }
            }
        } else if (kind == 1) {
            for (c = 0; c < CLEANUP_ECO_COUNT; c++) {
                if (_wcsicmp(cleanup_eco_name((CleanupEco)c), tok) == 0) {
                    opts->ecos |= CLEANUP_ECO_BIT(c);
                    found = 1;
                }
            }
        } else {
            if (_wcsicmp(tok, cleaner_kind_info(CLEANER_RECYCLE_BIN)->id) == 0) {
                swprintf(err, err_len,
                         L"emptying the Recycle Bin is permanent and is only available in the GUI");
                return -1;
            }
            found = known_system_id(tok);
            if (found) {
                if (*count >= CLI_CLEAN_MAX_IDS) {
                    swprintf(err, err_len, L"too many names");
                    return -1;
                }
                list[(*count)++] = tok;
            }
        }
        if (!found) {
            swprintf(err, err_len, L"unknown %ls: %ls",
                     kind == 0 ? L"category" : kind == 1 ? L"ecosystem" : L"target", tok);
            return -1;
        }
    }
    return 0;
}

CliParseResult cli_clean_parse_args(int argc, wchar_t **argv, CliCleanOptions *opts,
                                    wchar_t *err, size_t err_len)
{
    int i, options_done = 0, positional = 0;
    size_t used = 0;

    memset(opts, 0, sizeof(*opts));
    err[0] = L'\0';
    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--") == 0) {
            break;
        }
        if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0) {
            return CLI_PARSE_HELP;
        }
    }
    if (argc < 2) {
        swprintf(err, err_len, L"missing command (project or system)");
        return CLI_PARSE_ERROR;
    }
    if (wcscmp(argv[1], L"project") == 0) {
        opts->command = CLI_CLEAN_PROJECT;
    } else if (wcscmp(argv[1], L"system") == 0) {
        opts->command = CLI_CLEAN_SYSTEM;
    } else {
        swprintf(err, err_len, L"unknown command: %ls (expected project or system)", argv[1]);
        return CLI_PARSE_ERROR;
    }
    for (i = 2; i < argc; i++) {
        const wchar_t *arg = argv[i], *value = NULL;
        int kind = -1;
        const wchar_t **list = NULL;
        size_t *count = NULL;

        if (!options_done && wcscmp(arg, L"--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done && wcscmp(arg, L"--apply") == 0) {
            opts->apply = 1;
            continue;
        }
        if (!options_done && wcscmp(arg, L"--yes") == 0) {
            opts->yes = 1;
            continue;
        }
        if (!options_done && wcscmp(arg, L"--jsonl") == 0) {
            opts->jsonl = 1;
            continue;
        }
        if (!options_done && is_opt(arg, L"--category", &value)) {
            kind = 0;
        } else if (!options_done && is_opt(arg, L"--eco", &value)) {
            kind = 1;
        } else if (!options_done && is_opt(arg, L"--include", &value)) {
            kind = 2;
            list = opts->include;
            count = &opts->include_count;
        } else if (!options_done && is_opt(arg, L"--exclude", &value)) {
            kind = 2;
            list = opts->exclude;
            count = &opts->exclude_count;
        }
        if (kind >= 0) {
            const wchar_t *name = kind == 0 ? L"--category" : kind == 1 ? L"--eco"
                                  : list == opts->include ? L"--include"
                                                          : L"--exclude";
            if ((kind < 2) != (opts->command == CLI_CLEAN_PROJECT)) {
                swprintf(err, err_len, L"%ls is not valid for clean %ls", name, argv[1]);
                return CLI_PARSE_ERROR;
            }
            if (value == NULL) {
                if (i + 1 >= argc) {
                    swprintf(err, err_len, L"%ls requires a value", name);
                    return CLI_PARSE_ERROR;
                }
                value = argv[++i];
            }
            if (parse_list(opts, &used, value, kind, list, count, err, err_len) != 0) {
                return CLI_PARSE_ERROR;
            }
            continue;
        }
        if (!options_done && arg[0] == L'-' && arg[1] != L'\0') {
            swprintf(err, err_len, L"unknown option: %ls", arg);
            return CLI_PARSE_ERROR;
        }
        if (opts->command == CLI_CLEAN_SYSTEM) {
            swprintf(err, err_len, L"clean system takes no folder: %ls", arg);
            return CLI_PARSE_ERROR;
        }
        if (positional++ > 0) {
            swprintf(err, err_len, L"only one folder can be given");
            return CLI_PARSE_ERROR;
        }
        opts->target = arg;
    }
    if (opts->command == CLI_CLEAN_PROJECT && opts->target == NULL) {
        swprintf(err, err_len, L"missing folder for clean project");
        return CLI_PARSE_ERROR;
    }
    if (opts->yes && !opts->apply) {
        swprintf(err, err_len, L"--yes requires --apply");
        return CLI_PARSE_ERROR;
    }
    return CLI_PARSE_OK;
}

/* ---- 선택 ---- */

/* 공용 개발 캐시: 시스템 정리에서 직접 골라야 하는 대상 중 영구 삭제가 아닌 것. */
static int is_dev_cache(const CleanPlanTarget *t)
{
    return t->source == CLEAN_SOURCE_SYSTEM && (t->flags & CLEAN_PLAN_MANUAL) &&
           !(t->flags & CLEAN_PLAN_PERMANENT);
}

static void select_named(CleanPlan *plan, const wchar_t *id, int on)
{
    size_t i;

    if (_wcsicmp(id, cleaner_kind_info(CLEANER_DEV_CACHE)->id) == 0) {
        for (i = 0; i < plan->len; i++) {
            if (is_dev_cache(&plan->items[i])) {
                clean_plan_set_selected(plan, i, on);
            }
        }
        return;
    }
    clean_plan_select_id(plan, id, on);
}

size_t cli_clean_select(CleanPlan *plan, const CliCleanOptions *opts)
{
    size_t i;
    CleanPlanSummary s;

    /* GUI [모두 선택]과 같은 정책: MANUAL(휴지통 비우기·공용 개발 캐시)은 고르지 않는다. */
    clean_plan_select_all(plan, 0);
    clean_plan_select_all(plan, 1);
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        if (t->source != CLEAN_SOURCE_PROJECT) {
            continue;
        }
        if ((opts->categories != 0 && !(opts->categories & CLEANUP_MASK(t->category))) ||
            (opts->ecos != 0 && !(opts->ecos & CLEANUP_ECO_BIT(t->eco)))) {
            clean_plan_set_selected(plan, i, 0);
        }
    }
    for (i = 0; i < opts->include_count; i++) {
        select_named(plan, opts->include[i], 1);
    }
    for (i = 0; i < opts->exclude_count; i++) {
        select_named(plan, opts->exclude[i], 0);
    }
    /* 영구 삭제 대상은 CLI 에서 어떤 경우에도 고르지 않는다. */
    for (i = 0; i < plan->len; i++) {
        if (plan->items[i].flags & CLEAN_PLAN_PERMANENT) {
            clean_plan_set_selected(plan, i, 0);
        }
    }
    clean_plan_summarize(plan, &s);
    return s.targets;
}

/* ---- 텍스트 출력 ---- */

static void flags_text(const CleanPlanTarget *t, wchar_t *buf, size_t len)
{
    buf[0] = L'\0';
    swprintf(buf, len, L"%ls%ls%ls%ls", (t->flags & CLEAN_PLAN_MANUAL) ? L"manual," : L"",
             (t->flags & CLEAN_PLAN_CAUTION) ? L"caution," : L"",
             (t->flags & CLEAN_PLAN_GUESSED) ? L"guessed," : L"",
             (t->flags & CLEAN_PLAN_PERMANENT) ? L"permanent," : L"");
    if (buf[0] != L'\0') {
        buf[wcslen(buf) - 1] = L'\0';
    } else {
        swprintf(buf, len, L"-");
    }
}

static void print_plan(FILE *out, const CleanPlan *plan, const CliCleanOptions *opts,
                       const wchar_t *root)
{
    CleanPlanSummary s;
    size_t i;
    wchar_t reclaim[32], size[32], flags[64];

    if (opts->command == CLI_CLEAN_PROJECT) {
        fwprintf(out, L"Project cleanup plan: %ls\n", root);
    } else {
        fwprintf(out, L"System cleanup plan\n");
    }
    if (plan->len == 0) {
        fwprintf(out, L"  (no cleanup targets found)\n");
    } else {
        fwprintf(out, L"  %-4ls %-4ls %-10ls %-10ls %-16ls %-24ls %ls\n", L"#", L"Sel",
                 L"Reclaim", L"Size", L"State", L"Target", L"Location");
    }
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        const wchar_t *mark = t->selected ? L"[x]" : clean_plan_selectable(t) ? L"[ ]" : L"[-]";
        format_size(t->reclaim, reclaim, 32);
        format_size(t->bytes, size, 32);
        flags_text(t, flags, 64);
        fwprintf(out, L"  %-4u %-4ls %-10ls %-10ls %-16ls %-24ls %ls",
                 (unsigned)(i + 1), mark, t->ready ? reclaim : L"-", t->ready ? size : L"-",
                 t->state, t->id != NULL ? t->id : L"", t->location != NULL ? t->location : L"");
        if (wcscmp(flags, L"-") != 0) {
            fwprintf(out, L"  (%ls)", flags);
        }
        fwprintf(out, L"\n");
    }
    clean_plan_summarize(plan, &s);
    format_size(s.reclaim, reclaim, 32);
    format_size(s.bytes, size, 32);
    fwprintf(out, L"Selected %u of %u selectable target(s): reclaim %ls (size %ls)\n",
             (unsigned)s.targets, (unsigned)s.selectable, reclaim, size);
    if (s.caution > 0) {
        fwprintf(out, L"Caution: %u selected target(s) cannot be rebuilt without internet access.\n",
                 (unsigned)s.caution);
    }
    if (s.manual > 0) {
        fwprintf(out, L"Shared developer caches selected: %u (they will be downloaded again on the "
                      L"next build or install).\n",
                 (unsigned)s.manual);
    }
    if (s.guessed > 0) {
        fwprintf(out, L"Guessed: %u selected target(s) have no project file next to them and may "
                      L"not be build output.\n",
                 (unsigned)s.guessed);
    }
    for (i = 0; i < plan->len; i++) {
        if (is_dev_cache(&plan->items[i]) && !plan->items[i].selected &&
            clean_plan_selectable(&plan->items[i])) {
            fwprintf(out, L"Shared developer caches are not selected by default; use --include "
                          L"<name> or --include dev_cache.\n");
            break;
        }
    }
}

static int entry_unmoved(const CleanPlanEntry *e)
{
    CleanupTrashStatus st = e->outcome.status;

    return e->skip != CLEAN_SKIP_NONE ||
           (st != CLEANUP_TRASH_PENDING && st != CLEANUP_TRASH_MOVED &&
            st != CLEANUP_TRASH_QUARANTINED);
}

static void print_result(FILE *out, const CleanPlan *plan, const CleanPlanTotals *tot)
{
    size_t i, j;
    wchar_t size[32];

    fwprintf(out, L"Result:\n");
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        if (!t->ran) {
            continue;
        }
        format_size(t->moved_bytes, size, 32);
        fwprintf(out, L"  %-4u %-24ls moved %u (%ls, quarantine %u), failed %u, skipped %u%ls\n",
                 (unsigned)(i + 1), t->id != NULL ? t->id : L"", (unsigned)t->moved, size,
                 (unsigned)t->quarantined, (unsigned)t->failed, (unsigned)t->skipped,
                 t->cancelled > 0 ? L", cancelled" : L"");
        for (j = 0; j < t->entry_count; j++) {
            const CleanPlanEntry *e = &t->entries[j];
            if (!entry_unmoved(e)) {
                continue;
            }
            if (e->skip != CLEAN_SKIP_NONE) {
                fwprintf(out, L"         not moved: %ls (%ls)\n", e->item.path,
                         clean_skip_name(e->skip));
            } else {
                fwprintf(out, L"         not moved: %ls (%ls, 0x%08X)\n", e->item.path,
                         cleanup_trash_status_name(e->outcome.status),
                         (unsigned)e->outcome.code);
            }
        }
    }
    format_size(tot->moved_bytes, size, 32);
    fwprintf(out, L"Moved %u item(s) (%ls), %u to the quarantine folder; failed %u, skipped %u%ls.\n",
             (unsigned)tot->moved, size, (unsigned)tot->quarantined, (unsigned)tot->failed,
             (unsigned)tot->skipped, tot->was_cancelled ? L", cancelled" : L"");
    fwprintf(out, L"Space is freed when the Recycle Bin or quarantine folder is emptied. "
                  L"Moved items can be restored from there.\n");
}

/* ---- JSON Lines ---- */

static void jsonl_plan(JsonBuf *buf, const CleanPlan *plan, const CliCleanOptions *opts,
                       const wchar_t *root)
{
    CleanPlanSummary s;
    size_t i;

    jsonl_begin_event(buf, "clean_plan");
    jsonl_field_string(buf, "source", opts->command == CLI_CLEAN_PROJECT ? L"project" : L"system");
    jsonl_field_string(buf, "root", root);
    jsonl_field_u64(buf, "targets", plan->len);
    jsonl_end_event(buf);
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        jsonl_begin_event(buf, "clean_target");
        jsonl_field_u64(buf, "index", i + 1);
        jsonl_field_string(buf, "id", t->id);
        jsonl_field_string(buf, "label", t->label);
        jsonl_field_string(buf, "location", t->location);
        jsonl_field_string(buf, "state", t->state);
        jsonl_field_u64(buf, "bytes", t->bytes);
        jsonl_field_u64(buf, "reclaim", t->reclaim);
        jsonl_field_u64(buf, "shared", t->shared);
        jsonl_field_u64(buf, "files", t->files);
        jsonl_field_bool(buf, "selectable", clean_plan_selectable(t));
        jsonl_field_bool(buf, "selected", t->selected);
        jsonl_field_bool(buf, "manual", (t->flags & CLEAN_PLAN_MANUAL) != 0);
        jsonl_field_bool(buf, "caution", (t->flags & CLEAN_PLAN_CAUTION) != 0);
        jsonl_field_bool(buf, "guessed", (t->flags & CLEAN_PLAN_GUESSED) != 0);
        jsonl_field_bool(buf, "permanent", (t->flags & CLEAN_PLAN_PERMANENT) != 0);
        jsonl_end_event(buf);
    }
    clean_plan_summarize(plan, &s);
    jsonl_begin_event(buf, "clean_summary");
    jsonl_field_u64(buf, "selected", s.targets);
    jsonl_field_u64(buf, "selectable", s.selectable);
    jsonl_field_u64(buf, "bytes", s.bytes);
    jsonl_field_u64(buf, "reclaim", s.reclaim);
    jsonl_field_u64(buf, "caution", s.caution);
    jsonl_field_u64(buf, "manual", s.manual);
    jsonl_field_u64(buf, "guessed", s.guessed);
    jsonl_field_bool(buf, "apply", opts->apply);
    jsonl_end_event(buf);
}

static void jsonl_results(JsonBuf *buf, const CleanPlan *plan)
{
    size_t i, j;

    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        int first = 1;
        if (!t->ran) {
            continue;
        }
        jsonl_begin_event(buf, "clean_result");
        jsonl_field_u64(buf, "index", i + 1);
        jsonl_field_string(buf, "id", t->id);
        jsonl_field_u64(buf, "moved", t->moved);
        jsonl_field_u64(buf, "quarantined", t->quarantined);
        jsonl_field_u64(buf, "failed", t->failed);
        jsonl_field_u64(buf, "skipped", t->skipped);
        jsonl_field_u64(buf, "cancelled", t->cancelled);
        jsonl_field_u64(buf, "moved_bytes", t->moved_bytes);
        jsonl_field_key(buf, "entries");
        jsonl_raw(buf, "[");
        for (j = 0; j < t->entry_count; j++) {
            const CleanPlanEntry *e = &t->entries[j];
            if (e->skip == CLEAN_SKIP_NONE && e->outcome.status == CLEANUP_TRASH_PENDING) {
                continue;
            }
            jsonl_raw(buf, first ? "{" : ",{");
            first = 0;
            jsonl_field_string(buf, "path", e->item.path);
            jsonl_field_string(buf, "status", cleanup_trash_status_name(e->outcome.status));
            jsonl_field_string(buf, "skip", clean_skip_name(e->skip));
            jsonl_field_u64(buf, "code", e->outcome.code);
            jsonl_field_string(buf, "moved_to", e->outcome.recycled_path);
            jsonl_raw(buf, "}");
        }
        jsonl_raw(buf, "]");
        jsonl_end_event(buf);
    }
}

static void jsonl_done(JsonBuf *buf, const CleanPlanTotals *tot, int applied, int code)
{
    jsonl_begin_event(buf, "complete");
    jsonl_field_bool(buf, "applied", applied);
    jsonl_field_u64(buf, "moved", tot != NULL ? tot->moved : 0);
    jsonl_field_u64(buf, "quarantined", tot != NULL ? tot->quarantined : 0);
    jsonl_field_u64(buf, "failed", tot != NULL ? tot->failed : 0);
    jsonl_field_u64(buf, "skipped", tot != NULL ? tot->skipped : 0);
    jsonl_field_u64(buf, "moved_bytes", tot != NULL ? tot->moved_bytes : 0);
    jsonl_field_u64(buf, "exit_code", (uint64_t)code);
    jsonl_end_event(buf);
}

/* ---- 실행 ---- */

/* 탐지해 계획을 만든다. 탐지 상태(SCAN_*)를 돌려주며 메모리 부족이면 SCAN_FATAL·*oom. */
static ScanStatus detect(const CliCleanOptions *opts, CleanPlan *plan, wchar_t **root, int *oom)
{
    *oom = 0;
    *root = NULL;
    if (opts->command == CLI_CLEAN_PROJECT) {
        /* GUI [프로젝트 정리]와 같은 탐지 작업(보호 경로·앱 데이터 폴더 제외 포함). */
        CleanupResult r;
        CleanupJob *job = cleanup_job_start(opts->target, 1000, NULL, NULL);
        ScanStatus st;
        if (job == NULL) {
            *oom = 1;
            return SCAN_FATAL;
        }
        cleanup_result_init(&r);
        st = cleanup_job_finish(job, &r);
        if (st != SCAN_FATAL && clean_plan_from_project(plan, &r, NULL) != 0) {
            *oom = 1;
            st = SCAN_FATAL;
        }
        *root = r.root;
        r.root = NULL;
        cleanup_result_free(&r);
        return st;
    } else {
        /* GUI [시스템 정리]와 같은 분석. */
        CleanerList list;
        int rc;
        cleaner_list_init(&list);
        rc = cleaner_analyze(&list, NULL, NULL, NULL, NULL, NULL);
        if (rc != 0 || cleaner_build_plan(&list, plan) != 0) {
            *oom = 1;
            cleaner_list_free(&list);
            return SCAN_FATAL;
        }
        cleaner_list_free(&list);
        return SCAN_COMPLETE;
    }
}

int cli_clean_run(const CliCleanOptions *opts, FILE *out, FILE *err,
                  cli_clean_confirm_fn confirm, void *ctx)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CleanPlan plan;
    CleanPlanTotals totals;
    CleanPlanSummary s;
    JsonBuf buf;
    ScanStatus st;
    wchar_t *root = NULL;
    int oom, code = EXIT_COMPLETE, applied = 0;

    clean_plan_init(&plan);
    jsonl_buf_init(&buf);
    memset(&totals, 0, sizeof(totals));
    st = detect(opts, &plan, &root, &oom);
    if (st == SCAN_FATAL) {
        if (oom) {
            fwprintf(err, L"error: out of memory\n");
        } else {
            fwprintf(err, L"error: cannot scan %ls\n", opts->target != NULL ? opts->target : L"");
        }
        code = EXIT_FATAL;
        goto done;
    }
    if (st == SCAN_PARTIAL) {
        fwprintf(err, L"warning: some folders could not be read; sizes may be incomplete\n");
        code = EXIT_PARTIAL;
    }
    cli_clean_select(&plan, opts);
    clean_plan_summarize(&plan, &s);
    if (opts->jsonl) {
        jsonl_plan(&buf, &plan, opts, root);
        if (!jsonl_buf_flush(&buf, out)) {
            code = EXIT_FATAL;
            goto done;
        }
    } else {
        print_plan(out, &plan, opts, root);
    }
    if (!opts->apply) {
        if (!opts->jsonl) {
            fwprintf(out, L"Preview only - nothing was moved. Add --apply to move the selected "
                          L"targets to the Recycle Bin.\n");
        }
        goto done;
    }
    if (s.targets == 0) {
        fwprintf(opts->jsonl ? err : out, L"Nothing selected; nothing was moved.\n");
        goto done;
    }
    if (!opts->yes) {
        if (confirm == NULL) {
            fwprintf(err, L"error: confirmation is not possible (input is not interactive); "
                          L"add --yes to apply\n");
            code = EXIT_FATAL;
            goto done;
        }
        if (!confirm(&s, ctx)) {
            fwprintf(opts->jsonl ? err : out, L"Cancelled; nothing was moved.\n");
            goto done;
        }
    }
    {
        CleanPlanOps ops;
        memset(&ops, 0, sizeof(ops)); /* allow_permanent = 0: CLI 는 영구 삭제하지 않는다 */
        if (clean_plan_run(&plan, &ops, &totals) != 0) {
            fwprintf(opts->jsonl ? err : out, L"Nothing to move.\n");
            goto done;
        }
    }
    applied = 1;
    if (totals.failed + totals.skipped + totals.cancelled > 0) {
        code = EXIT_PARTIAL;
    }
    if (opts->jsonl) {
        jsonl_results(&buf, &plan);
    } else {
        print_result(out, &plan, &totals);
    }
done:
    if (opts->jsonl && code != EXIT_FATAL) {
        jsonl_done(&buf, applied ? &totals : NULL, applied, code);
        if (!jsonl_buf_flush(&buf, out)) {
            fwprintf(err, L"error: failed to write JSON output\n");
            code = EXIT_FATAL;
        }
    }
    jsonl_buf_free(&buf);
    clean_plan_free(&plan);
    free(root);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return code;
}
