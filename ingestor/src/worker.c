// worker.c - Pool de hilos y procesamiento concurrente (esqueleto)
#include "worker.h"
#include <stdio.h>

int worker_run(int num_threads, worker_task_t* tasks, size_t num_tasks, void* lmdb_ctx, const loaded_module_t* module, int batch_size) {
    printf("[worker] Procesando %zu tareas con %d hilos (batch=%d)\n", num_tasks, num_threads, batch_size);
    return 0;
}
