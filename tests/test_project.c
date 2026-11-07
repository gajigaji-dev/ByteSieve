#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleanup_scan.h"
#include "project_info.h"

/*
 * Git 기반 프로젝트 분석 검사. 임시 폴더에 git.exe 로 저장소를 만든다(git 이 없으면 Git 부분은 건너뛴다).
 *   old\   : 2020년 커밋, 원격(bare)으로 push 함, 파일도 2020년, node_modules + package-lock.json
 *   new\   : 지금 커밋, 원격 없음, 추적 안 하는 새 파일(미커밋 변경), node_modules(lockfile 없음) + dist
 *   plain\ : Git 아님, __pycache__
 * 확인: 프로젝트별 묶기, 루트·Git 여부, 마지막 커밋·수정, 미커밋 변경·미푸시·원격, 휴면, 복구 가능성,
 *       git.exe 를 쓰지 않을 때(.git 직접 읽기: 브랜치·reflog 시각·원격 ref 비교, 미커밋은 모름).
 */

static int g_failures = 0;
static wchar_t g_root[MAX_PATH];
static wchar_t g_git[MAX_PATH];

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

static void write_file(const wchar_t *rel, const char *text)
{
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD w;

    fx(p, rel);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, text, (DWORD)strlen(text), &w, NULL);
        CloseHandle(h);
    }
}

static void set_time(const wchar_t *path, FILETIME ft)
{
    HANDLE h = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &ft, NULL, &ft);
        CloseHandle(h);
    }
}

