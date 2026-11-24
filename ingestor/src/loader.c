// loader.c - Implementación de carga dinámica de módulos
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Lee un símbolo entero del módulo. Devuelve 0 si no existe.
static int sym_int(void* handle, const char* name) {
    const int* p = (const int*)dlsym(handle, name);
    return p ? *p : 0;
}

// Valida las constantes del módulo e imprime el motivo si algo falla.
static int validate(const char* path, const loaded_module_t* m) {
    if (!m->name) {
        fprintf(stderr, "Error: módulo %s no exporta MODULE_NAME\n", path);
        return -1;
    }
    if (!m->hash || !m->prefix) {
        fprintf(stderr, "Error: módulo %s no exporta module_hash y/o module_prefix\n", path);
        return -1;
    }
    // El tamaño lo decide el módulo; el techo es el de clave de LMDB y lo
    // comprueba ingestor_run, con el entorno ya abierto.
    if (m->hash_len <= 0) {
        fprintf(stderr, "Error: módulo %s declara MODULE_HASH_LEN=%d, debe ser positivo\n",
                path, m->hash_len);
        return -1;
    }
    if (m->max_dbs <= 0) {
        fprintf(stderr, "Error: módulo %s declara MODULE_MAX_DBS=%d, debe ser positivo\n",
                path, m->max_dbs);
        return -1;
    }
    return 0;
}

int loader_load(const char* path, loaded_module_t* out_module) {
    void* handle = dlopen(path, RTLD_NOW);
    if (!handle) return -1;

    memset(out_module, 0, sizeof(*out_module));
    out_module->handle = handle;

    const char** name_ptr = (const char**)dlsym(handle, "MODULE_NAME");
    out_module->name = name_ptr ? *name_ptr : NULL;
    out_module->hash_len = sym_int(handle, "MODULE_HASH_LEN");
    out_module->max_dbs  = sym_int(handle, "MODULE_MAX_DBS");
    out_module->hash   = (int (*)(const void*, size_t, void*, size_t))dlsym(handle, "module_hash");
    out_module->prefix = (int (*)(const void*, size_t, char*, size_t))dlsym(handle, "module_prefix");

    if (validate(path, out_module) != 0) {
        dlclose(handle);
        memset(out_module, 0, sizeof(*out_module));
        return -1;
    }
    return 0;
}

void loader_unload(loaded_module_t* module) {
    if (module && module->handle) dlclose(module->handle);
}
