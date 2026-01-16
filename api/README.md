# Hash Query API (Rust)

Esta es una API REST construida en Rust para realizar consultas de hashes almacenados en bases de datos LMDB generadas por el **Ingestor**.

## Características

- **Modular**: Carga dinámicamente archivos `.so` para manejar la lógica de diferentes algoritmos.
- **Eficiente**: Utiliza LMDB para búsquedas de alto rendimiento.
- **Configurable**: Gestiona los algoritmos y sus rutas mediante una base de datos SQLite central.
- **Soporte Flexible**: Acepta hashes tanto en formato hexadecimal como en formato string/raw (según el algoritmo).
- **Interfaz Técnica**: Dashboard integrado para realizar consultas directas a la base de datos.

## Requerimientos

- Rust (cargo)
- SQLite3
- Libreta de LMDB (`liblmdb-dev`)
- OpenSSL (`libssl-dev`)

## Instalación y Ejecución

1. Asegúrate de que los módulos `.so` estén compilados en la carpeta `ingestor/modules`.
2. Compila e inicia la API:

```sh
cd api
cargo run
```

La API se iniciará en `http://localhost:3000`.

## Endpoints

### 1. Listar algoritmos
Muestra los algoritmos registrados en SQLite.
- **URL**: `/algorithms`
- **Method**: `GET`
- **Response**: Lista JSON de algoritmos configurados.

### 2. Consultar un hash
Busca el plaintext de un hash específico.
- **URL**: `/lookup/{algo}/{hash}`
- **Method**: `GET`
- **Parámetros**:
    - `{algo}`: Nombre del algoritmo (ej: `md5`)
    - `{hash}`: Hash en formato hexadecimal (ej: 32 chars) o raw.
- **Ejemplo**: `GET /lookup/md5/5d41402abc4b2a76b9719d911017c592`

## Gestión de Algoritmos

Para añadir o listar los algoritmos en la base de datos de la API, utiliza el script `manage_algos.py`:

```sh
# Listar algoritmos registrados
./manage_algos.py list

# Añadir un algoritmo
./manage_algos.py add <nombre> <ruta_db> <ruta_modulo_so>

# Ejemplo
./manage_algos.py add md5 ../db/md5 ../ingestor/modules/md5.so
```

## Configuración SQLite

La API utiliza `api.db` (SQLite) para saber dónde están los datos. La tabla `algorithms` tiene la siguiente estructura:

- `name`: Nombre corto del algoritmo (ej: `md5`).
- `db_path`: Ruta al directorio del entorno LMDB.
- `module_path`: Ruta al archivo `.so` del módulo.

## Funcionamiento Interno

1. La API consulta en SQLite la ubicación del módulo `.so` y la base de datos LMDB para el algoritmo solicitado.
2. Carga dinámicamente el módulo `.so` para detectar la longitud del hash y generar el prefijo necesario.
3. Abre el entorno LMDB y la base de datos nombrada (tabla) correspondiente al prefijo (ej: `md5_5d`).
4. Realiza la búsqueda binaria del hash y retorna el plaintext si lo encuentra.
