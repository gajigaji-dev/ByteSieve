#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cli_clean.h"

/*
 * disk-scanner clean 의 인자 해석과 선택 정책(파일 시스템에 접근하지 않음).
 *   - project/system, 목록 옵션(쉼표·= 형식), 알 수 없는 이름·옵션·명령, 휴지통 비우기 거부,
 *     --yes 는 --apply 와 함께만, 명령에 맞지 않는 옵션
 *   - 선택: GUI [모두 선택]과 같이 MANUAL(공용 개발 캐시)은 기본 제외, --include 로 이름·dev_cache,
 *     --exclude, 프로젝트 --category·--eco 거르기, 영구 삭제 대상은 언제나 제외
 */

static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static CliParseResult parse(CliCleanOptions *o, wchar_t *err, int argc, ...)
{
    wchar_t *argv[16];
    va_list ap;
    int i;

    va_start(ap, argc);
    for (i = 0; i < argc && i < 16; i++) {
        argv[i] = va_arg(ap, wchar_t *);
    }
    va_end(ap);
    return cli_clean_parse_args(argc, argv, o, err, 256);
}

static void test_parse(void)
{
    CliCleanOptions o;
    wchar_t err[256];

    CHECK(parse(&o, err, 3, L"clean", L"project", L"D:\\work") == CLI_PARSE_OK);
    CHECK(o.command == CLI_CLEAN_PROJECT && wcscmp(o.target, L"D:\\work") == 0);
    CHECK(!o.apply && !o.yes && !o.jsonl && o.categories == 0 && o.ecos == 0);

    CHECK(parse(&o, err, 8, L"clean", L"project", L"--category", L"node_modules,build",
                L"--eco=node,python", L"--apply", L"--yes", L"D:\\w") == CLI_PARSE_OK);
    CHECK(o.categories == (CLEANUP_MASK(CLEANUP_NODE_MODULES) | CLEANUP_MASK(CLEANUP_BUILD)));
    CHECK(o.ecos == (CLEANUP_ECO_BIT(CLEANUP_ECO_NODE) | CLEANUP_ECO_BIT(CLEANUP_ECO_PYTHON)));
    CHECK(o.apply && o.yes && wcscmp(o.target, L"D:\\w") == 0);

    CHECK(parse(&o, err, 7, L"clean", L"system", L"--include", L"npm, gradle_wrapper",
                L"--exclude=chrome", L"--jsonl", L"--apply") == CLI_PARSE_OK);
    CHECK(o.command == CLI_CLEAN_SYSTEM && o.target == NULL && o.jsonl && o.apply && !o.yes);
    CHECK(o.include_count == 2 && wcscmp(o.include[0], L"npm") == 0 &&
          wcscmp(o.include[1], L"gradle_wrapper") == 0);
    CHECK(o.exclude_count == 1 && wcscmp(o.exclude[0], L"chrome") == 0);
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--include", L"dev_cache") == CLI_PARSE_OK);

    /* -- 뒤는 폴더 */
    CHECK(parse(&o, err, 4, L"clean", L"project", L"--", L"--apply") == CLI_PARSE_OK);
    CHECK(wcscmp(o.target, L"--apply") == 0 && !o.apply);

    /* 도움말·오류 */
    CHECK(parse(&o, err, 2, L"clean", L"--help") == CLI_PARSE_HELP);
    CHECK(parse(&o, err, 3, L"clean", L"system", L"-h") == CLI_PARSE_HELP);
    CHECK(parse(&o, err, 1, L"clean") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 2, L"clean", L"everything") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 2, L"clean", L"project") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"project", L"a", L"b") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"clean", L"system", L"C:\\x") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"project", L"D:\\w", L"--yes") == CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"--execute") != NULL);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--dry-run", L"--yes") ==
          CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"project", L"D:\\w", L"--force") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--category", L"music") ==
          CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"music") != NULL);
    CHECK(parse(&o, err, 4, L"clean", L"project", L"D:\\w", L"--category") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--include", L"steam") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--include", L"npm") ==
          CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--eco", L"node") == CLI_PARSE_ERROR);
    /* 휴지통 비우기(영구 삭제)는 CLI 에서 고를 수 없다 */
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--include", L"npm,recycle_bin") ==
          CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"Recycle Bin") != NULL);
}

