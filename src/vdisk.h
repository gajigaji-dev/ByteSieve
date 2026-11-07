#ifndef BYTESIEVE_VDISK_H
#define BYTESIEVE_VDISK_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/*
 * WSL2·Docker Desktop 가상 디스크(.vhdx) 찾기. 읽기만 하며 아무것도 바꾸지 않는다(관리자 권한 불필요).
 * 가상 디스크는 안에서 파일을 지워도 저절로 줄지 않아 디스크를 크게 차지하기 쉽다. ByteSieve 는 정리 대상으로
 * 삼지 않고(지우면 배포판·컨테이너가 모두 사라진다) 크기와 줄이는 방법만 알려 준다.
 *
 *   WSL    HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss\{GUID} 의 DistributionName·BasePath·
 *          VhdFileName(없으면 ext4.vhdx). 테스트는 환경 변수 BYTESIEVE_WSL_KEY 로 HKCU 아래 다른 키를 보게 한다.
 *   Docker %LOCALAPPDATA%\Docker\wsl\ 아래 data\ext4.vhdx, disk\docker_data.vhdx, main\ext4.vhdx,
 *          distro\ext4.vhdx. WSL 목록에 같은 파일이 있으면 Docker 로 한 번만 센다.
 * 파일이 없는 항목(WSL1 배포판 등)은 넣지 않는다.
 */

typedef enum {
    VDISK_WSL = 0, /* WSL 배포판 */
    VDISK_DOCKER   /* Docker Desktop */
} VdiskKind;

typedef struct {
    VdiskKind kind;
    wchar_t *name;     /* 배포판 이름 또는 "Docker Desktop (data)" 등(소유) */
    wchar_t *path;     /* .vhdx 전체 경로(소유) */
    uint64_t size;     /* 파일 크기(가상 디스크가 늘어난 크기) */
    uint64_t alloc;    /* 실제 점유(스파스 파일이면 더 작다) */
    uint64_t modified; /* 마지막 수정 시각(FILETIME UTC) */
    int sparse;        /* 스파스 파일(WSL --set-sparse) */
} Vdisk;

typedef struct {
    Vdisk *items;
    size_t len;
    size_t cap;
} VdiskList;

void vdisk_list_init(VdiskList *list);
void vdisk_list_free(VdiskList *list);

/* 가상 디스크를 찾는다(이전 내용은 지운다). 메모리가 부족하면 -1. */
int vdisk_find(VdiskList *list);

/*
 * 줄이는 명령(여러 줄, 언어와 무관): WSL 은 "wsl --shutdown" 뒤 Optimize-VHD(Hyper-V) 또는 diskpart compact,
 * Docker 는 docker system prune 뒤 같은 방법. 관리자 권한이 필요한 명령이 있다.
 */
void vdisk_hint(const Vdisk *v, wchar_t *buf, size_t len);

const wchar_t *vdisk_kind_name(VdiskKind kind); /* "wsl", "docker" */

#endif /* BYTESIEVE_VDISK_H */
