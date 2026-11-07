#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "devenv_history.h"
#include "path.h"

#define HEADER "# bytesieve devenv history v1\n"
#define MAX_FILE_BYTES (16u << 20)

void devhist_init(DevHistory *h)
{
    memset(h, 0, sizeof(*h));
}

void devhist_free(DevHistory *h)
{
    size_t i;

    if (h == NULL) {
        return;
    }
    for (i = 0; i < h->len; i++) {
        free(h->items[i].id);
        free(h->items[i].path);
    }
    free(h->items);
    devhist_init(h);
}

wchar_t *devhist_file_in(const wchar_t *dir)
{
    return dir != NULL ? path_join(dir, DEVHIST_FILE_NAME) : NULL;
}

static int push(DevHistory *h, uint64_t time, wchar_t *id, wchar_t *path, uint64_t bytes)
{
    if (h->len == h->cap) {
        size_t cap = h->cap ? h->cap * 2 : 64;
        DevRecord *grown = realloc(h->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        h->items = grown;
        h->cap = cap;
    }
    h->items[h->len].time = time;
    h->items[h->len].id = id;
    h->items[h->len].path = path;
    h->items[h->len].bytes = bytes;
    h->len++;
    return 0;
}

static wchar_t *utf8_to_wide(const char *s, int n)
{
    int w = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, NULL, 0);
    wchar_t *out;

    if (w <= 0) {
        return NULL;
    }
    out = malloc(((size_t)w + 1) * sizeof(*out));
    if (out != NULL) {
        MultiByteToWideChar(CP_UTF8, 0, s, n, out, w);
        out[w] = L'\0';
    }
    return out;
}

int devhist_load(DevHistory *h, const wchar_t *file)
{
    HANDLE f;
    LARGE_INTEGER size;
    char *text, *line, *next;
    DWORD got = 0;
    int rc = 0;

    devhist_free(h);
    f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0; /* 아직 기록이 없다 */
    }
    if (!GetFileSizeEx(f, &size) || size.QuadPart > MAX_FILE_BYTES) {
        CloseHandle(f);
        return 0;
    }
    text = malloc((size_t)size.QuadPart + 1);
    if (text == NULL) {
        CloseHandle(f);
        return -1;
    }
    if (!ReadFile(f, text, (DWORD)size.QuadPart, &got, NULL)) {
        got = 0;
    }
    CloseHandle(f);
    text[got] = '\0';
    for (line = text; line != NULL && *line != '\0' && rc == 0; line = next) {
        char *t1, *t2, *t3, *end;
        unsigned long long time, bytes;
        wchar_t *id, *path;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\r') {
            continue;
        }
        t1 = strchr(line, '\t');
        t2 = t1 != NULL ? strchr(t1 + 1, '\t') : NULL;
        t3 = t2 != NULL ? strchr(t2 + 1, '\t') : NULL;
        if (t3 == NULL) {
            h->skipped++;
            continue;
        }
        time = _strtoui64(line, &end, 10);
        if (end != t1 || time == 0) {
            h->skipped++;
            continue;
        }
        bytes = _strtoui64(t2 + 1, &end, 10);
        if (end != t3) {
            h->skipped++;
            continue;
        }
        end = t3 + 1 + strlen(t3 + 1);
        while (end > t3 + 1 && (end[-1] == '\r' || end[-1] == '\n')) {
            *--end = '\0';
        }
        id = utf8_to_wide(t1 + 1, (int)(t2 - t1 - 1));
        path = utf8_to_wide(t3 + 1, (int)strlen(t3 + 1));
        if (id == NULL || path == NULL || id[0] == L'\0' || path[0] == L'\0') {
            free(id);
            free(path);
            h->skipped++;
            continue;
        }
        if (push(h, time, id, path, bytes) != 0) {
            free(id);
            free(path);
            rc = -1;
        }
    }
    free(text);
    if (rc != 0) {
        devhist_free(h);
    }
    return rc;
}

