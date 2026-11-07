#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "path.h"
#include "project_info.h"

#define FT_DAY 864000000000ull           /* FILETIME 하루(100ns 단위) */
#define UNIX_EPOCH_FT 116444736000000000ull
#define GIT_TIMEOUT_MS 15000
#define GIT_OUT_CAP 65536
#define WALK_CANCEL_INTERVAL 256

/* ---- 이름 ---- */

const wchar_t *project_recovery_text(ProjectRecovery r)
{
    switch (r) {
    case PROJECT_RECOVER_NONE:
        return L"다시 생기지 않음";
    case PROJECT_RECOVER_REBUILD:
        return L"빌드하면 다시 생김";
    case PROJECT_RECOVER_LOCKED:
        return L"lockfile로 같은 버전 재설치";
    case PROJECT_RECOVER_UNLOCKED:
        return L"재설치 가능(버전이 바뀔 수 있음)";
    default:
        return L"";
    }
}

const wchar_t *project_recovery_name(ProjectRecovery r)
{
    switch (r) {
    case PROJECT_RECOVER_NONE:
        return L"none";
    case PROJECT_RECOVER_REBUILD:
        return L"rebuild";
    case PROJECT_RECOVER_LOCKED:
        return L"locked";
    case PROJECT_RECOVER_UNLOCKED:
        return L"unlocked";
    default:
        return L"unknown";
    }
}

const wchar_t *project_git_source_name(ProjectGitSource s)
{
    switch (s) {
    case PROJECT_GIT_NONE:
        return L"none";
    case PROJECT_GIT_EXE:
        return L"git";
    case PROJECT_GIT_DIRECT:
        return L"direct";
    default:
        return L"unknown";
    }
}

uint64_t project_unix_to_filetime(int64_t seconds)
{
    return seconds <= 0 ? 0 : (uint64_t)seconds * 10000000ull + UNIX_EPOCH_FT;
}

/* ---- 목록 ---- */

void project_list_init(ProjectList *list)
{
    memset(list, 0, sizeof(*list));
}

void project_list_free(ProjectList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->len; i++) {
        free(list->items[i].root);
        free(list->items[i].branch);
    }
    free(list->items);
    free(list->item_project);
    free(list->item_recovery);
    project_list_init(list);
}

/* ---- 경로 도우미 ---- */

/* path 의 부모 폴더(malloc). 볼륨 루트면 NULL. */
static wchar_t *parent_of(const wchar_t *path)
{
    size_t n = wcslen(path);
    wchar_t *out;

    while (n > 0 && path_is_separator(path[n - 1])) {
        n--;
    }
    while (n > 0 && !path_is_separator(path[n - 1])) {
        n--;
    }
    if (n == 0) {
        return NULL;
    }
    /* "C:\" 는 남기고 그 밖에는 끝의 '\' 를 뗀다. */
    if (n > 3) {
        n--;
    }
    out = malloc((n + 1) * sizeof(*out));
    if (out != NULL) {
        memcpy(out, path, n * sizeof(*out));
        out[n] = L'\0';
    }
    return out;
}

