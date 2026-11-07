#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "reclaim.h"
#include "path.h"
#include "scanner.h"

/* 임시 파일·오류 보고서에서 빼는 "최근" 기준(시간). */
#define CLEANER_DEFAULT_MIN_AGE_HOURS 24
/* FILETIME 1시간(100ns 단위). */
#define FT_HOUR (36000000000ULL)
/* 폴더를 훑을 때 취소를 확인하는 항목 간격. */
#define WALK_CANCEL_INTERVAL 256

/* ---- 종류·상태 ---- */

static const CleanerKindInfo g_kinds[CLEANER_KIND_COUNT] = {
    {L"user_temp", CLEANER_GROUP_WINDOWS, L"사용자 임시 파일",
     L"프로그램이 작업 중에 만들고 지우지 않은 임시 파일(%TEMP%)입니다. 최근 24시간 안에 바뀐 "
     L"항목은 지금 쓰고 있을 수 있어 빼고, 사용 중이라 옮기지 못한 파일은 건너뜁니다.",
     0, NULL},
    {L"wer", CLEANER_GROUP_WINDOWS, L"Windows 오류 보고서",
     L"프로그램이 비정상 종료될 때 Windows가 이 계정에 남긴 보고서입니다. 정리해도 프로그램 "
     L"동작에는 영향이 없지만, 아직 보내지 않은 보고서는 보내지 않게 됩니다.",
     0, NULL},
    {L"recycle_bin", CLEANER_GROUP_RECYCLE, L"휴지통",
     L"모든 드라이브의 휴지통을 비웁니다. 휴지통의 파일은 영구 삭제되어 복원할 수 없습니다"
     L"(ByteSieve가 이전에 휴지통으로 옮긴 항목 포함). 다른 항목은 비운 뒤에 휴지통으로 옮기므로 "
     L"이번에 정리한 항목은 복원할 수 있습니다.",
     1, NULL},
    {L"chrome", CLEANER_GROUP_BROWSER, L"Chrome 캐시",
     L"웹 페이지·이미지·스크립트 캐시(Cache·Code Cache·GPUCache)입니다. 로그인·북마크·방문 기록은 "
     L"그대로이며, 다음 방문 때 다시 내려받아 처음에는 조금 느릴 수 있습니다. Chrome이 실행 중이면 "
     L"정리하지 않습니다.",
     0, L"chrome.exe"},
    {L"edge", CLEANER_GROUP_BROWSER, L"Edge 캐시",
     L"웹 페이지·이미지·스크립트 캐시(Cache·Code Cache·GPUCache)입니다. 로그인·즐겨찾기·검색 기록은 "
     L"그대로입니다. Edge는 창을 닫아도 백그라운드에서 실행될 수 있으며(시작 부스트), 실행 중이면 "
     L"정리하지 않습니다.",
     0, L"msedge.exe"},
    {L"firefox", CLEANER_GROUP_BROWSER, L"Firefox 캐시",
     L"웹 페이지 캐시(cache2)입니다. 로그인·북마크·방문 기록은 그대로입니다. Firefox가 실행 중이면 "
     L"정리하지 않습니다.",
     0, L"firefox.exe"},
    {L"dev_cache", CLEANER_GROUP_DEV, L"개발 도구 캐시",
     L"여러 프로젝트가 함께 쓰는 패키지 관리자·빌드 도구 캐시입니다. 정리하면 다음 빌드·설치 때 "
     L"다시 내려받습니다.",
     0, NULL},
    {L"vdisk", CLEANER_GROUP_VDISK, L"가상 디스크",
     L"WSL 배포판·Docker Desktop의 가상 디스크(.vhdx)입니다. 안에서 파일을 지워도 이 파일은 저절로 "
     L"줄지 않습니다. 지우면 배포판·컨테이너가 모두 사라지므로 ByteSieve는 정리하지 않고 줄이는 방법만 "
     L"알려 줍니다(WSL 을 끈 뒤 압축, 관리자 권한 필요).",
     0, NULL},
};

const CleanerKindInfo *cleaner_kind_info(CleanerKind kind)
{
    return (int)kind >= 0 && kind < CLEANER_KIND_COUNT ? &g_kinds[kind] : NULL;
}

const wchar_t *cleaner_group_text(CleanerGroup group)
{
    switch (group) {
    case CLEANER_GROUP_WINDOWS:
        return L"Windows 임시 파일";
    case CLEANER_GROUP_RECYCLE:
        return L"휴지통";
    case CLEANER_GROUP_BROWSER:
        return L"브라우저 캐시";
    case CLEANER_GROUP_DEV:
        return L"개발 도구 캐시";
    case CLEANER_GROUP_VDISK:
        return L"가상 디스크(WSL·Docker)";
    default:
        return L"";
    }
}

const wchar_t *cleaner_status_text(CleanerStatus status)
{
    switch (status) {
    case CLEANER_READY:
        return L"정리 가능";
    case CLEANER_EMPTY:
        return L"정리할 것 없음";
    case CLEANER_NOT_FOUND:
        return L"없음";
    case CLEANER_APP_RUNNING:
        return L"프로그램 실행 중";
    case CLEANER_DENIED:
        return L"보호·확인 불가로 정리 안 함";
    case CLEANER_ERROR:
        return L"조회 실패";
    case CLEANER_CANCELLED:
        return L"분석 취소됨";
    case CLEANER_INFO_ONLY:
        return L"분석만(정리하지 않음)";
    default:
        return L"";
    }
}

const wchar_t *cleaner_status_name(CleanerStatus status)
{
    switch (status) {
    case CLEANER_READY:
        return L"ready";
    case CLEANER_EMPTY:
        return L"empty";
    case CLEANER_NOT_FOUND:
        return L"not_found";
    case CLEANER_APP_RUNNING:
        return L"app_running";
    case CLEANER_DENIED:
        return L"denied";
    case CLEANER_ERROR:
        return L"error";
    case CLEANER_CANCELLED:
        return L"cancelled";
    case CLEANER_INFO_ONLY:
        return L"info_only";
    default:
        return L"unknown";
    }
}

