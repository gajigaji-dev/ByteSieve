#include "booster.h"

#include <objbase.h>
#include <psapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "path.h"

#define RUN_SUBKEY L"\\Run"
#define APPROVED_SUBKEY L"\\Explorer\\StartupApproved\\"
#define APPROVED_VALUE_BYTES 12

/* ---- 이름 ---- */

const wchar_t *booster_source_text(BoosterStartupSource source)
{
    switch (source) {
    case BOOSTER_SRC_RUN_USER:
        return L"레지스트리(현재 사용자)";
    case BOOSTER_SRC_RUN_MACHINE:
        return L"레지스트리(모든 사용자)";
    case BOOSTER_SRC_RUN_MACHINE32:
        return L"레지스트리(모든 사용자, 32비트)";
    case BOOSTER_SRC_FOLDER_USER:
        return L"시작프로그램 폴더(현재 사용자)";
    case BOOSTER_SRC_FOLDER_COMMON:
        return L"시작프로그램 폴더(모든 사용자)";
    default:
        return L"";
    }
}

const wchar_t *booster_source_name(BoosterStartupSource source)
{
    switch (source) {
    case BOOSTER_SRC_RUN_USER:
        return L"run_user";
    case BOOSTER_SRC_RUN_MACHINE:
        return L"run_machine";
    case BOOSTER_SRC_RUN_MACHINE32:
        return L"run_machine32";
    case BOOSTER_SRC_FOLDER_USER:
        return L"folder_user";
    case BOOSTER_SRC_FOLDER_COMMON:
        return L"folder_common";
    default:
        return L"unknown";
    }
}

int booster_source_needs_admin(BoosterStartupSource source)
{
    return source == BOOSTER_SRC_RUN_MACHINE || source == BOOSTER_SRC_RUN_MACHINE32 ||
           source == BOOSTER_SRC_FOLDER_COMMON;
}

const wchar_t *booster_protect_text(BoosterProtect p)
{
    switch (p) {
    case BOOSTER_PROTECT_NONE:
        return L"끝낼 수 있음";
    case BOOSTER_PROTECT_SYSTEM:
        return L"시스템 프로세스";
    case BOOSTER_PROTECT_SESSION:
        return L"다른 사용자 세션";
    case BOOSTER_PROTECT_CRITICAL:
        return L"Windows 필수 프로세스";
    case BOOSTER_PROTECT_WINDOWS:
        return L"Windows 구성 요소";
    case BOOSTER_PROTECT_SELF:
        return L"ByteSieve";
    case BOOSTER_PROTECT_NO_ACCESS:
        return L"정보를 읽을 수 없음(권한)";
    default:
        return L"";
    }
}

const wchar_t *booster_protect_name(BoosterProtect p)
{
    switch (p) {
    case BOOSTER_PROTECT_NONE:
        return L"none";
    case BOOSTER_PROTECT_SYSTEM:
        return L"system";
    case BOOSTER_PROTECT_SESSION:
        return L"session";
    case BOOSTER_PROTECT_CRITICAL:
        return L"critical";
    case BOOSTER_PROTECT_WINDOWS:
        return L"windows";
    case BOOSTER_PROTECT_SELF:
        return L"self";
    case BOOSTER_PROTECT_NO_ACCESS:
        return L"no_access";
    default:
        return L"unknown";
    }
}

const wchar_t *booster_end_text(BoosterEndStatus s)
{
    switch (s) {
    case BOOSTER_END_OK:
        return L"끝냄";
    case BOOSTER_END_PROTECTED:
        return L"보호 대상이라 끝내지 않음";
    case BOOSTER_END_GONE:
        return L"이미 끝남";
    case BOOSTER_END_CHANGED:
        return L"다른 프로세스로 바뀌어 끝내지 않음";
    case BOOSTER_END_DENIED:
        return L"권한 없음";
    case BOOSTER_END_FAILED:
        return L"끝내지 못함";
    default:
        return L"";
    }
}

const wchar_t *booster_end_name(BoosterEndStatus s)
{
    switch (s) {
    case BOOSTER_END_OK:
        return L"ok";
    case BOOSTER_END_PROTECTED:
        return L"protected";
    case BOOSTER_END_GONE:
        return L"gone";
    case BOOSTER_END_CHANGED:
        return L"changed";
    case BOOSTER_END_DENIED:
        return L"denied";
    case BOOSTER_END_FAILED:
        return L"failed";
    default:
        return L"unknown";
    }
}

