#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TILE_SIZE 64
#define MICRO_TILE 8
#define MEM_ALIGNMENT 64
#ifndef N_CORES
#define N_CORES 12
#endif

#define ALIGN_UP(x) (((x) + TILE_SIZE - 1) & ~(TILE_SIZE - 1))

typedef struct {
    float *A, *B, *C;
    size_t stride_a, stride_b, stride_c;
    size_t n_k;
} task_t;

typedef struct queue_node_t {
    task_t task;
    struct queue_node_t *next;
} queue_node_t;

typedef struct {
    queue_node_t *head, *tail;
    pthread_mutex_t lock;
    pthread_cond_t not_empty, all_done;
    pthread_t *threads;
    size_t num_threads;
    atomic_int tasks_remaining;
    _Atomic bool shutdown;
} threadpool_t;

static inline void mm_tile(const task_t *task)
{
    for (size_t ti = 0; ti < TILE_SIZE; ti += MICRO_TILE) {
        for (size_t tj = 0; tj < TILE_SIZE; tj += MICRO_TILE) {
            float sum[MICRO_TILE][MICRO_TILE] = {0};
            for (size_t k = 0; k < task->n_k; k++) {
                for (size_t i = 0; i < MICRO_TILE; i++) {
                    float a = task->A[(ti + i) * task->stride_a + k];
                    for (size_t j = 0; j < MICRO_TILE; j++)
                        sum[i][j] += a * task->B[(tj + j) * task->stride_b + k];
                }
            }
            for (size_t i = 0; i < MICRO_TILE; i++) {
                for (size_t j = 0; j < MICRO_TILE; j++)
                    task->C[(ti + i) * task->stride_c + (tj + j)] = sum[i][j];
            }
        }
    }
}

void *worker_thread(void *arg)
{
    threadpool_t *pool = arg;
    while (!atomic_load(&pool->shutdown)) {
        pthread_mutex_lock(&pool->lock);
        while (!pool->head && !atomic_load(&pool->shutdown))
            pthread_cond_wait(&pool->not_empty, &pool->lock);

        //如果是被「關閉通知」喚醒，那這條 thread 不該再做事，直接退出：
        if (atomic_load(&pool->shutdown)) {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }

        queue_node_t *node = pool->head;
        pool->head = node->next;
        if (!pool->head)
            pool->tail = NULL;
        pthread_mutex_unlock(&pool->lock);

        mm_tile(&node->task);
        free(node);

        //任務數量減 1，使用 atomic 確保 thread-safe
        atomic_fetch_sub(&pool->tasks_remaining, 1);

        if (atomic_load(&pool->tasks_remaining) == 0){
            pthread_mutex_lock(&pool->lock);
            pthread_cond_broadcast(&pool->all_done);
            pthread_mutex_unlock(&pool->lock);
        }
    }
    return NULL;
}

void init_thread_pool(threadpool_t *pool, size_t num_threads)
{
    *pool = (threadpool_t){
        .num_threads = num_threads,
        .threads = malloc(num_threads * sizeof(pthread_t)),
    };
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->all_done, NULL);

    for (size_t i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % N_CORES, &cpuset);
        pthread_setaffinity_np(pool->threads[i], sizeof(cpu_set_t), &cpuset);
    }
}

void enqueue(threadpool_t *pool, task_t task)
{
    queue_node_t *node = malloc(sizeof(queue_node_t));
    *node = (queue_node_t){.task = task, .next = NULL};

    pthread_mutex_lock(&pool->lock);
    atomic_fetch_add(&pool->tasks_remaining, 1);
    if (pool->tail)
        pool->tail->next = node;
    else
        pool->head = node;
    pool->tail = node;

    //有一條新的任務近來，喚醒一條 worker thread
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
}

void wait_for_completion(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->lock);
    while (atomic_load(&pool->tasks_remaining) > 0)
        pthread_cond_wait(&pool->all_done, &pool->lock);
    pthread_mutex_unlock(&pool->lock);
}

