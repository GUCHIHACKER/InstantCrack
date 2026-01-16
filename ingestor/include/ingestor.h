// ingestor.h - API principal y coordinación del ingestor
#ifndef INGESTOR_H
#define INGESTOR_H

#include <stddef.h>
#include "module.h"
#include "lmdb_wrap.h"

// Estructura de configuración del ingestor
typedef struct {
    const char* wordlist_path;
    const char* module_path;
    int batch_size;
    const char* db_base_path;
} ingestor_config_t;

// Inicializa y ejecuta el proceso principal del ingestor
// Retorna 0 en éxito, !=0 en error
int ingestor_run(const ingestor_config_t* config);

#endif // INGESTOR_H