/* ---- 공통 ---- */

static uint64_t ft64(FILETIME ft)
{
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static wchar_t *concat3(const wchar_t *a, const wchar_t *b, const wchar_t *c)
{
    size_t la = wcslen(a), lb = wcslen(b), lc = c != NULL ? wcslen(c) : 0;
    wchar_t *s = malloc((la + lb + lc + 1) * sizeof(*s));

    if (s != NULL) {
        memcpy(s, a, la * sizeof(*s));
        memcpy(s + la, b, lb * sizeof(*s));
        if (lc > 0) {
            memcpy(s + la + lb, c, lc * sizeof(*s));
        }
        s[la + lb + lc] = L'\0';
    }
    return s;
}

/* path 가 dir 와 같거나 그 아래인지(대소문자 무시). */
static int is_under(const wchar_t *path, const wchar_t *dir)
{
    size_t n = wcslen(dir);

    while (n > 0 && path_is_separator(dir[n - 1])) {
        n--;
    }
    return n > 0 && _wcsnicmp(path, dir, n) == 0 &&
           (path[n] == L'\0' || path_is_separator(path[n]));
}

/* ---- 시작 프로그램 ---- */

void booster_startup_config_system(BoosterStartupConfig *cfg)
{
    static wchar_t user_folder[MAX_PATH], common_folder[MAX_PATH];
    PWSTR p = NULL;

    memset(cfg, 0, sizeof(*cfg));
    cfg->user_root = HKEY_CURRENT_USER;
    cfg->user_cv = L"Software\\Microsoft\\Windows\\CurrentVersion";
    cfg->machine_root = HKEY_LOCAL_MACHINE;
    cfg->machine_cv = L"Software\\Microsoft\\Windows\\CurrentVersion";
    cfg->machine32_cv = L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion";
    user_folder[0] = common_folder[0] = L'\0';
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Startup, 0, NULL, &p))) {
        wcsncpy_s(user_folder, MAX_PATH, p, _TRUNCATE);
    }
    CoTaskMemFree(p);
    p = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_CommonStartup, 0, NULL, &p))) {
        wcsncpy_s(common_folder, MAX_PATH, p, _TRUNCATE);
    }
    CoTaskMemFree(p);
    cfg->user_folder = user_folder[0] ? user_folder : NULL;
    cfg->common_folder = common_folder[0] ? common_folder : NULL;
}

void booster_startup_list_init(BoosterStartupList *list)
{
    memset(list, 0, sizeof(*list));
}

void booster_startup_list_free(BoosterStartupList *list)
{
    size_t i;

    for (i = 0; i < list->len; i++) {
        free(list->items[i].name);
        free(list->items[i].command);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int booster_approved_enabled(const unsigned char *value, size_t len, uint64_t *disabled_at)
{
    uint64_t when = 0;
    size_t i;

    if (disabled_at != NULL) {
        *disabled_at = 0;
    }
    if (value == NULL || len == 0 || (value[0] & 1u) == 0) {
        return 1;
    }
    if (len >= APPROVED_VALUE_BYTES) {
        for (i = 0; i < 8; i++) {
            when |= (uint64_t)value[4 + i] << (8 * i);
        }
    }
    if (disabled_at != NULL) {
        *disabled_at = when;
    }
    return 0;
}

void booster_approved_value(int enabled, uint64_t when, unsigned char out[12])
{
    size_t i;

    memset(out, 0, APPROVED_VALUE_BYTES);
    out[0] = enabled ? 0x02 : 0x03;
    if (!enabled) {
        for (i = 0; i < 8; i++) {
            out[4 + i] = (unsigned char)(when >> (8 * i));
        }
    }
}

/* 위치마다 StartupApproved 키(루트·경로). */
static int approved_key(BoosterStartupSource source, const BoosterStartupConfig *cfg, HKEY *root,
                        wchar_t **path)
{
    const wchar_t *cv, *leaf;

    switch (source) {
    case BOOSTER_SRC_RUN_USER:
        *root = cfg->user_root, cv = cfg->user_cv, leaf = L"Run";
        break;
    case BOOSTER_SRC_RUN_MACHINE:
        *root = cfg->machine_root, cv = cfg->machine_cv, leaf = L"Run";
        break;
    case BOOSTER_SRC_RUN_MACHINE32:
        *root = cfg->machine_root, cv = cfg->machine_cv, leaf = L"Run32";
        break;
    case BOOSTER_SRC_FOLDER_USER:
        *root = cfg->user_root, cv = cfg->user_cv, leaf = L"StartupFolder";
        break;
    case BOOSTER_SRC_FOLDER_COMMON:
        *root = cfg->machine_root, cv = cfg->machine_cv, leaf = L"StartupFolder";
        break;
    default:
        return -1;
    }
    if (*root == NULL || cv == NULL) {
        return -1;
    }
    *path = concat3(cv, APPROVED_SUBKEY, leaf);
    return *path != NULL ? 0 : -1;
}

/* name 의 켬/끔 상태를 읽는다(값이 없으면 켬). */
static int read_approved(BoosterStartupSource source, const BoosterStartupConfig *cfg,
                         const wchar_t *name, uint64_t *disabled_at)
{
    HKEY root, key;
    wchar_t *path = NULL;
    unsigned char value[64];
    DWORD size = sizeof(value), type = 0;
    int enabled = 1;

    *disabled_at = 0;
    if (approved_key(source, cfg, &root, &path) != 0) {
        return 1;
    }
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, NULL, &type, value, &size) == ERROR_SUCCESS &&
            type == REG_BINARY) {
            enabled = booster_approved_enabled(value, size, disabled_at);
        }
        RegCloseKey(key);
    }
    free(path);
    return enabled;
}

