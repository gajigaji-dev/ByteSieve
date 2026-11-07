#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "booster.h"

/*
 * Booster 단위 테스트. 시작 프로그램은 실제 Run 키 대신 HKCU\Software\ByteSieveTest\booster_<pid>
 * 아래에 만든 가짜 키와 임시 폴더를 쓰고 끝나면 지운다(실제 시작 프로그램은 바꾸지 않는다).
 * 프로세스 끝내기는 이 테스트가 직접 띄운 자식 프로세스(test_booster --sleep)만 끝낸다.
 */

static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

/* ---- StartupApproved 값 ---- */

static void test_approved(void)
{
    unsigned char v[12];
    uint64_t when = 0;
    static const unsigned char on2[12] = {0x02};
    static const unsigned char on6[12] = {0x06};
    static const unsigned char off7[1] = {0x07};

    CHECK(booster_approved_enabled(NULL, 0, &when) == 1 && when == 0);
    CHECK(booster_approved_enabled(on2, 12, &when) == 1);
    CHECK(booster_approved_enabled(on6, 12, &when) == 1);
    CHECK(booster_approved_enabled(off7, 1, &when) == 0 && when == 0);

    booster_approved_value(0, 0x0102030405060708ULL, v);
    CHECK(v[0] == 0x03 && v[1] == 0 && v[4] == 0x08 && v[11] == 0x01);
    CHECK(booster_approved_enabled(v, 12, &when) == 0 && when == 0x0102030405060708ULL);
    booster_approved_value(1, 123, v);
    CHECK(v[0] == 0x02 && v[4] == 0 && booster_approved_enabled(v, 12, &when) == 1);
}

/* ---- 시작 프로그램(가짜 레지스트리·폴더) ---- */

static wchar_t g_base[128], g_user_cv[160], g_machine_cv[160], g_machine32_cv[160];
static wchar_t g_folder[MAX_PATH];

static void set_sz(const wchar_t *cv, const wchar_t *name, const wchar_t *value)
{
    wchar_t path[256];
    HKEY key;

    swprintf(path, 256, L"%ls\\Run", cv);
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) ==
          ERROR_SUCCESS);
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

static const BoosterStartupItem *find_item(const BoosterStartupList *l, const wchar_t *name)
{
    size_t i;
    for (i = 0; i < l->len; i++) {
        if (wcscmp(l->items[i].name, name) == 0) {
            return &l->items[i];
        }
    }
    return NULL;
}