const wchar_t *cleaner_skip_text(CleanerSkip skip)
{
    switch (skip) {
    case CLEANER_SKIP_NONE:
        return L"";
    case CLEANER_SKIP_RECENT:
        return L"최근에 바뀌었거나 확인하지 못해 건너뜀";
    case CLEANER_SKIP_APP_RUNNING:
        return L"프로그램이 실행 중이라 건너뜀";
    default:
        return L"";
    }
}

const wchar_t *cleaner_skip_name(CleanerSkip skip)
{
    switch (skip) {
    case CLEANER_SKIP_NONE:
        return L"none";
    case CLEANER_SKIP_RECENT:
        return L"recent";
    case CLEANER_SKIP_APP_RUNNING:
        return L"app_running";
    default:
        return L"unknown";
    }
}

/* ---- 목록 ---- */

void cleaner_list_init(CleanerList *list)
{
    memset(list, 0, sizeof(*list));
}

static void target_free(CleanerTarget *t)
{
    size_t i;

    for (i = 0; i < t->entry_count; i++) {
        free(t->entries[i].item.path);
        free(t->entries[i].root);
    }
    free(t->entries);
    free(t->label);
    free(t->location);
    free(t->dev_origin);
    memset(t, 0, sizeof(*t));
}

void cleaner_list_free(CleanerList *list)
{
    size_t i;

    for (i = 0; i < list->len; i++) {
        target_free(&list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int cleaner_target_runnable(const CleanerTarget *t)
{
    return t != NULL && t->status == CLEANER_READY;
}

int cleaner_target_manual_only(const CleanerTarget *t)
{
    const CleanerKindInfo *k = t != NULL ? cleaner_kind_info(t->kind) : NULL;

    /* 모르는 종류는 안전하게 직접 고르게 한다. */
    return t != NULL && (k == NULL || k->permanent || t->kind == CLEANER_DEV_CACHE);
}

int cleaner_target_caution(const CleanerTarget *t)
{
    const DevCacheKindInfo *d;

    if (t == NULL || t->kind != CLEANER_DEV_CACHE) {
        return 0;
    }
    d = dev_cache_kind_info(t->dev_kind);
    return d == NULL || d->caution; /* 모르는 캐시는 주의로 본다 */
}

/* 새 대상을 붙인다(메모리 부족이면 NULL). location 은 복사한다. */
static CleanerTarget *list_add(CleanerList *list, CleanerKind kind, const wchar_t *location)
{
    CleanerTarget *t;

    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        CleanerTarget *grown = realloc(list->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        list->items = grown;
        list->cap = cap;
    }
    t = &list->items[list->len];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->status = CLEANER_NOT_FOUND;
    if (location != NULL) {
        t->location = path_dup(location);
        if (t->location == NULL) {
            return NULL;
        }
    }
    list->len++;
    return t;
}

/* 정리 항목을 붙인다. item->path 와 root 의 소유권을 넘겨받는다(실패하면 해제). */
static int target_add_entry(CleanerTarget *t, CleanupItem *item, wchar_t *root)
{
    if (t->entry_count == t->entry_cap) {
        size_t cap = t->entry_cap ? t->entry_cap * 2 : 16;
        CleanerEntry *grown = realloc(t->entries, cap * sizeof(*grown));
        if (grown == NULL) {
            free(item->path);
            free(root);
            return -1;
        }
        t->entries = grown;
        t->entry_cap = cap;
    }
    t->entries[t->entry_count].item = *item;
    t->entries[t->entry_count].root = root;
    t->entry_count++;
    t->bytes += item->bytes;
    t->reclaim_bytes += cleanup_item_reclaim(item);
    t->shared_bytes += item->shared_bytes;
    t->files += item->files;
    return 0;
}

/* ---- 시스템 연동 ---- */

int cleaner_app_running_system(const wchar_t *exe_name, void *ctx)
{
    return clean_plan_app_running_system(exe_name, ctx);
}

int cleaner_query_recycle_system(uint64_t *bytes, uint64_t *items, uint32_t *code, void *ctx)
{
    SHQUERYRBINFO info;
    HRESULT hr;

    (void)ctx;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    hr = SHQueryRecycleBinW(NULL, &info);
    if (FAILED(hr)) {
        *code = (uint32_t)hr;
        return -1;
    }
    *bytes = (uint64_t)info.i64Size;
    *items = (uint64_t)info.i64NumItems;
    return 0;
}

int cleaner_empty_recycle_system(uint32_t *code, void *ctx)
{
    return clean_plan_empty_recycle_system(code, ctx);
}

int cleaner_kinds_include(unsigned kinds, CleanerKind kind)
{
    if ((unsigned)kind >= (unsigned)CLEANER_KIND_COUNT) {
        return 0;
    }
    return kinds == 0 || (kinds & CLEANER_KIND_BIT(kind)) != 0;
}

static CleanerOps resolve_ops(const CleanerOps *ops)
{
    CleanerOps r;

    memset(&r, 0, sizeof(r));
    if (ops != NULL) {
        r = *ops;
    }
    if (r.app_running == NULL) {
        r.app_running = cleaner_app_running_system;
    }
    if (r.query_recycle == NULL) {
        r.query_recycle = cleaner_query_recycle_system;
    }
    if (r.empty_recycle == NULL) {
        r.empty_recycle = cleaner_empty_recycle_system;
    }
    if (r.min_age_hours == 0) {
        r.min_age_hours = CLEANER_DEFAULT_MIN_AGE_HOURS;
    }
    return r;
}

static uint64_t ft64(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static uint64_t now_ft(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft64(ft);
}

/* 이 시각 이후에 바뀐(만들어진) 항목은 "최근". */
static uint64_t recent_threshold(const CleanerOps *ops)
{
    uint64_t now = now_ft(), age = (uint64_t)ops->min_age_hours * FT_HOUR;
    return now > age ? now - age : 0;
}

/* 환경 변수 값(malloc). 없거나 비었으면 NULL. */
static wchar_t *env_dup(const wchar_t *name)
{
    DWORD n = GetEnvironmentVariableW(name, NULL, 0);
    wchar_t *buf;

    if (n <= 1) {
        return NULL;
    }
    buf = malloc(n * sizeof(*buf));
    if (buf != NULL && GetEnvironmentVariableW(name, buf, n) + 1 != n) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* 표시용 전체 경로로 정규화하고 끝의 '\' 를 뗀다(볼륨 루트 제외). */
static wchar_t *normalize_dir(wchar_t *path)
{
    wchar_t *full;
    size_t n;

    if (path == NULL) {
        return NULL;
    }
    full = path_full(path);
    free(path);
    if (full == NULL) {
        return NULL;
    }
    n = wcslen(full);
    while (n > 3 && path_is_separator(full[n - 1])) {
        full[--n] = L'\0';
    }
    return full;
}

static wchar_t *temp_dir(void)
{
    wchar_t *p = env_dup(L"TEMP");
    wchar_t buf[MAX_PATH + 1];

    if (p == NULL) {
        p = env_dup(L"TMP");
    }
    if (p == NULL && GetTempPathW(MAX_PATH + 1, buf) > 0) {
        p = path_dup(buf);
    }
    return normalize_dir(p);
}

static wchar_t *local_app_data(void)
{
    wchar_t *p = env_dup(L"LOCALAPPDATA");
    PWSTR known = NULL;

    if (p == NULL && SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &known))) {
        p = path_dup(known);
    }
    CoTaskMemFree(known);
    return normalize_dir(p);
}

/* 여러 이름을 차례로 이어 붙인다(끝은 NULL). */
static wchar_t *join_all(const wchar_t *base, ...)
{
    va_list ap;
    const wchar_t *name;
    wchar_t *cur = path_dup(base);

    va_start(ap, base);
    while (cur != NULL && (name = va_arg(ap, const wchar_t *)) != NULL) {
        wchar_t *next = path_join(cur, name);
        free(cur);
        cur = next;
    }
    va_end(ap);
    return cur;
}

static int is_dir_plain(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
           !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* ---- 폴더 훑기(용량·가장 최근 시각) ---- */

typedef struct {
    cleaner_cancel_fn cancel;
    void *ctx;
    int cancelled;
} CancelCtx;

static int check_cancel(CancelCtx *c)
{
    if (!c->cancelled && c->cancel != NULL && c->cancel(c->ctx)) {
        c->cancelled = 1;
    }
    return c->cancelled;
}

static int cancel_hook(void *ctx)
{
    return check_cancel((CancelCtx *)ctx);
}

/* 정리할 항목의 실제 확보 가능 용량을 센다(폴더는 다시 훑는다). */
static void measure_item(CleanupItem *item, uint32_t attributes, CancelCtx *cc)
{
    ReclaimSum sum;

    memset(&sum, 0, sizeof(sum));
    if (item->flags & CLEANUP_ITEM_DIR) {
        ScanReport report;
        scan_report_init(&report);
        reclaim_scan_tree(item->path, &report, &sum, cancel_hook, cc);
        scan_report_free(&report);
    } else {
        reclaim_add_file(item->path, attributes, item->bytes, &sum);
    }
    item->reclaim_bytes = sum.bytes;
    item->shared_bytes = sum.shared_bytes;
    item->flags |= CLEANUP_ITEM_MEASURED;
}

typedef struct {
    uint64_t bytes;
    uint64_t files;
    uint64_t dirs;
    uint64_t newest; /* 가장 최근의 수정·만든 시각(FILETIME) */
    int incomplete;  /* 다 읽지 못함(오류·취소) */
    int stopped;     /* stop_newer 이후 시각을 만나 멈춤 */
} WalkResult;

static uint64_t entry_time(const WIN32_FIND_DATAW *fd)
{
    uint64_t w = ft64(fd->ftLastWriteTime), c = ft64(fd->ftCreationTime);
    return w > c ? w : c;
}

static wchar_t *ext_child(const wchar_t *dir, const wchar_t *name)
{
    size_t a = wcslen(dir), b = wcslen(name);
    wchar_t *p = malloc((a + b + 2) * sizeof(*p));

    if (p != NULL) {
        memcpy(p, dir, a * sizeof(*p));
        p[a] = L'\\';
        memcpy(p + a + 1, name, (b + 1) * sizeof(*p));
    }
    return p;
}

/*
 * dir 아래를 모두 훑어 파일 용량·수와 가장 최근 시각을 더한다(링크는 들어가지 않음).
 * stop_newer 가 0 이 아니고 그 이후 시각을 만나면 멈춘다(stopped). 메모리가 부족하면 -1.
 */
static int walk_tree(const wchar_t *dir, uint64_t stop_newer, CancelCtx *cc, WalkResult *r)
{
    wchar_t **stack = NULL;
    size_t len = 0, cap = 0, seen = 0;
    wchar_t *ext = path_to_extended(dir);

    if (ext == NULL) {
        return -1;
    }
    stack = malloc(16 * sizeof(*stack));
    if (stack == NULL) {
        free(ext);
        return -1;
    }
    cap = 16;
    stack[len++] = ext;
    while (len > 0) {
        wchar_t *cur = stack[--len], *pattern = ext_child(cur, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE h;

        if (pattern == NULL) {
            free(cur);
            goto oom;
        }
        h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                             FIND_FIRST_EX_LARGE_FETCH);
        free(pattern);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                r->incomplete = 1;
            }
            free(cur);
            continue;
        }
        do {
            uint64_t t;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (++seen % WALK_CANCEL_INTERVAL == 0 && check_cancel(cc)) {
                r->incomplete = 1;
                break;
            }
            t = entry_time(&fd);
            if (t > r->newest) {
                r->newest = t;
            }
            if (stop_newer != 0 && r->newest >= stop_newer) {
                r->stopped = 1;
                break;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                r->dirs++;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    wchar_t *child = ext_child(cur, fd.cFileName);
                    if (child == NULL) {
                        FindClose(h);
                        free(cur);
                        goto oom;
                    }
                    if (len == cap) {
                        wchar_t **grown = realloc(stack, cap * 2 * sizeof(*stack));
                        if (grown == NULL) {
                            free(child);
                            FindClose(h);
                            free(cur);
                            goto oom;
                        }
                        stack = grown;
                        cap *= 2;
                    }
                    stack[len++] = child;
                }
            } else {
                r->files++;
                r->bytes += scan_combine_size(fd.nFileSizeHigh, fd.nFileSizeLow);
            }
        } while (FindNextFileW(h, &fd));
        if (!r->stopped && !r->incomplete && GetLastError() != ERROR_NO_MORE_FILES) {
            r->incomplete = 1;
        }
        FindClose(h);
        free(cur);
        if (r->stopped || cc->cancelled) {
            break;
        }
    }
    while (len > 0) {
        free(stack[--len]);
    }
    free(stack);
    return 0;
oom:
    while (len > 0) {
        free(stack[--len]);
    }
    free(stack);
    return -1;
}

/* ---- 분석 ---- */

typedef struct {
    CleanerList *list;
    const CleanupGuard *guard;
    CleanerOps ops;
    uint64_t threshold;
    CancelCtx cc;
} Analyze;

/*
 * dir 바로 아래 항목을 정리 대상으로 붙인다(임시 파일·오류 보고서). 최근에 바뀐 항목, 링크,
 * 다 읽지 못한 항목, 안전 판정에서 거부된 항목은 뺀다. dir 가 없으면 0, 있으면 1, 메모리 부족 -1.
 */
static int add_children(Analyze *a, CleanerTarget *t, const wchar_t *dir)
{
    wchar_t *ext, *pattern;
    WIN32_FIND_DATAW fd;
    HANDLE h;

    if (!is_dir_plain(dir)) {
        return 0;
    }
    ext = path_to_extended(dir);
    pattern = ext != NULL ? ext_child(ext, L"*") : NULL;
    free(ext);
    if (pattern == NULL) {
        return -1;
    }
    h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                         FIND_FIRST_EX_LARGE_FETCH);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) {
        return 1; /* 비어 있거나 읽을 수 없음 */
    }
    do {
        CleanupItem item;
        WalkResult w;
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        if (check_cancel(&a->cc)) {
            break;
        }
        memset(&item, 0, sizeof(item));
        memset(&w, 0, sizeof(w));
        item.category = CLEANUP_TEMP;
        item.eco = CLEANUP_ECO_GENERIC;
        item.flags = is_dir ? CLEANUP_ITEM_DIR : 0u;
        w.newest = entry_time(&fd);
        if (is_dir && !link) {
            item.path = path_join(dir, fd.cFileName);
            if (item.path == NULL || walk_tree(item.path, 0, &a->cc, &w) != 0) {
                free(item.path);
                FindClose(h);
                return -1;
            }
        } else if (!is_dir) {
            w.files = 1;
            w.bytes = scan_combine_size(fd.nFileSizeHigh, fd.nFileSizeLow);
        }
        item.bytes = w.bytes;
        item.files = w.files;
        item.dirs = w.dirs;
        if (link) {
            t->denied_count++;
            free(item.path);
            continue;
        }
        if (w.newest >= a->threshold) {
            t->recent_count++;
            t->recent_bytes += w.bytes;
            free(item.path);
            continue;
        }
        if (w.incomplete) {
            item.flags |= CLEANUP_ITEM_INCOMPLETE;
        }
        if (item.path == NULL) {
            item.path = path_join(dir, fd.cFileName);
            if (item.path == NULL) {
                FindClose(h);
                return -1;
            }
        }
        if (cleanup_guard_check(a->guard, dir, &item) != CLEANUP_TRASH_PENDING) {
            t->denied_count++;
            t->denied_bytes += w.bytes;
            free(item.path);
            continue;
        }
        measure_item(&item, fd.dwFileAttributes, &a->cc);
        {
            wchar_t *root = path_dup(dir);
            if (root == NULL || target_add_entry(t, &item, root) != 0) {
                if (root == NULL) {
                    free(item.path);
                }
                FindClose(h);
                return -1;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

/* 캐시 폴더 하나(root 아래)를 스캐너로 세어 정리 대상으로 붙인다. 없으면 0, 메모리 부족 -1. */
static int add_cache_dir(Analyze *a, CleanerTarget *t, const wchar_t *root, const wchar_t *name)
{
    CleanupItem item;
    ScanReport report;
    ReclaimSum sum;
    ScanStatus st;
    DWORD attr;
    wchar_t *path = path_join(root, name), *root_copy;

    if (path == NULL) {
        return -1;
    }
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        free(path);
        return 0;
    }
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        t->denied_count++;
        free(path);
        return 1;
    }
    scan_report_init(&report);
    memset(&sum, 0, sizeof(sum));
    st = reclaim_scan_tree(path, &report, &sum, cancel_hook, &a->cc);
    memset(&item, 0, sizeof(item));
    item.path = path;
    item.category = CLEANUP_CACHE;
    item.eco = CLEANUP_ECO_GENERIC;
    item.flags = CLEANUP_ITEM_DIR | CLEANUP_ITEM_MEASURED |
                 (st != SCAN_COMPLETE || report.cancelled ? CLEANUP_ITEM_INCOMPLETE : 0u);
    item.bytes = report.total_bytes;
    item.reclaim_bytes = sum.bytes;
    item.shared_bytes = sum.shared_bytes;
    item.files = report.file_count;
    item.dirs = report.dir_count;
    scan_report_free(&report);
    if (cleanup_guard_check(a->guard, root, &item) != CLEANUP_TRASH_PENDING) {
        t->denied_count++;
        t->denied_bytes += item.bytes;
        free(path);
        return 1;
    }
    root_copy = path_dup(root);
    if (root_copy == NULL) {
        free(path);
        return -1;
    }
    return target_add_entry(t, &item, root_copy) == 0 ? 1 : -1;
}

/* 분석이 끝난 대상의 상태를 정한다. */
static void settle(Analyze *a, CleanerTarget *t, int found)
{
    const CleanerKindInfo *k = cleaner_kind_info(t->kind);

    if (!found) {
        t->status = CLEANER_NOT_FOUND;
    } else if (a->cc.cancelled) {
        t->status = CLEANER_CANCELLED;
    } else if (t->entry_count == 0) {
        t->status = t->denied_count > 0 && t->recent_count == 0 ? CLEANER_DENIED : CLEANER_EMPTY;
    } else if (k->app != NULL && a->ops.app_running(k->app, a->ops.ctx)) {
        t->status = CLEANER_APP_RUNNING;
    } else {
        t->status = CLEANER_READY;
    }
}

static int analyze_temp(Analyze *a)
{
    wchar_t *dir = temp_dir();
    CleanerTarget *t = list_add(a->list, CLEANER_USER_TEMP, dir);
    int r = 0;

    if (t == NULL) {
        free(dir);
        return -1;
    }
    if (dir != NULL) {
        r = add_children(a, t, dir);
    }
    free(dir);
    if (r < 0) {
        return -1;
    }
    settle(a, t, r > 0);
    return 0;
}

static int analyze_wer(Analyze *a, const wchar_t *local)
{
    wchar_t *base = local != NULL ? join_all(local, L"Microsoft", L"Windows", L"WER", NULL) : NULL;
    static const wchar_t *const subs[] = {L"ReportArchive", L"ReportQueue"};
    CleanerTarget *t = list_add(a->list, CLEANER_WER, base);
    int found = 0;
    size_t i;

    if (t == NULL) {
        free(base);
        return -1;
    }
    for (i = 0; base != NULL && i < sizeof(subs) / sizeof(subs[0]); i++) {
        wchar_t *dir = path_join(base, subs[i]);
        int r = dir != NULL ? add_children(a, t, dir) : -1;
        free(dir);
        if (r < 0) {
            free(base);
            return -1;
        }
        found |= r > 0;
    }
    free(base);
    settle(a, t, found);
    return 0;
}

static int analyze_recycle(Analyze *a)
{
    CleanerTarget *t = list_add(a->list, CLEANER_RECYCLE_BIN, NULL);
    uint64_t bytes = 0, items = 0;
    uint32_t code = 0;

    if (t == NULL) {
        return -1;
    }
    free(t->location);
    t->location = path_dup(L"모든 드라이브");
    if (a->ops.query_recycle(&bytes, &items, &code, a->ops.ctx) != 0) {
        t->status = CLEANER_ERROR;
        t->code = code;
        return 0;
    }
    t->bytes = bytes;
    t->reclaim_bytes = bytes;
    t->files = items;
    t->status = items > 0 ? CLEANER_READY : CLEANER_EMPTY;
    return 0;
}

/* Chromium 계열: User Data 안의 프로필(Preferences 가 있는 폴더)마다 캐시 폴더. */
static int analyze_chromium(Analyze *a, CleanerKind kind, const wchar_t *local,
                            const wchar_t *vendor, const wchar_t *product)
{
    static const wchar_t *const caches[] = {L"Cache", L"Code Cache", L"GPUCache"};
    wchar_t *base = local != NULL ? join_all(local, vendor, product, L"User Data", NULL) : NULL;
    CleanerTarget *t = list_add(a->list, kind, base);
    wchar_t *pattern;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int found = 0, profiles = 0;

    if (t == NULL) {
        free(base);
        return -1;
    }
    if (base == NULL || !is_dir_plain(base)) {
        free(base);
        settle(a, t, 0);
        return 0;
    }
    pattern = path_join(base, L"*");
    if (pattern == NULL) {
        free(base);
        return -1;
    }
    h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
    free(pattern);
    found = 1;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *profile, *prefs;
            size_t i;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (check_cancel(&a->cc)) {
                break;
            }
            profile = path_join(base, fd.cFileName);
            prefs = profile != NULL ? path_join(profile, L"Preferences") : NULL;
            if (prefs == NULL) {
                free(profile);
                FindClose(h);
                free(base);
                return -1;
            }
            if (GetFileAttributesW(prefs) != INVALID_FILE_ATTRIBUTES) {
                profiles++;
                for (i = 0; i < sizeof(caches) / sizeof(caches[0]); i++) {
                    if (add_cache_dir(a, t, profile, caches[i]) < 0) {
                        free(prefs);
                        free(profile);
                        FindClose(h);
                        free(base);
                        return -1;
                    }
                }
            }
            free(prefs);
            free(profile);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    free(base);
    if (profiles > 0) {
        wchar_t label[64];
        swprintf(label, 64, L"프로필 %d개", profiles);
        t->label = path_dup(label);
    }
    settle(a, t, found);
    return 0;
}

static int analyze_firefox(Analyze *a, const wchar_t *local)
{
    wchar_t *base = local != NULL ? join_all(local, L"Mozilla", L"Firefox", L"Profiles", NULL)
                                  : NULL;
    CleanerTarget *t = list_add(a->list, CLEANER_FIREFOX, base);
    wchar_t *pattern;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int profiles = 0;

    if (t == NULL) {
        free(base);
        return -1;
    }
    if (base == NULL || !is_dir_plain(base)) {
        free(base);
        settle(a, t, 0);
        return 0;
    }
    pattern = path_join(base, L"*");
    if (pattern == NULL) {
        free(base);
        return -1;
    }
    h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 0);
    free(pattern);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *profile;
            int r;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (check_cancel(&a->cc)) {
                break;
            }
            profile = path_join(base, fd.cFileName);
            r = profile != NULL ? add_cache_dir(a, t, profile, L"cache2") : -1;
            free(profile);
            if (r < 0) {
                FindClose(h);
                free(base);
                return -1;
            }
            profiles += r > 0;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    free(base);
    if (profiles > 0) {
        wchar_t label[64];
        swprintf(label, 64, L"프로필 %d개", profiles);
        t->label = path_dup(label);
    }
    settle(a, t, 1);
    return 0;
}

