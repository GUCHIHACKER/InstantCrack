// ingestor.c - Orquestador principal del ingestor
#include "ingestor.h"
#include "loader.h"
#include "worker.h"
#include "lmdb_wrap.h"
#include "wordlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <lmdb.h>

// Guarda tras el buffer de hash: tripwire para módulos que ignoren output_len.
// Se comprueba al cerrar cada batch, no por palabra.
#define HASH_GUARD 16
#define HASH_GUARD_BYTE 0xA5

// Tamaño del buffer de prefijo que se le pasa al módulo.
#define PREFIX_MAX 16

static int hash_guard_intact(const unsigned char* hash, size_t hash_len) {
    for (size_t i = 0; i < HASH_GUARD; i++)
        if (hash[hash_len + i] != HASH_GUARD_BYTE) return 0;
    return 1;
}

// Si la guarda saltó, la memoria ya está corrupta: salimos sin tocar el heap,
// porque un free() aquí aborta con "double free" y entierra el diagnóstico.
static void hash_guard_fail(const char* module_name, size_t hash_len) {
    fprintf(stderr, "Error: el módulo %s escribió más de los %zu bytes que declara "
                    "y corrompió memoria del ingestor; abortando\n", module_name, hash_len);
    fflush(stderr);
    _exit(2);
}

// Caché de tablas indexada por la cadena del prefijo, no por bytes del hash:
// module_prefix() puede sacarlo de donde quiera. Lo que se ahorra por palabra es
// el snprintf del nombre de tabla y el escaneo lineal de mdb_dbi_open.
typedef struct {
    char    prefix[PREFIX_MAX];
    MDB_dbi dbi;
    int     used;
} dbi_slot_t;

typedef struct {
    dbi_slot_t* slots;
    size_t      mask;  // capacidad - 1; la capacidad es potencia de dos
    size_t      count;
} dbi_cache_t;

static inline uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

// Devuelve el slot del prefijo: ocupado si ya estaba, libre si toca insertarlo.
// NULL sólo si la tabla está llena.
static dbi_slot_t* dbi_cache_slot(dbi_cache_t* c, const char* prefix) {
    size_t i = fnv1a(prefix) & c->mask;
    for (size_t probes = 0; probes <= c->mask; probes++) {
        dbi_slot_t* s = &c->slots[i];
        if (!s->used || strcmp(s->prefix, prefix) == 0) return s;
        i = (i + 1) & c->mask;
    }
    return NULL;
}

static int dbi_cache_init(dbi_cache_t* c, int max_dbs) {
    size_t cap = 1;
    while (cap < (size_t)max_dbs * 2) cap <<= 1;  // factor de carga <= 0.5
    c->slots = calloc(cap, sizeof(*c->slots));
    if (!c->slots) return -1;
    c->mask = cap - 1;
    c->count = 0;
    return 0;
}

