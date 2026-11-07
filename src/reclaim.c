#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include "path.h"
#include "reclaim.h"

/* GetFileInformationByName 의 FileStatByNameInfo(FILE_INFO_BY_NAME_CLASS 의 0). 최신 SDK 가 없어도
 * 빌드되도록 값과 함수 형식을 직접 둔다. 결과는 FILE_STAT_INFORMATION 이다. */
#define RECLAIM_FILE_STAT_BY_NAME 0
/* 클라우드 파일을 열 때 내용을 불러오지 않게 한다(winbase.h 의 FILE_FLAG_OPEN_NO_RECALL). */
#ifndef FILE_FLAG_OPEN_NO_RECALL
#define FILE_FLAG_OPEN_NO_RECALL 0x00100000
#endif

typedef BOOL(WINAPI *ByNameFn)(PCWSTR name, int cls, PVOID buffer, ULONG size);

/* 스레드마다 마지막으로 본 볼륨의 클러스터 크기(같은 폴더의 파일을 연달아 조회하므로). */
static __declspec(thread) wchar_t t_volume[MAX_PATH + 1];
static __declspec(thread) uint32_t t_cluster;

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static ByNameFn g_by_name;
static volatile LONG g_force_handle;

static BOOL CALLBACK load_by_name(PINIT_ONCE once, PVOID param, PVOID *context)
{
    HMODULE k = GetModuleHandleW(L"kernel32.dll");

    (void)once;
    (void)param;
    (void)context;
    if (k != NULL) {
        g_by_name = (ByNameFn)(void (*)(void))GetProcAddress(k, "GetFileInformationByName");
    }
    return TRUE;
}

static ByNameFn by_name(void)
{
    InitOnceExecuteOnce(&g_once, load_by_name, NULL, NULL);
    return InterlockedCompareExchange(&g_force_handle, 0, 0) ? NULL : g_by_name;
}

void reclaim_force_handle_query(int on)
{
    InterlockedExchange(&g_force_handle, on ? 1 : 0);
}

int reclaim_by_name_available(void)
{
    InitOnceExecuteOnce(&g_once, load_by_name, NULL, NULL);
    return g_by_name != NULL;
}

void reclaim_sum_add(ReclaimSum *to, const ReclaimSum *from)
{
    to->bytes += from->bytes;
    to->shared_bytes += from->shared_bytes;
    to->shared_files += from->shared_files;
    to->unknown_files += from->unknown_files;
    to->files += from->files;
}

static uint64_t li64(LARGE_INTEGER v)
{
    return v.QuadPart > 0 ? (uint64_t)v.QuadPart : 0;
}

/* 압축·스파스 파일은 할당 크기 대신 실제 점유량을 쓴다. */
static uint64_t physical_alloc(const wchar_t *path, uint32_t attributes, uint64_t alloc)
{
    DWORD high = 0, low;

    if (!(attributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))) {
        return alloc;
    }
    SetLastError(NO_ERROR);
    low = GetCompressedFileSizeW(path, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        return alloc;
    }
    return ((uint64_t)high << 32) | low;
}

/* path 가 있는 볼륨의 클러스터 크기(모르면 0). */
static uint32_t cluster_of(const wchar_t *path)
{
    wchar_t volume[MAX_PATH + 1];
    DWORD spc = 0, bps = 0, free_clusters = 0, total = 0;

    if (!GetVolumePathNameW(path, volume, MAX_PATH + 1)) {
        return 0;
    }
    if (t_cluster != 0 && _wcsicmp(volume, t_volume) == 0) {
        return t_cluster;
    }
    if (!GetDiskFreeSpaceW(volume, &spc, &bps, &free_clusters, &total) || spc == 0 || bps == 0) {
        return 0;
    }
    wcscpy_s(t_volume, MAX_PATH + 1, volume);
    t_cluster = spc * bps;
    return t_cluster;
}

/*
 * 아주 작은 파일은 NTFS 가 MFT 레코드 안에 저장한다(할당 크기가 클러스터 배수가 아닌 8바이트 단위
 * 값으로 나온다). 지워도 MFT 레코드만 비고 드라이브 여유 공간은 늘지 않으므로 0 으로 센다.
 */
static uint64_t freeable_alloc(const wchar_t *path, uint64_t alloc)
{
    uint32_t cluster;

    if (alloc == 0) {
        return 0;
    }
    cluster = cluster_of(path);
    return cluster != 0 && alloc % cluster != 0 ? 0 : alloc;
}