static int exists(const wchar_t *path)
{
    return path != NULL && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int is_dir(const wchar_t *path)
{
    DWORD a = path != NULL ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* 작은 텍스트 파일 전체 또는 끝부분(tail 이 0 이 아니면 마지막 tail 바이트)을 읽는다(malloc, NUL 종료). */
static char *read_file(const wchar_t *path, DWORD tail)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size, pos;
    DWORD want, got = 0;
    char *buf;

    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    if (!GetFileSizeEx(h, &size) || (tail == 0 && size.QuadPart > (8LL << 20))) {
        CloseHandle(h);
        return NULL;
    }
    want = (DWORD)size.QuadPart;
    if (tail != 0 && size.QuadPart > (LONGLONG)tail) {
        pos.QuadPart = size.QuadPart - tail;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        want = tail;
    }
    buf = malloc((size_t)want + 1);
    if (buf != NULL && (!ReadFile(h, buf, want, &got, NULL))) {
        free(buf);
        buf = NULL;
    }
    CloseHandle(h);
    if (buf != NULL) {
        buf[got] = '\0';
    }
    return buf;
}

static wchar_t *join(const wchar_t *a, const wchar_t *b)
{
    return path_join(a, b);
}

/* dir\.git 이 있으면 1(폴더 또는 gitdir 파일). */
static int has_dot_git(const wchar_t *dir)
{
    wchar_t *p = join(dir, L".git");
    int r = exists(p);
    free(p);
    return r;
}

/* ---- git.exe ---- */

int project_find_git(wchar_t *buf, size_t len)
{
    DWORD n = GetEnvironmentVariableW(L"BYTESIEVE_GIT", buf, (DWORD)len);

    if (n > 0 && n < len) {
        if (_wcsicmp(buf, L"none") == 0) {
            buf[0] = L'\0';
            return 0;
        }
        return exists(buf);
    }
    n = SearchPathW(NULL, L"git.exe", NULL, (DWORD)len, buf, NULL);
    if (n == 0 || n >= len) {
        buf[0] = L'\0';
        return 0;
    }
    return 1;
}

/*
 * git 을 root 에서 실행하고 표준 출력(최대 cap-1 바이트, NUL 종료)을 out 에 받는다. 표준 입력·오류는 NUL.
 * 실행했으면 1(*exit 에 종료 코드), 실행하지 못했으면 0. 시간 제한을 넘으면 끝내고 *exit = 0xFFFFFFFF.
 */
static int run_git(const wchar_t *git, const wchar_t *root, const wchar_t *args, char *out,
                   size_t cap, DWORD *exit_code)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE rd = NULL, wr = NULL, nul;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *cmd;
    size_t n, used = 0;
    ULONGLONG start;
    int dot = 0, ok;

    *exit_code = 0xFFFFFFFFu;
    out[0] = '\0';
    n = wcslen(root);
    /* "C:\" 처럼 '\' 로 끝나면 따옴표 앞 '\' 가 이스케이프가 되지 않게 "." 을 붙인다. */
    if (n > 0 && path_is_separator(root[n - 1])) {
        dot = 1;
    }
    n = wcslen(git) + wcslen(root) + wcslen(args) + 96;
    cmd = malloc(n * sizeof(*cmd));
    if (cmd == NULL) {
        return 0;
    }
    swprintf(cmd, n, L"\"%ls\" --no-optional-locks -c core.fsmonitor=false -C \"%ls%ls\" %ls", git,
             root, dot ? L"." : L"", args);
    nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE || !CreatePipe(&rd, &wr, &sa, 0)) {
        if (nul != INVALID_HANDLE_VALUE) {
            CloseHandle(nul);
        }
        free(cmd);
        return 0;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = nul;
    memset(&pi, 0, sizeof(pi));
    ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    CloseHandle(wr);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return 0;
    }
    start = GetTickCount64();
    for (;;) {
        DWORD avail = 0, got = 0;
        char drop[4096];
        if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            if (used + 1 < cap) {
                DWORD room = (DWORD)(cap - 1 - used);
                if (ReadFile(rd, out + used, avail < room ? avail : room, &got, NULL)) {
                    used += got;
                }
            } else {
                ReadFile(rd, drop, avail < sizeof(drop) ? avail : (DWORD)sizeof(drop), &got, NULL);
            }
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
            /* 끝났다: 남은 출력을 마저 읽는다. */
            while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail > 0 && used + 1 < cap) {
                DWORD room = (DWORD)(cap - 1 - used);
                if (!ReadFile(rd, out + used, avail < room ? avail : room, &got, NULL)) {
                    break;
                }
                used += got;
            }
            GetExitCodeProcess(pi.hProcess, exit_code);
            break;
        }
        if (GetTickCount64() - start > GIT_TIMEOUT_MS) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            *exit_code = 0xFFFFFFFFu;
            break;
        }
    }
    out[used] = '\0';
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 1;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static wchar_t *utf8_dup(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;

    if (n <= 0) {
        return NULL;
    }
    w = malloc((size_t)n * sizeof(*w));
    if (w != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    }
    return w;
}