int ingestor_run(const ingestor_config_t* config) {
    int rc = -1;
    loaded_module_t module;
    wordlist_ctx_t* wl = NULL;
    lmdb_ctx_t* lmdb_ctx = NULL;
    unsigned char* hash = NULL;
    dbi_cache_t cache = {0};

    if (loader_load(config->module_path, &module) != 0) {
        // loader_load ya detalló el motivo si la interfaz era inválida;
        // dlerror() sólo dice algo si falló el dlopen.
        const char* err = dlerror();
        if (err) fprintf(stderr, "Error: no se pudo cargar el módulo %s: %s\n", config->module_path, err);
        return -1;
    }

    // Validados en loader_load, así que el bucle caliente no comprueba nada.
    const size_t hash_len = (size_t)module.hash_len;

    wl = wordlist_open(config->wordlist_path);
    if (!wl) {
        fprintf(stderr, "Error: no se pudo abrir la wordlist %s: %s\n", config->wordlist_path, strerror(errno));
        goto cleanup;
    }

    int batch_size = config->batch_size > 0 ? config->batch_size : 500000;
    int batch_count = 0;

    char default_db_path[512];
    const char* db_path = config->db_base_path;
    if (!db_path) {
        snprintf(default_db_path, sizeof(default_db_path), "../db/%s", module.name);
        db_path = default_db_path;
    }

    lmdb_ctx = lmdb_init(db_path, module.max_dbs, config->map_size_mb);
    if (!lmdb_ctx) {
        fprintf(stderr, "Error: no se pudo inicializar LMDB en %s\n", db_path);
        goto cleanup;
    }

    // El hash va como clave, así que el techo es el de LMDB. Se lo preguntamos
    // en vez de cablearlo: si recompilas LMDB con más, se aprovecha solo.
    int max_key = lmdb_max_key_size(lmdb_ctx);
    if (max_key <= 0 || hash_len > (size_t)max_key) {
        fprintf(stderr, "Error: el módulo %s produce hashes de %zu bytes, pero LMDB admite claves de %d como máximo\n",
                module.name, hash_len, max_key);
        goto cleanup;
    }

    // Buffer dimensionado por el módulo: su tamaño ES hash_len, así que no se
    // puede desincronizar de lo declarado.
    hash = malloc(hash_len + HASH_GUARD);
    if (!hash) {
        fprintf(stderr, "Error: sin memoria para el buffer de hash\n");
        goto cleanup;
    }
    memset(hash + hash_len, HASH_GUARD_BYTE, HASH_GUARD);

    if (dbi_cache_init(&cache, module.max_dbs) != 0) {
        fprintf(stderr, "Error: sin memoria para la caché de %d tablas\n", module.max_dbs);
        goto cleanup;
    }

    size_t total_lines = 0, total_inserted = 0, total_dups = 0, total_errors = 0;

    // La línea apunta dentro del mapa de la wordlist: no se copia en ningún
    // momento, ni para hashear ni para insertarla como valor.
    size_t len;
    const char* line;
    while ((line = wordlist_next(wl, &len)) != NULL) {
        total_lines++;

        // Calcular hash
        if (module.hash(line, len, hash, hash_len) != 0) {
            fprintf(stderr, "[!] Error al calcular hash para: %.*s\n", (int)len, line);
            total_errors++;
            continue;
        }

        // Por palabra: el módulo es el único que sabe de dónde sale el prefijo.
        char prefix[PREFIX_MAX];
        if (module.prefix(hash, hash_len, prefix, sizeof(prefix)) != 0) {
            fprintf(stderr, "[!] Error al generar prefijo para: %.*s\n", (int)len, line);
            total_errors++;
            continue;
        }
        // Terminador garantizado aunque el módulo se olvide, para que fnv1a y
        // strcmp no se salgan del buffer.
        prefix[sizeof(prefix) - 1] = '\0';

        dbi_slot_t* slot = dbi_cache_slot(&cache, prefix);
        if (!slot) {
            fprintf(stderr, "Error: caché de tablas llena; el módulo %s genera más prefijos de los que declara en MODULE_MAX_DBS (%d)\n",
                    module.name, module.max_dbs);
            goto cleanup;
        }

        // Primera palabra de esta tabla: resolver el handle. Como máximo
        // max_dbs veces en toda la ejecución.
        if (__builtin_expect(!slot->used, 0)) {
            if (cache.count >= (size_t)module.max_dbs) {
                fprintf(stderr, "Error: el módulo %s ha generado más de %d prefijos distintos (prefijo '%s')\n",
                        module.name, module.max_dbs, prefix);
                goto cleanup;
            }

            // Construir nombre de tabla: <algo>_<prefix>
            char table_name[64];
            int n = snprintf(table_name, sizeof(table_name), "%s_%s", module.name, prefix);
            if (n < 0 || (size_t)n >= sizeof(table_name)) {
                // Truncar haría que dos prefijos compartiesen tabla.
                fprintf(stderr, "Error: nombre de tabla demasiado largo: %s_%s\n", module.name, prefix);
                goto cleanup;
            }

            if (lmdb_open_dbi(lmdb_ctx, table_name, &slot->dbi) != 0) {
                fprintf(stderr, "Error: no se pudo abrir la tabla %s\n", table_name);
                goto cleanup;
            }
            memcpy(slot->prefix, prefix, sizeof(slot->prefix));
            slot->used = 1;
            cache.count++;
        }

        // Insertar en LMDB usando la tabla específica
        int put_rc = lmdb_put_dbi(lmdb_ctx, slot->dbi, hash, hash_len, line, len);
        if (put_rc == MDB_KEYEXIST) {
            total_dups++;
            continue;
        }
        if (put_rc == MDB_MAP_FULL) {
            // El mapa se amplía en los commits: llegar aquí significa que un
            // solo batch agotó el margen.
            fprintf(stderr, "Error: el mapa de LMDB se llenó dentro de un batch; reduce -b o arranca con un -s mayor\n");
            goto cleanup;
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
    fprintf(stderr, "[=] Finalizado: %zu líneas procesadas, %zu hashes insertados, %zu duplicados, %zu errores (%zu tablas)\n",
            total_lines, total_inserted, total_dups, total_errors, cache.count);
    rc = 0;

cleanup:
    free(cache.slots);
    free(hash);
    if (lmdb_ctx) lmdb_close(lmdb_ctx);
    if (wl) wordlist_close(wl);
    loader_unload(&module);
    return rc;
}
