#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "devenv_history.h"
#include "vdisk.h"

/*
 * 개발 환경 분석 검사.
 *   vdisk  : 가짜 WSL 레지스트리 키(HKCU\Software\ByteSieveTest\vdisk_<pid>, 끝나면 지움)와 LOCALAPPDATA 픽스처로
 *            배포판의 .vhdx 찾기(\\?\ BasePath, VhdFileName 없음 → ext4.vhdx), 파일 없는 WSL1 배포판 제외,
 *            Docker 기본 위치, WSL 목록과 같은 파일은 Docker 로 한 번만, 줄이는 명령 안내.
 *   devhist: 없는 파일 = 빈 기록, 저장·다시 읽기, 손상된 줄 건너뛰기, (id, 경로)마다 DEVHIST_KEEP 개, 최근 기록,
 *            저장 실패 시 이전 파일 유지.
 *   growth : cleaner_apply_growth — 직전 기록과의 차이, 첫 기록, 추적하지 않는 대상, record 0/1.
 */

static int g_failures = 0;
static wchar_t g_root[MAX_PATH];
static wchar_t g_key[128];

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

static void make_file(const wchar_t *rel, DWORD bytes)
{
    static char data[4096];
    wchar_t p[MAX_PATH], *s;
    HANDLE h;
    DWORD w;

    fx(p, rel);
    for (s = p + wcslen(g_root) + 1; *s != L'\0'; s++) {
        if (*s == L'\\') {
            *s = L'\0';
            CreateDirectoryW(p, NULL);
            *s = L'\\';
        }
    }
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, data, bytes, &w, NULL);
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
                DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static void add_distro(const wchar_t *guid, const wchar_t *name, const wchar_t *base)
{
    wchar_t path[256];
    HKEY k;

    swprintf(path, 256, L"%ls\\%ls", g_key, guid);
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) ==
          ERROR_SUCCESS);
    RegSetValueExW(k, L"DistributionName", 0, REG_SZ, (const BYTE *)name,
                   (DWORD)((wcslen(name) + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"BasePath", 0, REG_SZ, (const BYTE *)base,
                   (DWORD)((wcslen(base) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

static const Vdisk *find_disk(const VdiskList *l, const wchar_t *needle)
{
    size_t i;
    for (i = 0; i < l->len; i++) {
        if (wcsstr(l->items[i].path, needle) != NULL) {
            return &l->items[i];
        }
    }
    return NULL;
}

static void test_vdisk(void)
{
    VdiskList l;
    const Vdisk *v;
    wchar_t base[MAX_PATH + 8], plain[MAX_PATH], hint[1200];

    make_file(L"wsl\\ubuntu\\ext4.vhdx", 1000);
    make_file(L"local\\Docker\\wsl\\data\\ext4.vhdx", 2000);
    make_file(L"local\\Docker\\wsl\\disk\\docker_data.vhdx", 3000);
    fx(plain, L"wsl\\ubuntu");
    swprintf(base, MAX_PATH + 8, L"\\\\?\\%ls", plain);
    add_distro(L"{11111111-0000-0000-0000-000000000001}", L"Ubuntu", base);
    fx(plain, L"wsl\\legacy"); /* WSL1: 파일 없음 */
    add_distro(L"{11111111-0000-0000-0000-000000000002}", L"Legacy", plain);
    fx(plain, L"local\\Docker\\wsl\\data");
    add_distro(L"{11111111-0000-0000-0000-000000000003}", L"docker-desktop-data", plain);

    vdisk_list_init(&l);
    CHECK(vdisk_find(&l) == 0);
    CHECK(l.len == 3);
    v = find_disk(&l, L"ubuntu\\ext4.vhdx");
    CHECK(v != NULL && v->kind == VDISK_WSL && wcscmp(v->name, L"Ubuntu") == 0 &&
          v->size == 1000 && v->modified != 0 && wcsncmp(v->path, L"\\\\?\\", 4) != 0);
    v = find_disk(&l, L"wsl\\data\\ext4.vhdx");
    CHECK(v != NULL && v->kind == VDISK_DOCKER && wcscmp(v->name, L"Docker Desktop (data)") == 0 &&
          v->size == 2000);
    v = find_disk(&l, L"docker_data.vhdx");
    CHECK(v != NULL && v->kind == VDISK_DOCKER && v->size == 3000 && v->alloc >= 3000);
    CHECK(find_disk(&l, L"legacy") == NULL);

    v = find_disk(&l, L"ubuntu\\ext4.vhdx");
    if (v != NULL) {
        vdisk_hint(v, hint, 1200);
        CHECK(wcsstr(hint, L"wsl --shutdown") != NULL && wcsstr(hint, v->path) != NULL);
        CHECK(wcsstr(hint, L"--set-sparse") != NULL && wcsstr(hint, L"docker") == NULL);
    }
    v = find_disk(&l, L"docker_data.vhdx");
    if (v != NULL) {
        vdisk_hint(v, hint, 1200);
        CHECK(wcsstr(hint, L"docker system prune") != NULL && wcsstr(hint, L"compact vdisk") != NULL);
    }
    CHECK(wcscmp(vdisk_kind_name(VDISK_WSL), L"wsl") == 0 &&
          wcscmp(vdisk_kind_name(VDISK_DOCKER), L"docker") == 0);
    vdisk_list_free(&l);

    /* 키가 없으면 WSL 은 없음(Docker 는 그대로) */
    SetEnvironmentVariableW(L"BYTESIEVE_WSL_KEY", L"Software\\ByteSieveTest\\none-here");
    CHECK(vdisk_find(&l) == 0 && l.len == 2 && find_disk(&l, L"ubuntu") == NULL);
    vdisk_list_free(&l);
    SetEnvironmentVariableW(L"BYTESIEVE_WSL_KEY", g_key);
}

static void test_history(void)
{
    DevHistory h;
    const DevRecord *r;
    wchar_t file[MAX_PATH], bad[MAX_PATH];
    unsigned long e = 0;
    int i;

    fx(file, L"hist\\devenv-history.tsv");
    make_file(L"hist\\placeholder", 1);
    devhist_init(&h);
    CHECK(devhist_load(&h, file) == 0 && h.len == 0); /* 없음 = 빈 기록 */
    CHECK(devhist_add(&h, 100, L"npm", L"C:\\a\\npm-cache", 10) == 0);
    CHECK(devhist_add(&h, 200, L"npm", L"C:\\a\\npm-cache", 30) == 0);
    CHECK(devhist_add(&h, 150, L"pip", L"C:\\a\\pip", 5) == 0);
    r = devhist_latest(&h, L"NPM", L"c:\\A\\NPM-CACHE");
    CHECK(r != NULL && r->time == 200 && r->bytes == 30);
    CHECK(devhist_latest(&h, L"npm", L"C:\\other") == NULL);
    CHECK(devhist_save(&h, file, &e) == 0 && e == 0);
    devhist_free(&h);
    CHECK(devhist_load(&h, file) == 0 && h.len == 3 && h.skipped == 0);
    r = devhist_latest(&h, L"pip", L"C:\\a\\pip");
    CHECK(r != NULL && r->bytes == 5 && r->time == 150);

    /* 보관 개수: 같은 키를 DEVHIST_KEEP 개 넘게 더하면 가장 오래된 것부터 빠진다 */
    for (i = 0; i < DEVHIST_KEEP + 5; i++) {
        CHECK(devhist_add(&h, 1000 + (uint64_t)i, L"go_mod", L"C:\\g", (uint64_t)i) == 0);
    }
    {
        size_t n = 0, k;
        uint64_t oldest = UINT64_MAX;
        for (k = 0; k < h.len; k++) {
            if (wcscmp(h.items[k].id, L"go_mod") == 0) {
                n++;
                oldest = h.items[k].time < oldest ? h.items[k].time : oldest;
            }
        }
        CHECK(n == DEVHIST_KEEP && oldest == 1005);
    }
    devhist_free(&h);

    /* 손상된 줄은 건너뛰고 나머지는 읽는다 */
    {
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        const char *text = "# bytesieve devenv history v1\n"
                           "garbage line\n"
                           "300\tnpm\t77\tC:\\a\\npm-cache\r\n"
                           "abc\tnpm\t1\tC:\\x\n"
                           "400\tnpm\tnot-a-number\tC:\\x\n"
                           "500\t\t3\tC:\\y\n";
        DWORD w;
        CHECK(f != INVALID_HANDLE_VALUE);
        WriteFile(f, text, (DWORD)strlen(text), &w, NULL);
        CloseHandle(f);
    }
    CHECK(devhist_load(&h, file) == 0 && h.len == 1 && h.skipped == 4);
    r = devhist_latest(&h, L"npm", L"C:\\a\\npm-cache");
    CHECK(r != NULL && r->bytes == 77);

    /* 저장 실패(없는 폴더)는 -1, 기존 파일은 그대로 */
    fx(bad, L"no-such-dir\\x\\devenv-history.tsv");
    CHECK(devhist_save(&h, bad, &e) == -1 && e != 0);
    devhist_free(&h);
    CHECK(devhist_load(&h, file) == 0 && h.len == 1);
    devhist_free(&h);
}

static CleanerTarget *add_target(CleanerList *l, CleanerKind kind, const wchar_t *loc)
{
    CleanerTarget *t;
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        CleanerTarget *grown = realloc(l->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        l->items = grown;
        l->cap = cap;
    }
    t = &l->items[l->len++];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->location = _wcsdup(loc);
    return t;
}

static void test_growth(void)
{
    CleanerList l;
    DevHistory h;
    CleanerTarget *npm, *disk, *temp, *missing;

    cleaner_list_init(&l);
    devhist_init(&h);
    npm = add_target(&l, CLEANER_DEV_CACHE, L"C:\\a\\npm-cache");
    npm->dev_kind = DEV_CACHE_NPM;
    npm->dev_status = DEV_CACHE_FOUND;
    npm->status = CLEANER_READY;
    npm->bytes = 90;
    npm->denied_bytes = 10; /* 추적 용량은 둘의 합 */
    disk = add_target(&l, CLEANER_VDISK, L"C:\\w\\ext4.vhdx");
    disk->status = CLEANER_INFO_ONLY;
    disk->bytes = 50;
    temp = add_target(&l, CLEANER_USER_TEMP, L"C:\\t");
    temp->status = CLEANER_READY;
    temp->bytes = 7;
    missing = add_target(&l, CLEANER_DEV_CACHE, L"C:\\p");
    missing->dev_kind = DEV_CACHE_PIP;
    missing->dev_status = DEV_CACHE_UNRECOGNIZED;
    missing->status = CLEANER_DENIED;

    CHECK(cleaner_target_tracked(npm) && cleaner_target_tracked_bytes(npm) == 100);
    CHECK(cleaner_target_tracked(disk) && cleaner_target_tracked_bytes(disk) == 50);
    CHECK(!cleaner_target_tracked(temp) && !cleaner_target_tracked(missing));

    CHECK(devhist_add(&h, 1000, L"npm", L"c:\\A\\npm-cache", 40) == 0);
    /* 비교만(record 0) */
    CHECK(cleaner_apply_growth(&l, &h, 5000, 0) == 0 && h.len == 1);
    CHECK(npm->has_growth && npm->growth == 60 && npm->growth_since == 1000);
    CHECK(!disk->has_growth && !temp->has_growth);
    /* 기록(record 1): 추적 대상 둘 */
    CHECK(cleaner_apply_growth(&l, &h, 6000, 1) == 2 && h.len == 3);
    CHECK(devhist_latest(&h, L"vdisk", L"C:\\w\\ext4.vhdx")->bytes == 50);
    /* 다음 비교는 방금 기록과 */
    npm->bytes = 70;
    CHECK(cleaner_apply_growth(&l, &h, 7000, 0) == 0);
    CHECK(npm->growth == -20 && npm->growth_since == 6000 && disk->has_growth && disk->growth == 0);
    devhist_free(&h);
    cleaner_list_free(&l);
}

int main(void)
{
    wchar_t tmp[MAX_PATH], local[MAX_PATH];

    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_root, MAX_PATH, L"%lsbytesieve_devenv_%lu", tmp, GetCurrentProcessId());
    swprintf(g_key, 128, L"Software\\ByteSieveTest\\vdisk_%lu", GetCurrentProcessId());
    remove_tree(g_root);
    CreateDirectoryW(g_root, NULL);
    fx(local, L"local");
    SetEnvironmentVariableW(L"LOCALAPPDATA", local);
    SetEnvironmentVariableW(L"BYTESIEVE_WSL_KEY", g_key);

    test_vdisk();
    test_history();
    test_growth();

    RegDeleteTreeW(HKEY_CURRENT_USER, g_key);
    remove_tree(g_root);
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("devenv: all checks passed\n");
    return 0;
}
