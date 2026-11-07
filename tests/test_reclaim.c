#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleanup_scan.h"
#include "path.h"
#include "reclaim.h"

/*
 * 실제 확보 가능 용량(reclaim) 검사. 임시 폴더에 하드 링크 픽스처를 만들어
 *   - 링크가 하나뿐인 파일은 할당 크기만큼 확보 가능
 *   - 같은 실제 파일의 링크가 모두 대상 안이면 한 번만 확보 가능
 *   - 대상 밖에 링크가 남으면 확보 가능이 아니라 공유(shared)
 *   - 조회하지 못한 파일은 추정(unknown)
 * 을 GetFileInformationByName 경로와 파일을 여는 대체 경로 모두에서 확인한다. 정리 후보 탐지
 * (cleanup_scan)가 항목마다 같은 판정을 하는지도 본다(pnpm store 와 링크를 나누는 node_modules).
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

#define CHECK_U64(actual, expected)                                                 \
    do {                                                                            \
        unsigned long long a_ = (actual), e_ = (expected);                          \
        if (a_ != e_) {                                                             \
            fprintf(stderr, "%s:%d: %s: expected %llu, got %llu\n", __FILE__,       \
                    __LINE__, #actual, e_, a_);                                     \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static void fx(wchar_t *out, const wchar_t *rel)
{
    swprintf(out, MAX_PATH, L"%ls\\%ls", g_root, rel);
}

static void make_dir(const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    fx(p, rel);
    CreateDirectoryW(p, NULL);
}

/* 크기가 bytes 인 파일(MFT 에 상주하지 않도록 충분히 크게 쓴다). */
static void write_file(const wchar_t *rel, DWORD bytes)
{
    static char data[65536];
    wchar_t p[MAX_PATH];
    HANDLE h;
    DWORD written = 0;

    fx(p, rel);
    h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(WriteFile(h, data, bytes, &written, NULL) && written == bytes);
        CloseHandle(h);
    }
}

