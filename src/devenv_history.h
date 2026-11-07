#ifndef BYTESIEVE_DEVENV_HISTORY_H
#define BYTESIEVE_DEVENV_HISTORY_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/*
 * 개발 캐시·가상 디스크 용량 기록(증가 추적). 기록 폴더(scan_history 와 같은 곳)의 DEVHIST_FILE_NAME 에
 * UTF-8 TSV 로 둔다: 첫 줄 "# bytesieve devenv history v1", 이후 "시각(FILETIME)\tid\t바이트\t경로".
 *   - 저장은 임시 파일에 쓴 뒤 바꿔치기(MoveFileExW REPLACE_EXISTING|WRITE_THROUGH)라 중간에 끊겨도
 *     이전 기록이 남는다.
 *   - 읽다가 형식이 맞지 않는 줄은 건너뛴다(파일 전체를 버리지 않는다).
 *   - (id, 경로)마다 최근 DEVHIST_KEEP 개만 남긴다.
 * 아무것도 지우지 않으며 기록 파일 외에는 쓰지 않는다.
 */

#define DEVHIST_FILE_NAME L"devenv-history.tsv"
#define DEVHIST_KEEP 60

typedef struct {
    uint64_t time;  /* 기록 시각(FILETIME UTC) */
    wchar_t *id;    /* 캐시 종류("npm") 또는 "vdisk"(소유) */
    wchar_t *path;  /* 위치(소유) */
    uint64_t bytes; /* 그때 용량 */
} DevRecord;

typedef struct {
    DevRecord *items; /* 파일 순서(오래된 것부터) */
    size_t len;
    size_t cap;
    size_t skipped;   /* 읽을 때 건너뛴 줄 */
} DevHistory;

void devhist_init(DevHistory *h);
void devhist_free(DevHistory *h);

/* file 을 읽는다(이전 내용은 지운다). 없으면 빈 기록으로 0. 메모리가 부족하면 -1. */
int devhist_load(DevHistory *h, const wchar_t *file);

/* file 에 저장한다(임시 파일 → 바꿔치기). 성공 0, 실패 -1(*error 에 Windows 오류, NULL 가능). */
int devhist_save(const DevHistory *h, const wchar_t *file, unsigned long *error);

/* 기록 하나를 더한다(같은 (id, 경로)가 DEVHIST_KEEP 개를 넘으면 가장 오래된 것을 뺀다). 성공 0. */
int devhist_add(DevHistory *h, uint64_t time, const wchar_t *id, const wchar_t *path,
                uint64_t bytes);

/* (id, 경로, 대소문자 무시)의 가장 최근 기록. 없으면 NULL. */
const DevRecord *devhist_latest(const DevHistory *h, const wchar_t *id, const wchar_t *path);

/* dir\DEVHIST_FILE_NAME (malloc). */
wchar_t *devhist_file_in(const wchar_t *dir);

#endif /* BYTESIEVE_DEVENV_HISTORY_H */
