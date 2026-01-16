// lmdb_wrap.h - Abstracción simple sobre LMDB para el ingestor
#ifndef LMDB_WRAP_H
#define LMDB_WRAP_H

#include <stddef.h>
#include <lmdb.h>

// Contexto de LMDB para un prefijo
typedef struct lmdb_ctx lmdb_ctx_t;

// Inicializa el entorno LMDB en la ruta dada.
// max_dbs indica el número máximo de bases de datos nombradas (tablas/prefijos).
lmdb_ctx_t* lmdb_init(const char* db_path, int max_dbs);

// Inserta un par hash→plaintext en una tabla (sub-database) específica
// ctx: contexto LMDB
// table_name: nombre de la tabla/sub-db (ej: "md5_a7")
// hash: buffer del hash
// hash_len: longitud del hash
// plaintext: buffer del plaintext
// plaintext_len: longitud del plaintext
// Retorna 0 en éxito, !=0 en error
int lmdb_put(lmdb_ctx_t* ctx, const char* table_name, const void* hash, size_t hash_len, const void* plaintext, size_t plaintext_len);

// Hace commit de las inserciones pendientes (batch)
int lmdb_commit(lmdb_ctx_t* ctx);

// Cierra y libera el contexto LMDB
void lmdb_close(lmdb_ctx_t* ctx);

#endif // LMDB_WRAP_H
