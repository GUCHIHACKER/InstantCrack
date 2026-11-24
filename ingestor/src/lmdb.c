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
    char path[512];
};

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

// Flags para carga masiva. WRITEMAP evita una copia por página escrita, a
// cambio de perder la protección del SO frente a escrituras erróneas.
// NOMETASYNC deja un fsync por transacción en vez de dos; mantiene la
// integridad y un crash sólo deshace la última transacción.
// Añadir MDB_NOSYNC acelera más, pero un crash puede obligar a reconstruir.
#define LMDB_BULK_FLAGS (MDB_WRITEMAP | MDB_NOMETASYNC)

lmdb_ctx_t* lmdb_init(const char* db_path, int max_dbs) {
    lmdb_ctx_t* ctx = calloc(1, sizeof(lmdb_ctx_t));
    if (!ctx) return NULL;

    // snprintf y no strncpy: strncpy no garantiza el terminador si db_path
    // llena el buffer, y la ruta va luego a mkdir y mdb_env_open.
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
    if (mdb_env_set_mapsize(ctx->env, 1ULL << 40) != 0) goto fail; // 1TB
    if (mdb_env_open(ctx->env, ctx->path, LMDB_BULK_FLAGS, 0664) != 0) goto fail;

    // Iniciar primera transacción
    if (mdb_txn_begin(ctx->env, NULL, 0, &ctx->txn) != 0) goto fail;

    return ctx;
fail:
    if (ctx->txn) mdb_txn_abort(ctx->txn);
    if (ctx->env) mdb_env_close(ctx->env);
    free(ctx);
    return NULL;
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
    return mdb_put(ctx->txn, dbi, &key, &val, MDB_NOOVERWRITE);
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
        // LMDB ya abortó la transacción y sus MDB_dbi quedan invalidados,
        // así que no abrimos otra: el llamante debe abortar.
        fprintf(stderr, "Error: fallo al confirmar la transacción: %s\n", mdb_strerror(rc));
        return rc;
    }
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
