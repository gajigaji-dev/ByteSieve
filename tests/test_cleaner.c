#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "path.h"

/*
 * Cleaner 단위 테스트. 사용자 폴더 환경 변수(TEMP·LOCALAPPDATA·USERPROFILE·APPDATA)를 임시 픽스처로
 * 돌리고 BYTESIEVE_QUARANTINE_ROOT 를 픽스처로 두어, 옮긴 항목이 실제 휴지통이 아니라 픽스처의
 * 격리 폴더로 가게 한다. 휴지통 조회·비우기와 프로그램 실행 여부는 가짜 함수로 바꾼다
 * (실제 휴지통은 조회도 비우기도 하지 않는다).
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

/* ---- 픽스처 ---- */

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
    wchar_t p[MAX_PATH];
    size_t i;

    fx(p, rel);
    for (i = wcslen(g_root) + 1; p[i] != L'\0'; i++) {
        if (p[i] == L'\\') {
            p[i] = L'\0';
            CreateDirectoryW(p, NULL);
            p[i] = L'\\';
        }
    }
    CreateDirectoryW(p, NULL);
}

static void write_file(const wchar_t *rel, size_t bytes)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    char buf[512];
    DWORD done;

    fx(p, rel);
    memset(buf, 'x', sizeof(buf));
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    while (h != INVALID_HANDLE_VALUE && bytes > 0) {
        DWORD n = bytes > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)bytes;
        WriteFile(h, buf, n, &done, NULL);
        bytes -= n;
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

