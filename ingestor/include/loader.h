// loader.h - Descubrimiento y carga de módulos dinámicos
#ifndef LOADER_H
#define LOADER_H

#include "lmdb_wrap.h"
#include "module.h"
#include <dlfcn.h>

// Estructura para manejar el módulo cargado dinámicamente
typedef struct {
    void* handle; // handle de dlopen
    const char* name;
    int hash_len;     // garantizado > 0 tras loader_load()
    int max_dbs;      // garantizado > 0 tras loader_load()
    int (*hash)(const void*, size_t, void*, size_t);
    int (*prefix)(const void*, size_t, char*, size_t);
} loaded_module_t;

// Carga un módulo dinámico (.so) y valida su interfaz una sola vez, para que
// los consumidores no tengan que comprobar nada en sus bucles calientes.
// El techo de hash_len no se valida aquí porque depende de LMDB: lo comprueba
// ingestor_run con el entorno ya abierto.
// Imprime en stderr el motivo si la interfaz es inválida.
// Retorna 0 en éxito, !=0 en error
int loader_load(const char* path, loaded_module_t* out_module);

// Descarga el módulo y libera recursos
void loader_unload(loaded_module_t* module);

#endif // LOADER_H