/* 사용자에게 알릴 만한 캐시 상태: 찾았거나, 설정한 위치에 문제가 있어 정리할 수 없음.
 * 설치하지 않아 없는 캐시(NOT_FOUND·NO_PATH)는 목록에 넣지 않는다. */
static int dev_listed(DevCacheStatus status)
{
    return status == DEV_CACHE_FOUND || status == DEV_CACHE_NOT_DIR || status == DEV_CACHE_LINK ||
           status == DEV_CACHE_UNRECOGNIZED;
}

/* 공용 개발 캐시: dev_cache 가 찾은 것마다 한 대상(dev_listed). */
static int analyze_dev(Analyze *a)
{
    DevCacheList caches;
    size_t i;
    int rc = 0;

    dev_cache_list_init(&caches);
    if (dev_cache_detect(&caches) != 0) {
        dev_cache_list_free(&caches);
        return -1;
    }
    for (i = 0; i < caches.len && rc == 0; i++) {
        DevCache *c = &caches.items[i];
        const DevCacheKindInfo *k = dev_cache_kind_info(c->kind);
        CleanerTarget *t;
        wchar_t label[128];

        if (!dev_listed(c->status)) {
            continue;
        }
        t = list_add(a->list, CLEANER_DEV_CACHE, c->path);
        if (t == NULL) {
            rc = -1;
            break;
        }
        t->dev_kind = c->kind;
        t->dev_status = c->status;
        t->dev_source = c->source;
        if (c->origin != NULL && (t->dev_origin = path_dup(c->origin)) == NULL) {
            rc = -1;
            break;
        }
        swprintf(label, 128, L"%ls %ls", k->tool, k->label);
        t->label = path_dup(label);
        if (c->status != DEV_CACHE_FOUND) {
            /* 링크·폴더 아님·캐시 구조 아님: 정리하지 않고 이유만 보인다. */
            t->status = CLEANER_DENIED;
            continue;
        }
        if (a->cc.cancelled) {
            t->status = CLEANER_CANCELLED;
            continue;
        }
        dev_cache_measure(c, cancel_hook, &a->cc);
        if (dev_cache_check(a->guard, c) == CLEANUP_TRASH_PENDING) {
            CleanupItem item;
            wchar_t *path = path_dup(c->path), *root = path_dup(c->home);
            dev_cache_to_item(c, &item);
            item.path = path;
            if (path == NULL || root == NULL) {
                free(path);
                free(root);
                rc = -1;
                break;
            }
            if (target_add_entry(t, &item, root) != 0) {
                rc = -1;
                break;
            }
        } else {
            t->denied_count++;
            t->denied_bytes += c->bytes;
        }
        settle(a, t, 1);
    }
    dev_cache_list_free(&caches);
    return rc;
}

