use heed::types::{Bytes, Str};
use heed::{Database, Env, EnvFlags, EnvOpenOptions};
use std::collections::HashMap;
use std::path::Path;

/// Tabla de LMDB: clave = hash binario, valor = plaintext. Es Copy, así que se
/// guarda abierta y se reutiliza.
pub type Table = Database<Bytes, Bytes>;

/// Mapea el entorno LMDB del algoritmo. No lee datos: el kernel trae las
/// páginas según se tocan, así que el coste es el mismo para cualquier tamaño.
pub fn open_env(path: &str, max_dbs: u32) -> Result<Env, String> {
    let mut opts = EnvOpenOptions::new();
    opts.max_dbs(max_dbs).max_readers(256);
    // READ_ONLY: la API solo consulta. NO_TLS: con un pool de hilos, ata cada
    // lectura a la transacción y no a un slot thread-local.
    // El tamaño del mapa se toma de la meta page que escribió el ingestor.
    unsafe { opts.flags(EnvFlags::READ_ONLY | EnvFlags::NO_TLS) };

    unsafe { opts.open(Path::new(path)) }
        .map_err(|e| format!("no se pudo abrir LMDB en {path}: {e}"))
}

/// Abre todas las tablas del entorno y devuelve sus handles.
///
/// Usa una sola transacción y la confirma: LMDB cierra los handles que se
/// abrieron en una transacción abortada.
pub fn open_all_tables(env: &Env) -> Result<HashMap<String, Table>, String> {
    let rtxn = env.read_txn().map_err(|e| format!("no se pudo leer: {e}"))?;

    let main: Database<Str, Bytes> = match env.open_database(&rtxn, None) {
        Ok(Some(db)) => db,
        Ok(None) => return Ok(HashMap::new()),
        Err(e) => return Err(format!("no se pudo abrir la base principal: {e}")),
    };

    // Los nombres se recogen y el cursor se cierra antes de abrir ninguna
    // sub-base: abrirla con el cursor vivo lo invalidaría.
    let mut names = Vec::new();
    {
        let iter = main.iter(&rtxn).map_err(|e| format!("no se pudo iterar: {e}"))?;
        for item in iter {
            let (name, _) = item.map_err(|e| format!("error al iterar: {e}"))?;
            names.push(name.to_string());
        }
    }

    let mut tables = HashMap::new();
    for name in names {
        match env.open_database::<Bytes, Bytes>(&rtxn, Some(&name)) {
            Ok(Some(t)) => {
                tables.insert(name, t);
            }
            Ok(None) => {}
            Err(e) => return Err(format!("no se pudo abrir la tabla {name}: {e}")),
        }
    }

    rtxn.commit().map_err(|e| format!("no se pudo confirmar la apertura: {e}"))?;
    Ok(tables)
}

/// Abre una tabla por nombre. None significa que no existe, es decir que
/// ninguna palabra ingerida cayó en ese prefijo.
pub fn open_table(env: &Env, name: &str) -> Result<Option<Table>, String> {
    let rtxn = env.read_txn().map_err(|e| format!("no se pudo leer: {e}"))?;
    let table = env
        .open_database::<Bytes, Bytes>(&rtxn, Some(name))
        .map_err(|e| format!("no se pudo abrir la tabla {name}: {e}"))?;
    rtxn.commit().map_err(|e| format!("no se pudo confirmar la apertura: {e}"))?;
    Ok(table)
}

/// Busca un hash en una tabla ya abierta. La transacción dura solo la consulta:
/// una lectura viva fija el snapshot e impide a LMDB reciclar páginas.
pub fn lookup(env: &Env, table: &Table, hash: &[u8]) -> Result<Option<String>, String> {
    let rtxn = env.read_txn().map_err(|e| format!("no se pudo leer: {e}"))?;
    match table.get(&rtxn, hash) {
        Ok(Some(value)) => Ok(Some(String::from_utf8_lossy(value).into_owned())),
        Ok(None) => Ok(None),
        Err(e) => Err(format!("error de LMDB: {e}")),
    }
}
