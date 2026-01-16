// sha1.c - Módulo para SHA1 compatible con el ingestor
#include "../include/module.h"
#include <openssl/sha.h>
#include <stdio.h>

const char* MODULE_NAME = "sha1";
const int MODULE_HASH_LEN = 20;
const int MODULE_MAX_DBS = 256;

int module_hash(const void* input, size_t input_len, void* output, size_t output_len) {
    if (output_len < 20) return -1;
    SHA1((const unsigned char*)input, input_len, (unsigned char*)output);
    return 0;
}

int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len) {
    if (hash_len < 2 || prefix_len < 3) return -1;
    snprintf(prefix, prefix_len, "%02x", ((unsigned char*)hash)[0]);
    return 0;
}
