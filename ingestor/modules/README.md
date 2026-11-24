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
  Longitud del hash en bytes (ejemplo: 16). La decide el módulo y debe coincidir
  con lo que `module_hash` escribe realmente: el ingestor dimensiona su buffer
  con este valor. El único techo es el tamaño máximo de clave de LMDB (511 bytes
  en su compilación por defecto), que el ingestor consulta en arranque.
- `extern const int MODULE_MAX_DBS;`  
  Número máximo de tablas, es decir, de prefijos distintos que `module_prefix`
  puede llegar a devolver (ejemplo: 256 para md5, que usa un byte en hex). El
  ingestor aborta si el módulo genera más de los declarados.
- `int module_hash(const void* input, size_t input_len, void* output, size_t output_len);`  
  Calcula el hash de un plaintext. Nunca debe escribir más de `output_len` bytes.
- `int module_prefix(const void* hash, size_t hash_len, char* prefix, size_t prefix_len);`  
  Genera el prefijo a partir del hash. **Puede derivarlo de cualquier parte del
  hash**, no sólo de los primeros bytes: el ingestor la llama por cada palabra y
  no supone nada sobre su origen. Debe ser una función pura del hash y dejar
  `prefix` terminado en NUL.

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
