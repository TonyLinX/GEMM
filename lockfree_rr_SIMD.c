#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <immintrin.h>

#ifndef STEAL_CHUNK
#define STEAL_CHUNK 4
#endif
#define SPIN_LIMIT 1024
#define TILE_SIZE 64
#define MICRO_TILE 8
#define MEM_ALIGNMENT 64
#ifndef N_CORES
#define N_CORES 12
#endif

#define ALIGN_UP(x) (((x) + TILE_SIZE - 1) & ~(TILE_SIZE - 1))
static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("pause");
#else
    sched_yield();
#endif
}
typedef struct {
    float *A, *B, *C;
    size_t stride_a, stride_b, stride_c;
    size_t n_k;
} task_t;

typedef struct __attribute__((aligned(MEM_ALIGNMENT))) {
    task_t *tasks;       // ring buffer of tasks
    size_t capacity;     // slots per ring buffer
    size_t mask;
    atomic_size_t head;  // consumer index
    atomic_size_t tail;  // producer index
    sem_t sem;           // counts available tasks
} ring_buffer_t;

typedef struct {
    ring_buffer_t *queues;      // array of per-thread queues
    pthread_t *threads;         // worker threads
    size_t num_threads;         // number of workers
    atomic_size_t next_queue;   // for round-robin dispatch
    pthread_mutex_t done_lock;
    pthread_cond_t all_done;
    atomic_int tasks_remaining; // across all queues
    _Atomic bool shutdown;
} threadpool_t;


static inline bool try_dequeue_task(ring_buffer_t *q, task_t *out)
{
    if (sem_trywait(&q->sem) != 0)      /* 目前沒任務 → EAGAIN */
        return false;

    size_t idx = atomic_fetch_add_explicit(&q->head, 1,
                                           memory_order_relaxed) & q->mask;
    *out = q->tasks[idx];
    return true;
}

static bool steal_batch(ring_buffer_t *q, task_t *buf, size_t *n_stolen)
{
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);

    size_t available = tail - head;
    if (available <= STEAL_CHUNK)
        return false;                      /* 不夠就別偷 */

    size_t new_head = head + STEAL_CHUNK;

    /* CAS 把 head 往前推 – claim 這一段 */
    if (atomic_compare_exchange_strong_explicit(
            &q->head, &head, new_head,
            memory_order_acquire, memory_order_relaxed))
    {
        for (size_t k = 0; k < STEAL_CHUNK; ++k){
            buf[k] = q->tasks[(head + k) & q->mask];
            sem_trywait(&q->sem); 
        }
            

        *n_stolen = STEAL_CHUNK;
        return true;
    }
    return false;
}

