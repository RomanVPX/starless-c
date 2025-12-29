#include "parallel.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
    #include <windows.h>
    typedef HANDLE thread_handle_t;
    #define THREAD_FUNC_RETURN DWORD WINAPI
    #define THREAD_FUNC_CALL  __stdcall
    // Use InterlockedAdd for atomic operations on Windows
    #define ATOMIC_FETCH_ADD(ptr, val) InterlockedExchangeAdd((LONG*)(ptr), (LONG)(val))
#else
    #include <pthread.h>
    #include <stdatomic.h>
    #include <string.h>
    typedef pthread_t thread_handle_t;
    #define THREAD_FUNC_RETURN void *
    #define THREAD_FUNC_CALL
    // Use C11 stdatomic
    #define ATOMIC_FETCH_ADD(ptr, val) atomic_fetch_add((atomic_int*)(ptr), (val))
#endif

typedef struct {
#if defined(_WIN32)
    volatile LONG next_item; // Needs to be compatible with InterlockedExchangeAdd
#else
    atomic_int next_item;
#endif
    int total_items;
    int chunk_size;
    ParallelTaskFunc task_func;
    void *user_arg;
} ParallelContext;

typedef struct {
    int thread_id;
    ParallelContext *ctx;
} WorkerArg;

static THREAD_FUNC_RETURN THREAD_FUNC_CALL parallel_worker(void *arg) {
    WorkerArg *worker_data = (WorkerArg *)arg;
    ParallelContext *ctx = worker_data->ctx;
    
    while (1) {
        int start = (int)ATOMIC_FETCH_ADD(&ctx->next_item, ctx->chunk_size);
        if (start >= ctx->total_items) {
            break; // No more work
        }
        
        int end = start + ctx->chunk_size;
        if (end > ctx->total_items) {
            end = ctx->total_items;
        }
        ctx->task_func(start, end, ctx->user_arg, worker_data->thread_id);
    }
    
    return 0;
}

bool parallel_run(ParallelTaskFunc task_func, void *arg, int total_items, int num_threads, int chunk_size) {
    if (total_items <= 0) return true;
    if (num_threads <= 0) num_threads = 1;
    if (chunk_size <= 0) chunk_size = 1;

    ParallelContext ctx;
#if defined(_WIN32)
    ctx.next_item = 0;
#else
    atomic_init(&ctx.next_item, 0);
#endif
    ctx.total_items = total_items;
    ctx.chunk_size = chunk_size;
    ctx.task_func = task_func;
    ctx.user_arg = arg;

    thread_handle_t *threads = (thread_handle_t *)malloc(num_threads * sizeof(thread_handle_t));
    WorkerArg *worker_args = (WorkerArg *)malloc(num_threads * sizeof(WorkerArg));
    
    if (!threads || !worker_args) {
        fprintf(stderr, "! Error: Failed to allocate memory for parallel execution.\n");
        free(threads);
        free(worker_args);
        return false;
    }

    int threads_created = 0;
    for (int i = 0; i < num_threads; ++i) {
        worker_args[i].thread_id = i;
        worker_args[i].ctx = &ctx;

#if defined(_WIN32)
        threads[i] = CreateThread(NULL, 0, parallel_worker, &worker_args[i], 0, NULL);
        if (threads[i] == NULL) {
            fprintf(stderr, "! Error creating thread %d\n", i);
            break;
        }
#else
        int ret = pthread_create(&threads[i], NULL, parallel_worker, &worker_args[i]);
        if (ret != 0) {
            fprintf(stderr, "! Error creating thread %d: %s\n", i, strerror(ret));
            break;
        }
#endif
        threads_created++;
    }

    for (int i = 0; i < threads_created; ++i) {
#if defined(_WIN32)
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
    }

    free(threads);
    free(worker_args);
    
    if (threads_created == 0) {
         fprintf(stderr, "! Error: Failed to create any threads. Falling back to single-threaded execution.\n");
         ctx.next_item = 0; // Reset
         WorkerArg main_arg = {0, &ctx};
         parallel_worker(&main_arg);
    }

    return true;
}
