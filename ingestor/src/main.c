// main.c - Punto de entrada del ingestor
#include "ingestor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(const char* prog) {
    printf("Uso: %s -w <wordlist> -m <modulo.so> -d <ruta_db> [-b batch]\n", prog);
    printf("  -w <wordlist>      Ruta de la wordlist\n");
    printf("  -m <modulo.so>     Ruta del módulo .so\n");
    printf("  -d <ruta_db>       Ruta de la base de datos (ej: ../db/md5)\n");
    printf("  -b <batch>         Tamaño de batch (opcional, por defecto 500000)\n");
}

int main(int argc, char** argv) {
    ingestor_config_t config = {0};
    config.batch_size = 1000;
    config.db_base_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-w") && i+1 < argc) config.wordlist_path = argv[++i];
        else if (!strcmp(argv[i], "-m") && i+1 < argc) config.module_path = argv[++i];
        else if (!strcmp(argv[i], "-b") && i+1 < argc) config.batch_size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) config.db_base_path = argv[++i];
        else {
            print_usage(argv[0]);
            return 1;
        }
    }
    if (!config.wordlist_path || !config.module_path) {
        print_usage(argv[0]);
        return 1;
    }
    return ingestor_run(&config);
}
