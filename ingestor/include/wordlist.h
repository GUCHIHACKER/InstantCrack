// wordlist.h - Lectura eficiente de wordlists
#ifndef WORDLIST_H
#define WORDLIST_H

#include <stddef.h>
#include <sys/types.h> // Para ssize_t en sistemas POSIX

// Estructura para manejar la wordlist
typedef struct wordlist_ctx wordlist_ctx_t;

// Abre una wordlist para lectura
wordlist_ctx_t* wordlist_open(const char* path);

// Lee la siguiente línea (plaintext) de la wordlist
// Retorna la longitud de la línea, o -1 si EOF
ssize_t wordlist_next(wordlist_ctx_t* ctx, char* buffer, size_t max_len);

// Cierra y libera la wordlist
void wordlist_close(wordlist_ctx_t* ctx);

#endif // WORDLIST_H