/* 폴더 아래 파일 시각을 모두 ft 로(.git 은 건드리지 않음). */
static void age_tree(const wchar_t *dir, FILETIME ft)
{
    wchar_t pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0 ||
            wcscmp(fd.cFileName, L".git") == 0) {
            continue;
        }
        swprintf(child, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            age_tree(child, ft);
        }
        set_time(child, ft);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
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
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_tree(child);
            } else {
                DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* git <args> 를 fixture 의 rel 폴더에서 실행한다. 종료 코드 0 이면 1. */
static int git(const wchar_t *rel, const wchar_t *args)
{
    wchar_t cmd[2048], dir[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    fx(dir, rel);
    swprintf(cmd, 2048, L"\"%ls\" -c user.name=t -c user.email=t@example.com -C \"%ls\" %ls", g_git,
             dir, args);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

static int build_repos(void)
{
    FILETIME old;
    SYSTEMTIME st;
    wchar_t p[MAX_PATH];
    int ok = 1;

    /* old: 2020-01-15 커밋, bare 원격으로 push */
    make_dirs(L"remote.git");
    ok &= git(L"remote.git", L"init --bare -q -b main");
    make_dirs(L"work\\old\\node_modules\\dep");
    write_file(L"work\\old\\package.json", "{}");
    write_file(L"work\\old\\package-lock.json", "{}");
    write_file(L"work\\old\\index.js", "x");
    write_file(L"work\\old\\node_modules\\dep\\i.js", "y");
    write_file(L"work\\old\\.gitignore", "node_modules/\n");
    ok &= git(L"work\\old", L"init -q -b main");
    ok &= git(L"work\\old", L"add .");
    SetEnvironmentVariableW(L"GIT_AUTHOR_DATE", L"2020-01-15T12:00:00Z");
    SetEnvironmentVariableW(L"GIT_COMMITTER_DATE", L"2020-01-15T12:00:00Z");
    ok &= git(L"work\\old", L"commit -q -m first");
    SetEnvironmentVariableW(L"GIT_AUTHOR_DATE", NULL);
    SetEnvironmentVariableW(L"GIT_COMMITTER_DATE", NULL);
    fx(p, L"remote.git");
    {
        wchar_t args[MAX_PATH + 32];
        swprintf(args, MAX_PATH + 32, L"remote add origin \"%ls\"", p);
        ok &= git(L"work\\old", args);
    }
    ok &= git(L"work\\old", L"push -q origin main");
    memset(&st, 0, sizeof(st));
    st.wYear = 2020;
    st.wMonth = 1;
    st.wDay = 15;
    st.wHour = 12;
    SystemTimeToFileTime(&st, &old);
    fx(p, L"work\\old");
    age_tree(p, old);

    /* new: 지금 커밋, 원격 없음, 추적 안 하는 파일 */
    make_dirs(L"work\\new\\node_modules\\x");
    make_dirs(L"work\\new\\dist");
    write_file(L"work\\new\\package.json", "{}");
    write_file(L"work\\new\\main.js", "1");
    write_file(L"work\\new\\.gitignore", "node_modules/\ndist/\n");
    write_file(L"work\\new\\node_modules\\x\\a.js", "a");
    write_file(L"work\\new\\dist\\b.js", "b");
    ok &= git(L"work\\new", L"init -q -b main");
    ok &= git(L"work\\new", L"add .");
    ok &= git(L"work\\new", L"commit -q -m init");
    write_file(L"work\\new\\draft.txt", "not committed");

    /* plain: Git 아님 */
    make_dirs(L"work\\plain\\__pycache__");
    write_file(L"work\\plain\\__pycache__\\m.pyc", "p");
    write_file(L"work\\plain\\m.py", "print(1)");
    return ok;
}

static const ProjectInfo *project_of(const ProjectList *pl, const CleanupResult *r,
                                     const wchar_t *needle)
{
    size_t i;
    for (i = 0; i < r->len; i++) {
        if (wcsstr(r->items[i].path, needle) != NULL) {
            return &pl->items[pl->item_project[i]];
        }
    }
    return NULL;
}

static ProjectRecovery recovery_of(const ProjectList *pl, const CleanupResult *r,
                                   const wchar_t *needle)
{
    size_t i;
    for (i = 0; i < r->len; i++) {
        if (wcsstr(r->items[i].path, needle) != NULL) {
            return pl->item_recovery[i];
        }
    }
    return PROJECT_RECOVER_COUNT;
}

static void test_analyze(int with_git)
{
    CleanupResult r;
    ProjectList pl;
    ProjectOptions o;
    const ProjectInfo *old, *nw, *plain;
    wchar_t p[MAX_PATH];

    cleanup_result_init(&r);
    project_list_init(&pl);
    fx(p, L"work");
    CHECK(cleanup_scan(&r, p, NULL, NULL, NULL, NULL) == SCAN_COMPLETE);
    CHECK(r.len == 4); /* old/node_modules, new/node_modules, new/dist, plain/__pycache__ */
    memset(&o, 0, sizeof(o));
    o.no_git = !with_git;
    CHECK(project_analyze(&r, &o, &pl, NULL, NULL, NULL) == 0);
    CHECK(pl.len == 3 && pl.item_count == r.len && pl.dormant_days == 90);
    CHECK(pl.git_available == with_git);

    old = project_of(&pl, &r, L"\\old\\");
    nw = project_of(&pl, &r, L"\\new\\");
    plain = project_of(&pl, &r, L"\\plain\\");
    CHECK(old != NULL && nw != NULL && plain != NULL && old != nw && nw != plain);
    if (old == NULL || nw == NULL || plain == NULL) {
        goto done;
    }
    /* 같은 프로젝트의 후보는 한 프로젝트로 */
    CHECK(nw->candidates == 2 && old->candidates == 1 && plain->candidates == 1);
    CHECK(wcsstr(old->root, L"work\\old") != NULL && old->is_git);
    CHECK(!plain->is_git && plain->git_source == PROJECT_GIT_NONE && plain->changes == -1);
    CHECK(wcsstr(plain->root, L"work\\plain") != NULL);
    CHECK(old->branch != NULL && wcscmp(old->branch, L"main") == 0);
    CHECK(old->git_source == (with_git ? PROJECT_GIT_EXE : PROJECT_GIT_DIRECT));

    /* old: 2020년 → 휴면, push 했으므로 미푸시 0, 원격 있음 */
    CHECK(old->last_commit != 0 && old->last_commit == project_unix_to_filetime(1579089600));
    CHECK(old->has_remote == 1 && old->unpushed == 0);
    CHECK(old->dormant && old->age_days > 365);
    CHECK(old->last_modified != 0 && old->last_modified <= old->last_commit + 864000000000ull);

    /* new: 지금 → 휴면 아님 */
    CHECK(!nw->dormant && nw->age_days <= 1 && nw->last_commit != 0);
    if (with_git) {
        CHECK(old->changes == 0);
        CHECK(nw->changes == 1);         /* draft.txt(추적 안 함) */
        CHECK(nw->has_remote == 0 && nw->unpushed == 1); /* 원격 없음: 커밋이 이 PC 에만 */
    } else {
        CHECK(old->changes == -1 && nw->changes == -1); /* 직접 읽기로는 모름 */
        CHECK(nw->has_remote == 0 && nw->unpushed == -1);
    }

    /* 복구 가능성 */
    CHECK(recovery_of(&pl, &r, L"old\\node_modules") == PROJECT_RECOVER_LOCKED);
    CHECK(recovery_of(&pl, &r, L"new\\node_modules") == PROJECT_RECOVER_UNLOCKED);
    CHECK(recovery_of(&pl, &r, L"new\\dist") == PROJECT_RECOVER_REBUILD);
    CHECK(recovery_of(&pl, &r, L"__pycache__") == PROJECT_RECOVER_REBUILD);

    /* 휴면 기준을 아주 길게 하면 old 도 휴면이 아니다 */
    o.dormant_days = 100000;
    CHECK(project_analyze(&r, &o, &pl, NULL, NULL, NULL) == 0);
    old = project_of(&pl, &r, L"\\old\\");
    CHECK(old != NULL && !old->dormant);
done:
    project_list_free(&pl);
    cleanup_result_free(&r);
}

static void test_names(void)
{
    int i;
    CleanupItem item;

    for (i = 0; i < PROJECT_RECOVER_COUNT; i++) {
        CHECK(project_recovery_text((ProjectRecovery)i)[0] != L'\0');
        CHECK(wcscmp(project_recovery_name((ProjectRecovery)i), L"unknown") != 0);
    }
    for (i = 0; i < PROJECT_GIT_SOURCE_COUNT; i++) {
        CHECK(wcscmp(project_git_source_name((ProjectGitSource)i), L"unknown") != 0);
    }
    memset(&item, 0, sizeof(item));
    item.path = L"C:\\x\\a.log";
    item.category = CLEANUP_LOG;
    CHECK(project_item_recovery(&item) == PROJECT_RECOVER_NONE);
    item.category = CLEANUP_TEMP;
    CHECK(project_item_recovery(&item) == PROJECT_RECOVER_NONE);
    CHECK(project_unix_to_filetime(0) == 0);
    CHECK(project_unix_to_filetime(1) == 116444736010000000ull);
}

int main(void)
{
    wchar_t tmp[MAX_PATH];
    int have_git;

    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_root, MAX_PATH, L"%lsbytesieve_project_%lu", tmp, GetCurrentProcessId());
    remove_tree(g_root);
    CreateDirectoryW(g_root, NULL);
    SetEnvironmentVariableW(L"BYTESIEVE_GIT", NULL);
    test_names();
    have_git = project_find_git(g_git, MAX_PATH);
    if (!have_git) {
        printf("project: git.exe not found, Git checks skipped\n");
    } else if (!build_repos()) {
        fprintf(stderr, "project: could not build git fixture\n");
        g_failures++;
    } else {
        test_analyze(1);
        test_analyze(0);
        /* BYTESIEVE_GIT=none 도 git.exe 를 쓰지 않는다 */
        SetEnvironmentVariableW(L"BYTESIEVE_GIT", L"none");
        {
            wchar_t buf[MAX_PATH];
            CHECK(!project_find_git(buf, MAX_PATH));
        }
        SetEnvironmentVariableW(L"BYTESIEVE_GIT", NULL);
    }
    remove_tree(g_root);
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("project: all checks passed\n");
    return 0;
}
