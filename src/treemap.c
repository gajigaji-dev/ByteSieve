#include <stdlib.h>

#include "treemap.h"

double treemap_aspect(TreemapRect r)
{
    if (r.w <= 0.0 || r.h <= 0.0) {
        return 0.0;
    }
    return r.w >= r.h ? r.w / r.h : r.h / r.w;
}

/*
 * 한 줄(넓이 합 sum, 가장 큰 넓이 amax, 가장 작은 넓이 amin)을 길이 side 인 변에 붙였을 때
 * 가장 나쁜 가로세로 비율.
 */
static double worst_ratio(double sum, double amax, double amin, double side)
{
    double s2 = side * side, sum2 = sum * sum;
    double a = s2 * amax / sum2, b = sum2 / (s2 * amin);

    return a > b ? a : b;
}

/*
 * area[first..last) 를 남은 사각형 r 의 짧은 변을 따라 한 줄로 놓고, 줄이 차지한 만큼 r 을
 * 줄인다. last_row 이면 반올림 오차가 남지 않게 남은 사각형을 모두 쓴다.
 */
static void place_row(const double *area, const size_t *index, size_t first, size_t last,
                      double sum, TreemapRect *r, int last_row, TreemapRect *out)
{
    size_t i;

    if (r->w >= r->h) {
        /* 왼쪽에 세로 기둥 하나: 너비 = 넓이 합 / 높이 */
        double colw = last_row ? r->w : sum / r->h, y = r->y;
        for (i = first; i < last; i++) {
            double h = i + 1 == last ? r->y + r->h - y : area[i] / colw;
            if (h < 0.0) {
                h = 0.0; /* 실수 오차 */
            }
            out[index[i]].x = r->x;
            out[index[i]].y = y;
            out[index[i]].w = colw;
            out[index[i]].h = h;
            y += h;
        }
        r->x += colw;
        r->w -= colw;
    } else {
        /* 위쪽에 가로 줄 하나: 높이 = 넓이 합 / 너비 */
        double rowh = last_row ? r->h : sum / r->w, x = r->x;
        for (i = first; i < last; i++) {
            double w = i + 1 == last ? r->x + r->w - x : area[i] / rowh;
            if (w < 0.0) {
                w = 0.0; /* 실수 오차 */
            }
            out[index[i]].x = x;
            out[index[i]].y = r->y;
            out[index[i]].w = w;
            out[index[i]].h = rowh;
            x += w;
        }
        r->y += rowh;
        r->h -= rowh;
    }
    if (r->w < 0.0) {
        r->w = 0.0;
    }
    if (r->h < 0.0) {
        r->h = 0.0;
    }
}

void treemap_layout(const uint64_t *weights, size_t n, TreemapRect bounds, TreemapRect *out)
{
    TreemapRect r = bounds;
    double total = 0.0, scale, *area;
    size_t *index, count = 0, i, first;

    for (i = 0; i < n; i++) {
        out[i].x = bounds.x;
        out[i].y = bounds.y;
        out[i].w = 0.0;
        out[i].h = 0.0;
        total += (double)weights[i];
    }
    if (n == 0 || total <= 0.0 || bounds.w <= 0.0 || bounds.h <= 0.0) {
        return;
    }
    /* 무게가 0 이 아닌 항목만 받은 순서대로 넓이로 바꾼다. */
    area = malloc(n * sizeof(*area));
    index = malloc(n * sizeof(*index));
    if (area == NULL || index == NULL) {
        free(area);
        free(index);
        return;
    }
    scale = bounds.w * bounds.h / total;
    for (i = 0; i < n; i++) {
        if (weights[i] > 0) {
            area[count] = (double)weights[i] * scale;
            index[count++] = i;
        }
    }

    first = 0;
    while (first < count) {
        double side = r.w < r.h ? r.w : r.h;
        double sum = area[first], amax = area[first], amin = area[first];
        double best = worst_ratio(sum, amax, amin, side);
        size_t last = first + 1;

        /* 비율이 나빠지기 직전까지 줄에 더한다. */
        while (last < count) {
            double s = sum + area[last];
            double hi = area[last] > amax ? area[last] : amax;
            double lo = area[last] < amin ? area[last] : amin;
            double ratio = worst_ratio(s, hi, lo, side);
            if (ratio > best) {
                break;
            }
            sum = s;
            amax = hi;
            amin = lo;
            best = ratio;
            last++;
        }
        place_row(area, index, first, last, sum, &r, last == count, out);
        first = last;
    }
    free(area);
    free(index);
}