/* git.exe 로 상태를 채운다. 저장소로 열지 못하면 0(직접 읽기로 대신한다). */
static int query_git_exe(const wchar_t *git, ProjectInfo *p, char *buf)
{
    DWORD code;

    if (!run_git(git, p->root, L"rev-parse --git-dir", buf, GIT_OUT_CAP, &code) || code != 0) {
        p->git_error = code;
        return 0;
    }
    p->git_source = PROJECT_GIT_EXE;
    if (run_git(git, p->root, L"rev-parse --abbrev-ref HEAD", buf, GIT_OUT_CAP, &code) &&
        code == 0) {
        trim(buf);
        p->branch = utf8_dup(buf);
    } else if (run_git(git, p->root, L"symbolic-ref --short -q HEAD", buf, GIT_OUT_CAP, &code) &&
               code == 0) {
        /* 아직 커밋이 없는 브랜치(rev-parse 는 실패한다) */
        trim(buf);
        p->branch = utf8_dup(buf);
    }
    p->last_commit = 0;
    if (run_git(git, p->root, L"log -1 --format=%ct", buf, GIT_OUT_CAP, &code) && code == 0) {
        p->last_commit = project_unix_to_filetime(_strtoi64(buf, NULL, 10));
    }
    p->changes = -1;
    if (run_git(git, p->root, L"status --porcelain -z --untracked-files=normal", buf, GIT_OUT_CAP,
                &code) &&
        code == 0) {
        /* "XY path\0", 이름 바꾸기·복사는 원래 이름이 한 칸 더 붙는다("R  new\0old\0"). */
        const char *s = buf;
        int count = 0;
        while (*s != '\0') {
            int rename = s[0] == 'R' || s[0] == 'C';
            count++;
            s += strlen(s) + 1;
            if (rename && *s != '\0') {
                s += strlen(s) + 1;
            }
        }
        p->changes = count;
    }
    p->has_remote = -1;
    if (run_git(git, p->root, L"for-each-ref --count=1 --format=%(refname) refs/remotes", buf,
                GIT_OUT_CAP, &code) &&
        code == 0) {
        trim(buf);
        p->has_remote = buf[0] != '\0';
    }
    p->unpushed = -1;
    if (p->last_commit == 0) {
        p->unpushed = 0; /* 커밋이 없다 */
    } else if (p->has_remote >= 0 &&
               run_git(git, p->root,
                       p->has_remote ? L"rev-list --count HEAD --not --remotes"
                                     : L"rev-list --count HEAD",
                       buf, GIT_OUT_CAP, &code) &&
               code == 0) {
        p->unpushed = atoi(buf);
    }
    return 1;
}

/* ---- .git 직접 읽기 ---- */