/* 가상 디스크: 분석만(INFO_ONLY). 항목이 없어 정리 계획에서 고를 수 없다. */
static int analyze_vdisk(Analyze *a)
{
    VdiskList disks;
    size_t i;
    int rc = 0;

    vdisk_list_init(&disks);
    if (vdisk_find(&disks) != 0) {
        vdisk_list_free(&disks);
        return -1;
    }
    for (i = 0; i < disks.len && rc == 0; i++) {
        const Vdisk *v = &disks.items[i];
        CleanerTarget *t = list_add(a->list, CLEANER_VDISK, v->path);
        if (t == NULL || (t->label = path_dup(v->name)) == NULL) {
            rc = -1;
            break;
        }
        t->status = CLEANER_INFO_ONLY;
        t->vdisk_kind = v->kind;
        t->bytes = v->size;
        t->vdisk_alloc = v->alloc;
        t->vdisk_sparse = v->sparse;
        t->modified = v->modified;
        t->files = 1;
    }
    vdisk_list_free(&disks);
    return rc;
}

int cleaner_target_tracked(const CleanerTarget *t)
{
    if (t == NULL) {
        return 0;
    }
    if (t->kind == CLEANER_VDISK) {
        return 1;
    }
    return t->kind == CLEANER_DEV_CACHE && t->dev_status == DEV_CACHE_FOUND &&
           (t->status == CLEANER_READY || t->status == CLEANER_EMPTY ||
            t->status == CLEANER_DENIED);
}

