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
    CHECK(wcsstr(err, L"--apply") != NULL);
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
    clean_plan_free(&plan);
}

int main(void)
{
    test_parse();
    test_select_system();
    test_select_project();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("cli_clean: all checks passed\n");
    return 0;
}