/* 워크트리의 .git 파일("gitdir: ...")을 따라가 git 폴더를, commondir 이 있으면 공용 폴더를 정한다. */
static int git_dirs(const wchar_t *root, wchar_t **gitdir, wchar_t **common)
{
    wchar_t *dot = join(root, L".git");
    char *text, *line;

    *gitdir = *common = NULL;
    if (dot == NULL) {
        return 0;
    }
    if (is_dir(dot)) {
        *gitdir = dot;
    } else {
        text = read_file(dot, 0);
        free(dot);
        if (text == NULL || strncmp(text, "gitdir:", 7) != 0) {
            free(text);
            return 0;
        }
        line = text + 7;
        while (*line == ' ') {
            line++;
        }
        trim(line);
        {
            wchar_t *w = utf8_dup(line);
            if (w == NULL) {
                free(text);
                return 0;
            }
            for (wchar_t *c = w; *c; c++) {
                if (*c == L'/') {
                    *c = L'\\';
                }
            }
            *gitdir = (w[0] != L'\0' && (w[1] == L':' || w[0] == L'\\')) ? w : join(root, w);
            if (*gitdir != w) {
                free(w);
            }
        }
        free(text);
    }
    if (*gitdir == NULL) {
        return 0;
    }
    {
        wchar_t *cd = join(*gitdir, L"commondir");
        char *t = cd != NULL ? read_file(cd, 0) : NULL;
        free(cd);
        if (t != NULL) {
            wchar_t *w;
            trim(t);
            w = utf8_dup(t);
            if (w != NULL) {
                for (wchar_t *c = w; *c; c++) {
                    if (*c == L'/') {
                        *c = L'\\';
                    }
                }
                *common = (w[1] == L':') ? w : join(*gitdir, w);
                if (*common != w) {
                    free(w);
                }
            }
            free(t);
        }
    }
    if (*common == NULL) {
        *common = path_dup(*gitdir);
    }
    return *common != NULL;
}

/* ref(예: "refs/heads/main")의 해시(40자)를 loose 파일 또는 packed-refs 에서 찾는다. */
static int read_ref(const wchar_t *common, const char *ref, char hash[41])
{
    wchar_t wref[512], *path;
    char *text, *s;
    int found = 0;
    size_t rl = strlen(ref);

    MultiByteToWideChar(CP_UTF8, 0, ref, -1, wref, 512);
    for (wchar_t *c = wref; *c; c++) {
        if (*c == L'/') {
            *c = L'\\';
        }
    }
    path = join(common, wref);
    text = path != NULL ? read_file(path, 0) : NULL;
    free(path);
    if (text != NULL) {
        trim(text);
        if (strlen(text) >= 40) {
            memcpy(hash, text, 40);
            hash[40] = '\0';
            found = 1;
        }
        free(text);
        if (found) {
            return 1;
        }
    }
    path = join(common, L"packed-refs");
    text = path != NULL ? read_file(path, 0) : NULL;
    free(path);
    for (s = text; s != NULL && *s != '\0';) {
        char *eol = strchr(s, '\n');
        size_t len = eol != NULL ? (size_t)(eol - s) : strlen(s);
        if (len > 41 && s[40] == ' ' && len - 41 >= rl && strncmp(s + 41, ref, rl) == 0 &&
            (len - 41 == rl || s[41 + rl] == '\r')) {
            memcpy(hash, s, 40);
            hash[40] = '\0';
            found = 1;
            break;
        }
        s = eol != NULL ? eol + 1 : s + len;
    }
    free(text);
    return found;
}

