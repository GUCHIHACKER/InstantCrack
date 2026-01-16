// ntlm.c - Módulo para hashes NTLM (Windows)
// Compilar con: gcc -fPIC -shared -o ntlm.so ntlm.c -lssl -lcrypto

#include "../include/module.h"
#include <openssl/md4.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char* MODULE_NAME = "ntlm";
const int MODULE_HASH_LEN = 16;
const int MODULE_MAX_DBS = 256;

// NTLM requiere que la contraseña se convierta a UTF-16LE antes de aplicar MD4
// Esta función realiza una conversión simple de ASCII a UTF-16LE
static int to_utf16le(const unsigned char* input, size_t input_len, unsigned char* output, size_t max_output_len) {
    if (input_len * 2 > max_output_len) return -1;
    
    for (size_t i = 0; i < input_len; i++) {
        output[i * 2] = input[i];
        output[i * 2 + 1] = 0;
    }
    return 0;
}

int module_hash(const void* input, size_t input_len, void* output, size_t output_len) {
    if (output_len < 16) return -1;

    // Reservar espacio para la versión UTF-16LE (2 bytes por carácter)
    size_t utf16_len = input_len * 2;
    unsigned char* utf16_input = (unsigned char*)malloc(utf16_len);
    if (!utf16_input) return -1;

    if (to_utf16le((const unsigned char*)input, input_len, utf16_input, utf16_len) != 0) {
        free(utf16_input);
        return -1;
    }

    MD4(utf16_input, utf16_len, (unsigned char*)output);
    
    free(utf16_input);
    return 0;
}

int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len) {
    if (hash_len < 2 || prefix_len < 3) return -1;
    // Usamos el primer byte del hash como prefijo
    snprintf(prefix, prefix_len, "%02x", ((unsigned char*)hash)[0]);
    return 0;
}