static int write_all(HANDLE f, const char *s, size_t n)
{
    DWORD w = 0;
    return WriteFile(f, s, (DWORD)n, &w, NULL) && w == n;
}

static int write_wide(HANDLE f, const wchar_t *s)
{
    char buf[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, (int)sizeof(buf), NULL, NULL);
    size_t i;

    if (n <= 1) {
        return n == 1;
    }
    /* 구분자가 섞이면 줄이 깨지므로 탭·줄바꿈은 공백으로 */
    for (i = 0; i < (size_t)n - 1; i++) {
        if (buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = ' ';
        }
    }
    return write_all(f, buf, (size_t)n - 1);
}

int devhist_save(const DevHistory *h, const wchar_t *file, unsigned long *error)
{
    size_t n = wcslen(file), i;
    wchar_t *tmp = malloc((n + 8) * sizeof(*tmp));
    HANDLE f;
    int ok = 1;

    if (error != NULL) {
        *error = 0;
    }
    if (tmp == NULL) {
        if (error != NULL) {
            *error = ERROR_NOT_ENOUGH_MEMORY;
        }
        return -1;
    }
    swprintf(tmp, n + 8, L"%ls.tmp", file);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        if (error != NULL) {
            *error = GetLastError();
        }
        free(tmp);
        return -1;
    }
    ok = write_all(f, HEADER, strlen(HEADER));
    for (i = 0; ok && i < h->len; i++) {
        char num[64];
        const DevRecord *r = &h->items[i];
        snprintf(num, sizeof(num), "%llu\t", (unsigned long long)r->time);
        ok = write_all(f, num, strlen(num)) && write_wide(f, r->id);
        snprintf(num, sizeof(num), "\t%llu\t", (unsigned long long)r->bytes);
        ok = ok && write_all(f, num, strlen(num)) && write_wide(f, r->path) &&
             write_all(f, "\n", 1);
    }
    ok = ok && FlushFileBuffers(f);
    if (!ok && error != NULL) {
        *error = GetLastError();
    }
    CloseHandle(f);
    if (ok && !MoveFileExW(tmp, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = 0;
        if (error != NULL) {
            *error = GetLastError();
        }
    }
    if (!ok) {
        DeleteFileW(tmp);
    }
    free(tmp);
    return ok ? 0 : -1;
}

static int same_key(const DevRecord *r, const wchar_t *id, const wchar_t *path)
{
    return _wcsicmp(r->id, id) == 0 && _wcsicmp(r->path, path) == 0;
}

int devhist_add(DevHistory *h, uint64_t time, const wchar_t *id, const wchar_t *path,
                uint64_t bytes)
{
    wchar_t *i2, *p2;
    size_t i, count = 0, oldest = (size_t)-1;

    if (id == NULL || path == NULL) {
        return -1;
    }
    for (i = 0; i < h->len; i++) {
        if (same_key(&h->items[i], id, path)) {
            if (count++ == 0) {
                oldest = i;
            }
        }
    }
    if (count >= DEVHIST_KEEP && oldest != (size_t)-1) {
        free(h->items[oldest].id);
        free(h->items[oldest].path);
        memmove(&h->items[oldest], &h->items[oldest + 1],
                (h->len - oldest - 1) * sizeof(h->items[0]));
        h->len--;
    }
    i2 = path_dup(id);
    p2 = path_dup(path);
    if (i2 == NULL || p2 == NULL || push(h, time, i2, p2, bytes) != 0) {
        free(i2);
        free(p2);
        return -1;
    }
    return 0;
}

const DevRecord *devhist_latest(const DevHistory *h, const wchar_t *id, const wchar_t *path)
{
    const DevRecord *best = NULL;
    size_t i;

    for (i = 0; h != NULL && id != NULL && path != NULL && i < h->len; i++) {
        if (same_key(&h->items[i], id, path) && (best == NULL || h->items[i].time >= best->time)) {
            best = &h->items[i];
        }
    }
    return best;
}