// 一次計算 8*8 micro-tile，也是就是 64 個 float
static inline void mm_tile(const task_t *task)
{
    for (size_t ti = 0; ti < TILE_SIZE; ti += MICRO_TILE) {
        for (size_t tj = 0; tj < TILE_SIZE; tj += MICRO_TILE) {

            __m256 c[MICRO_TILE];
            for (int v = 0; v < MICRO_TILE; ++v) c[v] = _mm256_setzero_ps();

            for (size_t k = 0; k < task->n_k; ++k) {
                /* 1. broadcast A[ti~ti+7]][k] */
                __m256 a0 = _mm256_set1_ps(task->A[(ti + 0) * task->stride_a + k]);
                __m256 a1 = _mm256_set1_ps(task->A[(ti + 1) * task->stride_a + k]);
                __m256 a2 = _mm256_set1_ps(task->A[(ti + 2) * task->stride_a + k]);
                __m256 a3 = _mm256_set1_ps(task->A[(ti + 3) * task->stride_a + k]);
                __m256 a4 = _mm256_set1_ps(task->A[(ti + 4) * task->stride_a + k]);
                __m256 a5 = _mm256_set1_ps(task->A[(ti + 5) * task->stride_a + k]);
                __m256 a6 = _mm256_set1_ps(task->A[(ti + 6) * task->stride_a + k]);
                __m256 a7 = _mm256_set1_ps(task->A[(ti + 7) * task->stride_a + k]);

                /* 2. 對固定 k，把 B 的 8 個 column 分別取出 */
                const float *baseB = task->B + k; 
                const size_t sb = task->stride_b;
                
                //倒過來擺放的原因是 _mm256_set_ps 的設計是高位在前，低位在後，所以它的結構是 b[7][k]~b[0][k]，不是 b[0][k]~b[7][k]
                __m256 b = _mm256_set_ps(
                    baseB[(tj + 7) * sb],
                    baseB[(tj + 6) * sb],
                    baseB[(tj + 5) * sb],
                    baseB[(tj + 4) * sb],
                    baseB[(tj + 3) * sb],
                    baseB[(tj + 2) * sb],
                    baseB[(tj + 1) * sb],
                    baseB[(tj + 0) * sb]);   

                /* 3. FMA accumulate */
                c[0] = _mm256_fmadd_ps(a0, b, c[0]);
                c[1] = _mm256_fmadd_ps(a1, b, c[1]);
                c[2] = _mm256_fmadd_ps(a2, b, c[2]);
                c[3] = _mm256_fmadd_ps(a3, b, c[3]);
                c[4] = _mm256_fmadd_ps(a4, b, c[4]);
                c[5] = _mm256_fmadd_ps(a5, b, c[5]);
                c[6] = _mm256_fmadd_ps(a6, b, c[6]);
                c[7] = _mm256_fmadd_ps(a7, b, c[7]);
            }

            /* 4. store back 8×8 */
            for (int v = 0; v < 8; ++v)
                _mm256_storeu_ps(&task->C[(ti + v) * task->stride_c + tj], c[v]);
        }
    }
}

typedef struct {
    threadpool_t *pool;
    size_t index;
} worker_arg_t;

void *worker_thread(void *arg)
{
    worker_arg_t  *warg   = arg;
    threadpool_t  *pool   = warg->pool;
    size_t         selfID = warg->index;
    ring_buffer_t *selfQ  = &pool->queues[selfID];
    free(warg);

    task_t task;   
    task_t steal_buf[STEAL_CHUNK]; // 偷到的任務暫存在這
    size_t steal_n = 0, steal_pos = 0;
    for (;;) {
        /* 先吃完前面偷到的任務 */
        if (steal_pos < steal_n) {
            mm_tile(&steal_buf[steal_pos++]);
            atomic_fetch_sub(&pool->tasks_remaining, 1);
            if (atomic_load(&pool->tasks_remaining) == 0) {
                pthread_mutex_lock(&pool->done_lock);
                pthread_cond_broadcast(&pool->all_done);
                pthread_mutex_unlock(&pool->done_lock);
            }
            continue;
        }

        /* 試著從自己 queue 拿任務 */
        if (try_dequeue_task(selfQ, &task))
            goto got_job;

        /* busy-wait + work stealing */
        for (int spin = 0; spin < SPIN_LIMIT; ++spin) {
            for (size_t off = 1; off < pool->num_threads; ++off) {
                size_t victimID = (selfID + off) % pool->num_threads;
                ring_buffer_t *vQ = &pool->queues[victimID];

                if (steal_batch(vQ, steal_buf, &steal_n)) {
                    steal_pos = 0;
                    goto continue_loop;
                }
            }

            if (atomic_load(&pool->shutdown))
                return NULL;

            cpu_relax();
        }

        /* sleep 等自己 queue 的任務來 */
        sem_wait(&selfQ->sem);
        if (atomic_load(&pool->shutdown))
            return NULL;

        size_t idx = atomic_fetch_add_explicit(&selfQ->head, 1, memory_order_relaxed) % selfQ->capacity;
        task = selfQ->tasks[idx];

    got_job:
        mm_tile(&task);
        atomic_fetch_sub(&pool->tasks_remaining, 1);
        if (atomic_load(&pool->tasks_remaining) == 0) {
            pthread_mutex_lock(&pool->done_lock);
            pthread_cond_broadcast(&pool->all_done);
            pthread_mutex_unlock(&pool->done_lock);
        }

    continue_loop:
        continue;
    }
    return NULL;
}

