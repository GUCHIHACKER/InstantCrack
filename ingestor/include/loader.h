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
    int hash_len;
    int max_dbs;
    int (*hash)(const void*, size_t, void*, size_t);
    int (*prefix)(const void*, size_t, char*, size_t);
} loaded_module_t;

// Carga un módulo dinámico (.so) y valida su interfaz
// Retorna 0 en éxito, !=0 en error
int loader_load(const char* path, loaded_module_t* out_module);

// Descarga el módulo y libera recursos
void loader_unload(loaded_module_t* module);

#endif // LOADER_H