static void test_startup(void)
{
    BoosterStartupConfig cfg;
    BoosterStartupList list;
    const BoosterStartupItem *it;
    wchar_t tmp[MAX_PATH], path[MAX_PATH], approved[256];
    unsigned char off[12];
    HKEY key;
    DWORD dw = 1;
    HANDLE h;

    swprintf(g_base, 128, L"Software\\ByteSieveTest\\booster_%lu", GetCurrentProcessId());
    swprintf(g_user_cv, 160, L"%ls\\user", g_base);
    swprintf(g_machine_cv, 160, L"%ls\\machine", g_base);
    swprintf(g_machine32_cv, 160, L"%ls\\machine32", g_base);
    GetTempPathW(MAX_PATH, tmp);
    swprintf(g_folder, MAX_PATH, L"%lsbytesieve_booster_%lu", tmp, GetCurrentProcessId());
    CreateDirectoryW(g_folder, NULL);

    set_sz(g_user_cv, L"AppA", L"\"C:\\Apps\\a.exe\" --tray");
    set_sz(g_machine_cv, L"AppB", L"C:\\Apps\\b.exe");
    set_sz(g_machine32_cv, L"AppC", L"C:\\Apps\\c.exe");
    /* 문자열이 아닌 값은 시작 프로그램이 아니다 */
    swprintf(path, MAX_PATH, L"%ls\\Run", g_user_cv);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"Number", 0, REG_DWORD, (const BYTE *)&dw, sizeof(dw));
        RegCloseKey(key);
    }
    /* AppA 는 이미 꺼져 있다(작업 관리자처럼) */
    swprintf(approved, 256, L"%ls\\Explorer\\StartupApproved\\Run", g_user_cv);
    booster_approved_value(0, 42, off);
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, approved, 0, NULL, 0, KEY_SET_VALUE, NULL, &key,
                          NULL) == ERROR_SUCCESS);
    RegSetValueExW(key, L"AppA", 0, REG_BINARY, off, 12);
    RegCloseKey(key);
    /* 시작프로그램 폴더: 바로 가기 하나, desktop.ini·하위 폴더는 빼야 함 */
    swprintf(path, MAX_PATH, L"%ls\\Tool.lnk", g_folder);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    swprintf(path, MAX_PATH, L"%ls\\desktop.ini", g_folder);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(h);
    swprintf(path, MAX_PATH, L"%ls\\sub", g_folder);
    CreateDirectoryW(path, NULL);

    memset(&cfg, 0, sizeof(cfg));
    cfg.user_root = HKEY_CURRENT_USER;
    cfg.user_cv = g_user_cv;
    cfg.machine_root = HKEY_CURRENT_USER; /* HKLM 대신(관리자 권한 없이 테스트) */
    cfg.machine_cv = g_machine_cv;
    cfg.machine32_cv = g_machine32_cv;
    cfg.user_folder = g_folder;
    cfg.common_folder = NULL;

    booster_startup_list_init(&list);
    CHECK(booster_startup_list(&list, &cfg) == 0);
    CHECK(list.len == 4 && list.errors == 0);
    it = find_item(&list, L"AppA");
    CHECK(it != NULL && it->source == BOOSTER_SRC_RUN_USER && !it->enabled &&
          it->disabled_at == 42 && wcscmp(it->command, L"\"C:\\Apps\\a.exe\" --tray") == 0);
    it = find_item(&list, L"AppB");
    CHECK(it != NULL && it->source == BOOSTER_SRC_RUN_MACHINE && it->enabled);
    it = find_item(&list, L"AppC");
    CHECK(it != NULL && it->source == BOOSTER_SRC_RUN_MACHINE32 && it->enabled);
    it = find_item(&list, L"Tool.lnk");
    CHECK(it != NULL && it->source == BOOSTER_SRC_FOLDER_USER && it->enabled &&
          wcsstr(it->command, L"Tool.lnk") != NULL);
    CHECK(find_item(&list, L"desktop.ini") == NULL && find_item(&list, L"Number") == NULL);

    /* 끄기·켜기는 등록(Run 값·바로 가기)을 지우지 않고 승인 값만 바꾼다 */
    CHECK(booster_startup_set(find_item(&list, L"AppC"), 0, &cfg) == 0);
    CHECK(booster_startup_set(find_item(&list, L"Tool.lnk"), 0, &cfg) == 0);
    CHECK(booster_startup_set(find_item(&list, L"AppA"), 1, &cfg) == 0);
    CHECK(booster_startup_list(&list, &cfg) == 0);
    CHECK(list.len == 4);
    it = find_item(&list, L"AppC");
    CHECK(it != NULL && !it->enabled && it->disabled_at != 0);
    it = find_item(&list, L"Tool.lnk");
    CHECK(it != NULL && !it->enabled);
    it = find_item(&list, L"AppA");
    CHECK(it != NULL && it->enabled);
    swprintf(path, MAX_PATH, L"%ls\\Tool.lnk", g_folder);
    CHECK(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
    /* 32비트 Run 항목의 승인 값은 Run32 에 있다 */
    swprintf(approved, 256, L"%ls\\Explorer\\StartupApproved\\Run32", g_machine_cv);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, approved, 0, KEY_READ, &key) == ERROR_SUCCESS);
    RegCloseKey(key);

    CHECK(booster_source_needs_admin(BOOSTER_SRC_RUN_MACHINE) &&
          booster_source_needs_admin(BOOSTER_SRC_FOLDER_COMMON) &&
          !booster_source_needs_admin(BOOSTER_SRC_RUN_USER) &&
          !booster_source_needs_admin(BOOSTER_SRC_FOLDER_USER));
    CHECK(booster_startup_set(NULL, 0, &cfg) == ERROR_INVALID_PARAMETER);

    booster_startup_list_free(&list);
    RegDeleteTreeW(HKEY_CURRENT_USER, g_base);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\ByteSieveTest");
    swprintf(path, MAX_PATH, L"%ls\\Tool.lnk", g_folder);
    DeleteFileW(path);
    swprintf(path, MAX_PATH, L"%ls\\desktop.ini", g_folder);
    DeleteFileW(path);
    swprintf(path, MAX_PATH, L"%ls\\sub", g_folder);
    RemoveDirectoryW(path);
    RemoveDirectoryW(g_folder);
}

