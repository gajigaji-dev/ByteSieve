#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "clean_plan.h"
#include "path.h"

#define DEFAULT_MIN_AGE_HOURS 24
#define FT_HOUR 36000000000ull /* FILETIME 1시간(100ns 단위) */

/* ---- 이름 ---- */

const wchar_t *clean_skip_text(CleanSkip skip)
{
    switch (skip) {
    case CLEAN_SKIP_NONE:
        return L"건너뛰지 않음";
    case CLEAN_SKIP_RECENT:
        return L"분석 뒤 최근에 바뀌어 건너뜀";
    case CLEAN_SKIP_APP_RUNNING:
        return L"관련 프로그램이 실행 중이라 건너뜀";
    default:
        return L"";
    }
}

const wchar_t *clean_skip_name(CleanSkip skip)
{
    switch (skip) {
    case CLEAN_SKIP_NONE:
        return L"none";
    case CLEAN_SKIP_RECENT:
        return L"recent";
    case CLEAN_SKIP_APP_RUNNING:
        return L"app_running";
    default:
        return L"unknown";
    }
}

/* ---- 계획 ---- */

static wchar_t *dup_or_null(const wchar_t *s, int *oom)
{
    wchar_t *d;

    if (s == NULL) {
        return NULL;
    }
    d = path_dup(s);
    if (d == NULL) {
        *oom = 1;
    }
    return d;
}

static void target_free(CleanPlanTarget *t)
{
    size_t i;

    for (i = 0; i < t->entry_count; i++) {
        free(t->entries[i].item.path);
        free(t->entries[i].root);
        cleanup_trash_outcome_free(&t->entries[i].outcome);
    }
    free(t->entries);
    free(t->id);
    free(t->label);
    free(t->location);
    free(t->app);
    memset(t, 0, sizeof(*t));
}

void clean_plan_init(CleanPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
}

void clean_plan_free(CleanPlan *plan)
{
    size_t i;

    if (plan == NULL) {
        return;
    }
    for (i = 0; i < plan->len; i++) {
        target_free(&plan->items[i]);
    }
    free(plan->items);
    clean_plan_init(plan);
}

