#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "treemap.h"

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                \
                    __FILE__, __LINE__, #cond);                         \
            g_failures++;                                               \
        }                                                               \
    } while (0)

#define EPS 1e-6

static TreemapRect rect(double x, double y, double w, double h)
{
    TreemapRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static int near(double a, double b)
{
    return fabs(a - b) <= EPS * (1.0 + fabs(a) + fabs(b));
}

/* 두 사각형이 넓이를 가진 채로 겹치는지. */
static int overlaps(TreemapRect a, TreemapRect b)
{
    double ox = fmin(a.x + a.w, b.x + b.w) - fmax(a.x, b.x);
    double oy = fmin(a.y + a.h, b.y + b.h) - fmax(a.y, b.y);

    return ox > EPS && oy > EPS;
}

/*
 * 배치 계약 확인: 넓이 비례, bounds 안, 겹침 없음, 넓이 합 = bounds 넓이(무게가 있을 때),
 * 무게 0 은 넓이 0. 실패 수를 반환한다.
 */
static int check_layout(const uint64_t *weights, size_t n, TreemapRect bounds, TreemapRect *out)
{
    int before = g_failures;
    double total = 0.0, sum = 0.0, area = bounds.w * bounds.h;
    size_t i, j;

    treemap_layout(weights, n, bounds, out);
    for (i = 0; i < n; i++) {
        total += (double)weights[i];
    }
    for (i = 0; i < n; i++) {
        TreemapRect r = out[i];
        double expect = total > 0.0 ? area * (double)weights[i] / total : 0.0;
        CHECK(r.w >= 0.0 && r.h >= 0.0);
        CHECK(near(r.w * r.h, expect));
        if (weights[i] == 0 || total == 0.0) {
            CHECK(r.w * r.h == 0.0);
            continue;
        }
        CHECK(r.x >= bounds.x - EPS && r.y >= bounds.y - EPS);
        CHECK(r.x + r.w <= bounds.x + bounds.w + EPS && r.y + r.h <= bounds.y + bounds.h + EPS);
        sum += r.w * r.h;
        for (j = i + 1; j < n; j++) {
            if (weights[j] > 0) {
                CHECK(!overlaps(r, out[j]));
            }
        }
    }
    if (total > 0.0) {
        CHECK(near(sum, area));
    }
    return g_failures - before;
}

static double worst_aspect(const TreemapRect *out, size_t n)
{
    double worst = 0.0;
    size_t i;

    for (i = 0; i < n; i++) {
        double a = treemap_aspect(out[i]);
        if (a > worst) {
            worst = a;
        }
    }
    return worst;
}

static void test_single_and_equal(void)
{
    uint64_t one[1] = {42}, four[4] = {5, 5, 5, 5};
    TreemapRect out[4];

    /* 하나면 bounds 전체 */
    check_layout(one, 1, rect(10, 20, 300, 100), out);
    CHECK(near(out[0].x, 10) && near(out[0].y, 20) && near(out[0].w, 300) && near(out[0].h, 100));

    /* 정사각형을 넷으로 똑같이 나누면 모두 정사각형 */
    check_layout(four, 4, rect(0, 0, 100, 100), out);
    CHECK(near(worst_aspect(out, 4), 1.0));
}

/* 논문의 예: 6×4 사각형에 6,6,4,3,2,2,1 */
static void test_paper_example(void)
{
    uint64_t w[7] = {6, 6, 4, 3, 2, 2, 1};
    TreemapRect out[7];

    check_layout(w, 7, rect(0, 0, 6, 4), out);
    /* 처음 두 항목(6,6)은 왼쪽에 3×2 두 개로 세로로 쌓인다 */
    CHECK(near(out[0].x, 0) && near(out[0].w, 3) && near(out[0].h, 2));
    CHECK(near(out[1].x, 0) && near(out[1].y, 2) && near(out[1].w, 3) && near(out[1].h, 2));
    CHECK(worst_aspect(out, 7) < 3.0);
}

static void test_zero_and_empty(void)
{
    uint64_t w[5] = {0, 10, 0, 5, 0}, zeros[3] = {0, 0, 0};
    TreemapRect out[5];

    check_layout(w, 5, rect(0, 0, 50, 30), out);
    CHECK(out[0].w * out[0].h == 0.0 && out[2].w * out[2].h == 0.0);

    /* 무게 합 0, 넓이 0 인 bounds */
    check_layout(zeros, 3, rect(0, 0, 50, 30), out);
    check_layout(w, 5, rect(5, 5, 0, 30), out);
    CHECK(out[1].w * out[1].h == 0.0);
    treemap_layout(w, 0, rect(0, 0, 10, 10), out);

    CHECK(treemap_aspect(rect(0, 0, 0, 5)) == 0.0);
    CHECK(near(treemap_aspect(rect(0, 0, 2, 8)), 4.0));
}

/* 아주 큰 값(64비트에 가까운 용량)과 크기 차이가 큰 항목 */
static void test_large_values(void)
{
    uint64_t w[4] = {UINT64_MAX / 4, UINT64_MAX / 8, 1, 1};
    TreemapRect out[4];

    /* 작은 항목은 넓이가 1e-13 px² 수준이라 실수 오차 안에서만 맞으면 된다(check_layout). */
    check_layout(w, 4, rect(0, 0, 1920, 1080), out);
    CHECK(out[0].w * out[0].h > out[1].w * out[1].h);
}

/* 무작위 무게(내림차순·정렬하지 않음): 계약을 지키고, 정렬하면 비율이 좋다 */
static int cmp_desc(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

    return x < y ? 1 : x > y ? -1 : 0;
}

static void test_random(void)
{
    enum { N = 300 };
    uint64_t *w = malloc(N * sizeof(*w));
    TreemapRect *out = malloc(N * sizeof(*out));
    unsigned seed = 12345;
    size_t i, round;

    CHECK(w != NULL && out != NULL);
    if (w == NULL || out == NULL) {
        free(w);
        free(out);
        return;
    }
    for (round = 0; round < 5; round++) {
        for (i = 0; i < N; i++) {
            seed = seed * 1103515245u + 12345u;
            /* 1 ~ 약 1e9, 일부는 0 */
            w[i] = (seed >> 8) % 7 == 0 ? 0 : 1 + ((uint64_t)(seed >> 4) * (round + 1)) % 1000000000u;
        }
        check_layout(w, N, rect(0, 0, 800, 600), out);
        qsort(w, N, sizeof(*w), cmp_desc);
        check_layout(w, N, rect(3, 7, 800, 600), out);
    }
    free(w);
    free(out);
}

int main(void)
{
    test_single_and_equal();
    test_paper_example();
    test_zero_and_empty();
    test_large_values();
    test_random();

    if (g_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all treemap tests passed\n");
    return 0;
}
