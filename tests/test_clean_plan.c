#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "clean_plan.h"
#include "cleanup_scan.h"
#include "path.h"

/*
 * 정리 계획(clean_plan) 검사. 임시 폴더 픽스처를 BYTESIEVE_QUARANTINE_ROOT 로 두어 옮긴 항목이
 * 실제 휴지통이 아니라 픽스처의 격리 폴더로 가게 한다. 휴지통 비우기·실행 중 확인은 가짜 함수로 바꾼다.
 *   - 프로젝트 후보 → 계획: 순서·경로 판정(보호 경로 거부)·추정 표시·용량
 *   - 선택 정책: [모두 선택]은 MANUAL 을 고르지 않음, id 로 직접 고르기, 고를 수 없는 대상, 요약
 *   - 실행: 영구 삭제는 allow_permanent 일 때만 그리고 다른 항목보다 먼저, 실행 중 프로그램·최근 변경은
 *     건너뜀, 사용 중 파일은 실패로 남기고 다시 고를 수 있음(다시 실행하면 옮김), 큰 대상부터
 */

static int g_failures = 0;
static wchar_t g_root[MAX_PATH];

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static void fx(wchar_t *out, const wchar_t *rel)
{
    swprintf(out, MAX_PATH, L"%ls\\%ls", g_root, rel);
}

static int exists(const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    fx(p, rel);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static void make_dirs(const wchar_t *rel)
{
    wchar_t p[MAX_PATH], *s;
    fx(p, rel);
    for (s = p + wcslen(g_root) + 1; *s != L'\0'; s++) {
        if (*s == L'\\') {
            *s = L'\0';
            CreateDirectoryW(p, NULL);
            *s = L'\\';
        }
    }
    CreateDirectoryW(p, NULL);
}

static void write_file(const wchar_t *rel, DWORD bytes)
{
    static char data[8192];
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD written = 0;

    fx(p, rel);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, data, bytes, &written, NULL);
        CloseHandle(h);
    }
}

/* 파일·폴더의 시각을 hours_ago 시간 전으로 바꾼다. */
static void set_age(const wchar_t *rel, int hours_ago)
{
    wchar_t p[MAX_PATH];
    FILETIME ft;
    ULARGE_INTEGER u;
    HANDLE h;

    fx(p, rel);
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= (ULONGLONG)hours_ago * 36000000000ull;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    h = CreateFileW(p, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &ft, NULL, &ft);
        CloseHandle(h);
    }
}

