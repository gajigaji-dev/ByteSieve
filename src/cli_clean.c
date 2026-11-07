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
             L"  disk-scanner clean project <directory> [--category LIST] [--eco LIST] [--include-dirty]\n"
             L"                             [--dormant-days N] [--no-git]\n"
             L"                             [--dry-run | --execute [--yes]] [--json | --jsonl]\n"
             L"  disk-scanner clean system [--include LIST] [--exclude LIST]\n"
             L"                            [--dry-run | --execute [--yes]] [--json | --jsonl]\n"
             L"  disk-scanner clean -h | --help\n"
             L"\n"
             L"Finds cleanup targets the same way as the GUI ([Project cleanup] / [System cleanup])\n"
             L"and prints the plan and expected result. Nothing is moved unless --execute is given.\n"
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
             L"  --include-dirty   project: also select candidates in projects with uncommitted changes\n"
             L"                    (they are never selected by default)\n"
             L"  --dormant-days N  project: a project is dormant after N days without activity (default 90)\n"
             L"  --no-git          project: do not run git.exe; read .git directly (less detail)\n"
             L"  --dry-run         show the plan and expected result only (default)\n"
             L"  --execute         move the selected targets to the Recycle Bin (quarantine folder on\n"
             L"                    drives without one) after confirmation. Never deletes permanently.\n"
             L"                    (--apply is the old name and still works)\n"
             L"  --yes             do not ask for confirmation (required when input is not interactive)\n"
             L"  --json            write one JSON document (UTF-8) to stdout\n"
             L"  --jsonl           write JSON Lines events (UTF-8) to stdout\n"
             L"\n"
             L"Emptying the Recycle Bin is permanent and is only available in the GUI.\n"
             L"Exit codes: 0 done, 1 some folders unreadable or some items not moved, 2 error.\n");
}