/* --dry-run·--execute(--apply 는 예전 이름)·--json, 서로 함께 쓸 수 없는 조합. */
static void test_parse_modes(void)
{
    CliCleanOptions o;
    wchar_t err[256];

    CHECK(parse(&o, err, 4, L"clean", L"project", L"D:\\w", L"--dry-run") == CLI_PARSE_OK);
    CHECK(o.dry_run && !o.apply && !o.json);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--execute", L"--yes") ==
          CLI_PARSE_OK);
    CHECK(o.apply && o.yes && !o.dry_run);
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--apply", L"--json") == CLI_PARSE_OK);
    CHECK(o.apply && o.json && !o.jsonl);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--dry-run", L"--execute") ==
          CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"--dry-run") != NULL);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--json", L"--jsonl") ==
          CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"--jsonl") != NULL);
}

/* find: 폴더 하나, --category·--eco·--json·--jsonl 만. 실행 관련 옵션·시스템 대상 옵션은 오류. */
static void test_parse_find(void)
{
    CliCleanOptions o;
    wchar_t err[256];

    CHECK(parse(&o, err, 2, L"find", L"D:\\work") == CLI_PARSE_OK);
    CHECK(o.command == CLI_CLEAN_FIND && wcscmp(o.target, L"D:\\work") == 0 && !o.json);
    CHECK(parse(&o, err, 6, L"find", L"--category=build", L"--eco", L"rust,go", L"--json",
                L"D:\\w") == CLI_PARSE_OK);
    CHECK(o.categories == CLEANUP_MASK(CLEANUP_BUILD) && o.json);
    CHECK(o.ecos == (CLEANUP_ECO_BIT(CLEANUP_ECO_RUST) | CLEANUP_ECO_BIT(CLEANUP_ECO_GO)));
    CHECK(parse(&o, err, 3, L"find", L"--jsonl", L"D:\\w") == CLI_PARSE_OK && o.jsonl);
    CHECK(parse(&o, err, 3, L"find", L"--", L"--json") == CLI_PARSE_OK);
    CHECK(wcscmp(o.target, L"--json") == 0 && !o.json);
    CHECK(parse(&o, err, 2, L"find", L"-h") == CLI_PARSE_HELP);

    CHECK(parse(&o, err, 1, L"find") == CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"find") != NULL);
    CHECK(parse(&o, err, 3, L"find", L"a", L"b") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--execute") == CLI_PARSE_ERROR);
    CHECK(wcsstr(err, L"never changes") != NULL);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--apply") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--dry-run") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--yes") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--include", L"npm") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--json", L"--jsonl") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--category", L"music") == CLI_PARSE_ERROR);
}

static CleanPlanTarget *add(CleanPlan *plan, CleanSource src, const wchar_t *id, unsigned flags,
                            CleanupCategory cat, CleanupEco eco)
{
    CleanPlanTarget *t = clean_plan_add(plan, src, id, id, NULL);
    CleanupItem item;

    CHECK(t != NULL);
    if (t == NULL) {
        return NULL;
    }
    t->ready = 1;
    t->flags = flags;
    t->category = cat;
    t->eco = eco;
    if (!(flags & CLEAN_PLAN_PERMANENT)) {
        memset(&item, 0, sizeof(item));
        item.path = L"C:\\nowhere\\x";
        CHECK(clean_plan_add_entry(t, &item, L"C:\\nowhere") == 0);
    }
    return t;
}