static void query_git_direct(ProjectInfo *p)
{
    wchar_t *gitdir, *common, *path;
    char *text, branch[256] = "", local[41], remote[41];

    if (!git_dirs(p->root, &gitdir, &common)) {
        free(gitdir);
        free(common);
        return;
    }
    p->git_source = PROJECT_GIT_DIRECT;
    p->changes = -1;
    p->unpushed = -1;
    p->has_remote = -1;
    /* HEAD → 브랜치 */
    path = join(gitdir, L"HEAD");
    text = path != NULL ? read_file(path, 0) : NULL;
    free(path);
    if (text != NULL) {
        trim(text);
        if (strncmp(text, "ref: refs/heads/", 16) == 0) {
            strncpy_s(branch, sizeof(branch), text + 16, _TRUNCATE);
            p->branch = utf8_dup(branch);
        } else if (strlen(text) >= 40) {
            p->branch = path_dup(L"HEAD");
        }
        free(text);
    }
    /* logs/HEAD 마지막 줄: "<old> <new> Name <mail> <seconds> <tz>\t<message>" */
    path = join(gitdir, L"logs\\HEAD");
    text = path != NULL ? read_file(path, 65536) : NULL;
    free(path);
    if (text != NULL) {
        char *last = NULL, *s = text;
        trim(text);
        while ((s = strchr(s, '\n')) != NULL) {
            last = ++s;
        }
        if (last == NULL) {
            last = text;
        }
        {
            char *tab = strchr(last, '\t'), *gt = NULL, *c;
            for (c = last; tab != NULL && c < tab; c++) {
                if (*c == '>') {
                    gt = c;
                }
            }
            if (gt != NULL) {
                p->last_commit = project_unix_to_filetime(_strtoi64(gt + 1, NULL, 10));
            }
        }
        free(text);
    }
    /* 원격: refs/remotes 아래 무엇이든 있으면 원격이 있다. 같은 이름의 브랜치와 비교한다. */
    {
        wchar_t *remotes = join(common, L"refs\\remotes"), *pattern;
        WIN32_FIND_DATAW fd;
        HANDLE h;
        int any = 0, equal = 0, differs = 0;
        char ref[512];
        memset(&fd, 0, sizeof(fd));
        pattern = remotes != NULL ? join(remotes, L"*") : NULL;
        h = pattern != NULL ? FindFirstFileW(pattern, &fd) : INVALID_HANDLE_VALUE;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char name[256];
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                    continue;
                }
                any = 1;
                WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL);
                snprintf(ref, sizeof(ref), "refs/remotes/%s/%s", name, branch);
                if (branch[0] != '\0' && read_ref(common, ref, remote)) {
                    char lref[300];
                    snprintf(lref, sizeof(lref), "refs/heads/%s", branch);
                    if (read_ref(common, lref, local)) {
                        if (strcmp(local, remote) == 0) {
                            equal = 1;
                        } else {
                            differs = 1;
                        }
                    }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        free(pattern);
        free(remotes);
        if (!any) {
            char *packed;
            path = join(common, L"packed-refs");
            packed = path != NULL ? read_file(path, 0) : NULL;
            free(path);
            any = packed != NULL && strstr(packed, " refs/remotes/") != NULL;
            free(packed);
        }
        p->has_remote = any;
        if (equal) {
            p->unpushed = 0;
        } else if (differs) {
            p->remote_differs = 1;
        }
    }
    free(gitdir);
    free(common);
}

/* ---- 마지막 수정 시각 ---- */

typedef struct {
    const ProjectList *list;
    size_t self;
    int (*cancel)(void *ctx);
    void *ctx;
} WalkCtx;

