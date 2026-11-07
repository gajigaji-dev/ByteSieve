#ifndef BYTESIEVE_BOOSTER_H
#define BYTESIEVE_BOOSTER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/*
 * Booster: 시작 프로그램 관리, 프로세스별 CPU·메모리 사용량, 백그라운드 프로세스 끝내기.
 * UI 와 무관하며 GUI 와 테스트가 함께 쓴다.
 *
 * 시작 프로그램(작업 관리자 [시작 앱]과 같은 방식):
 *   - 목록: HKCU·HKLM 의 ...\CurrentVersion\Run(HKLM 은 64·32비트 보기), 사용자·공용 시작프로그램 폴더
 *   - 켜기/끄기는 등록을 지우지 않고 ...\Explorer\StartupApproved\(Run|Run32|StartupFolder) 의 값만
 *     바꾼다(첫 바이트 짝수 = 켬, 홀수 = 끔, 끈 시각 FILETIME). 언제든 되돌릴 수 있다.
 *   - HKLM·공용 폴더 항목을 바꾸려면 관리자 권한이 필요하다(없으면 ACCESS_DENIED 로 알림).
 *
 * 프로세스:
 *   - 목록은 스냅숏(Toolhelp) + 프로세스 정보(이미지 경로, 세션, 메모리, CPU 시간, 시작 시각)이며,
 *     보이는 창이 있으면 "앱", 없으면 "백그라운드"로 나눈다.
 *   - 끝내기 안전 장치(하나라도 걸리면 끝내지 않는다):
 *       1. 보호 판정(booster_process_protect): 시스템 프로세스(PID 0·4, 세션 0), 다른 세션,
 *          Windows 필수 프로세스 이름, Windows 폴더 안의 실행 파일, ByteSieve 자신, 정보를 읽을 수 없음
 *       2. 끝내기 직전 확인: 같은 PID 의 프로세스를 다시 열어 시작 시각이 목록과 같은지(PID 재사용 방지),
 *          보호 판정을 다시 통과하는지
 *       3. TerminateProcess. 저장하지 않은 데이터는 사라질 수 있으므로 GUI 가 확인을 받는다.
 */

/* ---- 시작 프로그램 ---- */

typedef enum {
    BOOSTER_SRC_RUN_USER = 0,  /* HKCU\...\Run */
    BOOSTER_SRC_RUN_MACHINE,   /* HKLM\...\Run(64비트) */
    BOOSTER_SRC_RUN_MACHINE32, /* HKLM\WOW6432Node\...\Run(32비트) */
    BOOSTER_SRC_FOLDER_USER,   /* 사용자 시작프로그램 폴더 */
    BOOSTER_SRC_FOLDER_COMMON, /* 모든 사용자 시작프로그램 폴더 */
    BOOSTER_SRC_COUNT
} BoosterStartupSource;

/* 화면용("사용자 레지스트리" 등), 식별자("run_user" 등). */
const wchar_t *booster_source_text(BoosterStartupSource source);
const wchar_t *booster_source_name(BoosterStartupSource source);
/* 바꾸는 데 관리자 권한이 필요한 위치인지(HKLM·공용 폴더). */
int booster_source_needs_admin(BoosterStartupSource source);

typedef struct {
    BoosterStartupSource source;
    wchar_t *name;    /* 레지스트리 값 이름 또는 폴더 안 파일 이름 */
    wchar_t *command; /* 실행 명령(레지스트리 값) 또는 파일 전체 경로 */
    int enabled;
    uint64_t disabled_at; /* 끈 시각(FILETIME, 모르면 0) */
} BoosterStartupItem;

typedef struct {
    BoosterStartupItem *items;
    size_t len;
    size_t cap;
    size_t errors; /* 읽지 못한 위치 수(없는 위치는 오류가 아님) */
} BoosterStartupList;

/*
 * 읽고 쓸 위치. 테스트는 레지스트리 루트·경로와 폴더를 바꿔 끼운다.
 * *_cv 는 "Software\Microsoft\Windows\CurrentVersion" 에 해당하는 하위 키 경로.
 */
typedef struct {
    HKEY user_root;
    const wchar_t *user_cv;
    HKEY machine_root;
    const wchar_t *machine_cv;
    const wchar_t *machine32_cv; /* 32비트 Run 이 있는 CurrentVersion(WOW6432Node) */
    const wchar_t *user_folder;  /* NULL 이면 건너뜀 */
    const wchar_t *common_folder;
} BoosterStartupConfig;

/* 실제 시스템 위치로 채운다(폴더 경로는 정적 버퍼, 다음 호출 때 덮어씀). */
void booster_startup_config_system(BoosterStartupConfig *cfg);

void booster_startup_list_init(BoosterStartupList *list);
void booster_startup_list_free(BoosterStartupList *list);

/* 목록을 읽는다(이전 내용은 지운다). cfg 가 NULL 이면 시스템. 메모리 부족 -1, 아니면 0. */
int booster_startup_list(BoosterStartupList *list, const BoosterStartupConfig *cfg);

/*
 * StartupApproved 값 해석: 없거나(len 0) 첫 바이트가 짝수면 켬(1). 끈 경우 시각을 *disabled_at 에.
 * 값 만들기: 켬은 02 + 0 11바이트, 끔은 03 00 00 00 + FILETIME(8바이트). out 은 12바이트.
 */
