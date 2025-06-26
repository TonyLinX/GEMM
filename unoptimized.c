#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TILE_SIZE 64

static inline void mm_tile(float *A,
                           float *B,
                           float *C,
                           size_t stride_a,
                           size_t stride_b,
                           size_t stride_c)
{
    for (size_t i = 0; i < TILE_SIZE; i++) {
        for (size_t j = 0; j < TILE_SIZE; j++) {
            const size_t ic = i * stride_c + j;
            for (size_t k = 0; k < TILE_SIZE; k++) {
                const size_t ia = i * stride_a + k;
                const size_t ib = k * stride_b + j;
                C[ic] += A[ia] * B[ib];
            }
        }
    }
}

static inline void mm_edge(float *A,
                           float *B,
                           float *C,
                           size_t stride_a,
                           size_t stride_b,
                           size_t stride_c,
                           size_t tile_m,
                           size_t tile_n,
                           size_t tile_p)
{
    for (size_t i = 0; i < tile_m; i++) {
        for (size_t j = 0; j < tile_p; j++) {
            const size_t ic = i * stride_c + j;
            for (size_t k = 0; k < tile_n; k++) {
                const size_t ia = i * stride_a + k;
                const size_t ib = k * stride_b + j;
                C[ic] += A[ia] * B[ib];
            }
        }
    }
}


/*
    * Matrix multiplication with tiling and edge handling.
    * A: m x n matrix
    * B: n x p matrix
    * C: m x p matrix (result)
    * m: number of rows in A and C
    * n: number of columns in A and rows in B
    * p: number of columns in B and C
    *
    * The function uses a tile size of TILE_SIZE x TILE_SIZE for the main computation,
    * and handles edge cases where the dimensions are not multiples of TILE_SIZE.
    *
*/
void mm(float *A, float *B, float *C, size_t m, size_t n, size_t p)
{
    const size_t inm = m - m % TILE_SIZE; // 可完整切 2×2 tile 的最後一列索引+1
    const size_t inn = n - n % TILE_SIZE; // 可完整切 2×2 tile 的最後一行索引+1
    const size_t inp = p - p % TILE_SIZE; // 可完整切 2×2 tile 的最後一欄索引+1

    memset(C, 0, m * p * sizeof(float));

    for (size_t i = 0; i < m; i += TILE_SIZE) {
        for (size_t j = 0; j < p; j += TILE_SIZE) {
            const size_t ic = i * p + j;
            for (size_t k = 0; k < n; k += TILE_SIZE) {
                const size_t ia = i * n + k;
                const size_t ib = k * p + j;

                if (i < inm && j < inp && k < inn) {
                    mm_tile(A + ia, B + ib, C + ic, n, p, p);
                } else {
                    const size_t tile_m =
                        (i + TILE_SIZE > m) ? (m - i) : TILE_SIZE;
                    const size_t tile_n =
                        (k + TILE_SIZE > n) ? (n - k) : TILE_SIZE;
                    const size_t tile_p =
                        (j + TILE_SIZE > p) ? (p - j) : TILE_SIZE;
                    mm_edge(A + ia, B + ib, C + ic, n, p, p, tile_m, tile_n,
                            tile_p);
                }
            }
        }
    }
}

void fill_rand(float *arr, size_t size)
{
    for (size_t i = 0; i < size; i++)
        arr[i] = (float) (rand() % 10);
}

void print_mat(const float *mat, size_t m, size_t n)
{
    for (size_t i = 0; i < m; i++) {
        for (size_t j = 0; j < n; j++) {
            printf("%.2f", mat[i * n + j]);
            if (j < n - 1)
                printf(", ");
        }
        printf("\n");
    }
    printf("---\n");
}

int parse_int(const char *str)
{
    char *end;
    long val = strtol(str, &end, 10);
    if (*end || str == end) {
        fprintf(stderr, "Invalid integer: '%s'\n", str);
        exit(1);
    }
    return (int) val;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <m> <n> <p>\n", argv[0]);
        return 1;
    }

    srand(314);
    size_t m = parse_int(argv[1]);
    size_t n = parse_int(argv[2]);
    size_t p = parse_int(argv[3]);

    float *A = malloc(m * n * sizeof(float));
    float *B = malloc(n * p * sizeof(float));
    float *C = malloc(m * p * sizeof(float));

    fill_rand(A, m * n);
    fill_rand(B, n * p);

#ifdef VALIDATE
    print_mat(A, m, n);
    print_mat(B, n, p);
#endif

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    mm(A, B, C, m, n, p);
    clock_gettime(CLOCK_MONOTONIC, &end);

#ifndef VALIDATE
    double elapsed =
        (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("%.9f\n", elapsed);
#endif

#ifdef VALIDATE
    print_mat(C, m, p);
#endif

    free(A);
    free(B);
    free(C);

    return 0;
}
