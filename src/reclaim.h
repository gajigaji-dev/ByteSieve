#ifndef BYTESIEVE_RECLAIM_H
#define BYTESIEVE_RECLAIM_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "scanner.h"

/*
 * 실제 확보 가능 용량. 정리 대상을 지웠을 때(휴지통·격리 폴더를 비운 뒤) 디스크에서 실제로 비는
 * 용량을 센다. 아무것도 바꾸지 않는다(파일을 읽기 속성으로만 조회한다).
 *
 * 논리 크기 합과 다른 점:
 *   - 파일마다 할당 크기를 쓴다. NTFS 압축·스파스 파일은 실제 점유량(GetCompressedFileSizeW),
 *     클라우드 전용 자리표시자(OneDrive 등)는 할당 크기(보통 0)라 지워도 거의 비지 않는다.
 *   - MFT 에 상주하는 아주 작은 파일(할당 크기가 클러스터 배수가 아님)은 지워도 여유 공간이 늘지
 *     않으므로 0 으로 센다.
 *   - 하드 링크: 같은 실제 파일은 한 번만 센다. 링크 수가 대상 안에서 본 경로 수보다 많으면
 *     대상 밖에 다른 링크가 남아(pnpm store 와 공유하는 node_modules 등) 지워도 비지 않으므로
 *     확보 가능에서 빼고 shared 로 센다.
 *   - 재분석 지점(심볼릭 링크·정션)은 대상이 다른 곳에 있으므로 세지 않는다.
 *
 * 조회 방법: Windows 11 24H2 이상은 GetFileInformationByName(FileStatByNameInfo)으로 파일을 열지
 * 않고 조회한다. 없거나 실패하면 파일을 FILE_READ_ATTRIBUTES 로 열어(공유 모두 허용, 데이터는 읽지
 * 않음) 조회한다. 조회하지 못한 파일은 열거로 얻은 할당 크기(없으면 논리 크기)를 확보 가능에 넣고
 * unknown_files 로 센다(대부분의 파일은 링크가 하나이므로).
 *
 * 링크 판정은 그룹(정리 항목 하나) 단위다. 서로 다른 항목이 같은 실제 파일을 나누면 각 항목에서
 * shared 로 보므로, 여러 항목을 함께 지울 때의 실제 확보 용량은 합보다 클 수 있다(작게 말하는 쪽).
 */

/* 파일 하나의 조회 결과. */
typedef struct {
    uint64_t alloc;   /* 지우면 비는 점유 바이트(MFT 상주 파일은 0) */
    uint64_t file_id; /* 볼륨 안의 파일 ID(0 이면 모름) */
    uint32_t links;   /* 하드 링크 수(0 이면 모름) */
    int known;        /* 조회에 성공했으면 1 */
} ReclaimFile;

/* 항목 하나의 합계. */
typedef struct {
    uint64_t bytes;         /* 실제 확보 가능 */
    uint64_t shared_bytes;  /* 대상 밖에 링크가 남아 확보되지 않는 실제 파일의 점유 합 */
    uint64_t shared_files;
    uint64_t unknown_files; /* 조회하지 못해 추정한 파일 */
    uint64_t files;         /* 센 파일(경로) 수 */
} ReclaimSum;

void reclaim_sum_add(ReclaimSum *to, const ReclaimSum *from);

/*
 * 파일 하나를 조회한다. path 는 일반·확장("\\?\") 경로 모두 받는다. attributes·size·alloc_hint 는
 * 열거로 얻은 값이며(alloc_hint 를 모르면 size), 조회에 실패하면 alloc 에 alloc_hint 를 쓴다.
 * 클라우드 전용 파일의 내용은 불러오지 않는다(이름으로 조회하거나 FILE_FLAG_OPEN_NO_RECALL 로 연다).
 * 조회에 성공하면 1.
 */
int reclaim_query(const wchar_t *path, uint32_t attributes, uint64_t size, uint64_t alloc_hint,
                  ReclaimFile *out);

/* 테스트용: 1 이면 GetFileInformationByName 을 쓰지 않고 파일을 열어 조회한다(대체 경로 검사). */
void reclaim_force_handle_query(int on);
/* 이 실행에서 GetFileInformationByName 을 쓸 수 있으면 1. */
int reclaim_by_name_available(void);

/*
 * 링크가 여러 개인 실제 파일을 (그룹, 파일 ID)로 모아 두는 표. 링크가 하나뿐이거나 모르는 파일은
 * 표에 넣지 않고 바로 합계에 더한다. 메모리가 모자라면 그 파일은 확보 가능으로 추정한다.
 */
typedef struct {
    uint64_t file_id;
    uint64_t alloc;
    uint32_t group;
    uint32_t links;
    uint32_t seen;
    uint32_t used;
} ReclaimSlot;

typedef struct {
    ReclaimSlot *slots;
    size_t cap; /* 2의 거듭제곱(0 이면 비었음) */
    size_t len;
} ReclaimMap;

void reclaim_map_init(ReclaimMap *map);
void reclaim_map_free(ReclaimMap *map);

/* group 의 파일 하나를 센다. 링크가 하나뿐인 파일은 sum 에 바로 더하고, 여러 개면 표에 모은다. */
void reclaim_tally(ReclaimMap *map, uint32_t group, const ReclaimFile *f, ReclaimSum *sum);

/*
 * 표에 모은 파일을 정산한다. 실제 파일마다 apply(group, alloc, freed, ctx) 를 한 번 부른다
 * (freed 가 1 이면 대상 안에서 모든 링크를 봤으므로 확보 가능). 정산 뒤 표는 비운다.
 */
typedef void (*reclaim_apply_fn)(uint32_t group, uint64_t alloc, int freed, void *ctx);
void reclaim_settle(ReclaimMap *map, reclaim_apply_fn apply, void *ctx);

/* reclaim_settle 의 apply 로 ReclaimSum 하나에 더할 때 쓴다(ctx = ReclaimSum *). */
void reclaim_apply_sum(uint32_t group, uint64_t alloc, int freed, void *ctx);

/*
 * dir 아래 파일을 모두 열거해(scan_tree_ex) 실제 확보 가능 용량을 sum 에 더한다(dir 하나가 한 그룹).
 * report 는 scan_tree_ex 와 같이 누적된다(호출자가 초기화). cancel 은 NULL 일 수 있다.
 * 반환값은 scan_tree_ex 와 같다.
 */
ScanStatus reclaim_scan_tree(const wchar_t *dir, ScanReport *report, ReclaimSum *sum,
                             int (*cancel)(void *ctx), void *ctx);

/* 파일 하나(정리 항목이 파일일 때)를 sum 에 더한다. 링크가 여러 개면 밖에 링크가 남으므로 shared. */
void reclaim_add_file(const wchar_t *path, uint32_t attributes, uint64_t size, ReclaimSum *sum);

#endif /* BYTESIEVE_RECLAIM_H */
