// wordlist.c - Lectura eficiente de wordlists (esqueleto)
#include "wordlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wordlist_ctx {
    FILE* fp;
};

wordlist_ctx_t* wordlist_open(const char* path) {
    wordlist_ctx_t* ctx = malloc(sizeof(wordlist_ctx_t));
    if (!ctx) return NULL;
    ctx->fp = fopen(path, "r");
    if (!ctx->fp) { free(ctx); return NULL; }
    return ctx;
}

ssize_t wordlist_next(wordlist_ctx_t* ctx, char* buffer, size_t max_len) {
    if (!ctx || !ctx->fp) return -1;
    if (!fgets(buffer, max_len, ctx->fp)) return -1;
    size_t len = strlen(buffer);
    if (len && buffer[len-1] == '\n') buffer[--len] = '\0';
    return (ssize_t)len;
}

void wordlist_close(wordlist_ctx_t* ctx) {
    if (ctx) {
        if (ctx->fp) fclose(ctx->fp);
        free(ctx);
    }
}