uint64_t cleaner_target_tracked_bytes(const CleanerTarget *t)
{
    if (!cleaner_target_tracked(t)) {
        return 0;
    }
    return t->kind == CLEANER_VDISK ? t->bytes : t->bytes + t->denied_bytes;
}

int cleaner_apply_growth(CleanerList *list, DevHistory *hist, uint64_t now, int record)
{
    size_t i;
    int added = 0;

    for (i = 0; list != NULL && hist != NULL && i < list->len; i++) {
        CleanerTarget *t = &list->items[i];
        const DevRecord *prev;
        const wchar_t *id;
        uint64_t bytes;
        if (!cleaner_target_tracked(t) || t->location == NULL) {
            continue;
        }
        id = cleaner_target_id(t);
        bytes = cleaner_target_tracked_bytes(t);
        prev = devhist_latest(hist, id, t->location);
        t->has_growth = prev != NULL;
        if (prev != NULL) {
            t->growth = (int64_t)bytes - (int64_t)prev->bytes;
            t->growth_since = prev->time;
        }
        if (record) {
            if (devhist_add(hist, now, id, t->location, bytes) != 0) {
                return -1;
            }
            added++;
        }
    }
    return added;
}

int cleaner_analyze(CleanerList *list, const CleanupGuard *guard, const CleanerOps *ops,
                    cleaner_cancel_fn cancel, cleaner_progress_fn progress, void *ctx)
{
    enum { STEPS = 8 };
    Analyze a;
    CleanupGuard own;
    wchar_t *local;
    int rc = 0;
    size_t step = 0;

    cleaner_list_free(list);
    memset(&a, 0, sizeof(a));
    a.list = list;
    a.ops = resolve_ops(ops);
    a.threshold = recent_threshold(&a.ops);
    a.cc.cancel = cancel;
    a.cc.ctx = ctx;
    if (guard == NULL) {
        cleanup_guard_init(&own);
        cleanup_guard_add_system(&own);
        a.guard = &own;
    } else {
        a.guard = guard;
    }
    local = local_app_data();

#define STEP(kind, call)                                                         \
    do {                                                                         \
        if (rc == 0 && !cleaner_kinds_include(a.ops.kinds, (kind))) {            \
            step++; /* 분석 범위 밖 종류 */                                      \
        } else if (rc == 0) {                                                    \
            uint64_t sum_ = 0;                                                   \
            size_t i_;                                                           \
            if (progress != NULL) {                                              \
                for (i_ = 0; i_ < list->len; i_++) {                             \
                    sum_ += list->items[i_].bytes;                               \
                }                                                                \
                progress((kind), step, STEPS, sum_, ctx);                         \
            }                                                                    \
            rc = (call);                                                         \
            step++;                                                              \
        }                                                                        \
    } while (0)

    STEP(CLEANER_USER_TEMP, analyze_temp(&a));
    STEP(CLEANER_WER, analyze_wer(&a, local));
    STEP(CLEANER_RECYCLE_BIN, analyze_recycle(&a));
    STEP(CLEANER_CHROME,
         analyze_chromium(&a, CLEANER_CHROME, local, L"Google", L"Chrome"));
    STEP(CLEANER_EDGE, analyze_chromium(&a, CLEANER_EDGE, local, L"Microsoft", L"Edge"));
    STEP(CLEANER_FIREFOX, analyze_firefox(&a, local));
    STEP(CLEANER_DEV_CACHE, analyze_dev(&a));
    STEP(CLEANER_VDISK, analyze_vdisk(&a));
#undef STEP

    free(local);
    if (guard == NULL) {
        cleanup_guard_free(&own);
    }
    list->cancelled = a.cc.cancelled;
    return rc;
}

