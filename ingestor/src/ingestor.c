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
#include <unistd.h>
#include <lmdb.h>

// Bytes de guarda tras el buffer de hash. Son un tripwire para módulos que
// ignoren output_len y escriban de más; se comprueban al cerrar cada batch y no
// por palabra, para no cargar el bucle caliente.
#define HASH_GUARD 16
#define HASH_GUARD_BYTE 0xA5

static int hash_guard_intact(const unsigned char* hash, size_t hash_len) {
    for (size_t i = 0; i < HASH_GUARD; i++)
        if (hash[hash_len + i] != HASH_GUARD_BYTE) return 0;
    return 1;
}

// Si la guarda ha saltado, la memoria del proceso ya está corrupta: salimos sin
// tocar el heap, porque un free() aquí aborta con "double free or corruption" y
// entierra el diagnóstico. La DB queda en el último commit y LMDB suelta el
// lock al morir el proceso.
static void hash_guard_fail(const char* module_name, size_t hash_len) {
    fprintf(stderr, "Error: el módulo %s escribió más de los %zu bytes que declara "
                    "y corrompió memoria del ingestor; abortando\n", module_name, hash_len);
    fflush(stderr);
    _exit(2);
}

int ingestor_run(const ingestor_config_t* config) {
    int rc = -1;
    loaded_module_t module;
    wordlist_ctx_t* wl = NULL;
    lmdb_ctx_t* lmdb_ctx = NULL;
    unsigned char* hash = NULL;
    MDB_dbi* dbi = NULL;      // caché de handles, indexada por los bytes de prefijo
    unsigned char* dbi_ok = NULL;

    if (loader_load(config->module_path, &module) != 0) {
        // loader_load ya detalló el motivo si la interfaz era inválida;
        // dlerror() sólo dice algo si falló el dlopen.
        const char* err = dlerror();
        if (err) fprintf(stderr, "Error: no se pudo cargar el módulo %s: %s\n", config->module_path, err);
        return -1;
    }

    // Validados en loader_load, así que el bucle caliente no comprueba nada.
    const size_t hash_len = (size_t)module.hash_len;
    const int prefix_bytes = module.prefix_bytes;
    const unsigned n_tables = 1u << (8 * prefix_bytes);

    wl = wordlist_open(config->wordlist_path);
    if (!wl) {
        fprintf(stderr, "Error: no se pudo abrir la wordlist %s: %s\n", config->wordlist_path, strerror(errno));
        goto cleanup;
    }

    // Buffers
    char line[4096];
    int batch_size = config->batch_size > 0 ? config->batch_size : 500000;
    int batch_count = 0;

    char default_db_path[512];
    const char* db_path = config->db_base_path;
    if (!db_path) {
        snprintf(default_db_path, sizeof(default_db_path), "../db/%s", module.name);
        db_path = default_db_path;
    }

    lmdb_ctx = lmdb_init(db_path, module.max_dbs);
    if (!lmdb_ctx) {
        fprintf(stderr, "Error: no se pudo inicializar LMDB en %s\n", db_path);
        goto cleanup;
    }

    // El hash va como clave, así que el único techo es el de LMDB. Se lo
    // preguntamos en vez de cablear una constante: si recompilas LMDB con un
    // MDB_MAXKEYSIZE mayor, el ingestor lo aprovecha sin tocar nada.
    int max_key = lmdb_max_key_size(lmdb_ctx);
    if (max_key <= 0 || hash_len > (size_t)max_key) {
        fprintf(stderr, "Error: el módulo %s produce hashes de %zu bytes, pero LMDB admite claves de %d como máximo\n",
                module.name, hash_len, max_key);
        goto cleanup;
    }

    // Buffer dimensionado por el módulo: su tamaño ES hash_len, así que no puede
    // desincronizarse de lo que el módulo declara.
    hash = malloc(hash_len + HASH_GUARD);
    if (!hash) {
        fprintf(stderr, "Error: sin memoria para el buffer de hash\n");
        goto cleanup;
    }
    memset(hash + hash_len, HASH_GUARD_BYTE, HASH_GUARD);

    // Caché de tablas: cada una se abre una vez, así el bucle caliente se
    // ahorra por palabra el snprintf del nombre, la llamada indirecta a
    // module.prefix y el escaneo de mdb_dbi_open.
    dbi = malloc((size_t)n_tables * sizeof(*dbi));
    dbi_ok = calloc(n_tables, 1);
    if (!dbi || !dbi_ok) {
        fprintf(stderr, "Error: sin memoria para la caché de %u tablas\n", n_tables);
        goto cleanup;
    }

    size_t total_lines = 0, total_inserted = 0, total_dups = 0, total_errors = 0;

    while (1) {
        ssize_t len = wordlist_next(wl, line, sizeof(line));
        if (len < 0) break;
        if (len == 0) continue;
        total_lines++;

        // Calcular hash
        if (module.hash(line, len, hash, hash_len) != 0) {
            fprintf(stderr, "[!] Error al calcular hash para: %s\n", line);
            total_errors++;
            continue;
        }

        // Por contrato el prefijo sólo depende de estos bytes, así que valen
        // como índice directo.
        unsigned idx = prefix_bytes == 1 ? hash[0]
                                         : ((unsigned)hash[0] << 8) | hash[1];

        // Primera palabra de esta tabla: resolver el handle. Como máximo
        // n_tables veces en toda la ejecución.
        if (__builtin_expect(!dbi_ok[idx], 0)) {
            char prefix[16];
            if (module.prefix(hash, hash_len, prefix, sizeof(prefix)) != 0) {
                fprintf(stderr, "[!] Error al generar prefijo para: %s\n", line);
                total_errors++;
                continue;
            }

            // Construir nombre de tabla: <algo>_<prefix>
            char table_name[64];
            int n = snprintf(table_name, sizeof(table_name), "%s_%s", module.name, prefix);
            if (n < 0 || (size_t)n >= sizeof(table_name)) {
                // Truncar haría que dos prefijos compartiesen tabla.
                fprintf(stderr, "Error: nombre de tabla demasiado largo para el módulo %s\n", module.name);
                goto cleanup;
            }

            if (lmdb_open_dbi(lmdb_ctx, table_name, &dbi[idx]) != 0) {
                fprintf(stderr, "Error: no se pudo abrir la tabla %s\n", table_name);
                goto cleanup;
            }
            dbi_ok[idx] = 1;
        }

        // Insertar en LMDB usando la tabla específica
        int put_rc = lmdb_put_dbi(lmdb_ctx, dbi[idx], hash, hash_len, line, (size_t)len);
        if (put_rc == MDB_KEYEXIST) {
            total_dups++;
            continue;
        }
        if (put_rc != 0) {
            fprintf(stderr, "[!] Error al insertar en LMDB: %s\n", mdb_strerror(put_rc));
            total_errors++;
            continue;
        }

        batch_count++;
        total_inserted++;
        if (batch_count >= batch_size) {
            if (!hash_guard_intact(hash, hash_len)) hash_guard_fail(module.name, hash_len);
            if (lmdb_commit(lmdb_ctx) != 0) goto cleanup;
            batch_count = 0;
            fprintf(stderr, "[+] Progreso: %zu líneas procesadas, %zu hashes insertados, %zu duplicados, %zu errores\n",
                    total_lines, total_inserted, total_dups, total_errors);
        }
    }

    // Commit final
    if (!hash_guard_intact(hash, hash_len)) hash_guard_fail(module.name, hash_len);
    if (lmdb_commit(lmdb_ctx) != 0) goto cleanup;
    fprintf(stderr, "[=] Finalizado: %zu líneas procesadas, %zu hashes insertados, %zu duplicados, %zu errores\n",
            total_lines, total_inserted, total_dups, total_errors);
    rc = 0;

cleanup:
    free(dbi_ok);
    free(dbi);
    free(hash);
    if (lmdb_ctx) lmdb_close(lmdb_ctx);
    if (wl) wordlist_close(wl);
    loader_unload(&module);
    return rc;
}