/* ---- 프로세스 ---- */

static void test_protect(void)
{
    BoosterProcess p;
    DWORD self = 1000, session = 2;

    memset(&p, 0, sizeof(p));
    p.info = 1;
    p.create_time = 1;
    p.session = session;
    p.name = L"tool.exe";
    p.path = L"C:\\Apps\\tool.exe";
    p.pid = 2000;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_NONE);
    p.pid = 4;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_SYSTEM);
    p.pid = self;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_SELF);
    p.pid = 2000;
    p.session = 0;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_SYSTEM);
    p.session = 3;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_SESSION);
    p.session = session;
    p.name = L"EXPLORER.EXE";
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_CRITICAL);
    p.name = L"helper.exe";
    p.path = L"C:\\Windows\\System32\\helper.exe";
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows\\") == BOOSTER_PROTECT_WINDOWS);
    p.path = L"C:\\WindowsApps\\helper.exe"; /* 이름만 비슷한 폴더는 Windows 폴더가 아니다 */
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_NONE);
    p.path = NULL;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_NO_ACCESS);
    p.path = L"C:\\Apps\\tool.exe";
    p.info = 0;
    CHECK(booster_process_protect(&p, self, session, L"C:\\Windows") == BOOSTER_PROTECT_NO_ACCESS);

    CHECK(booster_is_critical_name(L"csrss.exe") && booster_is_critical_name(L"svchost.exe") &&
          !booster_is_critical_name(L"chrome.exe") && !booster_is_critical_name(NULL));
}

static void test_cpu(void)
{
    BoosterProcess a[2], b[3];
    BoosterProcessList prev, cur;

    CHECK(booster_cpu_permille(5000000, 10000000, 1) == 500);
    CHECK(booster_cpu_permille(5000000, 10000000, 4) == 125);
    CHECK(booster_cpu_permille(1, 0, 4) == 0);
    CHECK(booster_cpu_permille(99999999, 10000000, 1) == 1000);
    CHECK(booster_cpu_permille(UINT64_MAX / 2, UINT64_MAX / 4, 8) == 250);

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    a[0].pid = 10, a[0].create_time = 100, a[0].cpu_time = 1000;
    a[1].pid = 20, a[1].create_time = 200, a[1].cpu_time = 0;
    b[0].pid = 10, b[0].create_time = 100, b[0].cpu_time = 1000 + 2500000; /* 25% */
    b[1].pid = 20, b[1].create_time = 999, b[1].cpu_time = 5000000;        /* PID 재사용: 비교 안 함 */
    b[2].pid = 30, b[2].create_time = 300, b[2].cpu_time = 5000000;        /* 새 프로세스 */
    prev.items = a, prev.len = 2, prev.taken_at = 1000000;
    cur.items = b, cur.len = 3, cur.taken_at = 1000000 + 10000000;
    booster_process_cpu(&prev, &cur, 1);
    CHECK(b[0].cpu_permille == 250 && b[1].cpu_permille == 0 && b[2].cpu_permille == 0);
    /* 시각이 거꾸로면 그대로 */
    b[0].cpu_permille = 7;
    booster_process_cpu(&cur, &prev, 1);
    CHECK(b[0].cpu_permille == 7);
}