int next_two_power(int n)
{
    int power = 1;
    while (power < n)
        power <<= 1;
    return power;
}

void init_thread_pool(threadpool_t *pool, size_t num_threads, size_t capacity)
{
    *pool = (threadpool_t){
        .num_threads = num_threads,
        .threads = malloc(num_threads * sizeof(pthread_t)),
        .queues = calloc(num_threads, sizeof(ring_buffer_t)),
    };
    atomic_init(&pool->next_queue, 0);
    pthread_mutex_init(&pool->done_lock, NULL);
    pthread_cond_init(&pool->all_done, NULL);

    for (size_t i = 0; i < num_threads; i++) {
        ring_buffer_t *q = &pool->queues[i];
        q->capacity = next_two_power(capacity); // ensure power of two
        q->mask = q->capacity - 1; // for modulo operations
        q->tasks = calloc(q->capacity, sizeof(task_t));
        atomic_init(&q->head, 0);
        atomic_init(&q->tail, 0);
        sem_init(&q->sem, 0, 0);

        worker_arg_t *warg = malloc(sizeof(worker_arg_t));
        *warg = (worker_arg_t){.pool = pool, .index = i};
        pthread_create(&pool->threads[i], NULL, worker_thread, warg);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % N_CORES, &cpuset);
        pthread_setaffinity_np(pool->threads[i], sizeof(cpu_set_t), &cpuset);
    }
}

void enqueue(threadpool_t *pool, task_t task)
{
    size_t qid = atomic_fetch_add(&pool->next_queue, 1) % pool->num_threads;
    ring_buffer_t *q = &pool->queues[qid];
    size_t idx = atomic_fetch_add(&q->tail, 1) & q->mask;
    q->tasks[idx] = task;
    atomic_fetch_add(&pool->tasks_remaining, 1);
    sem_post(&q->sem);
}

void wait_for_completion(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->done_lock);
    while (atomic_load(&pool->tasks_remaining) > 0)
        pthread_cond_wait(&pool->all_done, &pool->done_lock);
    pthread_mutex_unlock(&pool->done_lock);
}

void destroy_thread_pool(threadpool_t *pool)
{
    atomic_store(&pool->shutdown, true);
    for (size_t i = 0; i < pool->num_threads; i++)
        sem_post(&pool->queues[i].sem); // wake workers

    for (size_t i = 0; i < pool->num_threads; i++)
        pthread_join(pool->threads[i], NULL);

    for (size_t i = 0; i < pool->num_threads; i++) {
        ring_buffer_t *q = &pool->queues[i];
        free(q->tasks);
        sem_destroy(&q->sem);
    }
    free(pool->queues);
    free(pool->threads);
    pthread_mutex_destroy(&pool->done_lock);
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
                .A = A + i * n,
                .B = B + j * n,
                .C = C + i * p + j,
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

    float *A = malloc(m * n * sizeof(float));
    float *B = malloc(n * p * sizeof(float));
    float *C = malloc(m * p * sizeof(float));
    fill_rand(A, m * n);
    fill_rand(B, n * p);

    size_t padm = ALIGN_UP(m), padn = ALIGN_UP(n), padp = ALIGN_UP(p);
    size_t capacity = (padm / TILE_SIZE) * (padp / TILE_SIZE) / N_CORES + 1; 
    if (capacity < STEAL_CHUNK + 1) capacity = STEAL_CHUNK + 1;
    init_thread_pool(&pool, N_CORES, capacity);

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