// loader.c - Implementación de carga dinámica de módulos
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int loader_load(const char* path, loaded_module_t* out_module) {
    void* handle = dlopen(path, RTLD_NOW);
    if (!handle) return -1;
    out_module->handle = handle;
    out_module->name = *(const char**)dlsym(handle, "MODULE_NAME");
    const int* hash_len_ptr = dlsym(handle, "MODULE_HASH_LEN");
    out_module->hash_len = hash_len_ptr ? *hash_len_ptr : 0;
    const int* max_dbs_ptr = dlsym(handle, "MODULE_MAX_DBS");
    out_module->max_dbs = max_dbs_ptr ? *max_dbs_ptr : 0;
    out_module->hash = dlsym(handle, "module_hash");
    out_module->prefix = dlsym(handle, "module_prefix");
    if (!out_module->name || !out_module->hash || !out_module->prefix || out_module->hash_len == 0 || out_module->max_dbs == 0) {
        dlclose(handle);
        return -1;
    }
    return 0;
}

void loader_unload(loaded_module_t* module) {
    if (module && module->handle) dlclose(module->handle);
}
