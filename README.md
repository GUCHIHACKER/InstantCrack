# InstantCrack

InstantCrack es un motor de recuperación de hashes de alto rendimiento diseñado para realizar búsquedas instantáneas en bases de datos precalculadas de gran escala, pensado para ejecutarse en infraestructura local y mantener el control total sobre los datos. El sistema utiliza una arquitectura modular basada en C y Rust, optimizando el acceso a disco mediante el uso de LMDB (Lightning Memory‑Mapped Database), lo que permite trabajar con tablas y wordlists propias sin depender de servicios externos.

## Descripción del Proyecto

A diferencia de los métodos de fuerza bruta convencionales, InstantCrack se basa en la precomputación de hashes almacenados de forma estructurada. La utilización de LMDB garantiza tiempos de respuesta constantes en el rango de milisegundos, independientemente del volumen de datos almacenado.

### Características Principales

*   **Rendimiento**: Consultas de baja latencia mediante el mapeo de memoria de LMDB.
*   **Modularidad**: Soporte para cualquier algoritmo de hash mediante la implementación de bibliotecas compartidas (`.so`) en C.
*   **Escalabilidad**: El ingestor segmenta los diccionarios por prefijos para evitar cuellos de botella en el sistema de archivos.
*   **Interfaz Profesional**: API robusta desarrollada en Rust y dashboard de consulta.

## Estructura de Almacenamiento y Eficiencia

El sistema está optimizado para manejar archivos de datos masivos. A continuación se presentan ejemplos de ocupación de almacenamiento para un conjunto de datos basado en el diccionario Rockyou:

```text
# Ejemplo de almacenamiento para MD5 (Rockyou procesado)
drwxrwxr-x .                               4.0 KB
data.mdb                                    1.9 GB
lock.mdb                                    8.0 KB

# Ejemplo de almacenamiento para SHA1
drwxrwxr-x .                               4.0 KB
data.mdb                                    2.1 GB
lock.mdb                                    8.0 KB

# Ejemplo de almacenamiento para NTLM
drwxrwxr-x .                               4.0 KB
data.mdb                                    1.9 GB
lock.mdb                                    8.0 KB
```

### Rendimiento y Tiempos de Respuesta
Bajo condiciones de carga normal, las consultas en diccionarios como Rockyou (procesado en LMDB) presentan los siguientes tiempos de respuesta totales (incluyendo la sobrecarga de red de la API):

*   **MD5**: ~8ms (`0.008s total`)
*   **SHA1**: ~27ms (`0.027s total`)
*   **NTLM**: ~11ms (`0.011s total`)

*Nota: Estos tiempos representan la latencia completa de una petición HTTP local mediante `curl`.*

---

## Requisitos del Sistema

Para compilar y ejecutar tanto el ingestor como la API, es necesario disponer de las siguientes dependencias en un entorno Linux (basado en Debian/Ubuntu):

*   **Compiladores**: `gcc` (para C) y `rustc`/`cargo` (para Rust).
*   **Bibliotecas de Desarrollo**: `liblmdb-dev`, `libssl-dev` y `sqlite3`.
*   **Herramientas de Construcción**: `make` y `build-essential`.

### Instalación de dependencias en Debian/Ubuntu:
```bash
sudo apt update && sudo apt install -y liblmdb-dev libssl-dev build-essential sqlite3
```

## Instalación y Configuración

### 1. Clonar el repositorio
Clonación del repositorio y acceso al directorio raíz:
```bash
git clone https://github.com/GUCHIHACKER/InstantCrack.git
cd InstantCrack
```

### 2. Compilación de los Módulos de Hash
Los algoritmos son gestionados de forma modular. Compile los módulos requeridos en el directorio correspondiente:
```bash
cd ingestor/modules
make
```

### 3. Compilación del Ingestor
Regrese al código fuente del ingestor y proceda con la compilación del binario principal:
```bash
cd ../src
make
```

---

## Flujo de Trabajo y Uso

El funcionamiento de InstantCrack se divide en tres fases obligatorias: Ingesta, Registro y Servicio.

### Fase 1: Ingesta de Datos (Creación de Bases de Datos)
Utilice el componente `ingestor` para procesar un diccionario y generar el entorno LMDB. Es imperativo especificar el módulo adecuado.

```bash
cd ingestor
./ingestor -w /ruta/al/diccionario.txt -m modules/md5.so -d ../db/md5 -b 1000000
```
*Este comando generará una estructura de base de datos optimizada por prefijos en la ruta de destino.*

#### Optimización de Ingesta (Batch Processing)
Para maximizar la velocidad de escritura en LMDB, el ingestor agrupa las inserciones en transacciones atómicas denominadas **batches**.
*   **Funcionamiento**: En lugar de sincronizar con el disco por cada hash, el sistema acumula `N` inserciones y realiza un único `commit` al final del lote.
*   **Configuración**: Se define mediante el parámetro `-b`. El valor por defecto es **500000**.
*   **Recomendación**: Para diccionarios masivos como *Rockyou* o superiores, se recomienda utilizar valores entre **500,000** y **1,500,000** para reducir la sobrecarga de sincronización y aprovechar al máximo la velocidad de LMDB.

### Fase 2: Registro del Algoritmo en la API
Para que la interfaz de consulta reconozca la nueva base de datos, debe registrarla mediante el script de gestión ubicado en el directorio de la API:

```bash
cd ../api
python3 manage_algos.py add md5 ../db/md5 ../ingestor/modules/md5.so
```

### Fase 3: Ejecución del Servicio de Consulta
Inicie el servidor de la API para habilitar las consultas locales y remotas:

```bash
cargo run
```
*Nota: El comando `cargo run` se encarga de descargar las dependencias de Rust y compilar el binario automáticamente. Sin embargo, el sistema debe tener instaladas las bibliotecas de desarrollo (`libssl-dev`, etc.) mencionadas en la sección de requisitos.*

El servicio estará disponible de forma predeterminada en `http://localhost:3000`.
<img width="1070" height="866" alt="image" src="https://github.com/user-attachments/assets/b36daa5f-1891-4863-862a-c0410e631381" />

---

## Modularidad y Extensiones

InstantCrack permite la adición de nuevos algoritmos sin necesidad de recompilar el núcleo del sistema. Solo se requiere implementar la lógica de hash en C siguiendo la interfaz definida.

Para obtener detalles técnicos sobre cómo crear y registrar nuevos módulos, consulte la documentación interna en:
*   [Guía de creación de módulos](ingestor/modules/README.md)
*   [Especificaciones de la API](api/README.md)
*   [Especificaciones del ingestor](ingestor/README.md)

---

## Licencia

Este proyecto se distribuye bajo la licencia MIT. Consulte el archivo [LICENSE](LICENSE) para más información.

Desarrollado por [GUCHIHACKER](https://github.com/GUCHIHACKER).