int booster_approved_enabled(const unsigned char *value, size_t len, uint64_t *disabled_at);
void booster_approved_value(int enabled, uint64_t when, unsigned char out[12]);

/* 켬/끔을 바꾼다. 성공 0, 실패하면 Windows 오류 코드(ERROR_ACCESS_DENIED 등). */
unsigned long booster_startup_set(const BoosterStartupItem *item, int enabled,
                                  const BoosterStartupConfig *cfg);

/* ---- 프로세스 ---- */

typedef enum {
    BOOSTER_PROTECT_NONE = 0,  /* 끝낼 수 있음 */
    BOOSTER_PROTECT_SYSTEM,    /* 시스템 프로세스(PID 0·4, 세션 0 서비스) */
    BOOSTER_PROTECT_SESSION,   /* 다른 사용자 세션 */
    BOOSTER_PROTECT_CRITICAL,  /* Windows 필수 프로세스(csrss·winlogon·explorer 등) */
    BOOSTER_PROTECT_WINDOWS,   /* Windows 폴더 안의 실행 파일 */
    BOOSTER_PROTECT_SELF,      /* ByteSieve 자신 */
    BOOSTER_PROTECT_NO_ACCESS, /* 정보를 읽을 수 없음(권한) */
    BOOSTER_PROTECT_COUNT
} BoosterProtect;

const wchar_t *booster_protect_text(BoosterProtect p);
const wchar_t *booster_protect_name(BoosterProtect p);

typedef struct {
    DWORD pid;
    DWORD parent;
    DWORD session;
    wchar_t *name;        /* 실행 파일 이름 */
    wchar_t *path;        /* 실행 파일 전체 경로(읽지 못하면 NULL) */
    uint64_t create_time; /* FILETIME(PID 재사용 확인용, 모르면 0) */
    uint64_t cpu_time;    /* 커널 + 사용자 시간(100ns) */
    uint64_t working_set; /* 실제 메모리 사용(바이트) */
    uint64_t private_bytes;
    int info;             /* 프로세스를 열어 정보를 읽었으면 1 */
    int has_window;       /* 보이는 최상위 창이 있으면 1(앱), 아니면 백그라운드 */
    unsigned cpu_permille; /* 직전 목록과 비교한 CPU 사용률(0~1000, 비교 전 0) */
    BoosterProtect protect;
} BoosterProcess;

typedef struct {
    BoosterProcess *items;
    size_t len;
    size_t cap;
    uint64_t taken_at; /* 목록을 만든 시각(FILETIME) */
} BoosterProcessList;

void booster_process_list_init(BoosterProcessList *list);
void booster_process_list_free(BoosterProcessList *list);

/* 지금 실행 중인 프로세스 목록(이전 내용은 지운다). 보호 판정까지 채운다. 실패 -1. */
int booster_process_list(BoosterProcessList *list);

/*
 * prev 와 cur 를 비교해 cur 의 cpu_permille 을 채운다(같은 PID·시작 시각끼리).
 * cpus 는 논리 프로세서 수(0 이면 시스템 값). 시각이 거꾸로면 아무것도 하지 않는다.
 */
void booster_process_cpu(const BoosterProcessList *prev, BoosterProcessList *cur, unsigned cpus);

/* CPU 시간 증가분으로 사용률(0~1000)을 계산한다(순수 계산, 테스트용). */
unsigned booster_cpu_permille(uint64_t cpu_delta, uint64_t wall_delta, unsigned cpus);

/* 이름이 Windows 필수 프로세스인지(대소문자 무시). */
int booster_is_critical_name(const wchar_t *name);

/*
 * 보호 판정. self_pid 는 ByteSieve 자신, session 은 지금 사용자 세션, windows_dir 는
 * Windows 폴더(NULL 이면 시스템 값). 프로세스 정보(p->info, path, session)를 본다.
 */
BoosterProtect booster_process_protect(const BoosterProcess *p, DWORD self_pid, DWORD session,
                                       const wchar_t *windows_dir);

typedef enum {
    BOOSTER_END_OK = 0,    /* 끝냄 */
    BOOSTER_END_PROTECTED, /* 보호 대상이라 끝내지 않음(protect 참고) */
    BOOSTER_END_GONE,      /* 이미 끝남 */
    BOOSTER_END_CHANGED,   /* 같은 PID 가 다른 프로세스(재사용)라 끝내지 않음 */
    BOOSTER_END_DENIED,    /* 권한 없음 */
    BOOSTER_END_FAILED,    /* 그 밖의 실패(code) */
    BOOSTER_END_COUNT
} BoosterEndStatus;

const wchar_t *booster_end_text(BoosterEndStatus s);
const wchar_t *booster_end_name(BoosterEndStatus s);

typedef struct {
    BoosterEndStatus status;
    BoosterProtect protect; /* PROTECTED 일 때 사유 */
    unsigned long code;     /* FAILED·DENIED 일 때 Windows 오류 */
} BoosterEndResult;

/*
 * 목록의 프로세스 하나를 끝낸다(위의 안전 장치 2·3). wait_ms 만큼 끝나기를 기다린다.
 */
BoosterEndResult booster_process_end(const BoosterProcess *p, unsigned wait_ms);

#endif /* BYTESIEVE_BOOSTER_H */