CleanPlanTarget *clean_plan_add(CleanPlan *plan, CleanSource source, const wchar_t *id,
                                const wchar_t *label, const wchar_t *location)
{
    CleanPlanTarget *t;
    int oom = 0;

    if (plan->len == plan->cap) {
        size_t cap = plan->cap ? plan->cap * 2 : 32;
        CleanPlanTarget *grown = realloc(plan->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        plan->items = grown;
        plan->cap = cap;
    }
    t = &plan->items[plan->len];
    memset(t, 0, sizeof(*t));
    t->source = source;
    t->origin = plan->len;
    t->project = (size_t)-1;
    t->recovery = -1;
    t->deny = CLEANUP_TRASH_PENDING;
    t->state = L"";
    t->id = dup_or_null(id, &oom);
    t->label = dup_or_null(label, &oom);
    t->location = dup_or_null(location, &oom);
    if (oom) {
        target_free(t);
        return NULL;
    }
    plan->len++;
    return t;
}

int clean_plan_add_entry(CleanPlanTarget *t, const CleanupItem *item, const wchar_t *root)
{
    CleanPlanEntry *e;

    if (t->entry_count == t->entry_cap) {
        size_t cap = t->entry_cap ? t->entry_cap * 2 : 4;
        CleanPlanEntry *grown = realloc(t->entries, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        t->entries = grown;
        t->entry_cap = cap;
    }
    e = &t->entries[t->entry_count];
    memset(e, 0, sizeof(*e));
    e->item = *item;
    e->item.path = path_dup(item->path);
    e->root = path_dup(root);
    if (e->item.path == NULL || e->root == NULL) {
        free(e->item.path);
        free(e->root);
        return -1;
    }
    t->entry_count++;
    return 0;
}

int clean_plan_from_project(CleanPlan *plan, const CleanupResult *r, const CleanupGuard *guard)
{
    CleanupGuard own;
    size_t i;
    int rc = 0;

    clean_plan_free(plan);
    if (r == NULL) {
        return 0;
    }
    if (guard == NULL) {
        cleanup_guard_init(&own);
        cleanup_guard_add_system(&own);
        guard = &own;
    }
    for (i = 0; i < r->len && rc == 0; i++) {
        const CleanupItem *item = &r->items[i];
        CleanPlanTarget *t = clean_plan_add(plan, CLEAN_SOURCE_PROJECT,
                                            cleanup_category_name(item->category), item->path,
                                            item->path);
        if (t == NULL || clean_plan_add_entry(t, item, r->root) != 0) {
            rc = -1;
            break;
        }
        t->origin = i;
        t->category = item->category;
        t->eco = item->eco;
        t->bytes = item->bytes;
        t->reclaim = cleanup_item_reclaim(item);
        t->shared = item->shared_bytes;
        t->files = item->files;
        t->deny = cleanup_guard_check(guard, r->root, item);
        t->ready = t->deny == CLEANUP_TRASH_PENDING;
        t->state = t->ready ? L"ready" : cleanup_trash_status_name(t->deny);
        if ((item->flags & CLEANUP_ITEM_DIR) && cleanup_needs_marker(item->category) &&
            !(item->flags & CLEANUP_ITEM_CONFIRMED)) {
            t->flags |= CLEAN_PLAN_GUESSED;
        }
    }
    if (guard == &own) {
        cleanup_guard_free(&own);
    }
    if (rc != 0) {
        clean_plan_free(plan);
    }
    return rc;
}

void clean_plan_apply_projects(CleanPlan *plan, const ProjectList *projects)
{
    size_t i;

    if (plan == NULL || projects == NULL) {
        return;
    }
    for (i = 0; i < plan->len; i++) {
        CleanPlanTarget *t = &plan->items[i];
        const ProjectInfo *p;
        if (t->source != CLEAN_SOURCE_PROJECT || t->origin >= projects->item_count ||
            projects->item_project[t->origin] >= projects->len) {
            continue;
        }
        t->project = projects->item_project[t->origin];
        t->recovery = (int)projects->item_recovery[t->origin];
        p = &projects->items[t->project];
        t->flags &= ~(unsigned)(CLEAN_PLAN_DIRTY | CLEAN_PLAN_UNPUSHED | CLEAN_PLAN_DORMANT);
        if (p->changes > 0) {
            /* 작업 중인 프로젝트: [모두 선택]에서 빼고 직접 고르게 한다. */
            t->flags |= CLEAN_PLAN_DIRTY | CLEAN_PLAN_MANUAL;
            t->selected = 0;
        }
        if (p->unpushed > 0 || p->remote_differs) {
            t->flags |= CLEAN_PLAN_UNPUSHED;
        }
        if (p->dormant) {
            t->flags |= CLEAN_PLAN_DORMANT;
        }
    }
}

/* 다시 해 볼 만한 항목: 아직 시도 안 했거나 실패·취소. 건너뜀(최근·실행 중)은 다시 분석해야 한다. */
static int entry_pending(const CleanPlanEntry *e)
{
    CleanupTrashStatus s = e->outcome.status;

    if (e->skip != CLEAN_SKIP_NONE) {
        return 0;
    }
    return s == CLEANUP_TRASH_PENDING || s == CLEANUP_TRASH_FAILED ||
           s == CLEANUP_TRASH_QUARANTINE_FAILED || s == CLEANUP_TRASH_CANCELLED;
}

int clean_plan_selectable(const CleanPlanTarget *t)
{
    size_t i;

    if (t == NULL || !t->ready || t->deny != CLEANUP_TRASH_PENDING) {
        return 0;
    }
    if (t->flags & CLEAN_PLAN_PERMANENT) {
        return !(t->ran && t->moved > 0); /* 이미 비웠으면 다시 분석해야 한다 */
    }
    for (i = 0; i < t->entry_count; i++) {
        if (entry_pending(&t->entries[i])) {
            return 1;
        }
    }
    return 0;
}

int clean_plan_auto_selectable(const CleanPlanTarget *t)
{
    return t != NULL && !(t->flags & CLEAN_PLAN_MANUAL) && clean_plan_selectable(t);
}

int clean_plan_set_selected(CleanPlan *plan, size_t index, int on)
{
    CleanPlanTarget *t;

    if (plan == NULL || index >= plan->len) {
        return 0;
    }
    t = &plan->items[index];
    t->selected = on && clean_plan_selectable(t);
    return t->selected;
}

size_t clean_plan_select_all(CleanPlan *plan, int on)
{
    size_t i, n = 0;

    for (i = 0; plan != NULL && i < plan->len; i++) {
        CleanPlanTarget *t = &plan->items[i];
        if (on && (t->flags & CLEAN_PLAN_MANUAL)) {
            n += t->selected != 0;
            continue;
        }
        n += clean_plan_set_selected(plan, i, on);
    }
    return n;
}

size_t clean_plan_select_id(CleanPlan *plan, const wchar_t *id, int on)
{
    size_t i, n = 0;

    for (i = 0; plan != NULL && id != NULL && i < plan->len; i++) {
        if (plan->items[i].id != NULL && _wcsicmp(plan->items[i].id, id) == 0) {
            clean_plan_set_selected(plan, i, on);
            n++;
        }
    }
    return n;
}

void clean_plan_summarize(const CleanPlan *plan, CleanPlanSummary *out)
{
    size_t i, j;

    memset(out, 0, sizeof(*out));
    for (i = 0; plan != NULL && i < plan->len; i++) {
        const CleanPlanTarget *t = &plan->items[i];
        out->selectable += clean_plan_selectable(t) != 0;
        out->auto_selectable += clean_plan_auto_selectable(t) != 0;
        if (!t->selected) {
            continue;
        }
        out->targets++;
        out->caution += (t->flags & CLEAN_PLAN_CAUTION) != 0;
        out->guessed += (t->flags & CLEAN_PLAN_GUESSED) != 0;
        out->dirty += (t->flags & CLEAN_PLAN_DIRTY) != 0;
        out->unpushed += (t->flags & CLEAN_PLAN_UNPUSHED) != 0;
        if (t->flags & CLEAN_PLAN_PERMANENT) {
            out->permanent++;
            out->permanent_bytes += t->bytes;
            continue;
        }
        out->manual += (t->flags & CLEAN_PLAN_MANUAL) && !(t->flags & CLEAN_PLAN_DIRTY);
        out->bytes += t->bytes;
        out->reclaim += t->reclaim;
        for (j = 0; j < t->entry_count; j++) {
            out->entries += entry_pending(&t->entries[j]) != 0;
        }
    }
}

/* ---- 시스템 연동 ---- */

int clean_plan_app_running_system(const wchar_t *exe_name, void *ctx)
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

int clean_plan_empty_recycle_system(uint32_t *code, void *ctx)
{
    HRESULT hr = SHEmptyRecycleBinW(NULL, NULL,
                                    SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    SHQUERYRBINFO info;

    (void)ctx;
    if (SUCCEEDED(hr)) {
        return 0;
    }
    /* 이미 비어 있으면 실패 코드를 돌려주는 경우가 있어 다시 조회해 확인한다. */
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (SUCCEEDED(SHQueryRecycleBinW(NULL, &info)) && info.i64NumItems == 0) {
        return 0;
    }
    *code = (uint32_t)hr;
    return -1;
}

/* ---- 실행 직전 다시 확인 ---- */

static uint64_t ft64(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static uint64_t newest_of(const WIN32_FIND_DATAW *fd)
{
    uint64_t w = ft64(fd->ftLastWriteTime), c = ft64(fd->ftCreationTime);
    return w > c ? w : c;
}

static wchar_t *child_of(const wchar_t *dir, const wchar_t *name)
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

/* dir 아래에 threshold 이후에 바뀐(만들어진) 항목이 있거나 다 읽지 못하면 1. 링크 안으로는 들어가지 않는다. */
static int tree_has_newer(const wchar_t *dir, uint64_t threshold)
{
    wchar_t **stack, *ext = path_to_extended(dir);
    size_t len = 0, cap = 16;
    int newer = 0;

    if (ext == NULL || (stack = malloc(cap * sizeof(*stack))) == NULL) {
        free(ext);
        return 1;
    }
    stack[len++] = ext;
    while (len > 0 && !newer) {
        wchar_t *cur = stack[--len], *pattern = child_of(cur, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE h = pattern != NULL ? FindFirstFileExW(pattern, FindExInfoBasic, &fd,
                                                      FindExSearchNameMatch, NULL,
                                                      FIND_FIRST_EX_LARGE_FETCH)
                                   : INVALID_HANDLE_VALUE;
        free(pattern);
        if (h == INVALID_HANDLE_VALUE) {
            newer = GetLastError() != ERROR_FILE_NOT_FOUND;
            free(cur);
            continue;
        }
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (newest_of(&fd) >= threshold) {
                newer = 1;
                break;
            }
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                wchar_t *child = child_of(cur, fd.cFileName);
                if (child == NULL) {
                    newer = 1;
                    break;
                }
                if (len == cap) {
                    wchar_t **grown = realloc(stack, cap * 2 * sizeof(*stack));
                    if (grown == NULL) {
                        free(child);
                        newer = 1;
                        break;
                    }
                    stack = grown;
                    cap *= 2;
                }
                stack[len++] = child;
            }
        } while (FindNextFileW(h, &fd));
        if (!newer && GetLastError() != ERROR_NO_MORE_FILES) {
            newer = 1;
        }
        FindClose(h);
        free(cur);
    }
    while (len > 0) {
        free(stack[--len]);
    }
    free(stack);
    return newer;
}

/* 최근에 바뀌었거나 확인할 수 없으면 1(옮기지 않음). 없으면 0(옮기기에서 MISSING 이 된다). */
static int recently_changed(const CleanupItem *item, uint64_t threshold)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    uint64_t w, c;

    if (!GetFileAttributesExW(item->path, GetFileExInfoStandard, &fa)) {
        DWORD err = GetLastError();
        return !(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
    }
    w = ft64(fa.ftLastWriteTime);
    c = ft64(fa.ftCreationTime);
    if ((w > c ? w : c) >= threshold) {
        return 1;
    }
    if (!(fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (fa.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
    return tree_has_newer(item->path, threshold);
}

/* ---- 실행 ---- */

typedef struct {
    size_t plan_entry;          /* 대상 안의 항목 번호 */
    CleanupItem item;           /* path 소유 */
    wchar_t *root;
    CleanSkip skip;
    CleanupTrashOutcome outcome;
} RunEntry;

typedef struct {
    size_t index;  /* plan 의 대상 번호 */
    unsigned flags;
    wchar_t *app;
    uint64_t bytes;
    RunEntry *entries;
    size_t entry_count;
    /* 결과 */
    size_t moved, quarantined, failed, skipped, cancelled;
    uint64_t moved_bytes;
} RunTarget;

struct CleanPlanRunJob {
    HANDLE thread;
    CleanPlanOps ops;
    clean_plan_notify_fn notify;
    void *ctx;
    volatile LONG cancel;
    CRITICAL_SECTION lock;
    CleanPlanSnapshot snap;
    RunTarget *targets;
    size_t target_count;
    size_t plan_len;
    CleanPlanTotals totals;
};

static void run_notify(CleanPlanRunJob *job, int finished)
{
    if (job->notify != NULL) {
        job->notify(finished, job->ctx);
    }
}

static DWORD WINAPI run_main(LPVOID param)
{
    CleanPlanRunJob *job = param;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CleanupGuard guard;
    FILETIME now_ft;
    uint64_t now, age, threshold;
    size_t i, j, done = 0;

    GetSystemTimeAsFileTime(&now_ft);
    now = ft64(now_ft);
    age = (uint64_t)job->ops.min_age_hours * FT_HOUR;
    threshold = now > age ? now - age : 0;

    /* 1. 영구 삭제(휴지통 비우기): 다른 항목을 옮기기 전에 한다(대상은 맨 앞에 정렬돼 있다). */
    for (i = 0; i < job->target_count; i++) {
        RunTarget *t = &job->targets[i];
        uint32_t code = 0;
        int ok;
        if (!(t->flags & CLEAN_PLAN_PERMANENT)) {
            continue;
        }
        if (job->cancel != 0) {
            t->cancelled++;
            job->totals.was_cancelled = 1;
            continue;
        }
        EnterCriticalSection(&job->lock);
        job->snap.emptying_recycle = 1;
        LeaveCriticalSection(&job->lock);
        run_notify(job, 0);
        ok = job->ops.empty_recycle(&code, job->ops.ctx) == 0;
        job->totals.permanent_requested = 1;
        job->totals.permanent_done = job->totals.permanent_done || ok;
        job->totals.permanent_code = ok ? 0 : code;
        job->totals.permanent_bytes += ok ? t->bytes : 0;
        if (ok) {
            t->moved = 1;
            t->moved_bytes = t->bytes;
        } else {
            t->failed = 1;
        }
        EnterCriticalSection(&job->lock);
        job->snap.emptying_recycle = 0;
        LeaveCriticalSection(&job->lock);
        run_notify(job, 0);
    }

    /* 2. 나머지 항목: 같은 안전 장치(cleanup_trash_one)로 휴지통·격리 폴더로. */
    cleanup_guard_init(&guard);
    cleanup_guard_add_system(&guard);
    for (i = 0; i < job->target_count; i++) {
        RunTarget *t = &job->targets[i];
        int app_running = 0, app_checked = 0;
        for (j = 0; j < t->entry_count; j++) {
            RunEntry *e = &t->entries[j];
            int moved = 0;
            if (job->cancel != 0) {
                e->outcome.status = CLEANUP_TRASH_CANCELLED;
                t->cancelled++;
                job->totals.was_cancelled = 1;
            } else {
                if (!app_checked) {
                    app_checked = 1;
                    app_running = t->app != NULL && job->ops.app_running(t->app, job->ops.ctx);
                }
                if (app_running) {
                    e->skip = CLEAN_SKIP_APP_RUNNING;
                    t->skipped++;
                } else if ((t->flags & CLEAN_PLAN_CHECK_RECENT) &&
                           recently_changed(&e->item, threshold)) {
                    e->skip = CLEAN_SKIP_RECENT;
                    t->skipped++;
                } else {
                    cleanup_trash_one(&guard, e->root, &e->item, &e->outcome);
                    moved = e->outcome.status == CLEANUP_TRASH_MOVED ||
                            e->outcome.status == CLEANUP_TRASH_QUARANTINED;
                    if (moved) {
                        t->moved++;
                        t->quarantined += e->outcome.status == CLEANUP_TRASH_QUARANTINED;
                        t->moved_bytes += e->item.bytes;
                    } else {
                        t->failed++;
                    }
                }
            }
            done++;
            EnterCriticalSection(&job->lock);
            job->snap.done = done;
            if (moved) {
                job->snap.moved++;
                job->snap.quarantined += e->outcome.status == CLEANUP_TRASH_QUARANTINED;
                job->snap.moved_bytes += e->item.bytes;
            } else if (e->outcome.status != CLEANUP_TRASH_CANCELLED) {
                job->snap.failed++;
            }
            LeaveCriticalSection(&job->lock);
            run_notify(job, 0);
        }
    }
    cleanup_guard_free(&guard);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    run_notify(job, 1);
    return 0;
}

static void free_run_targets(CleanPlanRunJob *job)
{
    size_t i, j;

    for (i = 0; i < job->target_count; i++) {
        RunTarget *t = &job->targets[i];
        for (j = 0; j < t->entry_count; j++) {
            free(t->entries[j].item.path);
            free(t->entries[j].root);
            cleanup_trash_outcome_free(&t->entries[j].outcome);
        }
        free(t->entries);
        free(t->app);
    }
    free(job->targets);
    job->targets = NULL;
    job->target_count = 0;
}

/* 영구 삭제가 먼저, 그다음 논리 크기가 큰 대상부터(같으면 계획 순서). */
static int cmp_run_target(const void *a, const void *b)
{
    const RunTarget *x = a, *y = b;
    int px = (x->flags & CLEAN_PLAN_PERMANENT) != 0, py = (y->flags & CLEAN_PLAN_PERMANENT) != 0;

    if (px != py) {
        return py - px;
    }
    if (x->bytes != y->bytes) {
        return x->bytes > y->bytes ? -1 : 1;
    }
    return x->index < y->index ? -1 : x->index > y->index;
}

/* 이번 실행에 넣을 대상인지. */
static int runnable(const CleanPlanTarget *t, const CleanPlanOps *ops)
{
    if (!t->selected || !clean_plan_selectable(t)) {
        return 0;
    }
    return !(t->flags & CLEAN_PLAN_PERMANENT) || ops->allow_permanent;
}

CleanPlanRunJob *clean_plan_run_start(const CleanPlan *plan, const CleanPlanOps *ops,
                                      clean_plan_notify_fn notify, void *ctx)
{
    CleanPlanRunJob *job;
    size_t i, j, n = 0, entries = 0;

    if (plan == NULL) {
        return NULL;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }
    if (ops != NULL) {
        job->ops = *ops;
    }
    if (job->ops.app_running == NULL) {
        job->ops.app_running = clean_plan_app_running_system;
    }
    if (job->ops.empty_recycle == NULL) {
        job->ops.empty_recycle = clean_plan_empty_recycle_system;
    }
    if (job->ops.min_age_hours == 0) {
        job->ops.min_age_hours = DEFAULT_MIN_AGE_HOURS;
    }
    job->notify = notify;
    job->ctx = ctx;
    job->plan_len = plan->len;
    for (i = 0; i < plan->len; i++) {
        n += runnable(&plan->items[i], &job->ops) != 0;
    }
    if (n == 0 || (job->targets = calloc(n, sizeof(*job->targets))) == NULL) {
        free(job);
        return NULL;
    }
    for (i = 0; i < plan->len; i++) {
        const CleanPlanTarget *src = &plan->items[i];
        RunTarget *t;
        if (!runnable(src, &job->ops)) {
            continue;
        }
        t = &job->targets[job->target_count++];
        t->index = i;
        t->flags = src->flags;
        t->bytes = src->bytes;
        if (src->app != NULL && (t->app = path_dup(src->app)) == NULL) {
            goto fail;
        }
        if (src->flags & CLEAN_PLAN_PERMANENT) {
            continue;
        }
        t->entries = calloc(src->entry_count > 0 ? src->entry_count : 1, sizeof(*t->entries));
        if (t->entries == NULL) {
            goto fail;
        }
        for (j = 0; j < src->entry_count; j++) {
            const CleanPlanEntry *se = &src->entries[j];
            RunEntry *e;
            if (!entry_pending(se)) {
                continue; /* 이미 옮겼거나 거부·건너뛴 항목 */
            }
            e = &t->entries[t->entry_count++];
            e->plan_entry = j;
            e->item = se->item;
            e->item.path = path_dup(se->item.path);
            e->root = path_dup(se->root);
            if (e->item.path == NULL || e->root == NULL) {
                goto fail;
            }
            entries++;
        }
    }
    qsort(job->targets, job->target_count, sizeof(*job->targets), cmp_run_target);
    job->totals.targets = job->target_count;
    job->snap.total = entries;
    InitializeCriticalSection(&job->lock);
    job->thread = CreateThread(NULL, 0, run_main, job, 0, NULL);
    if (job->thread == NULL) {
        DeleteCriticalSection(&job->lock);
        goto fail;
    }
    return job;
fail:
    free_run_targets(job);
    free(job);
    return NULL;
}

void clean_plan_run_cancel(CleanPlanRunJob *job)
{
    if (job != NULL) {
        InterlockedExchange(&job->cancel, 1);
    }
}

void clean_plan_run_progress(CleanPlanRunJob *job, CleanPlanSnapshot *out)
{
    EnterCriticalSection(&job->lock);
    *out = job->snap;
    LeaveCriticalSection(&job->lock);
    out->cancel_requested = job->cancel != 0;
}

void clean_plan_run_finish(CleanPlanRunJob *job, CleanPlan *plan, CleanPlanTotals *totals)
{
    size_t i, j;

    if (job == NULL) {
        return;
    }
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    DeleteCriticalSection(&job->lock);
    for (i = 0; i < job->target_count; i++) {
        RunTarget *rt = &job->targets[i];
        CleanPlanTarget *t;
        if (!(rt->flags & CLEAN_PLAN_PERMANENT)) {
            /* 영구 삭제(휴지통 비우기)는 permanent_* 로 따로 센다. */
            job->totals.moved += rt->moved;
            job->totals.quarantined += rt->quarantined;
            job->totals.failed += rt->failed;
            job->totals.skipped += rt->skipped;
            job->totals.moved_bytes += rt->moved_bytes;
        }
        job->totals.cancelled += rt->cancelled;
        if (plan == NULL || plan->len != job->plan_len || rt->index >= plan->len) {
            continue;
        }
        t = &plan->items[rt->index];
        t->ran = 1;
        t->moved = rt->moved;
        t->quarantined = rt->quarantined;
        t->failed = rt->failed;
        t->skipped = rt->skipped;
        t->cancelled = rt->cancelled;
        t->moved_bytes = rt->moved_bytes;
        for (j = 0; j < rt->entry_count; j++) {
            RunEntry *e = &rt->entries[j];
            CleanPlanEntry *pe;
            if (e->plan_entry >= t->entry_count) {
                continue;
            }
            pe = &t->entries[e->plan_entry];
            cleanup_trash_outcome_free(&pe->outcome);
            pe->outcome = e->outcome;
            pe->skip = e->skip;
            memset(&e->outcome, 0, sizeof(e->outcome)); /* 소유권을 넘겼다 */
        }
        /* 실패·취소한 항목만 남았으면 다시 시도할 수 있게 선택을 남긴다. */
        if (!clean_plan_selectable(t)) {
            t->selected = 0;
        }
    }
    if (totals != NULL) {
        *totals = job->totals;
    }
    free_run_targets(job);
    free(job);
}

int clean_plan_run(CleanPlan *plan, const CleanPlanOps *ops, CleanPlanTotals *totals)
{
    CleanPlanRunJob *job = clean_plan_run_start(plan, ops, NULL, NULL);

    if (totals != NULL) {
        memset(totals, 0, sizeof(*totals));
    }
    if (job == NULL) {
        return -1;
    }
    clean_plan_run_finish(job, plan, totals);
    return 0;
}
