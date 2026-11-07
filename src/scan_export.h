#ifndef BYTESIEVE_SCAN_EXPORT_H
#define BYTESIEVE_SCAN_EXPORT_H

#include <stdint.h>
#include <wchar.h>

#include "scan_model.h"

/*
 * 스캔 결과 내보내기(CSV·HTML 보고서). 모델은 읽기만 한다.
 *
 * 공통
 *   - 파일은 "<file>.tmp" 에 쓴 뒤 file 로 바꿔치기한다. 실패하면 임시 파일을 지우고 기존 file 은
 *     그대로 둔다(반쯤 쓴 파일이 남지 않음). 덮어쓸지는 호출자가 미리 확인한다.
 *   - 큰 모델도 버퍼(64 KiB)로 나눠 쓰므로 결과 전체를 메모리에 만들지 않는다.
 *   - 모델이 비었거나 SCAN_FATAL 이면 쓰지 않고 ERROR_INVALID_PARAMETER 로 실패한다.
 *     취소·일부 실패한 스캔은 그때까지 모은 결과를 내보내고 상태를 함께 적는다.
 *   - 성공 0, 실패 -1(*error 에 Windows 오류, NULL 허용).
 */

typedef struct {
    uint64_t finished_time; /* 스캔을 마친 시각(FILETIME, UTC). 0 이면 적지 않는다 */
    uint64_t elapsed_ms;    /* 스캔에 걸린 시간. 0 이면 적지 않는다 */
} ExportInfo;

/* HTML 보고서의 용량 순위·최상위 폴더 표 최대 행 수. */
#define EXPORT_RANK_ROWS 100
/* HTML 보고서에 나열하는 오류 최대 개수. */
#define EXPORT_ERROR_ROWS 50

/*
 * 모든 폴더를 CSV 로 쓴다. Excel 에서 한글이 깨지지 않도록 UTF-8 BOM 과 CRLF 를 쓴다.
 *   열: 경로, 폴더명, 깊이, 크기(바이트), 크기, 디스크 사용(바이트), 파일 수(하위 포함),
 *       하위 폴더 수, 바로 아래 파일 수, 바로 아래 파일 크기(바이트), 비율(%), 상태
 *   행: 첫 행은 대상 폴더(루트), 이어서 나머지 폴더를 크기 내림차순(같으면 열거 순서).
 *   문자열 칸은 큰따옴표로 감싸고(안의 " 는 두 번), =·+·-·@·탭·CR 로 시작하면 Excel 이 수식으로
 *   실행하지 않도록 앞에 ' 를 붙인다. 숫자 칸은 정렬할 수 있게 그대로 쓴다.
 */
int export_csv(const ScanModel *model, const wchar_t *file, unsigned long *error);

/*
 * 한 파일짜리 HTML 보고서(UTF-8, 인라인 CSS, 스크립트 없음)를 쓴다.
 *   전체 요약(대상·상태·시각·용량·디스크 사용·파일·폴더·오류), 파일·폴더 통계,
 *   디스크 사용 분석, 최상위 폴더 구성, 용량 순위 상위 EXPORT_RANK_ROWS 개 표, 오류 목록.
 *   폴더 이름·경로는 모두 HTML 이스케이프한다. info 는 NULL 허용.
 */
int export_html(const ScanModel *model, const ExportInfo *info, const wchar_t *file,
                unsigned long *error);

/* 노드 상태 표시 문구("정상", "읽기 실패", "일부 누락", "취소됨", "링크", "깊이 제한"). */
const wchar_t *export_node_state(uint32_t flags);

/* 내보내기 형식(GUI 저장 대화 상자의 기본 이름·확장자). */
typedef enum { EXPORT_KIND_CSV = 0, EXPORT_KIND_HTML, EXPORT_KIND_COUNT } ExportKind;

/* 확장자("csv", "html"). */
const wchar_t *export_kind_ext(ExportKind kind);

/*
 * 기본 파일 이름 "ByteSieve-<대상 폴더 이름>-YYYYMMDD-HHMM.<확장자>"를 buf 에 쓴다.
 *   - 대상 폴더 이름은 root 의 마지막 경로 요소(끝 구분자 무시)이며 63자까지만 쓴다.
 *   - 파일 이름에 못 쓰는 글자(\ / : * ? " < > | 와 제어 문자)는 _ 로 바꾸고, 끝의 _ 는 지운다("D:\" → "D").
 *   - 이름이 비면 "ByteSieve-YYYYMMDD-HHMM.<확장자>".
 * time 은 지역 시각(NULL 이면 지금 시각, GetLocalTime). root 가 NULL 이면 빈 이름으로 본다.
 */
typedef struct {
    unsigned year, month, day, hour, minute;
} ExportTime;

void export_default_name(const wchar_t *root, ExportKind kind, const ExportTime *time,
                         wchar_t *buf, size_t len);

/* 내보내기 실패 원인(사용자용 한 문장). Windows 오류 번호로 고른다. */
const wchar_t *export_error_text(unsigned long error);

#endif /* BYTESIEVE_SCAN_EXPORT_H */
