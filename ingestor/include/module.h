// module.h - Interfaz obligatoria para módulos de hash del ingestor
#ifndef MODULE_H
#define MODULE_H

#include <stddef.h>

// Nombre del algoritmo (ej: "md5", "sha1", etc.)
extern const char* MODULE_NAME;

// Bytes que escribe module_hash(). Lo decide el módulo; el ingestor dimensiona
// su buffer con él. El único techo es la clave máxima de LMDB (511 por defecto).
extern const int MODULE_HASH_LEN;

// Prefijos distintos que module_prefix() puede devolver, o sea tablas.
extern const int MODULE_MAX_DBS;

// Calcula el hash de un plaintext
// input: puntero al buffer de entrada
// input_len: longitud del buffer de entrada
// output: buffer de salida (debe tener espacio suficiente para el hash)
// output_len: longitud del buffer de salida (por ejemplo, 16 para MD5)
// Escribe MODULE_HASH_LEN bytes y nunca más de output_len, que el ingestor pasa
// igual a MODULE_HASH_LEN: pasarse corrompe su memoria.
// Retorna 0 en éxito, !=0 en error
int module_hash(const void* input, size_t input_len, void* output, size_t output_len);

// Genera el prefijo a partir del hash (por ejemplo, los primeros 2 bytes en hex)
// hash: buffer con el hash calculado
// hash_len: longitud del hash
// prefix: buffer de salida para el prefijo (ej: 3 bytes para "5f")
// prefix_len: longitud máxima del prefijo
// Puede salir de cualquier parte del hash: el ingestor la llama por palabra y
// no supone nada. Función pura, prefix terminado en NUL.
// Retorna 0 en éxito, !=0 en error
int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len);

#endif // MODULE_H
