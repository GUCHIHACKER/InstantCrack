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

lmdb_ctx_t* lmdb_init(const char* db_path, int max_dbs) {
    lmdb_ctx_t* ctx = calloc(1, sizeof(lmdb_ctx_t));
    if (!ctx) return NULL;
    
    strncpy(ctx->path, db_path, sizeof(ctx->path));
    if (recursive_mkdir(ctx->path) != 0) {
        free(ctx);
        return NULL;
    }

    if (mdb_env_create(&ctx->env) != 0) goto fail;
    if (mdb_env_set_maxdbs(ctx->env, max_dbs) != 0) goto fail;
    if (mdb_env_set_mapsize(ctx->env, 1ULL << 32) != 0) goto fail; // 4GB
    if (mdb_env_open(ctx->env, ctx->path, 0, 0664) != 0) goto fail;
    
    // Iniciar primera transacción
    if (mdb_txn_begin(ctx->env, NULL, 0, &ctx->txn) != 0) goto fail;
    
    return ctx;
fail:
    if (ctx->txn) mdb_txn_abort(ctx->txn);
    if (ctx->env) mdb_env_close(ctx->env);
    free(ctx);
    return NULL;
}

int lmdb_put(lmdb_ctx_t* ctx, const char* table_name, const void* hash, size_t hash_len, const void* plaintext, size_t plaintext_len) {
    MDB_dbi dbi;
    // Abrir la "tabla" (sub-database) dentro de la transacción actual
    // MDB_CREATE asegura que se cree si no existe
    if (mdb_dbi_open(ctx->txn, table_name, MDB_CREATE, &dbi) != 0) return -1;

    MDB_val key = { .mv_size = hash_len, .mv_data = (void*)hash };
    MDB_val val = { .mv_size = plaintext_len, .mv_data = (void*)plaintext };
    return mdb_put(ctx->txn, dbi, &key, &val, 0);
}

int lmdb_commit(lmdb_ctx_t* ctx) {
    if (!ctx || !ctx->txn) return -1;
    int rc = mdb_txn_commit(ctx->txn);
    ctx->txn = NULL;
    // Iniciar nueva transacción para el siguiente batch
    if (mdb_txn_begin(ctx->env, NULL, 0, &ctx->txn) != 0) return -1;
    return rc;
}

void lmdb_close(lmdb_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->txn) mdb_txn_abort(ctx->txn);
    mdb_env_close(ctx->env);
    free(ctx);
}