/* 항목 하나를 붙인다(name·command 는 복사). */
static int add_startup(BoosterStartupList *list, BoosterStartupSource source, const wchar_t *name,
                       const wchar_t *command, const BoosterStartupConfig *cfg)
{
    BoosterStartupItem *it;

    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        BoosterStartupItem *grown = realloc(list->items, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        list->items = grown;
        list->cap = cap;
    }
    it = &list->items[list->len];
    memset(it, 0, sizeof(*it));
    it->source = source;
    it->name = path_dup(name);
    it->command = path_dup(command != NULL ? command : L"");
    if (it->name == NULL || it->command == NULL) {
        free(it->name);
        free(it->command);
        return -1;
    }
    it->enabled = read_approved(source, cfg, name, &it->disabled_at);
    list->len++;
    return 0;
}

static int read_run_key(BoosterStartupList *list, BoosterStartupSource source, HKEY root,
                        const wchar_t *cv, const BoosterStartupConfig *cfg)
{
    wchar_t *path, *name = NULL;
    BYTE *data = NULL;
    HKEY key;
    DWORD max_name = 0, max_data = 0, i;
    LONG rc;
    int result = 0;

    if (root == NULL || cv == NULL) {
        return 0;
    }
    path = concat3(cv, RUN_SUBKEY, NULL);
    if (path == NULL) {
        return -1;
    }
    rc = RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key);
    free(path);
    if (rc != ERROR_SUCCESS) {
        if (rc != ERROR_FILE_NOT_FOUND) {
            list->errors++;
        }
        return 0;
    }
    if (RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_name, &max_data,
                         NULL, NULL) != ERROR_SUCCESS) {
        list->errors++;
        RegCloseKey(key);
        return 0;
    }
    name = malloc((max_name + 1) * sizeof(*name));
    data = malloc(max_data + sizeof(wchar_t));
    if (name == NULL || data == NULL) {
        result = -1;
        goto done;
    }
    for (i = 0;; i++) {
        DWORD nlen = max_name + 1, dlen = max_data, type = 0;
        rc = RegEnumValueW(key, i, name, &nlen, NULL, &type, data, &dlen);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            list->errors++;
            break;
        }
        if ((type != REG_SZ && type != REG_EXPAND_SZ) || nlen == 0) {
            continue;
        }
        /* 끝에 NUL 이 없을 수 있다. */
        memset(data + dlen, 0, sizeof(wchar_t));
        if (add_startup(list, source, name, (const wchar_t *)data, cfg) != 0) {
            result = -1;
            break;
        }
    }
done:
    free(name);
    free(data);
    RegCloseKey(key);
    return result;
}

