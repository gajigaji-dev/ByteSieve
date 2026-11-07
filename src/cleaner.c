#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
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
    t->files += item->files;
    return 0;
}

/* ---- 시스템 연동 ---- */

int cleaner_app_running_system(const wchar_t *exe_name, void *ctx)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    int found = 0;

    (void)ctx;
    if (exe_name == NULL) {
        return 0;
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 1; /* 확인할 수 없으면 실행 중으로 본다(정리하지 않는 쪽) */
    }
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exe_name) == 0) {
                found = 1;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
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
    HRESULT hr = SHEmptyRecycleBinW(NULL, NULL,
                                    SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    uint64_t bytes = 0, items = 0;
    uint32_t qcode = 0;

    if (SUCCEEDED(hr)) {
        return 0;
    }
    /* 이미 비어 있으면 실패 코드를 돌려주는 경우가 있어 다시 조회해 확인한다. */
    if (cleaner_query_recycle_system(&bytes, &items, &qcode, ctx) == 0 && items == 0) {
        return 0;
    }
    *code = (uint32_t)hr;
    return -1;
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

typedef struct {
    CancelCtx *cc;
} ScanCancel;

static int scan_cancel_hook(void *ctx)
{
    return check_cancel(((ScanCancel *)ctx)->cc);
}

/* 캐시 폴더 하나(root 아래)를 스캐너로 세어 정리 대상으로 붙인다. 없으면 0, 메모리 부족 -1. */
static int add_cache_dir(Analyze *a, CleanerTarget *t, const wchar_t *root, const wchar_t *name)
{
    CleanupItem item;
    ScanReport report;
    ScanHooks hooks;
    ScanCancel sc;
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
    memset(&hooks, 0, sizeof(hooks));
    hooks.should_cancel = scan_cancel_hook;
    sc.cc = &a->cc;
    scan_report_init(&report);
    st = scan_tree_ex(path, SCAN_DEPTH_UNLIMITED, 0, &report, &hooks, &sc);
    memset(&item, 0, sizeof(item));
    item.path = path;
    item.category = CLEANUP_CACHE;
    item.eco = CLEANUP_ECO_GENERIC;
    item.flags = CLEANUP_ITEM_DIR |
                 (st != SCAN_COMPLETE || report.cancelled ? CLEANUP_ITEM_INCOMPLETE : 0u);
    item.bytes = report.total_bytes;
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

static int dev_cancel_hook(void *ctx)
{
    return check_cancel((CancelCtx *)ctx);
}

/* 공용 개발 캐시: dev_cache 가 찾은 것(FOUND)마다 한 대상. */
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

        if (c->status != DEV_CACHE_FOUND) {
            continue;
        }
        t = list_add(a->list, CLEANER_DEV_CACHE, c->path);
        if (t == NULL) {
            rc = -1;
            break;
        }
        t->dev_kind = c->kind;
        swprintf(label, 128, L"%ls %ls", k->tool, k->label);
        t->label = path_dup(label);
        if (a->cc.cancelled) {
            t->status = CLEANER_CANCELLED;
            continue;
        }
        dev_cache_measure(c, dev_cancel_hook, &a->cc);
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

int cleaner_analyze(CleanerList *list, const CleanupGuard *guard, const CleanerOps *ops,
                    cleaner_cancel_fn cancel, cleaner_progress_fn progress, void *ctx)
{
    enum { STEPS = 7 };
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
        if (rc == 0) {                                                           \
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

/* ---- 실행 ---- */

typedef struct {
    size_t target;
    CleanerKind kind;
    CleanupItem item; /* path 소유 */
    wchar_t *root;
} RunEntry;

struct CleanerRunJob {
    HANDLE thread;
    CleanerOps ops;
    cleaner_notify_fn notify;
    void *ctx;
    volatile LONG cancel;
    CRITICAL_SECTION lock;
    CleanerRunSnapshot snap;

    RunEntry *entries;
    size_t entry_count;
    size_t target_count;
    int recycle;
    uint64_t recycle_bytes;
    CleanerRunResult result;
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

/* 실행 직전 다시 확인: 최근에 바뀌었거나 확인할 수 없으면 1(옮기지 않음). 없으면 0(trash 가 MISSING). */
static int recently_changed(const CleanupItem *item, uint64_t threshold)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    WalkResult w;
    CancelCtx cc;
    uint64_t t;

    if (!GetFileAttributesExW(item->path, GetFileExInfoStandard, &fa)) {
        DWORD err = GetLastError();
        return !(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
    }
    t = ft64(fa.ftLastWriteTime) > ft64(fa.ftCreationTime) ? ft64(fa.ftLastWriteTime)
                                                           : ft64(fa.ftCreationTime);
    if (t >= threshold) {
        return 1;
    }
    if (!(fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (fa.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
    memset(&w, 0, sizeof(w));
    memset(&cc, 0, sizeof(cc));
    if (walk_tree(item->path, threshold, &cc, &w) != 0) {
        return 1;
    }
    return w.stopped || w.incomplete || w.newest >= threshold;
}

static void run_notify(CleanerRunJob *job)
{
    if (job->notify != NULL) {
        job->notify(0, job->ctx);
    }
}

static DWORD WINAPI run_job_main(LPVOID param)
{
    CleanerRunJob *job = param;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CleanupGuard guard;
    CleanerRunResult *r = &job->result;
    uint64_t threshold = recent_threshold(&job->ops);
    size_t i, last_target = (size_t)-1;
    int app_running = 0;

    /* 1. 휴지통 비우기(영구 삭제): 다른 항목을 옮기기 전에 한다. */
    if (job->recycle && job->cancel == 0) {
        uint32_t code = 0;
        EnterCriticalSection(&job->lock);
        job->snap.emptying_recycle = 1;
        LeaveCriticalSection(&job->lock);
        run_notify(job);
        r->recycle_emptied = job->ops.empty_recycle(&code, job->ops.ctx) == 0;
        r->recycle_code = r->recycle_emptied ? 0 : code;
        r->recycle_bytes = r->recycle_emptied ? job->recycle_bytes : 0;
        EnterCriticalSection(&job->lock);
        job->snap.emptying_recycle = 0;
        LeaveCriticalSection(&job->lock);
        run_notify(job);
    }

    /* 2. 나머지 항목: 같은 안전 장치(cleanup_trash_one)로 휴지통·격리 폴더로. */
    cleanup_guard_init(&guard);
    cleanup_guard_add_system(&guard);
    for (i = 0; i < job->entry_count; i++) {
        RunEntry *e = &job->entries[i];
        CleanerEntryOutcome *o = &r->entries[i];
        CleanerTargetResult *tr = &r->targets[e->target];
        const CleanerKindInfo *k = cleaner_kind_info(e->kind);

        if (e->target != last_target) {
            last_target = e->target;
            app_running = k->app != NULL && job->ops.app_running(k->app, job->ops.ctx);
        }
        if (job->cancel != 0) {
            o->trash.status = CLEANUP_TRASH_CANCELLED;
            tr->cancelled++;
            r->cancelled = 1;
        } else if (app_running) {
            o->skip = CLEANER_SKIP_APP_RUNNING;
            tr->skipped++;
        } else if ((e->kind == CLEANER_USER_TEMP || e->kind == CLEANER_WER) &&
                   recently_changed(&e->item, threshold)) {
            o->skip = CLEANER_SKIP_RECENT;
            tr->skipped++;
        } else {
            cleanup_trash_one(&guard, e->root, &e->item, &o->trash);
            if (o->trash.status == CLEANUP_TRASH_MOVED ||
                o->trash.status == CLEANUP_TRASH_QUARANTINED) {
                tr->moved++;
                tr->quarantined += o->trash.status == CLEANUP_TRASH_QUARANTINED;
                tr->moved_bytes += e->item.bytes;
            } else {
                tr->failed++;
            }
        }
        EnterCriticalSection(&job->lock);
        job->snap.done = i + 1;
        if (o->skip == CLEANER_SKIP_NONE && (o->trash.status == CLEANUP_TRASH_MOVED ||
                                             o->trash.status == CLEANUP_TRASH_QUARANTINED)) {
            job->snap.moved++;
            job->snap.moved_bytes += e->item.bytes;
        } else if (o->trash.status != CLEANUP_TRASH_CANCELLED) {
            job->snap.failed++;
        }
        LeaveCriticalSection(&job->lock);
        run_notify(job);
    }
    cleanup_guard_free(&guard);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    if (job->notify != NULL) {
        job->notify(1, job->ctx);
    }
    return 0;
}

static void free_run_entries(CleanerRunJob *job)
{
    size_t i;

    for (i = 0; i < job->entry_count; i++) {
        free(job->entries[i].item.path);
        free(job->entries[i].root);
    }
    free(job->entries);
    job->entries = NULL;
}

CleanerRunJob *cleaner_run_start(const CleanerList *list, const int *selected,
                                 const CleanerOps *ops, cleaner_notify_fn notify, void *ctx)
{
    CleanerRunJob *job;
    size_t i, j, n = 0;

    if (list == NULL || selected == NULL) {
        return NULL;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }
    job->ops = resolve_ops(ops);
    job->notify = notify;
    job->ctx = ctx;
    job->target_count = list->len;
    for (i = 0; i < list->len; i++) {
        if (selected[i] && cleaner_target_runnable(&list->items[i])) {
            n += list->items[i].entry_count;
        }
    }
    job->entries = calloc(n > 0 ? n : 1, sizeof(*job->entries));
    job->result.entries = calloc(n > 0 ? n : 1, sizeof(*job->result.entries));
    job->result.targets = calloc(list->len > 0 ? list->len : 1, sizeof(*job->result.targets));
    if (job->entries == NULL || job->result.entries == NULL || job->result.targets == NULL) {
        goto fail;
    }
    job->result.target_count = list->len;
    for (i = 0; i < list->len; i++) {
        const CleanerTarget *t = &list->items[i];
        if (!selected[i] || !cleaner_target_runnable(t)) {
            continue;
        }
        job->result.targets[i].selected = 1;
        if (cleaner_kind_info(t->kind)->permanent) {
            job->recycle = 1;
            job->recycle_bytes = t->bytes;
            job->result.recycle_requested = 1;
            continue;
        }
        for (j = 0; j < t->entry_count; j++) {
            RunEntry *e = &job->entries[job->entry_count];
            CleanerEntryOutcome *o = &job->result.entries[job->entry_count];
            e->target = i;
            e->kind = t->kind;
            e->item = t->entries[j].item;
            e->item.path = path_dup(t->entries[j].item.path);
            e->root = path_dup(t->entries[j].root);
            o->target = i;
            o->path = path_dup(t->entries[j].item.path);
            o->bytes = t->entries[j].item.bytes;
            job->entry_count++;
            job->result.entry_count++;
            if (e->item.path == NULL || e->root == NULL || o->path == NULL) {
                goto fail;
            }
        }
    }
    job->snap.total = job->entry_count;
    InitializeCriticalSection(&job->lock);
    job->thread = CreateThread(NULL, 0, run_job_main, job, 0, NULL);
    if (job->thread == NULL) {
        DeleteCriticalSection(&job->lock);
        goto fail;
    }
    return job;
fail:
    free_run_entries(job);
    cleaner_run_result_free(&job->result);
    free(job);
    return NULL;
}

void cleaner_run_cancel(CleanerRunJob *job)
{
    if (job != NULL) {
        InterlockedExchange(&job->cancel, 1);
    }
}

void cleaner_run_progress(CleanerRunJob *job, CleanerRunSnapshot *out)
{
    EnterCriticalSection(&job->lock);
    *out = job->snap;
    LeaveCriticalSection(&job->lock);
    out->cancel_requested = job->cancel != 0;
}

void cleaner_run_finish(CleanerRunJob *job, CleanerRunResult *out)
{
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    DeleteCriticalSection(&job->lock);
    free_run_entries(job);
    if (out != NULL) {
        *out = job->result;
    } else {
        cleaner_run_result_free(&job->result);
    }
    free(job);
}
