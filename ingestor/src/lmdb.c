#include <lmdb.h>
#include "lmdb_wrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// lmdb.c - Abstracción sobre LMDB (esqueleto)

struct lmdb_ctx {
    MDB_env* env;
    MDB_txn* txn;
    size_t mapsize;
    size_t batch_bytes;   // datos insertados desde el último commit
    size_t min_headroom;  // margen mínimo, estimado de un batch
    char path[512];
};

// Pequeño a propósito: ampliar cuesta un munmap/mmap, y un mapa inicial enorme
// hace que data.mdb aparente ese tamaño desde el principio.
#define LMDB_DEFAULT_MAP_MB 256
// Margen libre a dejar, en múltiplos del último batch: cubre la sobrecarga del
// B-tree y las páginas de copia en escritura.
#define LMDB_HEADROOM_FACTOR 4
#define LMDB_MIN_HEADROOM (16u * 1024 * 1024)

static int lmdb_maybe_grow(lmdb_ctx_t* ctx);

// Helper para crear directorios recursivamente (mkdir -p)
static int recursive_mkdir(const char *dir) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

// Carga masiva: WRITEMAP evita una copia por página escrita (a cambio de la
// protección del SO frente a escrituras erróneas) y NOMETASYNC deja un fsync
// por transacción en vez de dos. MDB_NOSYNC acelera más, pero un crash puede
// obligar a reconstruir.
#define LMDB_BULK_FLAGS (MDB_WRITEMAP | MDB_NOMETASYNC)

lmdb_ctx_t* lmdb_init(const char* db_path, int max_dbs, size_t map_size_mb,
                      size_t batch_bytes_hint) {
    lmdb_ctx_t* ctx = calloc(1, sizeof(lmdb_ctx_t));
    if (!ctx) return NULL;

    if (map_size_mb == 0) map_size_mb = LMDB_DEFAULT_MAP_MB;
    ctx->mapsize = map_size_mb * 1024 * 1024;
    ctx->min_headroom = batch_bytes_hint * LMDB_HEADROOM_FACTOR;

    // La ruta va luego a mkdir y mdb_env_open, así que tiene que quedar
    // terminada en NUL aunque db_path llene el buffer.
    int n = snprintf(ctx->path, sizeof(ctx->path), "%s", db_path);
    if (n < 0 || (size_t)n >= sizeof(ctx->path)) {
        fprintf(stderr, "Error: ruta de DB demasiado larga (máximo %zu caracteres)\n",
                sizeof(ctx->path) - 1);
        free(ctx);
        return NULL;
    }
    if (recursive_mkdir(ctx->path) != 0) {
        free(ctx);
        return NULL;
    }

    if (mdb_env_create(&ctx->env) != 0) goto fail;
    if (mdb_env_set_maxdbs(ctx->env, max_dbs) != 0) goto fail;
    if (mdb_env_set_mapsize(ctx->env, ctx->mapsize) != 0) goto fail;
    if (mdb_env_open(ctx->env, ctx->path, LMDB_BULK_FLAGS, 0664) != 0) goto fail;

    // Si la base ya existía, LMDB sube el mapa hasta lo que ocupan los datos,
    // así que el tamaño de verdad se lee del entorno.
    MDB_envinfo info;
    if (mdb_env_info(ctx->env, &info) == 0 && info.me_mapsize > ctx->mapsize)
        ctx->mapsize = info.me_mapsize;

    // Margen antes de la primera inserción: al reabrir una base llena el mapa
    // queda justo en lo ocupado y el primer batch lo desbordaría.
    if (lmdb_maybe_grow(ctx) != 0) goto fail;

    // Iniciar primera transacción
    if (mdb_txn_begin(ctx->env, NULL, 0, &ctx->txn) != 0) goto fail;

    return ctx;
fail:
    if (ctx->txn) mdb_txn_abort(ctx->txn);
    if (ctx->env) mdb_env_close(ctx->env);
    free(ctx);
    return NULL;
}