void destroy_thread_pool(threadpool_t *pool)
{
    atomic_store(&pool->shutdown, true);
    pthread_mutex_lock(&pool->lock);
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);

    for (size_t i = 0; i < pool->num_threads; i++)
        pthread_join(pool->threads[i], NULL);

    while (pool->head) {
        queue_node_t *tmp = pool->head;
        pool->head = tmp->next;
        free(tmp);
    }
    free(pool->threads);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->all_done);
}

void mm(float *A,
        float *B,
        float *C,
        size_t m,
        size_t n,
        size_t p,
        threadpool_t *pool)
{
    for (size_t i = 0; i < m; i += TILE_SIZE) {
        for (size_t j = 0; j < p; j += TILE_SIZE) {
            task_t task = {
                .A = A + i * n,       //轉成一維的列
                .B = B + j * n,      //轉成一維的行
                .C = C + i * p + j, //轉成一維的列行
                .stride_a = n,
                .stride_b = n,
                .stride_c = p,
                .n_k = n,
            };
            enqueue(pool, task);
        }
    }
    wait_for_completion(pool);
}

float *pad_mat(const float *src, size_t r, size_t c, size_t padr, size_t padc)
{
    float *dst = aligned_alloc(MEM_ALIGNMENT, padr * padc * sizeof(float));
    memset(dst, 0, padr * padc * sizeof(float));
    for (size_t i = 0; i < r; i++)
        memcpy(dst + i * padc, src + i * c, c * sizeof(float));
    return dst;
}

float *pad_t_mat(const float *src, size_t r, size_t c, size_t padr, size_t padc)
{
    float *dst = aligned_alloc(MEM_ALIGNMENT, padr * padc * sizeof(float));
    memset(dst, 0, padr * padc * sizeof(float));
    for (size_t i = 0; i < r; i++)
        for (size_t j = 0; j < c; j++)
            dst[j * padr + i] = src[i * c + j];
    return dst;
}

void unpad_mat(const float *src,       
               float *dst,
               size_t r,
               size_t c,
               size_t padr,
               size_t padc)
{
    for (size_t i = 0; i < r; i++)
        memcpy(dst + i * c, src + i * padc, c * sizeof(float));
}

void fill_rand(float *arr, size_t size)
{
    for (size_t i = 0; i < size; i++)
        arr[i] = (float)rand() / RAND_MAX;
}

size_t parse_int(const char *s)
{
    return strtoul(s, NULL, 10);
}
void print_mat(const float *mat, size_t m, size_t n)
{
    for (size_t i = 0; i < m; i++) {
        for (size_t j = 0; j < n; j++) {
            printf("%.6f", mat[i * n + j]);
            if (j < n - 1)
                printf(", ");
        }
        printf("\n");
    }
    printf("---\n");
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <m> <n> <p>\n", argv[0]);
        return 1;
    }

    size_t m = parse_int(argv[1]);
    size_t n = parse_int(argv[2]);
    size_t p = parse_int(argv[3]);

    threadpool_t pool;
    init_thread_pool(&pool, N_CORES);

    float *A = malloc(m * n * sizeof(float));
    float *B = malloc(n * p * sizeof(float));
    float *C = malloc(m * p * sizeof(float));
    fill_rand(A, m * n);
    fill_rand(B, n * p);

    size_t padm = ALIGN_UP(m), padn = ALIGN_UP(n), padp = ALIGN_UP(p);
    float *padA = pad_mat(A, m, n, padm, padn);
    float *padB = pad_t_mat(B, n, p, padn, padp);
    float *padC = aligned_alloc(MEM_ALIGNMENT, padm * padp * sizeof(float));
    memset(padC, 0, padm * padp * sizeof(float));

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    mm(padA, padB, padC, padm, padn, padp, &pool);
    clock_gettime(CLOCK_MONOTONIC, &end);

    #ifndef VALIDATE
        double elapsed = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;
        printf("Time: %.6f sec\n", elapsed);
    #endif

    #ifdef VALIDATE
        print_mat(A, m, n);
        print_mat(B, n, p);
    #endif

    unpad_mat(padC, C, m, p, padm, padp);

    #ifdef VALIDATE
        print_mat(C, m, p);
    #endif
    free(A);
    free(B);
    free(C);
    free(padA);
    free(padB);
    free(padC);
    destroy_thread_pool(&pool);
    return 0;
}
