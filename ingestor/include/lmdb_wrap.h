// lmdb_wrap.h - Abstracción simple sobre LMDB para el ingestor
#ifndef LMDB_WRAP_H
#define LMDB_WRAP_H

#include <stddef.h>
#include <lmdb.h>

// Contexto de LMDB para un prefijo
typedef struct lmdb_ctx lmdb_ctx_t;

// Inicializa el entorno LMDB en la ruta dada.
// max_dbs indica el número máximo de bases de datos nombradas (tablas/prefijos).
// map_size_mb es el tamaño INICIAL del mapa, no un techo: lmdb_commit() lo
// amplía solo. 0 usa el valor por defecto. No conviene pasarse: con WRITEMAP
// data.mdb aparenta el tamaño del mapa, aunque sea disperso.
lmdb_ctx_t* lmdb_init(const char* db_path, int max_dbs, size_t map_size_mb);

// Clave máxima que admite esta compilación de LMDB (511 por defecto): el único
// techo real para la longitud del hash de un módulo.
// Retorna el tamaño en bytes, o <0 en error
int lmdb_max_key_size(lmdb_ctx_t* ctx);

// Abre (creándola si no existe) una tabla/sub-database y devuelve su handle.
// Los MDB_dbi sobreviven a lmdb_commit(): una llamada por tabla, nunca por
// inserción, porque mdb_dbi_open() escanea linealmente las sub-DB abiertas.
// Retorna 0 en éxito, !=0 en error
int lmdb_open_dbi(lmdb_ctx_t* ctx, const char* table_name, MDB_dbi* out_dbi);

// Inserta un par hash→plaintext en una tabla ya abierta (camino caliente).
// Retorna 0 en éxito, MDB_KEYEXIST si la clave ya estaba, otro !=0 en error
int lmdb_put_dbi(lmdb_ctx_t* ctx, MDB_dbi dbi, const void* hash, size_t hash_len,
                 const void* plaintext, size_t plaintext_len);

// Inserta resolviendo la tabla por nombre. Cómoda pero lenta; para ingestas
// masivas usa lmdb_open_dbi() una vez y luego lmdb_put_dbi().
// ctx: contexto LMDB
// table_name: nombre de la tabla/sub-db (ej: "md5_a7")
// hash: buffer del hash
// hash_len: longitud del hash
// plaintext: buffer del plaintext
// plaintext_len: longitud del plaintext
// Retorna 0 en éxito, !=0 en error
int lmdb_put(lmdb_ctx_t* ctx, const char* table_name, const void* hash, size_t hash_len,
             const void* plaintext, size_t plaintext_len);

// Hace commit del batch y abre la transacción siguiente. Si falla, los MDB_dbi
// previos dejan de ser válidos y el llamante debe abortar.
// Retorna 0 en éxito, !=0 en error
int lmdb_commit(lmdb_ctx_t* ctx);

// Cierra y libera el contexto LMDB
void lmdb_close(lmdb_ctx_t* ctx);

#endif // LMDB_WRAP_H