void cli_find_print_usage(FILE *out)
{
    fwprintf(out,
             L"Usage:\n"
             L"  disk-scanner find <directory> [--category LIST] [--eco LIST] [--dormant-days N]\n"
             L"                    [--no-git] [--json | --jsonl]\n"
             L"  disk-scanner find -h | --help\n"
             L"\n"
             L"Lists project cleanup candidates (node_modules, build output, caches, temp files, logs)\n"
             L"under <directory>, largest first, with the space that deleting them would free.\n"
             L"Read-only: nothing is changed. Candidates in protected locations are listed with the\n"
             L"reason they would not be cleaned. Use 'disk-scanner clean project' to clean them.\n"
             L"\n"
             L"Options:\n"
             L"  --category LIST   only these types (node_modules,build,cache,temp,log)\n"
             L"  --eco LIST        only these ecosystems (node,python,rust,java,dotnet,cpp,go,generic)\n"
             L"  --dormant-days N  a project is dormant after N days without activity (default 90)\n"
             L"  --no-git          do not run git.exe; read .git directly (less detail)\n"
             L"  --json            write one JSON document (UTF-8) to stdout\n"
             L"  --jsonl           write JSON Lines events (UTF-8) to stdout\n"
             L"\n"
             L"Candidates are grouped by project (Git repository, or the folder that holds them) with\n"
             L"the last commit, last file change, dormancy, uncommitted changes and unpushed commits,\n"
             L"and whether each candidate can be restored after deletion.\n"
             L"\n"
             L"Exit codes: 0 complete, 1 some folders could not be read, 2 error.\n");
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
    int i, start, options_done = 0, positional = 0;
    size_t used = 0;
    const wchar_t *cmd;

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
    if (argc >= 1 && wcscmp(argv[0], L"find") == 0) {
        opts->command = CLI_CLEAN_FIND;
        cmd = L"find";
        start = 1;
    } else {
        if (argc < 2) {
            swprintf(err, err_len, L"missing command (project or system)");
            return CLI_PARSE_ERROR;
        }
        if (wcscmp(argv[1], L"project") == 0) {
            opts->command = CLI_CLEAN_PROJECT;
            cmd = L"clean project";
        } else if (wcscmp(argv[1], L"system") == 0) {
            opts->command = CLI_CLEAN_SYSTEM;
            cmd = L"clean system";
        } else {
            swprintf(err, err_len, L"unknown command: %ls (expected project or system)", argv[1]);
            return CLI_PARSE_ERROR;
        }
        start = 2;
    }
    for (i = start; i < argc; i++) {
        const wchar_t *arg = argv[i], *value = NULL;
        int kind = -1, *flag = NULL;
        const wchar_t **list = NULL;
        size_t *count = NULL;

        if (!options_done && wcscmp(arg, L"--") == 0) {
            options_done = 1;
            continue;
        }
        if (!options_done) {
            if (wcscmp(arg, L"--execute") == 0 || wcscmp(arg, L"--apply") == 0) {
                flag = &opts->apply;
            } else if (wcscmp(arg, L"--dry-run") == 0) {
                flag = &opts->dry_run;
            } else if (wcscmp(arg, L"--yes") == 0) {
                flag = &opts->yes;
            } else if (wcscmp(arg, L"--json") == 0) {
                flag = &opts->json;
            } else if (wcscmp(arg, L"--jsonl") == 0) {
                flag = &opts->jsonl;
            } else if (wcscmp(arg, L"--include-dirty") == 0 || wcscmp(arg, L"--no-git") == 0) {
                /* 프로젝트 분석 옵션: find·clean project 만 */
                if (opts->command == CLI_CLEAN_SYSTEM ||
                    (opts->command == CLI_CLEAN_FIND && arg[2] == L'i')) {
                    swprintf(err, err_len, L"%ls is not valid for %ls", arg, cmd);
                    return CLI_PARSE_ERROR;
                }
                if (arg[2] == L'i') {
                    opts->include_dirty = 1;
                } else {
                    opts->no_git = 1;
                }
                continue;
            }
        }
        if (!options_done && is_opt(arg, L"--dormant-days", &value)) {
            wchar_t *end = NULL;
            unsigned long days;
            if (opts->command == CLI_CLEAN_SYSTEM) {
                swprintf(err, err_len, L"--dormant-days is not valid for %ls", cmd);
                return CLI_PARSE_ERROR;
            }
            if (value == NULL) {
                if (i + 1 >= argc) {
                    swprintf(err, err_len, L"--dormant-days requires a value");
                    return CLI_PARSE_ERROR;
                }
                value = argv[++i];
            }
            days = wcstoul(value, &end, 10);
            if (end == value || *end != L'\0' || days == 0 || days > 36500 || value[0] == L'-') {
                swprintf(err, err_len, L"--dormant-days must be a number of days (1-36500): %ls",
                         value);
                return CLI_PARSE_ERROR;
            }
            opts->dormant_days = (unsigned)days;
            continue;
        }
        if (flag != NULL) {
            /* find 는 읽기 전용: 실행 관련 옵션이 없다. */
            if (opts->command == CLI_CLEAN_FIND && flag != &opts->json && flag != &opts->jsonl) {
                swprintf(err, err_len, L"%ls is not valid for find (find never changes anything)",
                         arg);
                return CLI_PARSE_ERROR;
            }
            *flag = 1;
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
            if ((kind < 2) == (opts->command == CLI_CLEAN_SYSTEM)) {
                swprintf(err, err_len, L"%ls is not valid for %ls", name, cmd);
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
    if (opts->command != CLI_CLEAN_SYSTEM && opts->target == NULL) {
        swprintf(err, err_len, L"missing folder for %ls", cmd);
        return CLI_PARSE_ERROR;
    }
    if (opts->apply && opts->dry_run) {
        swprintf(err, err_len, L"--dry-run and --execute cannot be used together");
        return CLI_PARSE_ERROR;
    }
    if (opts->json && opts->jsonl) {
        swprintf(err, err_len, L"--json and --jsonl cannot be used together");
        return CLI_PARSE_ERROR;
    }
    if (opts->yes && !opts->apply) {
        swprintf(err, err_len, L"--yes requires --execute");
        return CLI_PARSE_ERROR;
    }
    return CLI_PARSE_OK;
}

/* ---- 프로젝트(Git) 정보 ---- */

/* 지금 실행의 프로젝트 분석 결과(find·clean project). CLI 는 한 스레드에서만 실행한다. */
static const ProjectList *g_projects;

static const ProjectInfo *project_at(const CleanPlanTarget *t)
{
    return g_projects != NULL && t->project < g_projects->len ? &g_projects->items[t->project]
                                                              : NULL;
}

static uint64_t ft_to_unix(uint64_t ft)
{
    return ft > 116444736000000000ull ? (ft - 116444736000000000ull) / 10000000ull : 0;
}

/* FILETIME(UTC) → 현지 날짜 "YYYY-MM-DD"(0 이면 "-"). */
static void date_text(uint64_t ft, wchar_t *buf, size_t len)
{
    FILETIME u, l;
    SYSTEMTIME st;

    if (ft == 0) {
        swprintf(buf, len, L"-");
        return;
    }
    u.dwLowDateTime = (DWORD)ft;
    u.dwHighDateTime = (DWORD)(ft >> 32);
    if (!FileTimeToLocalFileTime(&u, &l) || !FileTimeToSystemTime(&l, &st)) {
        swprintf(buf, len, L"-");
        return;
    }
    swprintf(buf, len, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
}

/* 프로젝트 한 줄 설명: "git main · last activity 2020-01-15 (1757 days ago) · dormant · ..." */
static void project_line(const ProjectInfo *p, wchar_t *buf, size_t len)
{
    wchar_t date[32];
    size_t used;

    date_text(p->last_activity, date, 32);
    if (p->is_git) {
        used = (size_t)swprintf(buf, len, L"git %ls%ls", p->branch != NULL ? p->branch : L"?",
                                p->git_source == PROJECT_GIT_DIRECT ? L" (read .git)" : L"");
    } else {
        used = (size_t)swprintf(buf, len, L"not git");
    }
    used += (size_t)swprintf(buf + used, len - used, L" | last activity %ls", date);
    if (p->last_activity != 0) {
        used += (size_t)swprintf(buf + used, len - used, L" (%u days ago)", p->age_days);
    }
    if (p->dormant) {
        used += (size_t)swprintf(buf + used, len - used, L" | DORMANT");
    }
    if (p->changes > 0) {
        used += (size_t)swprintf(buf + used, len - used, L" | UNCOMMITTED CHANGES: %d",
                                 p->changes);
    } else if (p->is_git && p->changes < 0) {
        used += (size_t)swprintf(buf + used, len - used, L" | uncommitted changes unknown");
    }
    if (p->is_git && p->has_remote == 0 && p->unpushed > 0) {
        used += (size_t)swprintf(buf + used, len - used,
                                 L" | NO REMOTE: %d commit(s) only on this PC", p->unpushed);
    } else if (p->unpushed > 0) {
        used += (size_t)swprintf(buf + used, len - used, L" | UNPUSHED COMMITS: %d", p->unpushed);
    } else if (p->remote_differs) {
        used += (size_t)swprintf(buf + used, len - used, L" | DIFFERS FROM REMOTE");
    }
    if (p->modified_incomplete) {
        swprintf(buf + used, len - used, L" | partly scanned");
    }
}

static void print_projects(FILE *out)
{
    size_t i, dormant = 0, dirty = 0, unpushed = 0;
    wchar_t line[512];

    if (g_projects == NULL || g_projects->len == 0) {
        return;
    }
    for (i = 0; i < g_projects->len; i++) {
        const ProjectInfo *p = &g_projects->items[i];
        dormant += p->dormant != 0;
        dirty += p->changes > 0;
        unpushed += p->unpushed > 0 || p->remote_differs;
    }
    fwprintf(out, L"Projects: %u (%u dormant after %u days, %u with uncommitted changes, %u with "
                  L"unpushed commits)%ls\n",
             (unsigned)g_projects->len, (unsigned)dormant, g_projects->dormant_days,
             (unsigned)dirty, (unsigned)unpushed,
             g_projects->git_available ? L"" : L" - git.exe not used, .git read directly");
    for (i = 0; i < g_projects->len; i++) {
        const ProjectInfo *p = &g_projects->items[i];
        project_line(p, line, 512);
        fwprintf(out, L"  P%-3u %ls\n       %ls\n", (unsigned)(i + 1), p->root, line);
    }
}

static void json_int_or_null(JsonBuf *b, const char *key, int v)
{
    if (v < 0) {
        jsonl_field_key(b, key);
        jsonl_raw(b, "null");
    } else {
        jsonl_field_u64(b, key, (uint64_t)v);
    }
}

static void json_time(JsonBuf *b, const char *key, uint64_t ft)
{
    if (ft == 0) {
        jsonl_field_key(b, key);
        jsonl_raw(b, "null");
    } else {
        jsonl_field_u64(b, key, ft_to_unix(ft));
    }
}

/* 프로젝트 하나의 필드(객체를 연 뒤). 시각은 유닉스 초(UTC), 모르면 null. */
static void json_project_fields(JsonBuf *b, const ProjectInfo *p, size_t index)
{
    jsonl_field_u64(b, "index", index + 1);
    jsonl_field_string(b, "root", p->root);
    jsonl_field_bool(b, "git", p->is_git);
    jsonl_field_string(b, "git_source", project_git_source_name(p->git_source));
    jsonl_field_string(b, "branch", p->branch);
    json_time(b, "last_commit", p->last_commit);
    json_time(b, "last_modified", p->last_modified);
    json_time(b, "last_activity", p->last_activity);
    jsonl_field_u64(b, "age_days", p->age_days);
    jsonl_field_bool(b, "dormant", p->dormant);
    json_int_or_null(b, "changes", p->changes);
    json_int_or_null(b, "unpushed", p->unpushed);
    if (p->has_remote < 0) {
        jsonl_field_key(b, "has_remote");
        jsonl_raw(b, "null");
    } else {
        jsonl_field_bool(b, "has_remote", p->has_remote);
    }
    jsonl_field_bool(b, "remote_differs", p->remote_differs);
    jsonl_field_bool(b, "modified_incomplete", p->modified_incomplete);
    jsonl_field_u64(b, "candidates", p->candidates);
}

/* --json 의 "projects" 배열(키 포함). */
static void json_projects(JsonBuf *b)
{
    size_t i;

    jsonl_field_key(b, "projects");
    jsonl_raw(b, "[");
    for (i = 0; g_projects != NULL && i < g_projects->len; i++) {
        jsonl_raw(b, i == 0 ? "{" : ",{");
        json_project_fields(b, &g_projects->items[i], i);
        jsonl_raw(b, "}");
    }
    jsonl_raw(b, "]");
    if (g_projects != NULL) {
        jsonl_field_u64(b, "dormant_days", g_projects->dormant_days);
        jsonl_field_bool(b, "git_available", g_projects->git_available);
    }
}

/* --jsonl 의 "project" 이벤트들. */
static void jsonl_projects(JsonBuf *b)
{
    size_t i;

    for (i = 0; g_projects != NULL && i < g_projects->len; i++) {
        jsonl_begin_event(b, "project");
        json_project_fields(b, &g_projects->items[i], i);
        jsonl_end_event(b);
    }
}

/* 대상의 프로젝트 번호(1부터)·복구 가능성·프로젝트 경고 필드. */
static void json_project_ref(JsonBuf *b, const CleanPlanTarget *t)
{
    if (t->project != (size_t)-1) {
        jsonl_field_u64(b, "project", t->project + 1);
    } else {
        jsonl_field_key(b, "project");
        jsonl_raw(b, "null");
    }
    jsonl_field_string(b, "recovery",
                       t->recovery >= 0 ? project_recovery_name((ProjectRecovery)t->recovery)
                                        : NULL);
    jsonl_field_bool(b, "dirty", (t->flags & CLEAN_PLAN_DIRTY) != 0);
    jsonl_field_bool(b, "unpushed", (t->flags & CLEAN_PLAN_UNPUSHED) != 0);
    jsonl_field_bool(b, "dormant", (t->flags & CLEAN_PLAN_DORMANT) != 0);
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
    /* 미커밋 변경이 있는 프로젝트의 후보(DIRTY, MANUAL)는 --include-dirty 일 때만 고른다. */
    for (i = 0; opts->include_dirty && i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        if (t->source == CLEAN_SOURCE_PROJECT && (t->flags & CLEAN_PLAN_DIRTY) &&
            cli_clean_matches(t, opts)) {
            clean_plan_set_selected(plan, i, 1);
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
    size_t used;

    buf[0] = L'\0';
    used = (size_t)swprintf(buf, len, L"%ls%ls%ls%ls%ls%ls%ls",
             (t->flags & CLEAN_PLAN_MANUAL) && !(t->flags & CLEAN_PLAN_DIRTY) ? L"manual," : L"",
             (t->flags & CLEAN_PLAN_CAUTION) ? L"caution," : L"",
             (t->flags & CLEAN_PLAN_GUESSED) ? L"guessed," : L"",
             (t->flags & CLEAN_PLAN_PERMANENT) ? L"permanent," : L"",
             (t->flags & CLEAN_PLAN_DIRTY) ? L"uncommitted," : L"",
             (t->flags & CLEAN_PLAN_UNPUSHED) ? L"unpushed," : L"",
             (t->flags & CLEAN_PLAN_DORMANT) ? L"dormant," : L"");
    if (t->project != (size_t)-1) {
        swprintf(buf + used, len - used, L"P%u,", (unsigned)(t->project + 1));
    }
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
    wchar_t reclaim[32], size[32], flags[128];

    if (opts->command == CLI_CLEAN_PROJECT) {
        print_projects(out);
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
        flags_text(t, flags, 128);
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
    if (s.dirty > 0) {
        fwprintf(out, L"Uncommitted changes: %u selected target(s) are in projects with uncommitted "
                      L"changes.\n",
                 (unsigned)s.dirty);
    }
    if (s.unpushed > 0) {
        fwprintf(out, L"Unpushed commits: %u selected target(s) are in projects whose commits are "
                      L"not on any remote.\n",
                 (unsigned)s.unpushed);
    }
    {
        size_t held = 0;
        for (i = 0; i < plan->len; i++) {
            held += (plan->items[i].flags & CLEAN_PLAN_DIRTY) && !plan->items[i].selected &&
                    clean_plan_selectable(&plan->items[i]);
        }
        if (held > 0) {
            fwprintf(out, L"%u target(s) in projects with uncommitted changes were not selected; "
                          L"add --include-dirty to include them.\n",
                     (unsigned)held);
        }
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

/* ---- JSON 문서(--json) ---- */

static const wchar_t *source_name(const CliCleanOptions *opts)
{
    return opts->command == CLI_CLEAN_SYSTEM ? L"system" : L"project";
}

/* 실행 결과 필드("moved"…"entries" 배열). 객체를 연 뒤 호출한다. 실행하지 않은 대상은 호출하지 않는다. */
static void json_result_fields(JsonBuf *b, const CleanPlanTarget *t)
{
    size_t j;
    int first = 1;

    jsonl_field_u64(b, "moved", t->moved);
    jsonl_field_u64(b, "quarantined", t->quarantined);
    jsonl_field_u64(b, "failed", t->failed);
    jsonl_field_u64(b, "skipped", t->skipped);
    jsonl_field_u64(b, "cancelled", t->cancelled);
    jsonl_field_u64(b, "moved_bytes", t->moved_bytes);
    jsonl_field_key(b, "entries");
    jsonl_raw(b, "[");
    for (j = 0; j < t->entry_count; j++) {
        const CleanPlanEntry *e = &t->entries[j];
        if (e->skip == CLEAN_SKIP_NONE && e->outcome.status == CLEANUP_TRASH_PENDING) {
            continue;
        }
        jsonl_raw(b, first ? "{" : ",{");
        first = 0;
        jsonl_field_string(b, "path", e->item.path);
        jsonl_field_string(b, "status", cleanup_trash_status_name(e->outcome.status));
        jsonl_field_string(b, "skip", clean_skip_name(e->skip));
        jsonl_field_u64(b, "code", e->outcome.code);
        jsonl_field_string(b, "moved_to", e->outcome.recycled_path);
        jsonl_raw(b, "}");
    }
    jsonl_raw(b, "]");
}

/* 계획 대상의 공통 필드(객체를 연 뒤 호출). */
static void json_target_fields(JsonBuf *b, const CleanPlanTarget *t, size_t index)
{
    jsonl_field_u64(b, "index", index + 1);
    jsonl_field_string(b, "id", t->id);
    jsonl_field_string(b, "label", t->label);
    jsonl_field_string(b, "location", t->location);
    jsonl_field_string(b, "state", t->state);
    jsonl_field_u64(b, "bytes", t->bytes);
    jsonl_field_u64(b, "reclaim", t->reclaim);
    jsonl_field_u64(b, "shared", t->shared);
    jsonl_field_u64(b, "files", t->files);
    jsonl_field_bool(b, "selectable", clean_plan_selectable(t));
    jsonl_field_bool(b, "selected", t->selected);
    jsonl_field_bool(b, "manual", (t->flags & CLEAN_PLAN_MANUAL) != 0);
    jsonl_field_bool(b, "caution", (t->flags & CLEAN_PLAN_CAUTION) != 0);
    jsonl_field_bool(b, "guessed", (t->flags & CLEAN_PLAN_GUESSED) != 0);
    jsonl_field_bool(b, "permanent", (t->flags & CLEAN_PLAN_PERMANENT) != 0);
    if (t->source == CLEAN_SOURCE_PROJECT) {
        json_project_ref(b, t);
    }
}

static void json_summary(JsonBuf *b, const CleanPlanSummary *s)
{
    jsonl_raw(b, "{");
    jsonl_field_u64(b, "selected", s->targets);
    jsonl_field_u64(b, "selectable", s->selectable);
    jsonl_field_u64(b, "entries", s->entries);
    jsonl_field_u64(b, "bytes", s->bytes);
    jsonl_field_u64(b, "reclaim", s->reclaim);
    jsonl_field_u64(b, "caution", s->caution);
    jsonl_field_u64(b, "manual", s->manual);
    jsonl_field_u64(b, "guessed", s->guessed);
    jsonl_field_u64(b, "dirty", s->dirty);
    jsonl_field_u64(b, "unpushed", s->unpushed);
    jsonl_raw(b, "}");
}

static void json_totals(JsonBuf *b, const CleanPlanTotals *t)
{
    jsonl_raw(b, "{");
    jsonl_field_u64(b, "targets", t->targets);
    jsonl_field_u64(b, "moved", t->moved);
    jsonl_field_u64(b, "quarantined", t->quarantined);
    jsonl_field_u64(b, "failed", t->failed);
    jsonl_field_u64(b, "skipped", t->skipped);
    jsonl_field_u64(b, "cancelled", t->cancelled);
    jsonl_field_u64(b, "moved_bytes", t->moved_bytes);
    jsonl_raw(b, "}");
}

/* 문서 머리: {"version":1,"command":..,("source":..,)"root":.. */
static void json_head(JsonBuf *b, const CliCleanOptions *opts, const wchar_t *root)
{
    jsonl_raw(b, "{\"version\":1");
    jsonl_field_string(b, "command", opts->command == CLI_CLEAN_FIND ? L"find" : L"clean");
    if (opts->command != CLI_CLEAN_FIND) {
        jsonl_field_string(b, "source", source_name(opts));
        jsonl_field_string(b, "mode", opts->apply ? L"execute" : L"dry-run");
    }
    jsonl_field_string(b, "root", root);
}

/* 실패 문서: 탐지 전에 끝났을 때. */
static void json_error_doc(JsonBuf *b, const CliCleanOptions *opts, const wchar_t *root,
                           const wchar_t *message, int code)
{
    json_head(b, opts, root);
    jsonl_field_string(b, "status", L"failed");
    jsonl_field_string(b, "error", message);
    jsonl_field_u64(b, "exit_code", (uint64_t)code);
    jsonl_raw(b, "}\n");
}

static void json_clean_doc(JsonBuf *b, const CliCleanOptions *opts, const CleanPlan *plan,
                           const wchar_t *root, ScanStatus st, int executed,
                           const CleanPlanTotals *tot, const wchar_t *note, int code)
{
    CleanPlanSummary s;
    size_t i;

    json_head(b, opts, root);
    jsonl_field_string(b, "status", scan_status_name(st));
    jsonl_field_bool(b, "executed", executed);
    clean_plan_summarize(plan, &s);
    jsonl_field_key(b, "summary");
    json_summary(b, &s);
    jsonl_field_key(b, "targets");
    jsonl_raw(b, "[");
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        jsonl_raw(b, i == 0 ? "{" : ",{");
        json_target_fields(b, t, i);
        jsonl_field_key(b, "result");
        if (t->ran) {
            jsonl_raw(b, "{");
            json_result_fields(b, t);
            jsonl_raw(b, "}");
        } else {
            jsonl_raw(b, "null");
        }
        jsonl_raw(b, "}");
    }
    jsonl_raw(b, "]");
    if (opts->command == CLI_CLEAN_PROJECT) {
        json_projects(b);
    }
    jsonl_field_key(b, "totals");
    if (executed) {
        json_totals(b, tot);
    } else {
        jsonl_raw(b, "null");
    }
    jsonl_field_string(b, "note", note);
    jsonl_field_u64(b, "exit_code", (uint64_t)code);
    jsonl_raw(b, "}\n");
}

/* ---- JSON Lines(--jsonl) ---- */

static void jsonl_plan(JsonBuf *buf, const CleanPlan *plan, const CliCleanOptions *opts,
                       const wchar_t *root)
{
    CleanPlanSummary s;
    size_t i;

    jsonl_begin_event(buf, "clean_plan");
    jsonl_field_string(buf, "source", source_name(opts));
    jsonl_field_string(buf, "root", root);
    jsonl_field_u64(buf, "targets", plan->len);
    jsonl_end_event(buf);
    jsonl_projects(buf);
    for (i = 0; i < plan->len; i++) {
        jsonl_begin_event(buf, "clean_target");
        json_target_fields(buf, &plan->items[i], i);
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
    jsonl_field_bool(buf, "apply", opts->apply); /* 예전 이름(호환용), mode 와 같다 */
    jsonl_field_string(buf, "mode", opts->apply ? L"execute" : L"dry-run");
    jsonl_end_event(buf);
}

static void jsonl_results(JsonBuf *buf, const CleanPlan *plan)
{
    size_t i;

    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        if (!t->ran) {
            continue;
        }
        jsonl_begin_event(buf, "clean_result");
        jsonl_field_u64(buf, "index", i + 1);
        jsonl_field_string(buf, "id", t->id);
        json_result_fields(buf, t);
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

/* ---- find ---- */

int cli_clean_matches(const CleanPlanTarget *t, const CliCleanOptions *opts)
{
    return t != NULL && t->source == CLEAN_SOURCE_PROJECT &&
           (opts->categories == 0 || (opts->categories & CLEANUP_MASK(t->category))) &&
           (opts->ecos == 0 || (opts->ecos & CLEANUP_ECO_BIT(t->eco)));
}

static const CleanPlan *g_sort_plan;

/* 파일 크기 내림차순, 같으면 경로 순(GUI 목록·cleanup_select 와 같다). */
static int cmp_find(const void *a, const void *b)
{
    const CleanPlanTarget *x = &g_sort_plan->items[*(const size_t *)a];
    const CleanPlanTarget *y = &g_sort_plan->items[*(const size_t *)b];
    int by_path;

    if (x->bytes != y->bytes) {
        return x->bytes > y->bytes ? -1 : 1;
    }
    by_path = _wcsicmp(x->location != NULL ? x->location : L"",
                       y->location != NULL ? y->location : L"");
    if (by_path != 0) {
        return by_path;
    }
    return *(const size_t *)a < *(const size_t *)b ? -1 : 1;
}

/* 필터에 맞는 후보 번호를 크기순으로 order 에(malloc). 개수를 돌려주며 메모리가 없으면 -1 이 아닌 0 과 *oom. */
static size_t find_order(const CleanPlan *plan, const CliCleanOptions *opts, size_t **order,
                         int *oom)
{
    size_t i, n = 0;

    *order = malloc((plan->len > 0 ? plan->len : 1) * sizeof(**order));
    if (*order == NULL) {
        *oom = 1;
        return 0;
    }
    for (i = 0; i < plan->len; i++) {
        if (cli_clean_matches(&plan->items[i], opts)) {
            (*order)[n++] = i;
        }
    }
    g_sort_plan = plan; /* CLI 는 한 스레드에서만 부른다 */
    qsort(*order, n, sizeof(**order), cmp_find);
    return n;
}

static unsigned item_flags(const CleanPlanTarget *t)
{
    return t->entry_count > 0 ? t->entries[0].item.flags : 0u;
}

static void find_note(const CleanPlanTarget *t, wchar_t *buf, size_t len)
{
    unsigned f = item_flags(t);
    size_t used = 0;

    buf[0] = L'\0';
    if (t->deny != CLEANUP_TRASH_PENDING) {
        used += (size_t)swprintf(buf + used, len - used, L"protected:%ls,", t->state);
    }
    if (t->flags & CLEAN_PLAN_GUESSED) {
        used += (size_t)swprintf(buf + used, len - used, L"guessed,");
    } else if (f & CLEANUP_ITEM_CONFIRMED) {
        used += (size_t)swprintf(buf + used, len - used, L"confirmed,");
    }
    if (f & CLEANUP_ITEM_INCOMPLETE) {
        used += (size_t)swprintf(buf + used, len - used, L"incomplete,");
    }
    if (t->shared > 0) {
        used += (size_t)swprintf(buf + used, len - used, L"hardlinks,");
    }
    if (t->flags & CLEAN_PLAN_DIRTY) {
        used += (size_t)swprintf(buf + used, len - used, L"uncommitted,");
    }
    if (t->flags & CLEAN_PLAN_UNPUSHED) {
        used += (size_t)swprintf(buf + used, len - used, L"unpushed,");
    }
    if (t->flags & CLEAN_PLAN_DORMANT) {
        used += (size_t)swprintf(buf + used, len - used, L"dormant,");
    }
    if (used > 0) {
        buf[used - 1] = L'\0';
    } else {
        swprintf(buf, len, L"-");
    }
}

static void find_totals(const CleanPlan *plan, const size_t *order, size_t n, uint64_t *bytes,
                        uint64_t *reclaim, size_t *cleanable)
{
    size_t i;

    *bytes = *reclaim = 0;
    *cleanable = 0;
    for (i = 0; i < n; i++) {
        const CleanPlanTarget *t = &plan->items[order[i]];
        *bytes += t->bytes;
        if (t->ready) {
            *reclaim += t->reclaim;
            (*cleanable)++;
        }
    }
}

static void print_find(FILE *out, const CleanPlan *plan, const size_t *order, size_t n,
                       const wchar_t *root)
{
    size_t i, cleanable;
    uint64_t bytes, reclaim;
    wchar_t r[32], sz[32], note[128];

    find_totals(plan, order, n, &bytes, &reclaim, &cleanable);
    format_size(reclaim, r, 32);
    format_size(bytes, sz, 32);
    print_projects(out);
    fwprintf(out, L"Cleanup candidates under %ls: %u found, %u cleanable, reclaim %ls (size %ls)\n",
             root, (unsigned)n, (unsigned)cleanable, r, sz);
    if (n > 0) {
        fwprintf(out, L"  %-4ls %-10ls %-10ls %-13ls %-8ls %-5ls %-9ls %-22ls %ls\n", L"#",
                 L"Reclaim", L"Size", L"Type", L"Eco", L"Proj", L"Recovery", L"Note", L"Path");
    }
    for (i = 0; i < n; i++) {
        const CleanPlanTarget *t = &plan->items[order[i]];
        format_size(t->reclaim, r, 32);
        format_size(t->bytes, sz, 32);
        wchar_t proj[16];
        find_note(t, note, 128);
        if (t->project != (size_t)-1) {
            swprintf(proj, 16, L"P%u", (unsigned)(t->project + 1));
        } else {
            swprintf(proj, 16, L"-");
        }
        fwprintf(out, L"  %-4u %-10ls %-10ls %-13ls %-8ls %-5ls %-9ls %-22ls %ls\n",
                 (unsigned)(i + 1), r, sz, t->id != NULL ? t->id : L"", cleanup_eco_name(t->eco),
                 proj,
                 t->recovery >= 0 ? project_recovery_name((ProjectRecovery)t->recovery) : L"-",
                 note, t->location != NULL ? t->location : L"");
    }
    if (cleanable > 0) {
        fwprintf(out, L"Nothing was changed. Use 'disk-scanner clean project' to see the cleanup "
                      L"plan and move them to the Recycle Bin.\n");
    }
}

static void json_candidate_fields(JsonBuf *b, const CleanPlanTarget *t, size_t index)
{
    unsigned f = item_flags(t);

    jsonl_field_u64(b, "index", index + 1);
    jsonl_field_string(b, "category", t->id);
    jsonl_field_string(b, "eco", cleanup_eco_name(t->eco));
    jsonl_field_string(b, "path", t->location);
    jsonl_field_u64(b, "bytes", t->bytes);
    jsonl_field_u64(b, "reclaim", t->reclaim);
    jsonl_field_u64(b, "shared", t->shared);
    jsonl_field_u64(b, "files", t->files);
    jsonl_field_bool(b, "dir", (f & CLEANUP_ITEM_DIR) != 0);
    jsonl_field_bool(b, "confirmed", (f & CLEANUP_ITEM_CONFIRMED) != 0);
    jsonl_field_bool(b, "guessed", (t->flags & CLEAN_PLAN_GUESSED) != 0);
    jsonl_field_bool(b, "incomplete", (f & CLEANUP_ITEM_INCOMPLETE) != 0);
    jsonl_field_bool(b, "cleanable", t->ready);
    jsonl_field_string(b, "state", t->state);
    json_project_ref(b, t);
}

static void json_find(JsonBuf *b, const CliCleanOptions *opts, const CleanPlan *plan,
                      const size_t *order, size_t n, const wchar_t *root, ScanStatus st, int code)
{
    size_t i, cleanable;
    uint64_t bytes, reclaim;

    find_totals(plan, order, n, &bytes, &reclaim, &cleanable);
    if (opts->json) {
        json_head(b, opts, root);
        jsonl_field_string(b, "status", scan_status_name(st));
        jsonl_field_key(b, "summary");
        jsonl_raw(b, "{");
        jsonl_field_u64(b, "candidates", n);
        jsonl_field_u64(b, "cleanable", cleanable);
        jsonl_field_u64(b, "bytes", bytes);
        jsonl_field_u64(b, "reclaim", reclaim);
        jsonl_raw(b, "}");
        jsonl_field_key(b, "candidates");
        jsonl_raw(b, "[");
        for (i = 0; i < n; i++) {
            jsonl_raw(b, i == 0 ? "{" : ",{");
            json_candidate_fields(b, &plan->items[order[i]], i);
            jsonl_raw(b, "}");
        }
        jsonl_raw(b, "]");
        json_projects(b);
        jsonl_field_u64(b, "exit_code", (uint64_t)code);
        jsonl_raw(b, "}\n");
        return;
    }
    jsonl_begin_event(b, "find_start");
    jsonl_field_string(b, "root", root);
    jsonl_end_event(b);
    jsonl_projects(b);
    for (i = 0; i < n; i++) {
        jsonl_begin_event(b, "find_candidate");
        json_candidate_fields(b, &plan->items[order[i]], i);
        jsonl_end_event(b);
    }
    jsonl_begin_event(b, "complete");
    jsonl_field_string(b, "status", scan_status_name(st));
    jsonl_field_u64(b, "candidates", n);
    jsonl_field_u64(b, "cleanable", cleanable);
    jsonl_field_u64(b, "bytes", bytes);
    jsonl_field_u64(b, "reclaim", reclaim);
    jsonl_field_u64(b, "exit_code", (uint64_t)code);
    jsonl_end_event(b);
}

/* ---- 실행 ---- */

/* 탐지해 계획을 만든다. 탐지 상태(SCAN_*)를 돌려주며 메모리 부족이면 SCAN_FATAL·*oom. */
static ScanStatus detect(const CliCleanOptions *opts, CleanPlan *plan, ProjectList *projects,
                         wchar_t **root, int *oom)
{
    *oom = 0;
    *root = NULL;
    if (opts->command != CLI_CLEAN_SYSTEM) {
        /* GUI [프로젝트 정리]와 같은 탐지 작업(보호 경로·앱 데이터 폴더 제외 포함) + 프로젝트 분석. */
        CleanupResult r;
        ProjectOptions po;
        CleanupJob *job;
        ScanStatus st;
        memset(&po, 0, sizeof(po));
        po.dormant_days = opts->dormant_days;
        po.no_git = opts->no_git;
        job = cleanup_job_start_ex(opts->target, 1000, &po, NULL, NULL);
        if (job == NULL) {
            *oom = 1;
            return SCAN_FATAL;
        }
        cleanup_result_init(&r);
        st = cleanup_job_finish_ex(job, &r, projects);
        if (st != SCAN_FATAL && clean_plan_from_project(plan, &r, NULL) != 0) {
            *oom = 1;
            st = SCAN_FATAL;
        }
        clean_plan_apply_projects(plan, projects);
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

/* JSON 출력이면 buf 를 out 에 쓴다. 실패하면 EXIT_FATAL. */
static int flush_json(JsonBuf *buf, FILE *out, FILE *err, int code)
{
    if (!jsonl_buf_flush(buf, out)) {
        fwprintf(err, L"error: failed to write JSON output\n");
        return EXIT_FATAL;
    }
    return code;
}

static int run_find(const CliCleanOptions *opts, FILE *out, FILE *err, CleanPlan *plan,
                    const wchar_t *root, ScanStatus st, JsonBuf *buf)
{
    size_t *order = NULL, n;
    int oom = 0, code = st == SCAN_PARTIAL ? EXIT_PARTIAL : EXIT_COMPLETE;

    n = find_order(plan, opts, &order, &oom);
    if (oom) {
        fwprintf(err, L"error: out of memory\n");
        if (opts->json) {
            json_error_doc(buf, opts, root, L"out of memory", EXIT_FATAL);
            flush_json(buf, out, err, EXIT_FATAL);
        }
        return EXIT_FATAL;
    }
    if (opts->json || opts->jsonl) {
        json_find(buf, opts, plan, order, n, root, st, code);
        code = flush_json(buf, out, err, code);
    } else {
        print_find(out, plan, order, n, root);
    }
    free(order);
    return code;
}

int cli_clean_run(const CliCleanOptions *opts, FILE *out, FILE *err,
                  cli_clean_confirm_fn confirm, void *ctx)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CleanPlan plan;
    ProjectList projects;
    CleanPlanTotals totals;
    CleanPlanSummary s;
    JsonBuf buf;
    ScanStatus st;
    wchar_t *root = NULL, msg[600];
    const wchar_t *note = NULL;
    int oom, code = EXIT_COMPLETE, applied = 0;
    /* 텍스트에서 안내 문구를 쓸 곳: JSON 출력이면 stdout 은 JSON 만이므로 stderr. */
    FILE *info = (opts->json || opts->jsonl) ? err : out;

    clean_plan_init(&plan);
    project_list_init(&projects);
    jsonl_buf_init(&buf);
    memset(&totals, 0, sizeof(totals));
    st = detect(opts, &plan, &projects, &root, &oom);
    g_projects = opts->command == CLI_CLEAN_SYSTEM ? NULL : &projects;
    if (st == SCAN_FATAL) {
        if (oom) {
            swprintf(msg, 600, L"out of memory");
        } else {
            swprintf(msg, 600, L"cannot scan %ls", opts->target != NULL ? opts->target : L"");
        }
        fwprintf(err, L"error: %ls\n", msg);
        code = EXIT_FATAL;
        if (opts->json) {
            json_error_doc(&buf, opts, root != NULL ? root : opts->target, msg, code);
            flush_json(&buf, out, err, code);
        }
        goto done;
    }
    if (st == SCAN_PARTIAL) {
        fwprintf(err, L"warning: some folders could not be read; sizes may be incomplete\n");
        code = EXIT_PARTIAL;
    }
    if (opts->command == CLI_CLEAN_FIND) {
        code = run_find(opts, out, err, &plan, root, st, &buf);
        goto done;
    }

    cli_clean_select(&plan, opts);
    clean_plan_summarize(&plan, &s);
    if (opts->jsonl) {
        jsonl_plan(&buf, &plan, opts, root);
        if (!jsonl_buf_flush(&buf, out)) {
            code = EXIT_FATAL;
            goto done;
        }
    } else if (!opts->json) {
        print_plan(out, &plan, opts, root);
    }
    if (!opts->apply) {
        wchar_t r[32];
        format_size(s.reclaim, r, 32);
        if (!opts->json && !opts->jsonl) {
            fwprintf(out, L"Dry run - nothing was moved. Expected result: %u item(s) in %u "
                          L"target(s) would be moved to the Recycle Bin (quarantine folder on drives "
                          L"without one), freeing about %ls once it is emptied.\n"
                          L"Add --execute to move them.\n",
                     (unsigned)s.entries, (unsigned)s.targets, r);
        }
        note = L"dry run: nothing was moved";
        goto finish;
    }
    if (s.targets == 0) {
        fwprintf(info, L"Nothing selected; nothing was moved.\n");
        note = L"nothing selected";
        goto finish;
    }
    if (!opts->yes) {
        if (confirm == NULL) {
            fwprintf(err, L"error: confirmation is not possible (input is not interactive); "
                          L"add --yes to execute\n");
            code = EXIT_FATAL;
            note = L"confirmation is not possible (input is not interactive); add --yes";
            goto finish;
        }
        if (!confirm(&s, ctx)) {
            fwprintf(info, L"Cancelled; nothing was moved.\n");
            note = L"cancelled at confirmation";
            goto finish;
        }
    }
    {
        CleanPlanOps ops;
        memset(&ops, 0, sizeof(ops)); /* allow_permanent = 0: CLI 는 영구 삭제하지 않는다 */
        if (clean_plan_run(&plan, &ops, &totals) != 0) {
            fwprintf(info, L"Nothing to move.\n");
            note = L"nothing to move";
            goto finish;
        }
    }
    applied = 1;
    if (totals.failed + totals.skipped + totals.cancelled > 0) {
        code = EXIT_PARTIAL;
    }
    if (opts->jsonl) {
        jsonl_results(&buf, &plan);
    } else if (!opts->json) {
        print_result(out, &plan, &totals);
    }
finish:
    if (opts->json) {
        json_clean_doc(&buf, opts, &plan, root, st, applied, &totals, note, code);
        code = flush_json(&buf, out, err, code);
    } else if (opts->jsonl && code != EXIT_FATAL) {
        jsonl_done(&buf, applied ? &totals : NULL, applied, code);
        code = flush_json(&buf, out, err, code);
    }
done:
    jsonl_buf_free(&buf);
    g_projects = NULL;
    project_list_free(&projects);
    clean_plan_free(&plan);
    free(root);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return code;
}
