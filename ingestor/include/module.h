// module.h - Interfaz obligatoria para módulos de hash del ingestor
#ifndef MODULE_H
#define MODULE_H

#include <stddef.h>

// Nombre del algoritmo (ej: "md5", "sha1", etc.)
extern const char* MODULE_NAME;

// Bytes que escribe module_hash(). Lo decide el módulo: el ingestor dimensiona
// su buffer a partir de este valor. El único techo es el tamaño máximo de clave
// de LMDB (511 bytes en su compilación por defecto), que el ingestor consulta en
// arranque y no tiene cableado.
extern const int MODULE_HASH_LEN;

// Bytes iniciales del hash de los que depende module_prefix(). 1 o 2.
// El ingestor los usa como índice de su caché de tablas.
extern const int MODULE_PREFIX_BYTES;

// Número máximo de tablas. No puede superar 256^MODULE_PREFIX_BYTES.
extern const int MODULE_MAX_DBS;

// Calcula el hash de un plaintext
// input: puntero al buffer de entrada
// input_len: longitud del buffer de entrada
// output: buffer de salida (debe tener espacio suficiente para el hash)
// output_len: longitud del buffer de salida (por ejemplo, 16 para MD5)
// Debe escribir MODULE_HASH_LEN bytes y nunca más de output_len. El ingestor
// pasa output_len == MODULE_HASH_LEN, así que escribir de más corrompe su
// memoria: comprueba output_len y devuelve error si no cabe.
// Retorna 0 en éxito, !=0 en error
int module_hash(const void* input, size_t input_len, void* output, size_t output_len);

// Genera el prefijo a partir del hash (por ejemplo, los primeros 2 bytes en hex)
// hash: buffer con el hash calculado
// hash_len: longitud del hash
// prefix: buffer de salida para el prefijo (ej: 3 bytes para "5f")
// prefix_len: longitud máxima del prefijo
// Sólo puede depender de los primeros MODULE_PREFIX_BYTES bytes del hash.
// Retorna 0 en éxito, !=0 en error
int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len);

#endif // MODULE_H