// Amplía el mapa si hace falta. Sólo sin transacción activa: set_mapsize hace
// munmap y vuelve a mapear, lo que invalida punteros a datos (los MDB_dbi son
// enteros y sobreviven).
static int lmdb_maybe_grow(lmdb_ctx_t* ctx) {
    MDB_envinfo info;
    MDB_stat st;
    if (mdb_env_info(ctx->env, &info) != 0) return 0;
    if (mdb_env_stat(ctx->env, &st) != 0) return 0;

    size_t used = (size_t)(info.me_last_pgno + 1) * st.ms_psize;

    // El margen se mide contra el último batch porque el mapa solo se puede
    // ampliar en el commit: tiene que quedar sitio para otro batch entero.
    size_t needed = ctx->batch_bytes * LMDB_HEADROOM_FACTOR;
    if (needed < ctx->min_headroom) needed = ctx->min_headroom;
    if (needed < LMDB_MIN_HEADROOM) needed = LMDB_MIN_HEADROOM;

    size_t target = ctx->mapsize;
    while (target <= used || target - used < needed) target *= 2;
    if (target == ctx->mapsize) return 0;

    int rc = mdb_env_set_mapsize(ctx->env, target);
    if (rc != 0) {
        fprintf(stderr, "Error: no se pudo ampliar el mapa a %zu MB: %s\n",
                target / (1024 * 1024), mdb_strerror(rc));
        return -1;
    }
    fprintf(stderr, "[+] Mapa de LMDB ampliado de %zu a %zu MB (%zu MB en uso)\n",
            ctx->mapsize / (1024 * 1024), target / (1024 * 1024), used / (1024 * 1024));
    ctx->mapsize = target;
    return 0;
}

int lmdb_max_key_size(lmdb_ctx_t* ctx) {
    if (!ctx || !ctx->env) return -1;
    return mdb_env_get_maxkeysize(ctx->env);
}

int lmdb_open_dbi(lmdb_ctx_t* ctx, const char* table_name, MDB_dbi* out_dbi) {
    if (!ctx || !ctx->txn || !table_name || !out_dbi) return -1;
    // MDB_CREATE la crea si no existe; el handle sobrevive a los commits.
    return mdb_dbi_open(ctx->txn, table_name, MDB_CREATE, out_dbi);
}

int lmdb_put_dbi(lmdb_ctx_t* ctx, MDB_dbi dbi, const void* hash, size_t hash_len,
                 const void* plaintext, size_t plaintext_len) {
    MDB_val key = { .mv_size = hash_len, .mv_data = (void*)hash };
    MDB_val val = { .mv_size = plaintext_len, .mv_data = (void*)plaintext };
    // NOOVERWRITE: si el hash ya está, devuelve MDB_KEYEXIST sin reescribir.
    // Hace la ingesta idempotente y ahorra escrituras en wordlists que solapan.
    int rc = mdb_put(ctx->txn, dbi, &key, &val, MDB_NOOVERWRITE);
    if (rc == 0) ctx->batch_bytes += hash_len + plaintext_len;
    return rc;
}

int lmdb_put(lmdb_ctx_t* ctx, const char* table_name, const void* hash, size_t hash_len, const void* plaintext, size_t plaintext_len) {
    MDB_dbi dbi;
    if (lmdb_open_dbi(ctx, table_name, &dbi) != 0) return -1;
    return lmdb_put_dbi(ctx, dbi, hash, hash_len, plaintext, plaintext_len);
}

int lmdb_commit(lmdb_ctx_t* ctx) {
    if (!ctx || !ctx->txn) return -1;
    int rc = mdb_txn_commit(ctx->txn);
    ctx->txn = NULL;
    if (rc != 0) {
        // LMDB ya abortó la transacción y sus MDB_dbi quedan inválidos, así que
        // no abrimos otra: el llamante debe abortar.
        fprintf(stderr, "Error: fallo al confirmar la transacción: %s\n", mdb_strerror(rc));
        return rc;
    }
    // Sin transacción activa: es el único momento en que se puede ampliar.
    if (lmdb_maybe_grow(ctx) != 0) return -1;
    ctx->batch_bytes = 0;

    // Iniciar nueva transacción para el siguiente batch
    if (mdb_txn_begin(ctx->env, NULL, 0, &ctx->txn) != 0) return -1;
    return 0;
}

void lmdb_close(lmdb_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->txn) mdb_txn_abort(ctx->txn);
    mdb_env_close(ctx->env);
    free(ctx);
}