static int query_by_name(ByNameFn fn, const wchar_t *path, ReclaimFile *out, uint32_t *attributes)
{
    FILE_STAT_INFORMATION st;

    memset(&st, 0, sizeof(st));
    if (!fn(path, RECLAIM_FILE_STAT_BY_NAME, &st, (ULONG)sizeof(st))) {
        return 0;
    }
    out->alloc = li64(st.AllocationSize);
    out->file_id = (uint64_t)st.FileId.QuadPart;
    out->links = st.NumberOfLinks;
    *attributes = st.FileAttributes;
    return 1;
}

static int query_by_handle(const wchar_t *path, ReclaimFile *out, uint32_t *attributes)
{
    HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
                               FILE_FLAG_OPEN_NO_RECALL,
                           NULL);
    FILE_STANDARD_INFO std;
    BY_HANDLE_FILE_INFORMATION info;
    int ok;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    memset(&std, 0, sizeof(std));
    memset(&info, 0, sizeof(info));
    ok = GetFileInformationByHandleEx(h, FileStandardInfo, &std, (DWORD)sizeof(std)) &&
         GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    if (!ok) {
        return 0;
    }
    out->alloc = li64(std.AllocationSize);
    out->file_id = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    out->links = std.NumberOfLinks;
    *attributes = info.dwFileAttributes;
    return 1;
}

int reclaim_query(const wchar_t *path, uint32_t attributes, uint64_t size, uint64_t alloc_hint,
                  ReclaimFile *out)
{
    ByNameFn fn = by_name();
    wchar_t *ext = NULL;
    const wchar_t *p = path;
    uint32_t attr = attributes;
    int ok = 0;

    (void)size;
    memset(out, 0, sizeof(*out));
    out->alloc = alloc_hint;
    if (path == NULL) {
        return 0;
    }
    if (!path_is_extended(path)) {
        ext = path_to_extended(path);
        if (ext != NULL) {
            p = ext;
        }
    }
    if (fn != NULL) {
        ok = query_by_name(fn, p, out, &attr);
    }
    if (!ok) {
        ok = query_by_handle(p, out, &attr);
    }
    if (ok) {
        out->alloc = freeable_alloc(p, physical_alloc(p, attr, out->alloc));
        out->known = 1;
    } else {
        memset(out, 0, sizeof(*out));
        out->alloc = alloc_hint;
    }
    free(ext);
    return ok;
}

/* ---- 링크가 여러 개인 파일 표 ---- */

void reclaim_map_init(ReclaimMap *map)
{
    memset(map, 0, sizeof(*map));
}

void reclaim_map_free(ReclaimMap *map)
{
    free(map->slots);
    reclaim_map_init(map);
}

static size_t slot_hash(uint64_t id, uint32_t group)
{
    uint64_t h = (id ^ ((uint64_t)group << 47) ^ group) * 0x9E3779B97F4A7C15ull;
    return (size_t)(h ^ (h >> 29));
}

static ReclaimSlot *slot_find(ReclaimSlot *slots, size_t cap, uint64_t id, uint32_t group)
{
    size_t i = slot_hash(id, group) & (cap - 1);

    while (slots[i].used && (slots[i].file_id != id || slots[i].group != group)) {
        i = (i + 1) & (cap - 1);
    }
    return &slots[i];
}

static int map_grow(ReclaimMap *map)
{
    size_t cap = map->cap ? map->cap * 2 : 256, i;
    ReclaimSlot *slots;

    if (cap < map->cap) {
        return -1;
    }
    slots = calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < map->cap; i++) {
        if (map->slots[i].used) {
            *slot_find(slots, cap, map->slots[i].file_id, map->slots[i].group) = map->slots[i];
        }
    }
    free(map->slots);
    map->slots = slots;
    map->cap = cap;
    return 0;
}

void reclaim_tally(ReclaimMap *map, uint32_t group, const ReclaimFile *f, ReclaimSum *sum)
{
    ReclaimSlot *slot;

    sum->files++;
    if (!f->known) {
        sum->unknown_files++;
        sum->bytes += f->alloc;
        return;
    }
    if (f->links <= 1 || f->file_id == 0) {
        sum->bytes += f->alloc;
        return;
    }
    if ((map->len + 1) * 2 > map->cap && map_grow(map) != 0) {
        /* 표를 늘리지 못하면 링크를 확인할 수 없으므로 추정한다. */
        sum->unknown_files++;
        sum->bytes += f->alloc;
        return;
    }
    slot = slot_find(map->slots, map->cap, f->file_id, group);
    if (!slot->used) {
        slot->used = 1;
        slot->file_id = f->file_id;
        slot->group = group;
        slot->links = f->links;
        slot->alloc = f->alloc;
        map->len++;
    }
    slot->seen++;
}