static int read_folder(BoosterStartupList *list, BoosterStartupSource source,
                       const wchar_t *folder, const BoosterStartupConfig *cfg)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t *pattern;
    int result = 0;

    if (folder == NULL) {
        return 0;
    }
    pattern = path_join(folder, L"*");
    if (pattern == NULL) {
        return -1;
    }
    h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            list->errors++;
        }
        return 0;
    }
    do {
        wchar_t *full;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            _wcsicmp(fd.cFileName, L"desktop.ini") == 0) {
            continue;
        }
        full = path_join(folder, fd.cFileName);
        if (full == NULL || add_startup(list, source, fd.cFileName, full, cfg) != 0) {
            free(full);
            result = -1;
            break;
        }
        free(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

int booster_startup_list(BoosterStartupList *list, const BoosterStartupConfig *cfg)
{
    BoosterStartupConfig sys;

    booster_startup_list_free(list);
    if (cfg == NULL) {
        booster_startup_config_system(&sys);
        cfg = &sys;
    }
    if (read_run_key(list, BOOSTER_SRC_RUN_USER, cfg->user_root, cfg->user_cv, cfg) != 0 ||
        read_run_key(list, BOOSTER_SRC_RUN_MACHINE, cfg->machine_root, cfg->machine_cv, cfg) != 0 ||
        read_run_key(list, BOOSTER_SRC_RUN_MACHINE32, cfg->machine_root, cfg->machine32_cv, cfg) !=
            0 ||
        read_folder(list, BOOSTER_SRC_FOLDER_USER, cfg->user_folder, cfg) != 0 ||
        read_folder(list, BOOSTER_SRC_FOLDER_COMMON, cfg->common_folder, cfg) != 0) {
        return -1;
    }
    return 0;
}

unsigned long booster_startup_set(const BoosterStartupItem *item, int enabled,
                                  const BoosterStartupConfig *cfg)
{
    BoosterStartupConfig sys;
    unsigned char value[APPROVED_VALUE_BYTES];
    HKEY root, key;
    wchar_t *path = NULL;
    FILETIME now;
    LONG rc;

    if (item == NULL || item->name == NULL) {
        return ERROR_INVALID_PARAMETER;
    }
    if (cfg == NULL) {
        booster_startup_config_system(&sys);
        cfg = &sys;
    }
    if (approved_key(item->source, cfg, &root, &path) != 0) {
        return ERROR_INVALID_PARAMETER;
    }
    rc = RegCreateKeyExW(root, path, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key,
                         NULL);
    free(path);
    if (rc != ERROR_SUCCESS) {
        return (unsigned long)rc;
    }
    GetSystemTimeAsFileTime(&now);
    booster_approved_value(enabled, ft64(now), value);
    rc = RegSetValueExW(key, item->name, 0, REG_BINARY, value, APPROVED_VALUE_BYTES);
    RegCloseKey(key);
    return (unsigned long)rc;
}

/* ---- 프로세스 ---- */

void booster_process_list_init(BoosterProcessList *list)
{
    memset(list, 0, sizeof(*list));
}

void booster_process_list_free(BoosterProcessList *list)
{
    size_t i;

    for (i = 0; i < list->len; i++) {
        free(list->items[i].name);
        free(list->items[i].path);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int booster_is_critical_name(const wchar_t *name)
{
    static const wchar_t *const names[] = {
        L"System",          L"Registry",          L"Idle",
        L"Secure System",   L"MemCompression",    L"smss.exe",
        L"csrss.exe",       L"wininit.exe",       L"winlogon.exe",
        L"services.exe",    L"lsass.exe",         L"LsaIso.exe",
        L"svchost.exe",     L"dwm.exe",           L"fontdrvhost.exe",
        L"sihost.exe",      L"ctfmon.exe",        L"explorer.exe",
        L"taskhostw.exe",   L"RuntimeBroker.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe",           L"SearchHost.exe",
        L"SearchIndexer.exe", L"TextInputHost.exe", L"conhost.exe",
        L"audiodg.exe",     L"spoolsv.exe",       L"MsMpEng.exe",
        L"NisSrv.exe",      L"SecurityHealthService.exe",
        L"SecurityHealthSystray.exe",             L"dllhost.exe",
        L"WmiPrvSE.exe",    L"LogonUI.exe",       L"userinit.exe",
        L"smartscreen.exe", L"SgrmBroker.exe",    L"ApplicationFrameHost.exe",
        L"WUDFHost.exe",    L"lsm.exe",
    };
    size_t i;

    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (_wcsicmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

BoosterProtect booster_process_protect(const BoosterProcess *p, DWORD self_pid, DWORD session,
                                       const wchar_t *windows_dir)
{
    wchar_t win[MAX_PATH];

    if (p->pid == 0 || p->pid == 4) {
        return BOOSTER_PROTECT_SYSTEM;
    }
    if (p->pid == self_pid) {
        return BOOSTER_PROTECT_SELF;
    }
    if (p->session == 0) {
        return BOOSTER_PROTECT_SYSTEM;
    }
    if (p->session != session) {
        return BOOSTER_PROTECT_SESSION;
    }
    if (booster_is_critical_name(p->name)) {
        return BOOSTER_PROTECT_CRITICAL;
    }
    if (!p->info || p->path == NULL || p->create_time == 0) {
        return BOOSTER_PROTECT_NO_ACCESS;
    }
    if (windows_dir == NULL) {
        UINT n = GetWindowsDirectoryW(win, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            return BOOSTER_PROTECT_NO_ACCESS; /* 판정할 수 없으면 끝내지 않는다 */
        }
        windows_dir = win;
    }
    if (is_under(p->path, windows_dir)) {
        return BOOSTER_PROTECT_WINDOWS;
    }
    return BOOSTER_PROTECT_NONE;
}

/* 열린 프로세스 핸들에서 경로·시각·메모리를 읽는다. */
static void read_process_info(HANDLE h, BoosterProcess *p)
{
    wchar_t path[MAX_PATH * 2];
    DWORD n = (DWORD)(sizeof(path) / sizeof(path[0]));
    FILETIME create, exit_time, kernel, user;
    PROCESS_MEMORY_COUNTERS_EX mem;

    if (QueryFullProcessImageNameW(h, 0, path, &n)) {
        p->path = path_dup(path);
    }
    if (GetProcessTimes(h, &create, &exit_time, &kernel, &user)) {
        p->create_time = ft64(create);
        p->cpu_time = ft64(kernel) + ft64(user);
    }
    memset(&mem, 0, sizeof(mem));
    mem.cb = sizeof(mem);
    if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem))) {
        p->working_set = mem.WorkingSetSize;
        p->private_bytes = mem.PrivateUsage;
    }
    p->info = p->path != NULL;
}

typedef struct {
    DWORD *pids;
    size_t len;
    size_t cap;
} PidSet;

static BOOL CALLBACK collect_window(HWND hwnd, LPARAM lp)
{
    PidSet *s = (PidSet *)lp;
    DWORD pid = 0;

    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL ||
        GetWindowTextLengthW(hwnd) == 0) {
        return TRUE;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if (s->len == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        DWORD *grown = realloc(s->pids, cap * sizeof(*grown));
        if (grown == NULL) {
            return FALSE;
        }
        s->pids = grown;
        s->cap = cap;
    }
    s->pids[s->len++] = pid;
    return TRUE;
}

static int pid_in(const PidSet *s, DWORD pid)
{
    size_t i;

    for (i = 0; i < s->len; i++) {
        if (s->pids[i] == pid) {
            return 1;
        }
    }
    return 0;
}

int booster_process_list(BoosterProcessList *list)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    PidSet windows = {NULL, 0, 0};
    DWORD self = GetCurrentProcessId(), session = 0;
    wchar_t win[MAX_PATH];
    FILETIME now;
    int rc = 0;

    booster_process_list_free(list);
    if (GetWindowsDirectoryW(win, MAX_PATH) == 0) {
        win[0] = L'\0';
    }
    ProcessIdToSessionId(self, &session);
    EnumWindows(collect_window, (LPARAM)&windows);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        free(windows.pids);
        return -1;
    }
    GetSystemTimeAsFileTime(&now);
    list->taken_at = ft64(now);
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            BoosterProcess *p;
            HANDLE h;
            if (list->len == list->cap) {
                size_t cap = list->cap ? list->cap * 2 : 256;
                BoosterProcess *grown = realloc(list->items, cap * sizeof(*grown));
                if (grown == NULL) {
                    rc = -1;
                    break;
                }
                list->items = grown;
                list->cap = cap;
            }
            p = &list->items[list->len];
            memset(p, 0, sizeof(*p));
            p->pid = pe.th32ProcessID;
            p->parent = pe.th32ParentProcessID;
            p->name = path_dup(pe.szExeFile);
            if (p->name == NULL) {
                rc = -1;
                break;
            }
            if (!ProcessIdToSessionId(p->pid, &p->session)) {
                p->session = 0;
            }
            h = p->pid != 0 ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p->pid) : NULL;
            if (h != NULL) {
                read_process_info(h, p);
                CloseHandle(h);
            }
            p->has_window = pid_in(&windows, p->pid);
            p->protect = booster_process_protect(p, self, session, win[0] ? win : NULL);
            list->len++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    free(windows.pids);
    return rc;
}