/* ---- 분석 작업 스레드 ---- */

struct CleanerScanJob {
    HANDLE thread;
    CleanerOps ops;
    cleaner_notify_fn notify;
    void *ctx;
    volatile LONG cancel;
    CRITICAL_SECTION lock;
    CleanerScanSnapshot snap;
    CleanerList list;
    int rc;
};

static int scan_job_cancelled(void *ctx)
{
    return ((CleanerScanJob *)ctx)->cancel != 0;
}

static void scan_job_progress(CleanerKind kind, size_t done, size_t total, uint64_t bytes,
                              void *ctx)
{
    CleanerScanJob *job = ctx;

    EnterCriticalSection(&job->lock);
    job->snap.kind = kind;
    job->snap.done = done;
    job->snap.total = total;
    job->snap.bytes = bytes;
    LeaveCriticalSection(&job->lock);
    if (job->notify != NULL) {
        job->notify(0, job->ctx);
    }
}

static DWORD WINAPI scan_job_main(LPVOID param)
{
    CleanerScanJob *job = param;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    job->rc = cleaner_analyze(&job->list, NULL, &job->ops, scan_job_cancelled,
                              scan_job_progress, job);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    EnterCriticalSection(&job->lock);
    job->snap.done = job->snap.total;
    LeaveCriticalSection(&job->lock);
    if (job->notify != NULL) {
        job->notify(1, job->ctx);
    }
    return 0;
}

