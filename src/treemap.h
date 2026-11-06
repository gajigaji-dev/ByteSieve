#ifndef BYTESIEVE_TREEMAP_H
#define BYTESIEVE_TREEMAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * 스퀘어리파이드 트리맵 배치(Bruls·Huizing·van Wijk, 2000). 사각형 하나를 무게(용량)에
 * 비례하는 넓이로 나누되, 각 조각의 가로세로 비율이 1 에 가깝도록 줄 단위로 쌓는다.
 * 화면·스캔 모델과 무관한 순수 계산이며 좌표는 실수다(화면에서는 반올림해 쓴다).
 */

typedef struct {
    double x, y; /* 왼쪽 위 */
    double w, h; /* 너비·높이(0 이상) */
} TreemapRect;

/*
 * weights 의 n 개 항목을 bounds 안에 배치해 out[i] 에 쓴다(out 은 n 개).
 *   - 무게가 0 인 항목은 넓이 0(bounds 의 왼쪽 위 점)이다.
 *   - 무게 합이 0 이거나 bounds 의 넓이가 0 이면 모두 넓이 0 이다.
 *   - 그 밖의 항목은 넓이가 무게에 비례하고, 서로 겹치지 않으며, 합쳐서 bounds 를 빈틈없이
 *     채운다.
 * 무게를 내림차순으로 주어야 비율이 좋다(정렬하지 않아도 위 조건은 지킨다).
 */
void treemap_layout(const uint64_t *weights, size_t n, TreemapRect bounds, TreemapRect *out);

/* 긴 변 / 짧은 변(1 이상). 넓이가 0 이면 0. */
double treemap_aspect(TreemapRect r);

#endif /* BYTESIEVE_TREEMAP_H */
