// wordlist.c - Lectura de wordlists sin copias
#include "wordlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define WL_BUF (1u << 20)

struct wordlist_ctx {
    int fd;
    // Camino rápido: fichero entero mapeado, cero copias.
    char*  map;
    size_t map_len;
    size_t pos;
    // Reserva para lo que no se puede mapear (tuberías, /dev/stdin).
    char*  buf;
    size_t len;
    size_t off;
    int    eof;
};

wordlist_ctx_t* wordlist_open(const char* path) {
    wordlist_ctx_t* ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->fd = open(path, O_RDONLY);
    if (ctx->fd < 0) { free(ctx); return NULL; }

    struct stat st;
    if (fstat(ctx->fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        void* m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, ctx->fd, 0);
        if (m != MAP_FAILED) {
            ctx->map = m;
            ctx->map_len = (size_t)st.st_size;
            // Se recorre de principio a fin, así que el kernel puede leer por delante.
            posix_madvise(m, ctx->map_len, POSIX_MADV_SEQUENTIAL);
            return ctx;
        }
    }

    ctx->buf = malloc(WL_BUF);
    if (!ctx->buf) { close(ctx->fd); free(ctx); return NULL; }
    return ctx;
}

static const char* next_mapped(wordlist_ctx_t* ctx, size_t* out_len) {
    while (ctx->pos < ctx->map_len) {
        const char* start = ctx->map + ctx->pos;
        size_t remaining = ctx->map_len - ctx->pos;
        const char* nl = memchr(start, '\n', remaining);
        size_t len = nl ? (size_t)(nl - start) : remaining;
        ctx->pos += nl ? len + 1 : len;
        if (len && start[len - 1] == '\r') len--;
        if (len) { *out_len = len; return start; }
    }
    return NULL;
}

static const char* next_buffered(wordlist_ctx_t* ctx, size_t* out_len) {
    for (;;) {
        if (ctx->off < ctx->len) {
            char* start = ctx->buf + ctx->off;
            char* nl = memchr(start, '\n', ctx->len - ctx->off);
            if (nl) {
                size_t len = (size_t)(nl - start);
                ctx->off += len + 1;
                if (len && start[len - 1] == '\r') len--;
                if (!len) continue;
                *out_len = len;
                return start;
            }
        }

        if (ctx->eof) {
            // Última línea sin salto final.
            size_t len = ctx->len - ctx->off;
            char* start = ctx->buf + ctx->off;
            ctx->off = ctx->len;
            if (len && start[len - 1] == '\r') len--;
            if (!len) return NULL;
            *out_len = len;
            return start;
        }

        // Compactar lo que queda al principio y rellenar.
        size_t rest = ctx->len - ctx->off;
        if (rest) memmove(ctx->buf, ctx->buf + ctx->off, rest);
        ctx->len = rest;
        ctx->off = 0;

        if (ctx->len == WL_BUF) {
            // Línea más larga que el buffer: se corta aquí.
            *out_len = ctx->len;
            ctx->off = ctx->len;
            return ctx->buf;
        }

        ssize_t n = read(ctx->fd, ctx->buf + ctx->len, WL_BUF - ctx->len);
        if (n < 0) return NULL;
        if (n == 0) ctx->eof = 1;
        else ctx->len += (size_t)n;
    }
}

const char* wordlist_next(wordlist_ctx_t* ctx, size_t* out_len) {
    if (!ctx || !out_len) return NULL;
    return ctx->map ? next_mapped(ctx, out_len) : next_buffered(ctx, out_len);
}

void wordlist_close(wordlist_ctx_t* ctx) {
    if (!ctx) return;
    if (ctx->map) munmap(ctx->map, ctx->map_len);
    free(ctx->buf);
    if (ctx->fd >= 0) close(ctx->fd);
    free(ctx);
}
