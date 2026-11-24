// wordlist.h - Lectura eficiente de wordlists
#ifndef WORDLIST_H
#define WORDLIST_H

#include <stddef.h>
#include <sys/types.h> // Para ssize_t en sistemas POSIX

// Estructura para manejar la wordlist
typedef struct wordlist_ctx wordlist_ctx_t;

// Abre una wordlist para lectura
wordlist_ctx_t* wordlist_open(const char* path);

// Devuelve la siguiente línea sin copiarla, y su longitud en *out_len.
// El puntero NO está terminado en NUL y sólo vale hasta la siguiente llamada.
// Se salta las líneas vacías y quita el \r de las wordlists con CRLF.
// Retorna NULL al final.
const char* wordlist_next(wordlist_ctx_t* ctx, size_t* out_len);

// Cierra y libera la wordlist
void wordlist_close(wordlist_ctx_t* ctx);

#endif // WORDLIST_H