static void remove_tree(const wchar_t *dir)
{
    wchar_t pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            swprintf(child, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_tree(child);
            } else {
                SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* ---- 가짜 시스템 함수 ---- */

typedef struct {
    int empty_calls;
    int big_existed_at_empty; /* 비울 때 아직 옮기지 않은 항목이 있었는지(순서 확인) */
    const wchar_t *running;
} Fake;

static int fake_running(const wchar_t *exe, void *ctx)
{
    const Fake *f = ctx;
    return f->running != NULL && _wcsicmp(exe, f->running) == 0;
}

static int fake_empty(uint32_t *code, void *ctx)
{
    Fake *f = ctx;
    (void)code;
    f->empty_calls++;
    f->big_existed_at_empty = exists(L"run\\big.bin");
    return 0;
}

static CleanPlanOps make_ops(Fake *f)
{
    CleanPlanOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.app_running = fake_running;
    ops.empty_recycle = fake_empty;
    ops.ctx = f;
    return ops;
}

/* ---- 프로젝트 후보 → 계획 ---- */

static void test_from_project(void)
{
    CleanupResult r;
    CleanupGuard guard;
    CleanPlan plan;
    wchar_t p[MAX_PATH], prot[MAX_PATH];
    size_t i, nm = 0, guessed = 0, denied = 0;

    make_dirs(L"proj\\web\\node_modules\\dep");
    write_file(L"proj\\web\\package.json", 10);
    write_file(L"proj\\web\\node_modules\\dep\\i.js", 3000);
    make_dirs(L"proj\\loose\\dist");
    write_file(L"proj\\loose\\dist\\a.js", 2000); /* 표시 파일 없음 → 추정 */
    make_dirs(L"proj\\locked\\node_modules");
    write_file(L"proj\\locked\\package.json", 10);
    write_file(L"proj\\locked\\node_modules\\x.js", 1000);

    cleanup_result_init(&r);
    fx(p, L"proj");
    CHECK(cleanup_scan(&r, p, NULL, NULL, NULL, NULL) == SCAN_COMPLETE);
    /* 보호 경로 하나를 더한 판정: 그 아래 후보는 고를 수 없다. */
    cleanup_guard_init(&guard);
    cleanup_guard_add_system(&guard);
    fx(prot, L"proj\\locked");
    cleanup_protect_add(&guard.protect, prot);
    clean_plan_init(&plan);
    CHECK(clean_plan_from_project(&plan, &r, &guard) == 0);
    CHECK(plan.len == r.len && r.len == 3);
    for (i = 0; i < plan.len; i++) {
        const CleanPlanTarget *t = &plan.items[i];
        CHECK(t->origin == i && t->source == CLEAN_SOURCE_PROJECT && t->entry_count == 1);
        CHECK(wcscmp(t->entries[0].item.path, r.items[i].path) == 0);
        CHECK(wcscmp(t->entries[0].root, r.root) == 0);
        CHECK(t->bytes == r.items[i].bytes && t->reclaim == cleanup_item_reclaim(&r.items[i]));
        CHECK(wcscmp(t->id, cleanup_category_name(r.items[i].category)) == 0);
        CHECK(!(t->flags & (CLEAN_PLAN_MANUAL | CLEAN_PLAN_PERMANENT)));
        if (t->deny != CLEANUP_TRASH_PENDING) {
            denied++;
            CHECK(!t->ready && !clean_plan_selectable(t));
            CHECK(wcscmp(t->state, cleanup_trash_status_name(t->deny)) == 0);
            CHECK(wcsstr(t->location, L"locked") != NULL);
        } else {
            CHECK(t->ready && clean_plan_selectable(t) && wcscmp(t->state, L"ready") == 0);
        }
        guessed += (t->flags & CLEAN_PLAN_GUESSED) != 0;
        nm += wcscmp(t->id, L"node_modules") == 0;
    }
    CHECK(denied == 1 && guessed == 1 && nm == 2);
    /* [모두 선택]: 보호된 것은 고르지 않는다 */
    CHECK(clean_plan_select_all(&plan, 1) == 2);
    {
        CleanPlanSummary s;
        clean_plan_summarize(&plan, &s);
        CHECK(s.targets == 2 && s.selectable == 2 && s.guessed == 1 && s.entries == 2);
        CHECK(s.bytes == 5000 && s.permanent == 0 && s.manual == 0);
    }
    clean_plan_free(&plan);
    cleanup_guard_free(&guard);
    cleanup_result_free(&r);
}

/* ---- 직접 만든 계획(시스템 정리와 같은 꼴) ---- */

/* 파일·폴더 하나짜리 항목을 붙인다. */
static void add_entry(CleanPlanTarget *t, const wchar_t *rel, int dir, uint64_t bytes)
{
    CleanupItem item;
    wchar_t p[MAX_PATH], root[MAX_PATH];

    fx(p, rel);
    fx(root, L"run");
    memset(&item, 0, sizeof(item));
    item.path = p;
    item.category = CLEANUP_TEMP;
    item.flags = dir ? CLEANUP_ITEM_DIR : 0u;
    item.bytes = bytes;
    item.files = 1;
    CHECK(clean_plan_add_entry(t, &item, root) == 0);
    t->bytes += bytes;
    t->reclaim += bytes;
}

static CleanPlanTarget *add_target(CleanPlan *plan, const wchar_t *id, unsigned flags)
{
    CleanPlanTarget *t = clean_plan_add(plan, CLEAN_SOURCE_SYSTEM, id, id, g_root);
    CHECK(t != NULL);
    if (t != NULL) {
        t->ready = 1;
        t->flags = flags;
        t->state = L"ready";
    }
    return t;
}

static void test_selection(void)
{
    CleanPlan plan;
    CleanPlanSummary s;
    CleanPlanTarget *t;

    clean_plan_init(&plan);
    make_dirs(L"sel");
    write_file(L"sel\\a.tmp", 100);
    write_file(L"sel\\b.tmp", 100);
    t = add_target(&plan, L"user_temp", 0);
    add_entry(t, L"sel\\a.tmp", 0, 100);
    t = add_target(&plan, L"npm", CLEAN_PLAN_MANUAL);
    add_entry(t, L"sel\\b.tmp", 0, 100);
    t = add_target(&plan, L"nuget_packages", CLEAN_PLAN_MANUAL | CLEAN_PLAN_CAUTION);
    add_entry(t, L"sel\\b.tmp", 0, 50);
    t = add_target(&plan, L"recycle_bin", CLEAN_PLAN_MANUAL | CLEAN_PLAN_PERMANENT);
    t->bytes = t->reclaim = 1234;
    t = add_target(&plan, L"edge", 0);
    t->ready = 0; /* 설치하지 않음 */
    t->state = L"not_found";
    add_entry(t, L"sel\\a.tmp", 0, 1);

    /* [모두 선택]은 MANUAL(개발 캐시·휴지통)과 고를 수 없는 대상을 고르지 않는다 */
    CHECK(clean_plan_select_all(&plan, 1) == 1);
    CHECK(plan.items[0].selected && !plan.items[1].selected && !plan.items[2].selected &&
          !plan.items[3].selected && !plan.items[4].selected);
    /* id 로 직접 고르면 MANUAL 도 들어간다(대소문자 무시) */
    CHECK(clean_plan_select_id(&plan, L"NPM", 1) == 1 && plan.items[1].selected);
    CHECK(clean_plan_select_id(&plan, L"nuget_packages", 1) == 1);
    CHECK(clean_plan_select_id(&plan, L"recycle_bin", 1) == 1 && plan.items[3].selected);
    CHECK(clean_plan_select_id(&plan, L"edge", 1) == 1 && !plan.items[4].selected);
    CHECK(clean_plan_select_id(&plan, L"none", 1) == 0);
    clean_plan_summarize(&plan, &s);
    CHECK(s.targets == 4 && s.selectable == 4);
    CHECK(s.manual == 2 && s.caution == 1 && s.permanent == 1 && s.permanent_bytes == 1234);
    CHECK(s.bytes == 250 && s.reclaim == 250); /* 영구 삭제 제외 */
    /* [모두 선택]을 다시 눌러도 직접 고른 MANUAL 은 그대로, 해제는 모두 */
    CHECK(clean_plan_select_all(&plan, 1) == 4);
    CHECK(clean_plan_select_all(&plan, 0) == 0);
    clean_plan_summarize(&plan, &s);
    CHECK(s.targets == 0 && s.bytes == 0);
    CHECK(!clean_plan_set_selected(&plan, 4, 1) && !clean_plan_set_selected(&plan, 99, 1));
    clean_plan_free(&plan);
}

static void test_run(void)
{
    CleanPlan plan;
    CleanPlanTotals tot;
    CleanPlanOps ops;
    CleanPlanTarget *t;
    Fake f;
    HANDLE locked;
    wchar_t p[MAX_PATH];
    size_t i;

    make_dirs(L"run\\olddir\\sub");
    write_file(L"run\\big.bin", 8000);
    write_file(L"run\\small.bin", 100);
    write_file(L"run\\locked.bin", 100);
    write_file(L"run\\app.bin", 100);
    write_file(L"run\\olddir\\sub\\x.bin", 100);
    write_file(L"run\\fresh.bin", 100);
    set_age(L"run\\olddir\\sub\\x.bin", 72);
    set_age(L"run\\olddir\\sub", 72);
    set_age(L"run\\olddir", 72);

    clean_plan_init(&plan);
    t = add_target(&plan, L"small", 0);                  /* 0: 작은 대상 */
    add_entry(t, L"run\\small.bin", 0, 100);
    add_entry(t, L"run\\locked.bin", 0, 100);           /* 사용 중 → 실패 */
    t = add_target(&plan, L"recycle_bin", CLEAN_PLAN_MANUAL | CLEAN_PLAN_PERMANENT); /* 1 */
    t->bytes = t->reclaim = 777;
    t = add_target(&plan, L"big", 0);                    /* 2: 큰 대상 */
    add_entry(t, L"run\\big.bin", 0, 8000);
    t = add_target(&plan, L"browser", 0);                /* 3: 프로그램 실행 중 → 건너뜀 */
    t->app = path_dup(L"fakebrowser.exe");
    add_entry(t, L"run\\app.bin", 0, 100);
    t = add_target(&plan, L"temp", CLEAN_PLAN_CHECK_RECENT); /* 4: 최근 변경 확인 */
    add_entry(t, L"run\\olddir", 1, 100);                /* 오래됨 → 옮김 */
    add_entry(t, L"run\\fresh.bin", 0, 100);             /* 방금 만듦 → 건너뜀 */

    for (i = 0; i < plan.len; i++) {
        clean_plan_set_selected(&plan, i, 1);
    }
    memset(&f, 0, sizeof(f));
    f.running = L"fakebrowser.exe";
    ops = make_ops(&f);

    /* allow_permanent 가 0 이면 휴지통은 골라져 있어도 비우지 않는다 */
    fx(p, L"run\\locked.bin");
    locked = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(clean_plan_run(&plan, &ops, &tot) == 0);
    CHECK(f.empty_calls == 0 && !tot.permanent_requested && !plan.items[1].ran);
    CHECK(plan.items[1].selected); /* 실행하지 않은 영구 삭제 대상은 선택이 남는다 */
    CHECK(tot.targets == 4);
    CHECK(!exists(L"run\\big.bin") && !exists(L"run\\small.bin") && !exists(L"run\\olddir"));
    CHECK(exists(L"run\\locked.bin") && exists(L"run\\app.bin") && exists(L"run\\fresh.bin"));
    CHECK(tot.moved == 3 && tot.quarantined == 3 && tot.failed == 1 && tot.skipped == 2);
    CHECK(tot.moved_bytes == 8200);
    /* 대상별 결과와 항목별 결과 */
    CHECK(plan.items[0].ran && plan.items[0].moved == 1 && plan.items[0].failed == 1);
    CHECK(plan.items[0].entries[0].outcome.status == CLEANUP_TRASH_QUARANTINED &&
          plan.items[0].entries[0].outcome.recycled_path != NULL);
    CHECK(plan.items[0].entries[1].outcome.status == CLEANUP_TRASH_FAILED ||
          plan.items[0].entries[1].outcome.status == CLEANUP_TRASH_QUARANTINE_FAILED);
    CHECK(plan.items[3].skipped == 1 && plan.items[3].entries[0].skip == CLEAN_SKIP_APP_RUNNING);
    CHECK(plan.items[4].moved == 1 && plan.items[4].skipped == 1 &&
          plan.items[4].entries[1].skip == CLEAN_SKIP_RECENT);
    /* 실패만 남은 대상은 다시 고를 수 있고 선택이 남는다. 다 옮긴 대상·건너뛴 대상은 아니다. */
    CHECK(clean_plan_selectable(&plan.items[0]) && plan.items[0].selected);
    CHECK(!clean_plan_selectable(&plan.items[2]) && !plan.items[2].selected);
    CHECK(!clean_plan_selectable(&plan.items[3]) && !clean_plan_selectable(&plan.items[4]));
    CloseHandle(locked);

    /* 다시 실행: 영구 삭제를 허용하면 먼저 비우고, 실패했던 항목만 다시 옮긴다 */
    ops.allow_permanent = 1;
    write_file(L"run\\big.bin", 10); /* 비우기가 다른 항목보다 먼저인지 보려는 표시 */
    CHECK(clean_plan_run(&plan, &ops, &tot) == 0);
    CHECK(f.empty_calls == 1 && f.big_existed_at_empty);
    CHECK(tot.permanent_requested && tot.permanent_done && tot.permanent_bytes == 777);
    CHECK(plan.items[1].ran && plan.items[1].moved == 1 && !plan.items[1].selected);
    CHECK(!clean_plan_selectable(&plan.items[1])); /* 비운 뒤에는 다시 분석해야 한다 */
    CHECK(!exists(L"run\\locked.bin") && plan.items[0].moved == 1 && plan.items[0].failed == 0);
    CHECK(plan.items[0].entries[1].outcome.status == CLEANUP_TRASH_QUARANTINED);
    CHECK(exists(L"run\\big.bin")); /* 다 옮긴 대상은 다시 실행하지 않는다 */
    CHECK(tot.moved == 1 && tot.failed == 0); /* locked 하나(휴지통 비우기는 permanent_*) */

    /* 더 할 것이 없으면 실행하지 않는다 */
    CHECK(clean_plan_run(&plan, &ops, &tot) == -1 && f.empty_calls == 1);
    clean_plan_free(&plan);
}

/* 프로젝트 분석 결과 붙이기: 미커밋 변경 → DIRTY|MANUAL(선택 해제, [모두 선택] 제외), 미푸시·휴면 표시,
 * 복구 가능성·프로젝트 번호, 요약의 dirty·unpushed. 시스템 대상·범위 밖 번호는 건드리지 않는다. */
static void test_apply_projects(void)
{
    CleanPlan plan;
    ProjectList pl;
    ProjectInfo items[2];
    size_t map[3] = {0, 1, 1};
    ProjectRecovery rec[3] = {PROJECT_RECOVER_UNLOCKED, PROJECT_RECOVER_LOCKED,
                              PROJECT_RECOVER_REBUILD};
    CleanPlanSummary s;
    CleanPlanTarget *t;
    size_t i;

    clean_plan_init(&plan);
    make_dirs(L"apply");
    write_file(L"apply\\a", 10);
    for (i = 0; i < 3; i++) {
        t = add_target(&plan, L"node_modules", 0);
        t->source = CLEAN_SOURCE_PROJECT;
        t->origin = i;
        add_entry(t, L"apply\\a", 0, 10);
    }
    t = add_target(&plan, L"user_temp", 0); /* 시스템 대상: 건드리지 않음 */
    add_entry(t, L"apply\\a", 0, 10);
    memset(items, 0, sizeof(items));
    items[0].changes = 2;      /* 작업 중 */
    items[0].unpushed = 0;
    items[1].changes = 0;
    items[1].unpushed = 3;     /* 미푸시 */
    items[1].dormant = 1;
    project_list_init(&pl);
    pl.items = items;
    pl.len = 2;
    pl.item_project = map;
    pl.item_recovery = rec;
    pl.item_count = 3;

    clean_plan_set_selected(&plan, 0, 1); /* 미리 골라 둔 것도 풀려야 한다 */
    clean_plan_apply_projects(&plan, &pl);
    CHECK((plan.items[0].flags & (CLEAN_PLAN_DIRTY | CLEAN_PLAN_MANUAL)) ==
          (CLEAN_PLAN_DIRTY | CLEAN_PLAN_MANUAL));
    CHECK(!plan.items[0].selected && !(plan.items[0].flags & CLEAN_PLAN_UNPUSHED));
    CHECK(plan.items[1].flags == (CLEAN_PLAN_UNPUSHED | CLEAN_PLAN_DORMANT));
    CHECK(plan.items[0].project == 0 && plan.items[2].project == 1);
    CHECK(plan.items[0].recovery == PROJECT_RECOVER_UNLOCKED &&
          plan.items[1].recovery == PROJECT_RECOVER_LOCKED);
    CHECK(plan.items[3].project == (size_t)-1 && plan.items[3].recovery == -1 &&
          plan.items[3].flags == 0);
    /* [모두 선택]은 작업 중인 프로젝트의 후보를 고르지 않고, 직접 고르면 요약에 나온다 */
    CHECK(clean_plan_select_all(&plan, 1) == 3 && !plan.items[0].selected);
    clean_plan_set_selected(&plan, 0, 1);
    clean_plan_summarize(&plan, &s);
    CHECK(s.targets == 4 && s.dirty == 1 && s.unpushed == 2 && s.manual == 0);
    /* 다시 붙여도 표시가 쌓이지 않고 같은 결과 */
    clean_plan_apply_projects(&plan, &pl);
    CHECK(plan.items[1].flags == (CLEAN_PLAN_UNPUSHED | CLEAN_PLAN_DORMANT));
    clean_plan_apply_projects(&plan, NULL);
    clean_plan_free(&plan);
}

int main(void)
{
    wchar_t tmp[MAX_PATH];

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_root, MAX_PATH, L"%lsbytesieve_plan_%lu", tmp, GetCurrentProcessId());
    remove_tree(g_root);
    CreateDirectoryW(g_root, NULL);
    SetEnvironmentVariableW(L"BYTESIEVE_QUARANTINE_ROOT", g_root);

    CHECK(wcscmp(clean_skip_name(CLEAN_SKIP_RECENT), L"recent") == 0);
    CHECK(clean_skip_text(CLEAN_SKIP_APP_RUNNING)[0] != L'\0');
    test_from_project();
    test_selection();
    test_run();
    test_apply_projects();

    remove_tree(g_root);
    CoUninitialize();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("clean_plan: all checks passed\n");
    return 0;
}