unsigned booster_cpu_permille(uint64_t cpu_delta, uint64_t wall_delta, unsigned cpus)
{
    double v;

    if (wall_delta == 0 || cpus == 0) {
        return 0;
    }
    /* 64비트 곱셈 넘침을 피하려고 실수로 계산한다(내림). */
    v = (double)cpu_delta * 1000.0 / ((double)wall_delta * (double)cpus);
    return v >= 1000.0 ? 1000u : (unsigned)v;
}

void booster_process_cpu(const BoosterProcessList *prev, BoosterProcessList *cur, unsigned cpus)
{
    size_t i, j;
    uint64_t wall;

    if (prev == NULL || cur == NULL || cur->taken_at <= prev->taken_at) {
        return;
    }
    if (cpus == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cpus = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
    }
    wall = cur->taken_at - prev->taken_at;
    for (i = 0; i < cur->len; i++) {
        BoosterProcess *c = &cur->items[i];
        c->cpu_permille = 0;
        if (c->create_time == 0) {
            continue;
        }
        for (j = 0; j < prev->len; j++) {
            const BoosterProcess *p = &prev->items[j];
            if (p->pid == c->pid && p->create_time == c->create_time) {
                if (c->cpu_time >= p->cpu_time) {
                    c->cpu_permille = booster_cpu_permille(c->cpu_time - p->cpu_time, wall, cpus);
                }
                break;
            }
        }
    }
}

