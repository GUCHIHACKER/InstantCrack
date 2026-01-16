// ingestor.c - Orquestador principal del ingestor
#include "ingestor.h"
#include "loader.h"
#include "worker.h"
#include "lmdb_wrap.h"
#include "wordlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <lmdb.h>

int ingestor_run(const ingestor_config_t* config) {
    loaded_module_t module;
    if (loader_load(config->module_path, &module) != 0) {
        fprintf(stderr, "Error: no se pudo cargar el módulo %s: %s\n", config->module_path, dlerror());
        return -1;
    }

    wordlist_ctx_t* wl = wordlist_open(config->wordlist_path);
    if (!wl) {
        fprintf(stderr, "Error: no se pudo abrir la wordlist %s: %s\n", config->wordlist_path, strerror(errno));
        loader_unload(&module);
        return -1;
    }

    // Buffers
    char line[4096];
    unsigned char hash[128]; // Suficiente para la mayoría de hashes
    char prefix[16];
    int batch_size = config->batch_size > 0 ? config->batch_size : 500000;
    int batch_count = 0;
    
    char default_db_path[512];
    const char* db_path = config->db_base_path;
    if (!db_path) {
        snprintf(default_db_path, sizeof(default_db_path), "../db/%s", module.name);
        db_path = default_db_path;
    }

    lmdb_ctx_t* lmdb_ctx = lmdb_init(db_path, module.max_dbs);
    if (!lmdb_ctx) {
        fprintf(stderr, "Error: no se pudo inicializar LMDB en %s\n", db_path);
        wordlist_close(wl);
        loader_unload(&module);
        return -1;
    }

    int hash_len = module.hash_len;
    size_t total_lines = 0, total_inserted = 0, total_errors = 0;

    while (1) {
        ssize_t len = wordlist_next(wl, line, sizeof(line));
        if (len < 0) break;
        if (len == 0) continue;
        total_lines++;

        // Calcular hash
        if (module.hash(line, len, hash, sizeof(hash)) != 0) {
            fprintf(stderr, "[!] Error al calcular hash para: %s\n", line);
            total_errors++;
            continue;
        }

        // Generar prefijo
        if (module.prefix(hash, hash_len, prefix, sizeof(prefix)) != 0) {
            fprintf(stderr, "[!] Error al generar prefijo para: %s\n", line);
            total_errors++;
            continue;
        }
        
        // Construir nombre de tabla: <algo>_<prefix>
        char table_name[64];
        snprintf(table_name, sizeof(table_name), "%s_%s", module.name, prefix);

        // Insertar en LMDB usando la tabla específica
        if (lmdb_put(lmdb_ctx, table_name, hash, hash_len, line, len) != 0) {
            fprintf(stderr, "[!] Error al insertar en LMDB tabla %s\n", table_name);
            total_errors++;
            continue;
        }

        batch_count++;
        total_inserted++;
        if (batch_count >= batch_size) {
            lmdb_commit(lmdb_ctx);
            batch_count = 0;
            fprintf(stderr, "[+] Progreso: %zu líneas procesadas, %zu hashes insertados, %zu errores\n", total_lines, total_inserted, total_errors);
        }
    }

    // Commit final y cierre
    lmdb_commit(lmdb_ctx);
    lmdb_close(lmdb_ctx);
    wordlist_close(wl);
    loader_unload(&module);
    fprintf(stderr, "[=] Finalizado: %zu líneas procesadas, %zu hashes insertados, %zu errores\n", total_lines, total_inserted, total_errors);
    return 0;
}
