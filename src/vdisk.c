#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "path.h"
#include "vdisk.h"

#define LXSS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"

const wchar_t *vdisk_kind_name(VdiskKind kind)
{
    return kind == VDISK_DOCKER ? L"docker" : L"wsl";
}

void vdisk_list_init(VdiskList *list)
{
    memset(list, 0, sizeof(*list));
}

void vdisk_list_free(VdiskList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->len; i++) {
        free(list->items[i].name);
        free(list->items[i].path);
    }
    free(list->items);
    vdisk_list_init(list);
}

static uint64_t ft64(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

/* path 의 가상 디스크를 붙인다. 파일이 없으면 0, 붙였으면 1, 같은 파일이 이미 있으면 kind·name 만 고친다.
 * 메모리가 부족하면 -1. */
static int add_disk(VdiskList *l, VdiskKind kind, const wchar_t *name, const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    Vdisk *v;
    size_t i;
    DWORD high = 0, low;

    if (path == NULL || !GetFileAttributesExW(path, GetFileExInfoStandard, &fa) ||
        (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
    }
    for (i = 0; i < l->len; i++) {
        if (_wcsicmp(l->items[i].path, path) == 0) {
            if (kind == VDISK_DOCKER && l->items[i].kind != VDISK_DOCKER) {
                wchar_t *n = path_dup(name);
                if (n == NULL) {
                    return -1;
                }
                free(l->items[i].name);
                l->items[i].name = n;
                l->items[i].kind = VDISK_DOCKER;
            }
            return 1;
        }
    }
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        Vdisk *grown = realloc(l->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        l->items = grown;
        l->cap = cap;
    }
    v = &l->items[l->len];
    memset(v, 0, sizeof(*v));
    v->kind = kind;
    v->name = path_dup(name);
    v->path = path_dup(path);
    if (v->name == NULL || v->path == NULL) {
        free(v->name);
        free(v->path);
        return -1;
    }
    v->size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    v->modified = ft64(fa.ftLastWriteTime);
    v->sparse = (fa.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
    SetLastError(NO_ERROR);
    low = GetCompressedFileSizeW(path, &high);
    v->alloc = (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
                   ? v->size
                   : (((uint64_t)high << 32) | low);
    l->len++;
    return 1;
}

/* 레지스트리 문자열 값(malloc, 없으면 NULL). */
static wchar_t *reg_string(HKEY key, const wchar_t *name)
{
    DWORD size = 0;
    wchar_t *buf;

    if (RegGetValueW(key, NULL, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, NULL, &size) !=
            ERROR_SUCCESS ||
        size == 0) {
        return NULL;
    }
    buf = malloc(size + sizeof(wchar_t));
    if (buf != NULL &&
        RegGetValueW(key, NULL, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, buf, &size) !=
            ERROR_SUCCESS) {
        free(buf);
        buf = NULL;
    }
    return buf;
}

static int find_wsl(VdiskList *l)
{
    wchar_t keypath[512];
    HKEY root;
    DWORD i, n;
    int rc = 0;

    n = GetEnvironmentVariableW(L"BYTESIEVE_WSL_KEY", keypath, 512);
    if (n == 0 || n >= 512) {
        wcscpy_s(keypath, 512, LXSS_KEY);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keypath, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        return 0;
    }
    for (i = 0; rc >= 0; i++) {
        wchar_t sub[128];
        DWORD sublen = 128;
        HKEY key;
        wchar_t *name, *base, *file, *stripped, *full;
        if (RegEnumKeyExW(root, i, sub, &sublen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            continue;
        }
        name = reg_string(key, L"DistributionName");
        base = reg_string(key, L"BasePath");
        file = reg_string(key, L"VhdFileName");
        RegCloseKey(key);
        stripped = base != NULL ? path_strip_extended(base) : NULL;
        full = stripped != NULL ? path_join(stripped, file != NULL ? file : L"ext4.vhdx") : NULL;
        if (full != NULL) {
            rc = add_disk(l, VDISK_WSL, name != NULL ? name : sub, full);
        }
        free(name);
        free(base);
        free(file);
        free(stripped);
        free(full);
    }
    RegCloseKey(root);
    return rc < 0 ? -1 : 0;
}

static int find_docker(VdiskList *l)
{
    static const struct {
        const wchar_t *rel;
        const wchar_t *name;
    } known[] = {
        {L"Docker\\wsl\\disk\\docker_data.vhdx", L"Docker Desktop (data)"},
        {L"Docker\\wsl\\data\\ext4.vhdx", L"Docker Desktop (data)"},
        {L"Docker\\wsl\\main\\ext4.vhdx", L"Docker Desktop (system)"},
        {L"Docker\\wsl\\distro\\ext4.vhdx", L"Docker Desktop (system)"},
    };
    wchar_t local[MAX_PATH];
    PWSTR known_local = NULL;
    size_t i;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &known_local))) {
            return 0;
        }
        wcscpy_s(local, MAX_PATH, known_local);
        CoTaskMemFree(known_local);
    }
    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        wchar_t *p = path_join(local, known[i].rel);
        int r = p != NULL ? add_disk(l, VDISK_DOCKER, known[i].name, p) : -1;
        free(p);
        if (r < 0) {
            return -1;
        }
    }
    return 0;
}

int vdisk_find(VdiskList *list)
{
    vdisk_list_free(list);
    if (find_wsl(list) != 0 || find_docker(list) != 0) {
        return -1;
    }
    return 0;
}

void vdisk_hint(const Vdisk *v, wchar_t *buf, size_t len)
{
    if (v == NULL || buf == NULL || len == 0) {
        return;
    }
    swprintf(buf, len,
             L"%ls"
             L"wsl --shutdown\n"
             L"Optimize-VHD -Path \"%ls\" -Mode Full   (Hyper-V, admin)\n"
             L"or diskpart (admin): select vdisk file=\"%ls\" / attach vdisk readonly / "
             L"compact vdisk / detach vdisk\n"
             L"%ls",
             v->kind == VDISK_DOCKER ? L"docker system prune   (remove unused images/containers first)\n"
                                     : L"",
             v->path, v->path,
             v->kind == VDISK_WSL && !v->sparse
                 ? L"or keep it small automatically: wsl --manage <distro> --set-sparse true\n"
                 : L"");
}
