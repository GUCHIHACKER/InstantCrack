# Módulos de Hash para el Ingestor

En este directorio se encuentran los módulos dinámicos (`.so`) que implementan algoritmos de hash compatibles con el ingestor.

## Requisitos de los módulos

- Cada módulo debe ser un archivo `.so` compilado desde C.
- Debe implementar la interfaz definida en `include/module.h`.
- El nombre del módulo (por ejemplo, `md5.so`) define el tipo de hash y la carpeta de la base de datos.
- Debe ser completamente independiente del core.

## Interfaz obligatoria

Cada módulo debe exponer:

- `extern const char* MODULE_NAME;`  
  Nombre del algoritmo (ejemplo: "md5").
- `extern const int MODULE_HASH_LEN;`  
  Longitud del hash en bytes (ejemplo: 16).
- `extern const int MODULE_MAX_DBS;`  
  Número máximo de bases de datos/tablas (prefijos) dependiendo del tipo de hash y como ahays impelmentado el prefijo (ejemplo: 256 para md5).
- `int module_hash(const void* input, size_t input_len, void* output, size_t output_len);`  
  Calcula el hash de un plaintext.
- `int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len);`  
  Genera el prefijo a partir del hash.

Consulta el README principal y `include/module.h` para más detalles y ejemplos.

## Ejemplo de compilación

```sh
gcc -fPIC -shared -o md5.so md5.c -lssl -lcrypto
```

Coloca el `.so` resultante en este directorio.

## Ejemplo de módulos

- `md5.so` — Implementa el hash MD5
- `sha1.so` — Implementa el hash SHA1

Puedes añadir nuevos algoritmos creando nuevos módulos siguiendo la plantilla.