CleanerScanJob *cleaner_scan_start(const CleanerOps *ops, cleaner_notify_fn notify, void *ctx)
{
    CleanerScanJob *job = calloc(1, sizeof(*job));

    if (job == NULL) {
        return NULL;
    }
    if (ops != NULL) {
        job->ops = *ops;
    }
    job->notify = notify;
    job->ctx = ctx;
    cleaner_list_init(&job->list);
    InitializeCriticalSection(&job->lock);
    job->thread = CreateThread(NULL, 0, scan_job_main, job, 0, NULL);
    if (job->thread == NULL) {
        DeleteCriticalSection(&job->lock);
        free(job);
        return NULL;
    }
    return job;
}

void cleaner_scan_cancel(CleanerScanJob *job)
{
    if (job != NULL) {
        InterlockedExchange(&job->cancel, 1);
    }
}

void cleaner_scan_progress(CleanerScanJob *job, CleanerScanSnapshot *out)
{
    EnterCriticalSection(&job->lock);
    *out = job->snap;
    LeaveCriticalSection(&job->lock);
    out->cancel_requested = job->cancel != 0;
}

int cleaner_scan_finish(CleanerScanJob *job, CleanerList *out)
{
    int rc;

    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    DeleteCriticalSection(&job->lock);
    rc = job->rc;
    if (out != NULL) {
        cleaner_list_free(out);
        *out = job->list;
    } else {
        cleaner_list_free(&job->list);
    }
    free(job);
    return rc;
}

/* ---- 계획 ---- */

/* 계획 대상 이름: 개발 캐시는 도구·캐시 이름, 브라우저는 프로필 수를 덧붙인다(GUI 대상 칸과 같다). */
static void plan_label(const CleanerTarget *t, wchar_t *buf, size_t len)
{
    const CleanerKindInfo *k = cleaner_kind_info(t->kind);

    if (t->kind == CLEANER_DEV_CACHE && t->label != NULL) {
        swprintf(buf, len, L"%ls", t->label);
    } else if (t->label != NULL) {
        swprintf(buf, len, L"%ls (%ls)", k->name, t->label);
    } else {
        swprintf(buf, len, L"%ls", k->name);
    }
}

const wchar_t *cleaner_target_id(const CleanerTarget *t)
{
    const DevCacheKindInfo *d;

    if (t == NULL) {
        return L"";
    }
    if (t->kind == CLEANER_DEV_CACHE && (d = dev_cache_kind_info(t->dev_kind)) != NULL) {
        return d->id;
    }
    return cleaner_kind_info(t->kind) != NULL ? cleaner_kind_info(t->kind)->id : L"";
}

int cleaner_build_plan(const CleanerList *list, CleanPlan *plan)
{
    size_t i, j;

    clean_plan_free(plan);
    for (i = 0; list != NULL && i < list->len; i++) {
        const CleanerTarget *src = &list->items[i];
        const CleanerKindInfo *k = cleaner_kind_info(src->kind);
        CleanPlanTarget *t;
        wchar_t label[256];

        plan_label(src, label, sizeof(label) / sizeof(label[0]));
        t = clean_plan_add(plan, CLEAN_SOURCE_SYSTEM, cleaner_target_id(src), label,
                           src->location);
        if (t == NULL) {
            goto fail;
        }
        t->origin = i;
        t->ready = cleaner_target_runnable(src);
        t->state = src->kind == CLEANER_DEV_CACHE && src->dev_status != DEV_CACHE_FOUND
                       ? dev_cache_status_name(src->dev_status)
                       : cleaner_status_name(src->status);
        t->bytes = src->bytes;
        t->reclaim = src->reclaim_bytes;
        t->shared = src->shared_bytes;
        t->files = src->files;
        t->category = CLEANUP_CACHE;
        t->eco = CLEANUP_ECO_GENERIC;
        t->flags = (cleaner_target_manual_only(src) ? CLEAN_PLAN_MANUAL : 0u) |
                   (cleaner_target_caution(src) ? CLEAN_PLAN_CAUTION : 0u) |
                   (k != NULL && k->permanent ? CLEAN_PLAN_PERMANENT : 0u) |
                   (src->kind == CLEANER_USER_TEMP || src->kind == CLEANER_WER
                        ? CLEAN_PLAN_CHECK_RECENT
                        : 0u);
        if (k != NULL && k->app != NULL && (t->app = path_dup(k->app)) == NULL) {
            goto fail;
        }
        for (j = 0; j < src->entry_count; j++) {
            if (clean_plan_add_entry(t, &src->entries[j].item, src->entries[j].root) != 0) {
                goto fail;
            }
        }
    }
    return 0;
fail:
    clean_plan_free(plan);
    return -1;
}