BoosterEndResult booster_process_end(const BoosterProcess *p, unsigned wait_ms)
{
    BoosterEndResult r;
    BoosterProcess now;
    FILETIME create, exit_time, kernel, user;
    DWORD self = GetCurrentProcessId(), session = 0, exit_code = 0;
    HANDLE h;

    memset(&r, 0, sizeof(r));
    if (p == NULL) {
        r.status = BOOSTER_END_FAILED;
        r.code = ERROR_INVALID_PARAMETER;
        return r;
    }
    /* 1. 목록 때의 판정 */
    if (p->protect != BOOSTER_PROTECT_NONE) {
        r.status = BOOSTER_END_PROTECTED;
        r.protect = p->protect;
        return r;
    }
    h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                    p->pid);
    if (h == NULL) {
        DWORD err = GetLastError();
        r.status = err == ERROR_INVALID_PARAMETER ? BOOSTER_END_GONE
                   : err == ERROR_ACCESS_DENIED   ? BOOSTER_END_DENIED
                                                  : BOOSTER_END_FAILED;
        r.code = err;
        return r;
    }
    /* 2. 끝내기 직전: 같은 프로세스인지(PID 재사용), 아직 실행 중인지, 판정을 다시 통과하는지 */
    if (!GetProcessTimes(h, &create, &exit_time, &kernel, &user) || p->create_time == 0 ||
        ft64(create) != p->create_time) {
        CloseHandle(h);
        r.status = BOOSTER_END_CHANGED;
        return r;
    }
    if (GetExitCodeProcess(h, &exit_code) && exit_code != STILL_ACTIVE) {
        CloseHandle(h);
        r.status = BOOSTER_END_GONE;
        return r;
    }
    memset(&now, 0, sizeof(now));
    now.pid = p->pid;
    now.name = p->name;
    if (!ProcessIdToSessionId(p->pid, &now.session)) {
        now.session = 0;
    }
    read_process_info(h, &now);
    ProcessIdToSessionId(self, &session);
    r.protect = booster_process_protect(&now, self, session, NULL);
    free(now.path);
    if (r.protect != BOOSTER_PROTECT_NONE) {
        CloseHandle(h);
        r.status = BOOSTER_END_PROTECTED;
        return r;
    }
    /* 3. 끝내기 */
    if (!TerminateProcess(h, 1)) {
        DWORD err = GetLastError();
        CloseHandle(h);
        r.status = err == ERROR_ACCESS_DENIED ? BOOSTER_END_DENIED : BOOSTER_END_FAILED;
        r.code = err;
        return r;
    }
    WaitForSingleObject(h, wait_ms);
    CloseHandle(h);
    r.status = BOOSTER_END_OK;
    return r;
}
