// md5.c - Módulo de ejemplo para MD5 compatible con el ingestor
// Compilar con: gcc -fPIC -shared -o md5.so md5.c -lssl -lcrypto

#include "../include/module.h"
#include <openssl/md5.h>
#include <stdio.h>

// Nombre del algoritmo (debe coincidir con el nombre del módulo y la carpeta de la DB)
const char* MODULE_NAME = "md5";
const int MODULE_HASH_LEN = 16;
const int MODULE_MAX_DBS = 256;

// Calcula el hash MD5 de un buffer de entrada
// input: puntero a los datos
// input_len: longitud de los datos
// output: buffer de salida (debe tener al menos 16 bytes)
// output_len: longitud del buffer de salida
// Retorna 0 en éxito, !=0 en error
int module_hash(const void* input, size_t input_len, void* output, size_t output_len) {
    if (output_len < 16) return -1; // MD5 produce 16 bytes
    MD5((const unsigned char*)input, input_len, (unsigned char*)output);
    return 0;
}

// Genera el prefijo a partir del hash (por ejemplo, los primeros 2 bytes en hex)
// hash: buffer con el hash calculado
// hash_len: longitud del hash (debe ser 16 para MD5)
// prefix: buffer de salida para el prefijo (al menos 3 bytes para "5f")
// prefix_len: longitud máxima del prefijo
// Retorna 0 en éxito, !=0 en error
int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len) {
    if (hash_len < 2 || prefix_len < 3) return -1;
    // Usamos solo el primer byte como prefijo (2 dígitos hex)
    snprintf(prefix, prefix_len, "%02x", ((unsigned char*)hash)[0]);
    return 0;
}

// Notas:
// - Este módulo es thread-safe.
// - El formato del hash es binario (16 bytes).
// - El prefijo es de 2 dígitos hexadecimales ("00" a "ff").
// - No accede a LMDB ni a wordlists directamente.
