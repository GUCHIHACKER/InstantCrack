// worker.h - Pool de hilos y estructuras de trabajo concurrente
#ifndef WORKER_H
#define WORKER_H

#include <stddef.h>
#include "module.h"
#include "loader.h" // Para loaded_module_t
#include "lmdb_wrap.h"

// Estructura para una tarea de hash
typedef struct {
    char* plaintext;
    size_t plaintext_len;
} worker_task_t;

// Inicializa el pool de hilos y comienza el procesamiento
// num_threads: cantidad de hilos a usar
// tasks: array de tareas
// num_tasks: cantidad de tareas
// module: módulo cargado
// batch_size: tamaño de batch para inserciones
// Retorna 0 en éxito, !=0 en error
int worker_run(int num_threads, worker_task_t* tasks, size_t num_tasks, void* lmdb_ctx, const loaded_module_t* module, int batch_size);

#endif // WORKER_H