static uint64_t ft64(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

/* 다른 프로젝트의 루트이면 1(그 안은 그 프로젝트가 센다). */
static int other_root(const WalkCtx *w, const wchar_t *path)
{
    size_t i;

    for (i = 0; i < w->list->len; i++) {
        if (i != w->self && _wcsicmp(w->list->items[i].root, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void walk_modified(ProjectInfo *p, WalkCtx *w)
{
    wchar_t **stack;
    size_t len = 0, cap = 32, seen = 0;

    stack = malloc(cap * sizeof(*stack));
    if (stack == NULL || (stack[0] = path_dup(p->root)) == NULL) {
        free(stack);
        p->modified_incomplete = 1;
        return;
    }
    len = 1;
    while (len > 0) {
        wchar_t *cur = stack[--len], *pattern = join(cur, L"*"), *ext;
        WIN32_FIND_DATAW fd;
        HANDLE h;

        memset(&fd, 0, sizeof(fd));
        ext = pattern != NULL ? path_to_extended(pattern) : NULL;
        h = ext != NULL ? FindFirstFileExW(ext, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
                                          FIND_FIRST_EX_LARGE_FETCH)
                        : INVALID_HANDLE_VALUE;
        free(pattern);
        free(ext);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) {
                p->modified_incomplete = 1;
            }
            free(cur);
            continue;
        }
        do {
            CleanupDirRule rule;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            if (++seen > PROJECT_WALK_LIMIT ||
                (seen % WALK_CANCEL_INTERVAL == 0 && w->cancel != NULL && w->cancel(w->ctx))) {
                p->modified_incomplete = 1;
                break;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                wchar_t *child;
                if (_wcsicmp(fd.cFileName, L".git") == 0 ||
                    cleanup_is_quarantine_name(fd.cFileName) ||
                    cleanup_dir_rule(fd.cFileName, &rule)) {
                    continue;
                }
                child = join(cur, fd.cFileName);
                if (child == NULL || other_root(w, child)) {
                    free(child);
                    continue;
                }
                if (len == cap) {
                    wchar_t **grown = realloc(stack, cap * 2 * sizeof(*stack));
                    if (grown == NULL) {
                        free(child);
                        p->modified_incomplete = 1;
                        continue;
                    }
                    stack = grown;
                    cap *= 2;
                }
                stack[len++] = child;
            } else {
                uint64_t t = ft64(fd.ftLastWriteTime);
                if (t > p->last_modified) {
                    p->last_modified = t;
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        free(cur);
        if (p->modified_incomplete && seen > PROJECT_WALK_LIMIT) {
            break;
        }
    }
    while (len > 0) {
        free(stack[--len]);
    }
    free(stack);
}

/* ---- 복구 가능성 ---- */

ProjectRecovery project_item_recovery(const CleanupItem *item)
{
    static const wchar_t *const locks[] = {L"package-lock.json", L"npm-shrinkwrap.json",
                                           L"yarn.lock",         L"pnpm-lock.yaml",
                                           L"bun.lockb",         L"bun.lock"};
    wchar_t *parent;
    size_t i;
    int locked = 0;

    if (item == NULL) {
        return PROJECT_RECOVER_NONE;
    }
    switch (item->category) {
    case CLEANUP_NODE_MODULES:
        parent = parent_of(item->path);
        for (i = 0; parent != NULL && i < sizeof(locks) / sizeof(locks[0]) && !locked; i++) {
            wchar_t *p = join(parent, locks[i]);
            locked = exists(p);
            free(p);
        }
        free(parent);
        return locked ? PROJECT_RECOVER_LOCKED : PROJECT_RECOVER_UNLOCKED;
    case CLEANUP_BUILD:
    case CLEANUP_CACHE:
        return PROJECT_RECOVER_REBUILD;
    default:
        return PROJECT_RECOVER_NONE;
    }
}

/* ---- 분석 ---- */

/* 후보 경로의 프로젝트 루트와 Git 여부. 찾은 결과는 memo 에 둔다(같은 부모 폴더가 많다). */
typedef struct {
    wchar_t *dir;
    wchar_t *root;
    int is_git;
} RootMemo;

static wchar_t *find_root(const wchar_t *item_path, int *is_git, RootMemo **memo, size_t *memo_len)
{
    wchar_t *start = parent_of(item_path), *cur;
    size_t i;

    *is_git = 0;
    if (start == NULL) {
        return NULL;
    }
    for (i = 0; i < *memo_len; i++) {
        if (_wcsicmp((*memo)[i].dir, start) == 0) {
            free(start);
            *is_git = (*memo)[i].is_git;
            return path_dup((*memo)[i].root);
        }
    }
    cur = path_dup(start);
    while (cur != NULL) {
        wchar_t *up;
        if (has_dot_git(cur)) {
            *is_git = 1;
            break;
        }
        up = parent_of(cur);
        free(cur);
        cur = up;
    }
    if (cur == NULL) {
        cur = path_dup(start); /* Git 이 아니면 후보가 든 폴더 */
    }
    {
        RootMemo *grown = realloc(*memo, (*memo_len + 1) * sizeof(**memo));
        if (grown != NULL && cur != NULL) {
            *memo = grown;
            (*memo)[*memo_len].dir = start;
            (*memo)[*memo_len].root = path_dup(cur);
            (*memo)[*memo_len].is_git = *is_git;
            if ((*memo)[*memo_len].root != NULL) {
                (*memo_len)++;
                start = NULL;
            }
        }
    }
    free(start);
    return cur;
}

static size_t add_project(ProjectList *list, wchar_t *root, int is_git)
{
    size_t i;
    ProjectInfo *p;

    for (i = 0; i < list->len; i++) {
        if (_wcsicmp(list->items[i].root, root) == 0) {
            free(root);
            return i;
        }
    }
    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        ProjectInfo *grown = realloc(list->items, cap * sizeof(*grown));
        if (grown == NULL) {
            free(root);
            return (size_t)-1;
        }
        list->items = grown;
        list->cap = cap;
    }
    p = &list->items[list->len];
    memset(p, 0, sizeof(*p));
    p->root = root;
    p->is_git = is_git;
    p->changes = -1;
    p->unpushed = -1;
    p->has_remote = -1;
    return list->len++;
}

int project_analyze(const CleanupResult *r, const ProjectOptions *opts, ProjectList *out,
                    int (*cancel)(void *ctx), project_progress_fn progress, void *ctx)
{
    ProjectOptions o;
    RootMemo *memo = NULL;
    size_t memo_len = 0, i;
    wchar_t git[MAX_PATH];
    char *buf = NULL;
    int rc = 0;

    project_list_free(out);
    memset(&o, 0, sizeof(o));
    if (opts != NULL) {
        o = *opts;
    }
    out->dormant_days = o.dormant_days != 0 ? o.dormant_days : PROJECT_DEFAULT_DORMANT_DAYS;
    if (o.now == 0) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        o.now = ft64(ft);
    }
    out->now = o.now;
    out->git_available = !o.no_git && project_find_git(git, MAX_PATH);
    if (r == NULL || r->len == 0) {
        return 0;
    }
    out->item_count = r->len;
    out->item_project = malloc(r->len * sizeof(*out->item_project));
    out->item_recovery = malloc(r->len * sizeof(*out->item_recovery));
    buf = malloc(GIT_OUT_CAP);
    if (out->item_project == NULL || out->item_recovery == NULL || buf == NULL) {
        rc = -1;
        goto done;
    }
    /* 1. 후보 → 프로젝트 */
    for (i = 0; i < r->len; i++) {
        int is_git;
        wchar_t *root = find_root(r->items[i].path, &is_git, &memo, &memo_len);
        size_t pi = root != NULL ? add_project(out, root, is_git) : (size_t)-1;
        if (pi == (size_t)-1) {
            rc = -1;
            goto done;
        }
        out->item_project[i] = pi;
        out->items[pi].candidates++;
        out->item_recovery[i] = project_item_recovery(&r->items[i]);
    }
    /* 2. 프로젝트마다 Git·마지막 수정·휴면 */
    for (i = 0; i < out->len; i++) {
        ProjectInfo *p = &out->items[i];
        WalkCtx w;
        if (cancel != NULL && cancel(ctx)) {
            out->cancelled = 1;
            break;
        }
        if (p->is_git) {
            if (!out->git_available || !query_git_exe(git, p, buf)) {
                query_git_direct(p);
            }
        }
        w.list = out;
        w.self = i;
        w.cancel = cancel;
        w.ctx = ctx;
        walk_modified(p, &w);
        p->last_activity = p->last_commit > p->last_modified ? p->last_commit : p->last_modified;
        if (p->last_activity != 0 && out->now > p->last_activity) {
            p->age_days = (unsigned)((out->now - p->last_activity) / FT_DAY);
        }
        p->dormant = p->last_activity != 0 && p->age_days >= out->dormant_days;
        if (progress != NULL) {
            progress(i + 1, out->len, ctx);
        }
    }
done:
    for (i = 0; i < memo_len; i++) {
        free(memo[i].dir);
        free(memo[i].root);
    }
    free(memo);
    free(buf);
    if (rc != 0) {
        project_list_free(out);
    }
    return rc;
}