/* ---- 실행(계획 실행 엔진을 감싼다) ---- */

struct CleanerRunJob {
    CleanPlan plan;
    CleanPlanRunJob *job; /* 실행할 항목이 없으면 NULL */
};

void cleaner_run_result_free(CleanerRunResult *r)
{
    size_t i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->entry_count; i++) {
        free(r->entries[i].path);
        cleanup_trash_outcome_free(&r->entries[i].trash);
    }
    free(r->entries);
    free(r->targets);
    memset(r, 0, sizeof(*r));
}

CleanerRunJob *cleaner_run_start(const CleanerList *list, const int *selected,
                                 const CleanerOps *ops, cleaner_notify_fn notify, void *ctx)
{
    CleanerRunJob *job;
    CleanerOps r;
    CleanPlanOps pops;
    size_t i;

    if (list == NULL || selected == NULL) {
        return NULL;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }
    clean_plan_init(&job->plan);
    if (cleaner_build_plan(list, &job->plan) != 0) {
        free(job);
        return NULL;
    }
    for (i = 0; i < job->plan.len; i++) {
        clean_plan_set_selected(&job->plan, i, selected[i]);
    }
    r = resolve_ops(ops);
    memset(&pops, 0, sizeof(pops));
    pops.app_running = r.app_running;
    pops.empty_recycle = r.empty_recycle;
    pops.ctx = r.ctx;
    pops.min_age_hours = r.min_age_hours;
    /* 이 API 는 호출자가 휴지통 비우기를 이미 확인했다고 본다(골랐으면 실행). */
    pops.allow_permanent = 1;
    job->job = clean_plan_run_start(&job->plan, &pops, (clean_plan_notify_fn)notify, ctx);
    if (job->job == NULL && notify != NULL) {
        notify(1, ctx); /* 실행할 것이 없음: 바로 끝났다고 알린다 */
    }
    return job;
}

void cleaner_run_cancel(CleanerRunJob *job)
{
    if (job != NULL && job->job != NULL) {
        clean_plan_run_cancel(job->job);
    }
}

void cleaner_run_progress(CleanerRunJob *job, CleanerRunSnapshot *out)
{
    CleanPlanSnapshot s;

    memset(out, 0, sizeof(*out));
    if (job == NULL || job->job == NULL) {
        return;
    }
    clean_plan_run_progress(job->job, &s);
    out->emptying_recycle = s.emptying_recycle;
    out->total = s.total;
    out->done = s.done;
    out->moved = s.moved;
    out->failed = s.failed;
    out->moved_bytes = s.moved_bytes;
    out->cancel_requested = s.cancel_requested;
}

void cleaner_run_finish(CleanerRunJob *job, CleanerRunResult *out)
{
    CleanPlanTotals totals;
    CleanerRunResult r;
    size_t i, j, n = 0;

    memset(&totals, 0, sizeof(totals));
    memset(&r, 0, sizeof(r));
    if (job == NULL) {
        if (out != NULL) {
            *out = r;
        }
        return;
    }
    if (job->job != NULL) {
        clean_plan_run_finish(job->job, &job->plan, &totals);
    }
    for (i = 0; i < job->plan.len; i++) {
        const CleanPlanTarget *t = &job->plan.items[i];
        for (j = 0; t->ran && j < t->entry_count; j++) {
            n += t->entries[j].skip != CLEAN_SKIP_NONE ||
                 t->entries[j].outcome.status != CLEANUP_TRASH_PENDING;
        }
    }
    r.targets = calloc(job->plan.len > 0 ? job->plan.len : 1, sizeof(*r.targets));
    r.entries = calloc(n > 0 ? n : 1, sizeof(*r.entries));
    if (r.targets != NULL && r.entries != NULL) {
        r.target_count = job->plan.len;
        for (i = 0; i < job->plan.len; i++) {
            CleanPlanTarget *t = &job->plan.items[i];
            CleanerTargetResult *tr = &r.targets[i];
            if (!t->ran) {
                continue;
            }
            tr->selected = 1;
            if (t->flags & CLEAN_PLAN_PERMANENT) {
                continue;
            }
            tr->moved = t->moved;
            tr->quarantined = t->quarantined;
            tr->failed = t->failed;
            tr->skipped = t->skipped;
            tr->cancelled = t->cancelled;
            tr->moved_bytes = t->moved_bytes;
            for (j = 0; j < t->entry_count; j++) {
                CleanPlanEntry *e = &t->entries[j];
                CleanerEntryOutcome *o;
                if (e->skip == CLEAN_SKIP_NONE && e->outcome.status == CLEANUP_TRASH_PENDING) {
                    continue;
                }
                o = &r.entries[r.entry_count++];
                o->target = i;
                o->path = e->item.path;
                e->item.path = NULL; /* 소유권을 넘긴다 */
                o->bytes = e->item.bytes;
                o->skip = (CleanerSkip)e->skip;
                o->trash = e->outcome;
                memset(&e->outcome, 0, sizeof(e->outcome));
            }
        }
        r.recycle_requested = totals.permanent_requested;
        r.recycle_emptied = totals.permanent_done;
        r.recycle_code = totals.permanent_code;
        r.recycle_bytes = totals.permanent_bytes;
        r.cancelled = totals.was_cancelled;
    } else {
        free(r.targets);
        free(r.entries);
        memset(&r, 0, sizeof(r));
    }
    clean_plan_free(&job->plan);
    free(job);
    if (out != NULL) {
        *out = r;
    } else {
        cleaner_run_result_free(&r);
    }
}