static void test_select_system(void)
{
    CleanPlan plan;
    CliCleanOptions o;
    wchar_t err[256];

    clean_plan_init(&plan);
    add(&plan, CLEAN_SOURCE_SYSTEM, L"user_temp", 0, CLEANUP_CACHE, CLEANUP_ECO_GENERIC);
    add(&plan, CLEAN_SOURCE_SYSTEM, L"chrome", 0, CLEANUP_CACHE, CLEANUP_ECO_GENERIC);
    add(&plan, CLEAN_SOURCE_SYSTEM, L"recycle_bin", CLEAN_PLAN_MANUAL | CLEAN_PLAN_PERMANENT,
        CLEANUP_CACHE, CLEANUP_ECO_GENERIC);
    add(&plan, CLEAN_SOURCE_SYSTEM, L"npm", CLEAN_PLAN_MANUAL, CLEANUP_CACHE, CLEANUP_ECO_NODE);
    add(&plan, CLEAN_SOURCE_SYSTEM, L"gradle_wrapper", CLEAN_PLAN_MANUAL | CLEAN_PLAN_CAUTION,
        CLEANUP_CACHE, CLEANUP_ECO_JAVA);

    /* 기본: [모두 선택]과 같이 임시 파일·브라우저만 */
    CHECK(parse(&o, err, 2, L"clean", L"system") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 2);
    CHECK(plan.items[0].selected && plan.items[1].selected && !plan.items[2].selected &&
          !plan.items[3].selected && !plan.items[4].selected);

    /* 이름으로 개발 캐시를 넣고 브라우저를 뺀다 */
    CHECK(parse(&o, err, 5, L"clean", L"system", L"--include", L"npm", L"--exclude=chrome") ==
          CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 2);
    CHECK(plan.items[0].selected && !plan.items[1].selected && plan.items[3].selected &&
          !plan.items[4].selected);

    /* dev_cache 는 공용 개발 캐시 전체(휴지통 비우기는 아님) */
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--include", L"dev_cache") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 4);
    CHECK(plan.items[3].selected && plan.items[4].selected && !plan.items[2].selected);
    CHECK(parse(&o, err, 6, L"clean", L"system", L"--include", L"dev_cache", L"--exclude",
                L"gradle_wrapper") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 3 && !plan.items[4].selected);

    /* 계획에서 휴지통이 미리 골라져 있어도 CLI 선택은 언제나 뺀다 */
    clean_plan_set_selected(&plan, 2, 1);
    CHECK(parse(&o, err, 2, L"clean", L"system") == CLI_PARSE_OK);
    cli_clean_select(&plan, &o);
    CHECK(!plan.items[2].selected);
    clean_plan_free(&plan);
}

static void test_select_project(void)
{
    CleanPlan plan;
    CliCleanOptions o;
    wchar_t err[256];

    clean_plan_init(&plan);
    add(&plan, CLEAN_SOURCE_PROJECT, L"node_modules", 0, CLEANUP_NODE_MODULES, CLEANUP_ECO_NODE);
    add(&plan, CLEAN_SOURCE_PROJECT, L"build", CLEAN_PLAN_GUESSED, CLEANUP_BUILD,
        CLEANUP_ECO_GENERIC);
    add(&plan, CLEAN_SOURCE_PROJECT, L"build", 0, CLEANUP_BUILD, CLEANUP_ECO_RUST);
    add(&plan, CLEAN_SOURCE_PROJECT, L"cache", 0, CLEANUP_CACHE, CLEANUP_ECO_PYTHON);
    plan.items[3].ready = 0; /* 보호 경로 */
    plan.items[3].deny = CLEANUP_TRASH_DENY_PROTECTED;

    CHECK(parse(&o, err, 3, L"clean", L"project", L"D:\\w") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 3 && !plan.items[3].selected);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--category", L"build") ==
          CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 2 && !plan.items[0].selected);
    CHECK(parse(&o, err, 7, L"clean", L"project", L"D:\\w", L"--category", L"build", L"--eco",
                L"rust") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 1 && plan.items[2].selected);
    CHECK(parse(&o, err, 5, L"clean", L"project", L"D:\\w", L"--category", L"cache") ==
          CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 0); /* 보호 경로는 거른 뒤에도 고르지 않는다 */

    /* find 의 거르기: 보호된 후보도 맞으면 보인다(고르기와 별개), 시스템 대상은 아니다 */
    CHECK(parse(&o, err, 2, L"find", L"D:\\w") == CLI_PARSE_OK);
    CHECK(cli_clean_matches(&plan.items[0], &o) && cli_clean_matches(&plan.items[3], &o));
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--category", L"cache") == CLI_PARSE_OK);
    CHECK(!cli_clean_matches(&plan.items[0], &o) && cli_clean_matches(&plan.items[3], &o));
    CHECK(parse(&o, err, 6, L"find", L"D:\\w", L"--category", L"build", L"--eco", L"rust") ==
          CLI_PARSE_OK);
    CHECK(!cli_clean_matches(&plan.items[1], &o) && cli_clean_matches(&plan.items[2], &o));
    clean_plan_free(&plan);
    {
        CleanPlan sys;
        clean_plan_init(&sys);
        add(&sys, CLEAN_SOURCE_SYSTEM, L"user_temp", 0, CLEANUP_CACHE, CLEANUP_ECO_GENERIC);
        CHECK(parse(&o, err, 2, L"find", L"D:\\w") == CLI_PARSE_OK);
        CHECK(!cli_clean_matches(&sys.items[0], &o) && !cli_clean_matches(NULL, &o));
        clean_plan_free(&sys);
    }
}