static BoosterProcess *find_pid(BoosterProcessList *l, DWORD pid)
{
    size_t i;
    for (i = 0; i < l->len; i++) {
        if (l->items[i].pid == pid) {
            return &l->items[i];
        }
    }
    return NULL;
}

static void test_end(const wchar_t *self_exe)
{
    BoosterProcessList list;
    BoosterProcess *me, *child, copy;
    BoosterEndResult r;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH + 32];
    DWORD code = 0;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" --sleep", self_exe);
    CHECK(CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));

    booster_process_list_init(&list);
    CHECK(booster_process_list(&list) == 0);
    CHECK(list.len > 10);
    me = find_pid(&list, GetCurrentProcessId());
    CHECK(me != NULL && me->protect == BOOSTER_PROTECT_SELF && me->info && me->working_set > 0);
    CHECK(find_pid(&list, 4) == NULL || find_pid(&list, 4)->protect == BOOSTER_PROTECT_SYSTEM);
    child = find_pid(&list, pi.dwProcessId);
    CHECK(child != NULL && child->protect == BOOSTER_PROTECT_NONE && child->create_time != 0 &&
          !child->has_window);

    /* 자신은 끝내지 않는다 */
    r = booster_process_end(me, 0);
    CHECK(r.status == BOOSTER_END_PROTECTED && r.protect == BOOSTER_PROTECT_SELF);
    if (child != NULL) {
        /* 시작 시각이 다르면(PID 재사용) 끝내지 않는다 */
        copy = *child;
        copy.create_time += 1;
        r = booster_process_end(&copy, 0);
        CHECK(r.status == BOOSTER_END_CHANGED);
        CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT);
        /* 같은 프로세스면 끝낸다 */
        r = booster_process_end(child, 5000);
        CHECK(r.status == BOOSTER_END_OK);
        CHECK(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0);
        CHECK(GetExitCodeProcess(pi.hProcess, &code) && code == 1);
        /* 이미 끝났으면 GONE(핸들이 남아 있어 같은 프로세스로 열림) */
        r = booster_process_end(child, 0);
        CHECK(r.status == BOOSTER_END_GONE || r.status == BOOSTER_END_CHANGED);
    }
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    booster_process_list_free(&list);
}

static void test_names(void)
{
    int i;
    for (i = 0; i < BOOSTER_SRC_COUNT; i++) {
        CHECK(booster_source_text((BoosterStartupSource)i)[0] != L'\0');
        CHECK(wcscmp(booster_source_name((BoosterStartupSource)i), L"unknown") != 0);
    }
    for (i = 0; i < BOOSTER_PROTECT_COUNT; i++) {
        CHECK(booster_protect_text((BoosterProtect)i)[0] != L'\0');
        CHECK(wcscmp(booster_protect_name((BoosterProtect)i), L"unknown") != 0);
    }
    for (i = 0; i < BOOSTER_END_COUNT; i++) {
        CHECK(booster_end_text((BoosterEndStatus)i)[0] != L'\0');
        CHECK(wcscmp(booster_end_name((BoosterEndStatus)i), L"unknown") != 0);
    }
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t self_exe[MAX_PATH];

    if (argc > 1 && wcscmp(argv[1], L"--sleep") == 0) {
        Sleep(30000);
        return 0;
    }
    GetModuleFileNameW(NULL, self_exe, MAX_PATH);
    test_names();
    test_approved();
    test_startup();
    test_protect();
    test_cpu();
    test_end(self_exe);
    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("booster: all checks passed\n");
    return 0;
}