static int hard_link(const wchar_t *link_rel, const wchar_t *target_rel)
{
    wchar_t l[MAX_PATH], t[MAX_PATH];
    fx(l, link_rel);
    fx(t, target_rel);
    return CreateHardLinkW(l, t, NULL) != 0;
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

static uint64_t alloc_of(const wchar_t *rel)
{
    wchar_t p[MAX_PATH];
    ReclaimFile f;

    fx(p, rel);
    CHECK(reclaim_query(p, 0, 0, 0, &f));
    return f.alloc;
}

/* 픽스처:
 *   solo\a.bin                      40000  링크 하나
 *   both\x.bin, both\y.bin          30000  서로 링크(둘 다 대상 안)
 *   shared\s.bin  ↔ store\s.bin     20000  대상 밖(store)에 링크가 남음
 *   proj\package.json, proj\node_modules\dep\i.js ↔ store\i.js   (pnpm 처럼 store 와 공유)
 *   proj\node_modules\dep\own.js    10000  링크 하나
 */
static int build_fixture(void)
{
    make_dir(L"solo");
    write_file(L"solo\\a.bin", 40000);
    make_dir(L"both");
    write_file(L"both\\x.bin", 30000);
    make_dir(L"shared");
    make_dir(L"store");
    write_file(L"store\\s.bin", 20000);
    write_file(L"store\\i.js", 15000);
    make_dir(L"proj");
    write_file(L"proj\\package.json", 10);
    make_dir(L"proj\\node_modules");
    make_dir(L"proj\\node_modules\\dep");
    write_file(L"proj\\node_modules\\dep\\own.js", 10000);
    return hard_link(L"both\\y.bin", L"both\\x.bin") && hard_link(L"shared\\s.bin", L"store\\s.bin") &&
           hard_link(L"proj\\node_modules\\dep\\i.js", L"store\\i.js");
}

static ReclaimSum measure(const wchar_t *rel, ScanStatus *status)
{
    wchar_t p[MAX_PATH];
    ScanReport report;
    ReclaimSum sum;

    fx(p, rel);
    memset(&sum, 0, sizeof(sum));
    scan_report_init(&report);
    *status = reclaim_scan_tree(p, &report, &sum, NULL, NULL);
    scan_report_free(&report);
    return sum;
}

static void test_query(void)
{
    wchar_t p[MAX_PATH], ext[MAX_PATH + 8];
    ReclaimFile f, g;

    fx(p, L"solo\\a.bin");
    CHECK(reclaim_query(p, 0, 40000, 0, &f));
    CHECK(f.known && f.links == 1 && f.file_id != 0);
    CHECK(f.alloc >= 40000); /* 클러스터 단위로 올림 */

    /* 확장 경로도 같은 결과 */
    swprintf(ext, MAX_PATH + 8, L"\\\\?\\%ls", p);
    CHECK(reclaim_query(ext, 0, 40000, 0, &g));
    CHECK(g.alloc == f.alloc && g.file_id == f.file_id && g.links == 1);

    fx(p, L"both\\x.bin");
    CHECK(reclaim_query(p, 0, 0, 0, &f));
    fx(p, L"both\\y.bin");
    CHECK(reclaim_query(p, 0, 0, 0, &g));
    CHECK(f.links == 2 && g.links == 2 && f.file_id == g.file_id);

    /* 없는 파일: 모름, alloc 은 힌트 */
    fx(p, L"solo\\missing.bin");
    CHECK(!reclaim_query(p, 0, 123, 4096, &f));
    CHECK(!f.known && f.alloc == 4096 && f.links == 0);
}

static void test_tree(void)
{
    ScanStatus st;
    ReclaimSum s;
    uint64_t a = alloc_of(L"solo\\a.bin"), x = alloc_of(L"both\\x.bin"),
             sh = alloc_of(L"store\\s.bin");

    s = measure(L"solo", &st);
    CHECK(st == SCAN_COMPLETE);
    CHECK_U64(s.bytes, a);
    CHECK_U64(s.shared_bytes, 0);
    CHECK_U64(s.files, 1);
    CHECK_U64(s.unknown_files, 0);

    /* 링크 둘이 모두 대상 안: 한 번만 센다 */
    s = measure(L"both", &st);
    CHECK_U64(s.bytes, x);
    CHECK_U64(s.shared_bytes, 0);
    CHECK_U64(s.files, 2);

    /* 대상 밖(store)에 링크가 남음: 확보되지 않음 */
    s = measure(L"shared", &st);
    CHECK_U64(s.bytes, 0);
    CHECK_U64(s.shared_bytes, sh);
    CHECK_U64(s.shared_files, 1);

    /* 둘을 함께 담은 상위 폴더면 모든 링크를 보므로 확보 가능 */
    {
        wchar_t p[MAX_PATH];
        ScanReport report;
        ReclaimSum all;
        memset(&all, 0, sizeof(all));
        scan_report_init(&report);
        fx(p, L"");
        p[wcslen(p) - 1] = L'\0';
        CHECK(reclaim_scan_tree(p, &report, &all, NULL, NULL) == SCAN_COMPLETE);
        scan_report_free(&report);
        CHECK_U64(all.shared_bytes, 0);
        CHECK(all.bytes >= a + x + sh);
    }

    /* 파일 하나짜리 항목 */
    {
        wchar_t p[MAX_PATH];
        ReclaimSum one;
        memset(&one, 0, sizeof(one));
        fx(p, L"shared\\s.bin");
        reclaim_add_file(p, 0, 20000, &one);
        CHECK_U64(one.bytes, 0);
        CHECK_U64(one.shared_bytes, sh);
        memset(&one, 0, sizeof(one));
        fx(p, L"solo\\a.bin");
        reclaim_add_file(p, 0, 40000, &one);
        CHECK_U64(one.bytes, a);
        /* 링크(재분석 지점) 파일은 세지 않는다 */
        memset(&one, 0, sizeof(one));
        reclaim_add_file(p, FILE_ATTRIBUTE_REPARSE_POINT, 40000, &one);
        CHECK_U64(one.files, 0);
    }
}

/* 표 자체: 그룹이 다르면 같은 실제 파일도 따로 판정하고, 많이 넣어도 정산이 맞는다. */
static void test_map(void)
{
    ReclaimMap map;
    ReclaimSum sum, groups[2];
    ReclaimFile f;
    uint32_t i;

    reclaim_map_init(&map);
    memset(&f, 0, sizeof(f));
    f.known = 1;
    f.links = 2;
    f.file_id = 77;
    f.alloc = 4096;
    memset(groups, 0, sizeof(groups));
    reclaim_tally(&map, 0, &f, &groups[0]);
    reclaim_tally(&map, 1, &f, &groups[1]);
    memset(&sum, 0, sizeof(sum));
    reclaim_settle(&map, reclaim_apply_sum, &sum);
    /* 그룹마다 링크 하나씩만 봤으므로 둘 다 공유 */
    CHECK_U64(sum.bytes, 0);
    CHECK_U64(sum.shared_bytes, 8192);
    CHECK_U64(sum.shared_files, 2);
    CHECK(map.len == 0);

    /* 링크 3개 중 3개를 한 그룹에서 봄 → 확보 가능, 1000개 넣어 표가 늘어나도 맞음 */
    memset(&sum, 0, sizeof(sum));
    for (i = 0; i < 1000; i++) {
        int k;
        f.file_id = 1000 + i;
        f.links = 3;
        f.alloc = 10;
        for (k = 0; k < 3; k++) {
            reclaim_tally(&map, 5, &f, &sum);
        }
    }
    CHECK(map.len == 1000 && map.cap >= 2000);
    reclaim_settle(&map, reclaim_apply_sum, &sum);
    CHECK_U64(sum.bytes, 10000);
    CHECK_U64(sum.shared_bytes, 0);
    CHECK_U64(sum.files, 3000);

    /* 모르는 파일은 추정, 링크 하나는 바로 더함 */
    memset(&sum, 0, sizeof(sum));
    f.known = 0;
    f.alloc = 7;
    reclaim_tally(&map, 0, &f, &sum);
    f.known = 1;
    f.links = 1;
    f.alloc = 5;
    reclaim_tally(&map, 0, &f, &sum);
    CHECK_U64(sum.bytes, 12);
    CHECK_U64(sum.unknown_files, 1);
    CHECK(map.len == 0);
    reclaim_map_free(&map);
}

/* 정리 후보 탐지: node_modules 항목은 store 와 나누는 i.js 를 공유로, own.js 만 확보 가능으로 센다. */
static void test_cleanup_scan(void)
{
    CleanupResult r;
    wchar_t p[MAX_PATH];
    size_t i, found = 0;
    uint64_t own = alloc_of(L"proj\\node_modules\\dep\\own.js"),
             shared = alloc_of(L"store\\i.js");

    cleanup_result_init(&r);
    fx(p, L"proj");
    CHECK(cleanup_scan(&r, p, NULL, NULL, NULL, NULL) == SCAN_COMPLETE);
    for (i = 0; i < r.len; i++) {
        const CleanupItem *it = &r.items[i];
        if (wcsstr(it->path, L"node_modules") == NULL) {
            continue;
        }
        found++;
        CHECK(it->flags & CLEANUP_ITEM_MEASURED);
        CHECK_U64(it->bytes, 25000);
        CHECK_U64(it->reclaim_bytes, own);
        CHECK_U64(it->shared_bytes, shared);
        CHECK_U64(cleanup_item_reclaim(it), own);
    }
    CHECK(found == 1);
    CHECK_U64(r.total_reclaim, own);
    cleanup_result_free(&r);

    /* MEASURED 가 아니면 논리 크기로 대신한다 */
    {
        CleanupItem it;
        memset(&it, 0, sizeof(it));
        it.bytes = 99;
        it.reclaim_bytes = 1;
        CHECK_U64(cleanup_item_reclaim(&it), 99);
    }
}

int main(void)
{
    wchar_t tmp[MAX_PATH];
    int pass;

    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_root, MAX_PATH, L"%lsbytesieve_reclaim_%lu", tmp, GetCurrentProcessId());
    remove_tree(g_root);
    CreateDirectoryW(g_root, NULL);
    if (!build_fixture()) {
        /* 하드 링크를 지원하지 않는 임시 폴더(FAT 등)에서는 표 검사만 한다. */
        fprintf(stderr, "reclaim: hard links unavailable in %ls, checking table only\n", g_root);
        test_map();
    } else {
        printf("reclaim: GetFileInformationByName %s\n",
               reclaim_by_name_available() ? "available" : "unavailable");
        /* 0: 이름으로 조회(있으면), 1: 파일을 열어 조회 */
        for (pass = 0; pass < 2; pass++) {
            reclaim_force_handle_query(pass);
            test_query();
            test_tree();
            test_cleanup_scan();
        }
        reclaim_force_handle_query(0);
        test_map();
    }
    remove_tree(g_root);
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("reclaim: all checks passed\n");
    return 0;
}
