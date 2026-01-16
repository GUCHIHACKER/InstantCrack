# Ingestor

El **Ingestor** es un sistema modular, concurrente y de alto rendimiento para precalcular y almacenar hashes de wordlists en bases de datos LMDB, optimizadas para búsquedas rápidas en cracking.

---

## Requisitos del sistema

- Linux (probado en Ubuntu/Debian)
- Dependencias:
  - `liblmdb-dev`
  - `libssl-dev`
  - `build-essential`

Instala dependencias con:

```sh
sudo apt-get update && sudo apt-get install -y liblmdb-dev libssl-dev build-essential
```

---

## Compilación

1. Compila el core:

```sh
make clean && make
```

2. Compila los módulos:

```sh
cd modules && make clean && make
```

---

## Ejecución

```sh
./ingestor -w <wordlist.txt> -m modules/<modulo>.so -d <ruta_db> [-b batch_size]
```

- `-w <wordlist>`: Ruta de la wordlist
- `-m <modulo.so>`: Ruta del módulo `.so` (ej: `modules/md5.so`)
- `-d <ruta_db>`: Ruta de la base de datos para el hash (ej: `../db/md5`). Si no existe, se crea; si existe, se abre. El usuario puede darle el nombre que quiera.
- `-b <batch>`: Tamaño de batch (opcional, por defecto 500000)

Ejemplo:

```sh
./ingestor -w /usr/share/wordlists/rockyou.txt -m modules/md5.so -d ./misdbs/md5 -b 500000
```

---

## Estructura de la base de datos

- Se crea un entorno LMDB por cada tipo de hash en la ruta especificada por `-d`.
- Dentro de cada entorno, se utilizan **Named Databases** (tablas) para cada prefijo (ej: `md5_5f`).
- Estructura de archivos: `<ruta_db>/data.mdb` y `<ruta_db>/lock.mdb`.
- Clave: hash (binario)
- Valor: plaintext original
- Los duplicados se sobrescriben automáticamente

---

## Flujo de procesamiento

1. Carga dinámica del módulo de hash
2. Lectura eficiente de la wordlist
3. Para cada palabra:
   - Cálculo del hash
   - Generación del prefijo
   - Inserción en la base de datos LMDB correspondiente
4. Commit por batch para eficiencia
5. Cierre y liberación de recursos

---

## Buenas prácticas y robustez

- Manejo de errores en todas las operaciones críticas (I/O, LMDB, módulos)
- Creación automática de carpetas de salida si no existen
- Liberación de todos los recursos aunque haya errores
- Logs de progreso y errores en stderr
- Código modular y documentado

---

## Añadir nuevos módulos

1. Crea un archivo `.c` en `modules/` siguiendo la plantilla de `md5.c`
2. Implementa la interfaz de `include/module.h`, asegurándote de definir `MODULE_NAME`, `MODULE_HASH_LEN` y `MODULE_MAX_DBS`
3. Compila con `make` dentro de `modules/`
4. Usa el nuevo `.so` con el ingestor

---

## Ejemplo de módulo: MD5

```c
#include "module.h"
#include <openssl/md5.h>
#include <stdio.h>

const char* MODULE_NAME = "md5";
const int MODULE_HASH_LEN = 16;
const int MODULE_MAX_DBS = 256;

int module_hash(const void* input, size_t input_len, void* output, size_t output_len) {
    if (output_len < 16) return -1;
    MD5((const unsigned char*)input, input_len, (unsigned char*)output);
    return 0;
}

int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len) {
    if (hash_len < 2 || prefix_len < 3) return -1;
    snprintf(prefix, prefix_len, "%02x", ((unsigned char*)hash)[0]);
    return 0;
}
```

## Pruebas y validación

- Prueba con una wordlist pequeña antes de usar listas grandes
- Verifica que los archivos en `../db/` se crean y contienen datos
- Usa herramientas como `mdb_stat` para inspeccionar las bases LMDB

---

## TODO y mejoras sugeridas

- Paralelización real con hilos (worker.c)
- Estadísticas de progreso y rendimiento
- Soporte para wordlists comprimidas
- Tests automáticos y scripts de validación

---

## Licencia

MIT