void reclaim_settle(ReclaimMap *map, reclaim_apply_fn apply, void *ctx)
{
    size_t i;

    for (i = 0; i < map->cap; i++) {
        ReclaimSlot *s = &map->slots[i];
        if (s->used && apply != NULL) {
            apply(s->group, s->alloc, s->seen >= s->links, ctx);
        }
    }
    if (map->slots != NULL) {
        memset(map->slots, 0, map->cap * sizeof(*map->slots));
    }
    map->len = 0;
}

void reclaim_apply_sum(uint32_t group, uint64_t alloc, int freed, void *ctx)
{
    ReclaimSum *sum = ctx;

    (void)group;
    if (freed) {
        sum->bytes += alloc;
    } else {
        sum->shared_bytes += alloc;
        sum->shared_files++;
    }
}

/* ---- 폴더 하나 ---- */

typedef struct {
    ReclaimMap map;
    ReclaimSum *sum;
    wchar_t *cur;     /* 지금 열거 중인 폴더(확장 경로) */
    size_t cur_len;
    wchar_t *buf;     /* 파일 경로를 만드는 버퍼 */
    size_t buf_cap;
    int (*cancel)(void *ctx);
    void *ctx;
} TreeCtx;

/* dir\name 을 c->buf 에 만든다. 메모리가 모자라면 NULL. */
static const wchar_t *child_path(TreeCtx *c, const wchar_t *name)
{
    size_t n = wcslen(name), need = c->cur_len + 1 + n + 1;
    int sep = c->cur_len > 0 && !path_is_separator(c->cur[c->cur_len - 1]);

    if (c->cur == NULL) {
        return NULL;
    }
    if (need > c->buf_cap) {
        size_t cap = need * 2;
        wchar_t *grown = realloc(c->buf, cap * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        c->buf = grown;
        c->buf_cap = cap;
    }
    memcpy(c->buf, c->cur, c->cur_len * sizeof(wchar_t));
    if (sep) {
        c->buf[c->cur_len] = L'\\';
    }
    memcpy(c->buf + c->cur_len + sep, name, (n + 1) * sizeof(wchar_t));
    return c->buf;
}

static uintptr_t tree_entry(const ScanEntry *entry, uintptr_t parent, int will_descend, void *ctx)
{
    TreeCtx *c = ctx;
    ReclaimFile f;
    const wchar_t *path;
    uint64_t hint;

    (void)parent;
    if (entry->is_dir || entry->is_reparse) {
        return will_descend ? 1 : 0;
    }
    hint = entry->alloc_known ? entry->alloc_size : entry->size;
    path = child_path(c, entry->name);
    if (path == NULL) {
        memset(&f, 0, sizeof(f));
        f.alloc = hint;
    } else {
        reclaim_query(path, entry->attributes, entry->size, hint, &f);
    }
    reclaim_tally(&c->map, 0, &f, c->sum);
    return 0;
}

static void tree_dir_begin(const ScanDirInfo *dir, void *ctx)
{
    TreeCtx *c = ctx;

    free(c->cur);
    c->cur = path_dup(dir->path);
    c->cur_len = c->cur != NULL ? wcslen(c->cur) : 0;
}

static int tree_cancel(void *ctx)
{
    TreeCtx *c = ctx;

    return c->cancel != NULL && c->cancel(c->ctx);
}

ScanStatus reclaim_scan_tree(const wchar_t *dir, ScanReport *report, ReclaimSum *sum,
                             int (*cancel)(void *ctx), void *ctx)
{
    ScanHooks hooks;
    TreeCtx c;
    ScanStatus st;

    memset(&hooks, 0, sizeof(hooks));
    hooks.on_entry = tree_entry;
    hooks.on_dir_begin = tree_dir_begin;
    hooks.should_cancel = tree_cancel;
    memset(&c, 0, sizeof(c));
    reclaim_map_init(&c.map);
    c.sum = sum;
    c.cancel = cancel;
    c.ctx = ctx;
    st = scan_tree_ex(dir, SCAN_DEPTH_UNLIMITED, 0, report, &hooks, &c);
    reclaim_settle(&c.map, reclaim_apply_sum, sum);
    reclaim_map_free(&c.map);
    free(c.cur);
    free(c.buf);
    return st;
}

void reclaim_add_file(const wchar_t *path, uint32_t attributes, uint64_t size, ReclaimSum *sum)
{
    ReclaimFile f;

    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return;
    }
    reclaim_query(path, attributes, size, size, &f);
    sum->files++;
    if (!f.known) {
        sum->unknown_files++;
        sum->bytes += f.alloc;
    } else if (f.links > 1) {
        sum->shared_bytes += f.alloc;
        sum->shared_files++;
    } else {
        sum->bytes += f.alloc;
    }
}
