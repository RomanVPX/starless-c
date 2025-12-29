#ifndef PARALLEL_H
#define PARALLEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback function type for parallel tasks.
// start_index: inclusive start index of the chunk
// end_index: exclusive end index of the chunk
// arg: user-provided argument
typedef void (*ParallelTaskFunc)(int start_index, int end_index, void *arg, int thread_id);

// Run a task in parallel across a range of items [0, total_items).
// task_func: the function to call for each chunk
// arg: user data passed to task_func
// total_items: total number of items to process
// num_threads: number of threads to use (if <= 0, defaults to 1)
// chunk_size: number of items per task chunk (if <= 0, defaults to 1 or a heuristic)
//
// Returns true on success, false on failure (e.g., thread creation failed).
bool parallel_run(ParallelTaskFunc task_func, void *arg, int total_items, int num_threads, int chunk_size);

#ifdef __cplusplus
}
#endif

#endif // PARALLEL_H