/* 만든 시각·수정 시각을 hours_ago 시간 전으로. 폴더도 된다. */
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
    u.QuadPart -= (ULONGLONG)hours_ago * 36000000000ULL;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    h = CreateFileW(p, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(SetFileTime(h, &ft, NULL, &ft));
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
            SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                remove_tree(child);
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(child);
            } else {
                DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static void set_env(const wchar_t *name, const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    if (rel == NULL) {
        SetEnvironmentVariableW(name, NULL);
        return;
    }
    fx(p, rel);
    SetEnvironmentVariableW(name, p);
}

static void build_fixture(void)
{
    /* 사용자 임시 폴더 */
    make_dirs(L"temp");
    write_file(L"temp\\old.txt", 100);
    set_age(L"temp\\old.txt", 72);
    write_file(L"temp\\new.txt", 50); /* 지금 만든 파일: 최근이라 뺀다 */
    write_file(L"temp\\locked.txt", 30);
    set_age(L"temp\\locked.txt", 72);
    make_dirs(L"temp\\olddir\\sub");
    write_file(L"temp\\olddir\\sub\\a.bin", 200);
    set_age(L"temp\\olddir\\sub\\a.bin", 72);
    set_age(L"temp\\olddir\\sub", 72);
    set_age(L"temp\\olddir", 72);
    make_dirs(L"temp\\mixed");
    write_file(L"temp\\mixed\\old.bin", 10);
    set_age(L"temp\\mixed\\old.bin", 72);
    write_file(L"temp\\mixed\\new.bin", 10); /* 안에 최근 파일이 있어 폴더 전체를 뺀다 */
    set_age(L"temp\\mixed", 72);

    /* Windows 오류 보고서 */
    make_dirs(L"local\\Microsoft\\Windows\\WER\\ReportArchive\\rep1");
    write_file(L"local\\Microsoft\\Windows\\WER\\ReportArchive\\rep1\\Report.wer", 60);
    set_age(L"local\\Microsoft\\Windows\\WER\\ReportArchive\\rep1\\Report.wer", 72);
    set_age(L"local\\Microsoft\\Windows\\WER\\ReportArchive\\rep1", 72);

    /* Chrome: 프로필(Default)의 캐시 둘, Preferences 가 없는 폴더는 프로필이 아님 */
    make_dirs(L"local\\Google\\Chrome\\User Data\\Default\\Cache\\Cache_Data");
    write_file(L"local\\Google\\Chrome\\User Data\\Default\\Preferences", 5);
    write_file(L"local\\Google\\Chrome\\User Data\\Default\\Cache\\Cache_Data\\data_1", 300);
    make_dirs(L"local\\Google\\Chrome\\User Data\\Default\\Code Cache\\js");
    write_file(L"local\\Google\\Chrome\\User Data\\Default\\Code Cache\\js\\x", 10);
    make_dirs(L"local\\Google\\Chrome\\User Data\\NotProfile\\Cache");
    write_file(L"local\\Google\\Chrome\\User Data\\NotProfile\\Cache\\y", 7);

    /* Firefox: 실행 중(가짜)이라 정리하지 않음 */
    make_dirs(L"local\\Mozilla\\Firefox\\Profiles\\abc.default\\cache2\\entries");
    write_file(L"local\\Mozilla\\Firefox\\Profiles\\abc.default\\cache2\\entries\\e1", 40);

    /* 개발 도구 캐시: npm 기본 위치 */
    make_dirs(L"local\\npm-cache\\_cacache\\index-v5");
    write_file(L"local\\npm-cache\\_cacache\\index-v5\\i", 20);

    make_dirs(L"home");
    make_dirs(L"roaming");

    set_env(L"TEMP", L"temp");
    set_env(L"TMP", L"temp");
    set_env(L"LOCALAPPDATA", L"local");
    set_env(L"USERPROFILE", L"home");
    set_env(L"APPDATA", L"roaming");
    set_env(L"npm_config_cache", NULL);
    set_env(L"NPM_CONFIG_USERCONFIG", NULL);
    set_env(L"PIP_CACHE_DIR", NULL);
    set_env(L"PIP_CONFIG_FILE", NULL);
    set_env(L"GRADLE_USER_HOME", NULL);
    set_env(L"CARGO_HOME", NULL);
    set_env(L"NUGET_PACKAGES", NULL);
    set_env(L"NUGET_HTTP_CACHE_PATH", NULL);
    SetEnvironmentVariableW(L"BYTESIEVE_QUARANTINE_ROOT", g_root);
}

/* ---- 가짜 시스템 함수 ---- */

typedef struct {
    int empty_calls;
    int empty_fail;
    int old_existed_at_empty; /* 비울 때 아직 옮기지 않은 항목이 있었는지(순서 확인) */
    const wchar_t *running;   /* 실행 중으로 볼 프로그램 */
} Fake;

static int fake_running(const wchar_t *exe, void *ctx)
{
    Fake *f = ctx;
    return f->running != NULL && _wcsicmp(exe, f->running) == 0;
}

static int fake_query(uint64_t *bytes, uint64_t *items, uint32_t *code, void *ctx)
{
    (void)ctx;
    (void)code;
    *bytes = 1234;
    *items = 3;
    return 0;
}

static int fake_empty(uint32_t *code, void *ctx)
{
    Fake *f = ctx;
    f->empty_calls++;
    f->old_existed_at_empty = exists(L"temp\\old.txt");
    if (f->empty_fail) {
        *code = 0x80004005u;
        return -1;
    }
    return 0;
}

static CleanerOps make_ops(Fake *f)
{
    CleanerOps ops;
    memset(&ops, 0, sizeof(ops));
    ops.app_running = fake_running;
    ops.query_recycle = fake_query;
    ops.empty_recycle = fake_empty;
    ops.ctx = f;
    return ops;
}

static const CleanerTarget *find(const CleanerList *l, CleanerKind kind)
{
    size_t i;
    for (i = 0; i < l->len; i++) {
        if (l->items[i].kind == kind) {
            return &l->items[i];
        }
    }
    return NULL;
}

static size_t index_of(const CleanerList *l, CleanerKind kind)
{
    size_t i;
    for (i = 0; i < l->len; i++) {
        if (l->items[i].kind == kind) {
            return i;
        }
    }
    return (size_t)-1;
}

static int has_entry(const CleanerTarget *t, const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    size_t i;
    fx(p, rel);
    for (i = 0; t != NULL && i < t->entry_count; i++) {
        if (_wcsicmp(t->entries[i].item.path, p) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- 테스트 ---- */

static void test_names(void)
{
    int i;
    for (i = 0; i < CLEANER_STATUS_COUNT; i++) {
        CHECK(cleaner_status_text((CleanerStatus)i)[0] != L'\0');
        CHECK(wcscmp(cleaner_status_name((CleanerStatus)i), L"unknown") != 0);
    }
    for (i = 0; i < CLEANER_SKIP_COUNT; i++) {
        CHECK(wcscmp(cleaner_skip_name((CleanerSkip)i), L"unknown") != 0);
    }
    for (i = 0; i < CLEANER_KIND_COUNT; i++) {
        const CleanerKindInfo *k = cleaner_kind_info((CleanerKind)i);
        CHECK(k != NULL && k->name[0] != L'\0' && k->note[0] != L'\0');
        CHECK(cleaner_group_text(k->group)[0] != L'\0');
    }
    CHECK(cleaner_kind_info(CLEANER_RECYCLE_BIN)->permanent == 1);
    CHECK(cleaner_kind_info(CLEANER_USER_TEMP)->permanent == 0);
}

/* [모두 선택] 정책: 휴지통 비우기·개발 도구 캐시는 직접 골라야 하고, 오프라인 빌드가 안 되는 캐시는 주의. */
static void test_selection_policy(void)
{
    CleanerTarget t;
    int k;

    memset(&t, 0, sizeof(t));
    for (k = 0; k < CLEANER_KIND_COUNT; k++) {
        t.kind = (CleanerKind)k;
        t.dev_kind = DEV_CACHE_NPM;
        CHECK(cleaner_target_manual_only(&t) ==
              (k == CLEANER_RECYCLE_BIN || k == CLEANER_DEV_CACHE));
        CHECK(cleaner_target_caution(&t) == 0); /* npm 은 주의가 아니고, 개발 캐시 밖은 주의 없음 */
    }
    t.kind = CLEANER_DEV_CACHE;
    for (k = 0; k < DEV_CACHE_KIND_COUNT; k++) {
        t.dev_kind = (DevCacheKind)k;
        CHECK(cleaner_target_manual_only(&t));
        CHECK(cleaner_target_caution(&t) == dev_cache_kind_info((DevCacheKind)k)->caution);
    }
    t.dev_kind = DEV_CACHE_GRADLE_WRAPPER;
    CHECK(cleaner_target_caution(&t));
    t.dev_kind = DEV_CACHE_NUGET_PACKAGES;
    CHECK(cleaner_target_caution(&t));
    /* dev_kind 는 개발 캐시일 때만 본다 */
    t.kind = CLEANER_USER_TEMP;
    CHECK(!cleaner_target_caution(&t) && !cleaner_target_manual_only(&t));
    CHECK(!cleaner_target_manual_only(NULL) && !cleaner_target_caution(NULL));
}

static void test_analyze(CleanerList *l, Fake *f)
{
    CleanerOps ops = make_ops(f);
    const CleanerTarget *t;

    CHECK(cleaner_analyze(l, NULL, &ops, NULL, NULL, NULL) == 0);
    CHECK(!l->cancelled);

    t = find(l, CLEANER_USER_TEMP);
    CHECK(t != NULL && t->status == CLEANER_READY);
    CHECK(t != NULL && t->entry_count == 3);
    CHECK(has_entry(t, L"temp\\old.txt") && has_entry(t, L"temp\\olddir") &&
          has_entry(t, L"temp\\locked.txt"));
    CHECK(!has_entry(t, L"temp\\new.txt") && !has_entry(t, L"temp\\mixed"));
    CHECK(t != NULL && t->recent_count == 2 && t->recent_bytes == 70);
    CHECK(t != NULL && t->bytes == 330 && t->files == 3);

    t = find(l, CLEANER_WER);
    CHECK(t != NULL && t->status == CLEANER_READY && t->entry_count == 1 && t->bytes == 60);

    t = find(l, CLEANER_RECYCLE_BIN);
    CHECK(t != NULL && t->status == CLEANER_READY && t->bytes == 1234 && t->files == 3);
    CHECK(t != NULL && t->entry_count == 0);

    t = find(l, CLEANER_CHROME);
    CHECK(t != NULL && t->status == CLEANER_READY && t->entry_count == 2 && t->bytes == 310);
    CHECK(has_entry(t, L"local\\Google\\Chrome\\User Data\\Default\\Cache"));
    CHECK(!has_entry(t, L"local\\Google\\Chrome\\User Data\\NotProfile\\Cache"));
    CHECK(t != NULL && t->label != NULL && wcsstr(t->label, L"1") != NULL);

    t = find(l, CLEANER_EDGE);
    CHECK(t != NULL && t->status == CLEANER_NOT_FOUND && t->entry_count == 0);

    t = find(l, CLEANER_FIREFOX);
    CHECK(t != NULL && t->status == CLEANER_APP_RUNNING && t->entry_count == 1 && t->bytes == 40);
    CHECK(!cleaner_target_runnable(t));

    t = find(l, CLEANER_DEV_CACHE);
    CHECK(t != NULL && t->status == CLEANER_READY && t->entry_count == 1 && t->bytes == 20);
    CHECK(t != NULL && t->dev_kind == DEV_CACHE_NPM);
    /* 위치 근거·상태: 환경 변수를 지웠으므로 기본 위치에서 찾음 */
    CHECK(t != NULL && t->dev_status == DEV_CACHE_FOUND && t->dev_source == DEV_CACHE_SRC_DEFAULT &&
          t->dev_origin == NULL);

    /* 실제 확보 가능 용량: 항목마다 셌고(MEASURED), 대상 합계는 항목 합. 하드 링크가 없으니 공유 0.
     * 휴지통은 조회한 용량 그대로. 작은 픽스처 파일은 MFT 상주라 0 일 수 있으므로 논리 크기 이하인지만 본다. */
    {
        size_t i, j;
        for (i = 0; i < l->len; i++) {
            const CleanerTarget *x = &l->items[i];
            uint64_t sum = 0;
            for (j = 0; j < x->entry_count; j++) {
                CHECK(x->entries[j].item.flags & CLEANUP_ITEM_MEASURED);
                sum += cleanup_item_reclaim(&x->entries[j].item);
            }
            if (x->kind == CLEANER_RECYCLE_BIN) {
                CHECK(x->reclaim_bytes == x->bytes);
            } else {
                CHECK(x->reclaim_bytes == sum);
                CHECK(x->shared_bytes == 0);
            }
        }
    }
}

/* 모두 골라 실행: 휴지통을 먼저 비우고, 최근에 바뀐 항목·사용 중 파일·실행 중 브라우저는 남는다. */
static void test_run(CleanerList *l, Fake *f)
{
    CleanerOps ops = make_ops(f);
    CleanerRunResult r;
    CleanerRunJob *job;
    int *selected = calloc(l->len, sizeof(*selected));
    wchar_t locked_path[MAX_PATH];
    HANDLE locked;
    size_t i, temp = index_of(l, CLEANER_USER_TEMP), chrome = index_of(l, CLEANER_CHROME);
    size_t ff = index_of(l, CLEANER_FIREFOX), rb = index_of(l, CLEANER_RECYCLE_BIN);
    size_t dev = index_of(l, CLEANER_DEV_CACHE);

    if (selected == NULL) {
        g_failures++;
        return;
    }
    for (i = 0; i < l->len; i++) {
        selected[i] = 1;
    }
    /* 분석 뒤 olddir 안 파일이 바뀌었다 → 실행 때 다시 확인해 건너뛴다. */
    write_file(L"temp\\olddir\\sub\\a.bin", 200);
    /* locked.txt 는 공유 없이 열어 둬 옮기지 못한다. */
    fx(locked_path, L"temp\\locked.txt");
    locked = CreateFileW(locked_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);

    job = cleaner_run_start(l, selected, &ops, NULL, NULL);
    CHECK(job != NULL);
    if (job == NULL) {
        free(selected);
        CloseHandle(locked);
        return;
    }
    cleaner_run_finish(job, &r);
    CloseHandle(locked);

    /* 휴지통(가짜)은 한 번, 다른 항목을 옮기기 전에 비웠다 */
    CHECK(f->empty_calls == 1 && f->old_existed_at_empty);
    CHECK(r.recycle_requested && r.recycle_emptied && r.recycle_bytes == 1234);

    /* 임시 파일: old.txt 만 옮김(격리 폴더), olddir 은 최근 변경, locked 는 실패 */
    CHECK(!exists(L"temp\\old.txt"));
    CHECK(exists(L"temp\\olddir\\sub\\a.bin"));
    CHECK(exists(L"temp\\locked.txt"));
    CHECK(exists(L"temp\\new.txt") && exists(L"temp\\mixed\\new.bin"));
    CHECK(r.targets[temp].selected && r.targets[temp].moved == 1 &&
          r.targets[temp].quarantined == 1 && r.targets[temp].moved_bytes == 100);
    CHECK(r.targets[temp].skipped == 1 && r.targets[temp].failed == 1);

    /* Chrome 캐시는 옮기고 프로필 설정(Preferences)은 남는다 */
    CHECK(!exists(L"local\\Google\\Chrome\\User Data\\Default\\Cache"));
    CHECK(!exists(L"local\\Google\\Chrome\\User Data\\Default\\Code Cache"));
    CHECK(exists(L"local\\Google\\Chrome\\User Data\\Default\\Preferences"));
    CHECK(r.targets[chrome].moved == 2 && r.targets[chrome].moved_bytes == 310);

    /* 실행 중(실행할 수 없는) 대상은 골라도 하지 않는다 */
    CHECK(!r.targets[ff].selected && exists(L"local\\Mozilla\\Firefox\\Profiles\\abc.default\\cache2"));
    CHECK(r.targets[rb].selected);

    /* 개발 도구 캐시 */
    CHECK(!exists(L"local\\npm-cache") && exists(L"local"));
    CHECK(r.targets[dev].moved == 1);

    /* 항목별 결과: 옮긴 것은 격리 폴더 안 위치가 있고, 건너뛴 이유·실패가 남는다 */
    for (i = 0; i < r.entry_count; i++) {
        const CleanerEntryOutcome *o = &r.entries[i];
        if (o->skip == CLEANER_SKIP_NONE && o->trash.status == CLEANUP_TRASH_QUARANTINED) {
            CHECK(o->trash.recycled_path != NULL &&
                  GetFileAttributesW(o->trash.recycled_path) != INVALID_FILE_ATTRIBUTES);
        }
        if (wcsstr(o->path, L"olddir") != NULL) {
            CHECK(o->skip == CLEANER_SKIP_RECENT);
        }
        if (wcsstr(o->path, L"locked.txt") != NULL) {
            CHECK(o->skip == CLEANER_SKIP_NONE && o->trash.status != CLEANUP_TRASH_MOVED &&
                  o->trash.status != CLEANUP_TRASH_QUARANTINED);
        }
    }
    cleaner_run_result_free(&r);
    free(selected);
}

/* 휴지통 비우기가 실패해도 나머지는 계속, 고르지 않으면 비우지 않는다. */
static void test_recycle_failure(Fake *f)
{
    CleanerList l;
    CleanerOps ops;
    CleanerRunResult r;
    CleanerRunJob *job;
    int *selected;
    size_t i;

    write_file(L"temp\\old2.txt", 11);
    set_age(L"temp\\old2.txt", 72);
    cleaner_list_init(&l);
    f->empty_calls = 0;
    f->empty_fail = 1;
    ops = make_ops(f);
    CHECK(cleaner_analyze(&l, NULL, &ops, NULL, NULL, NULL) == 0);
    selected = calloc(l.len, sizeof(*selected));
    if (selected == NULL) {
        g_failures++;
        cleaner_list_free(&l);
        return;
    }
    selected[index_of(&l, CLEANER_RECYCLE_BIN)] = 1;
    selected[index_of(&l, CLEANER_USER_TEMP)] = 1;
    job = cleaner_run_start(&l, selected, &ops, NULL, NULL);
    CHECK(job != NULL);
    if (job != NULL) {
        cleaner_run_finish(job, &r);
        CHECK(f->empty_calls == 1 && r.recycle_requested && !r.recycle_emptied);
        CHECK(r.recycle_code == 0x80004005u && r.recycle_bytes == 0);
        CHECK(!exists(L"temp\\old2.txt")); /* 나머지는 계속 */
        cleaner_run_result_free(&r);
    }

    /* 휴지통을 고르지 않으면 비우지 않는다 */
    for (i = 0; i < l.len; i++) {
        selected[i] = 0;
    }
    selected[index_of(&l, CLEANER_WER)] = 1;
    f->empty_calls = 0;
    job = cleaner_run_start(&l, selected, &ops, NULL, NULL);
    if (job != NULL) {
        cleaner_run_finish(job, &r);
        CHECK(f->empty_calls == 0 && !r.recycle_requested);
        cleaner_run_result_free(&r);
    }
    free(selected);
    cleaner_list_free(&l);
}

static int cancel_now(void *ctx)
{
    (void)ctx;
    return 1;
}

static void test_cancel(Fake *f)
{
    CleanerList l;
    CleanerOps ops = make_ops(f);
    size_t i;

    cleaner_list_init(&l);
    CHECK(cleaner_analyze(&l, NULL, &ops, cancel_now, NULL, NULL) == 0);
    CHECK(l.cancelled);
    for (i = 0; i < l.len; i++) {
        CHECK(!cleaner_target_runnable(&l.items[i]) ||
              l.items[i].kind == CLEANER_RECYCLE_BIN);
    }
    cleaner_list_free(&l);
}

int main(void)
{
    wchar_t tmp[MAX_PATH];
    CleanerList list;
    Fake fake;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_root, MAX_PATH, L"%lsbytesieve_cleaner_%lu", tmp, GetCurrentProcessId());
    remove_tree(g_root);
    CreateDirectoryW(g_root, NULL);
    build_fixture();

    memset(&fake, 0, sizeof(fake));
    fake.running = L"firefox.exe";
    test_names();
    test_selection_policy();
    cleaner_list_init(&list);
    test_analyze(&list, &fake);
    test_run(&list, &fake);
    cleaner_list_free(&list);
    test_recycle_failure(&fake);
    test_cancel(&fake);

    remove_tree(g_root);
    CoUninitialize();
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("cleaner: all checks passed\n");
    return 0;
}