/* 프로젝트 분석 옵션: --include-dirty(clean project 만), --dormant-days N(find·clean project),
 * --no-git(find·clean project). --include-dirty 는 거르기에 맞는 미커밋 프로젝트 후보를 고른다. */
static void test_project_options(void)
{
    CliCleanOptions o;
    wchar_t err[256];
    CleanPlan plan;
    CleanPlanTarget *t;

    CHECK(parse(&o, err, 6, L"clean", L"project", L"D:\\w", L"--include-dirty", L"--dormant-days",
                L"30") == CLI_PARSE_OK);
    CHECK(o.include_dirty && o.dormant_days == 30 && !o.no_git);
    CHECK(parse(&o, err, 4, L"find", L"--dormant-days=7", L"--no-git", L"D:\\w") == CLI_PARSE_OK);
    CHECK(o.dormant_days == 7 && o.no_git && !o.include_dirty);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--include-dirty") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"clean", L"system", L"--no-git") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"clean", L"system", L"--dormant-days", L"5") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--dormant-days=0") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--dormant-days=-3") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 4, L"find", L"D:\\w", L"--dormant-days=9x") == CLI_PARSE_ERROR);
    CHECK(parse(&o, err, 3, L"find", L"D:\\w", L"--dormant-days") == CLI_PARSE_ERROR);

    clean_plan_init(&plan);
    add(&plan, CLEAN_SOURCE_PROJECT, L"node_modules", 0, CLEANUP_NODE_MODULES, CLEANUP_ECO_NODE);
    t = add(&plan, CLEAN_SOURCE_PROJECT, L"node_modules", CLEAN_PLAN_DIRTY | CLEAN_PLAN_MANUAL,
            CLEANUP_NODE_MODULES, CLEANUP_ECO_NODE);
    (void)t;
    add(&plan, CLEAN_SOURCE_PROJECT, L"build", CLEAN_PLAN_DIRTY | CLEAN_PLAN_MANUAL, CLEANUP_BUILD,
        CLEANUP_ECO_GENERIC);
    CHECK(parse(&o, err, 3, L"clean", L"project", L"D:\\w") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 1 && !plan.items[1].selected);
    CHECK(parse(&o, err, 4, L"clean", L"project", L"D:\\w", L"--include-dirty") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 3);
    CHECK(parse(&o, err, 6, L"clean", L"project", L"D:\\w", L"--include-dirty", L"--category",
                L"node_modules") == CLI_PARSE_OK);
    CHECK(cli_clean_select(&plan, &o) == 2 && !plan.items[2].selected);
    clean_plan_free(&plan);
}

int main(void)
{
    test_parse();
    test_parse_modes();
    test_parse_find();
    test_project_options();
    test_select_system();
    test_select_project();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("cli_clean: all checks passed\n");
    return 0;
}
