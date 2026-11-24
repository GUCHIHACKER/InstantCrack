use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use heed::Env;
use serde::{Deserialize, Serialize};
use sqlx::sqlite::SqlitePool;
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use tower_http::cors::CorsLayer;
use tower_http::services::ServeDir;

mod db;
mod modules;

use crate::modules::{HashModule, PREFIX_MAX};

/// Un algoritmo listo para consultar: entorno LMDB, módulo y handles de tabla
/// abiertos. Una consulta no abre nada, solo lee.
struct Algo {
    env: Env,
    module: HashModule,
    tables: HashMap<String, db::Table>,
}

struct AppState {
    pool: SqlitePool,
    // Se precarga al arrancar; el RwLock deja añadir los que se registren
    // después sin reiniciar el servicio.
    algos: RwLock<HashMap<String, Arc<Algo>>>,
}

#[derive(Serialize, Deserialize, sqlx::FromRow)]
struct AlgorithmEntry {
    name: String,
    db_path: String,
    module_path: String,
}

#[derive(Serialize)]
struct LookupResponse {
    hash: String,
    algorithm: String,
    plaintext: Option<String>,
    error: Option<String>,
}

fn reply(code: StatusCode, hash: &str, algo: &str, error: Option<String>, plaintext: Option<String>) -> Response {
    (
        code,
        Json(LookupResponse {
            hash: hash.to_string(),
            algorithm: algo.to_string(),
            plaintext,
            error,
        }),
    )
        .into_response()
}

fn open_algo(entry: &AlgorithmEntry) -> Result<Algo, String> {
    let module = unsafe { HashModule::load(&entry.module_path) }?;
    let env = db::open_env(&entry.db_path, module.max_dbs as u32)?;

    let tables = db::open_all_tables(&env)?;

    Ok(Algo { env, module, tables })
}

impl AppState {
    /// Devuelve el algoritmo, cargándolo si se registró después del arranque.
    async fn algo(&self, name: &str) -> Result<Arc<Algo>, (StatusCode, String)> {
        {
            let cache = self.algos.read().unwrap();
            if let Some(a) = cache.get(name) {
                return Ok(Arc::clone(a));
            }
        }

        let entry = sqlx::query_as::<_, AlgorithmEntry>(
            "SELECT name, db_path, module_path FROM algorithms WHERE name = ?",
        )
        .bind(name)
        .fetch_optional(&self.pool)
        .await
        .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, format!("Database error: {e}")))?
        .ok_or((StatusCode::NOT_FOUND, "Algorithm not found in registry".to_string()))?;

        let algo = Arc::new(open_algo(&entry).map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e))?);
        // Si dos peticiones lo cargan a la vez, una gana el insert y la otra
        // descarta su copia.
        self.algos.write().unwrap().insert(entry.name, Arc::clone(&algo));
        Ok(algo)
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let db_url = "sqlite:api.db";
    let pool = SqlitePool::connect(db_url)
        .await
        .expect("Failed to connect to api.db. Please run ./manage_algos.py first.");

    let entries = sqlx::query_as::<_, AlgorithmEntry>("SELECT name, db_path, module_path FROM algorithms")
        .fetch_all(&pool)
        .await
        .unwrap_or_default();

    // Un algoritmo que no se pueda abrir se avisa y se omite; el resto sirve.
    let mut algos = HashMap::new();
    for entry in entries {
        match open_algo(&entry) {
            Ok(a) => {
                println!(
                    "[+] {}: hash de {} bytes, {} tablas abiertas",
                    entry.name,
                    a.module.hash_len,
                    a.tables.len()
                );
                algos.insert(entry.name.clone(), Arc::new(a));
            }
            Err(e) => eprintln!("[!] {} no disponible: {}", entry.name, e),
        }
    }

    let shared_state = Arc::new(AppState { pool, algos: RwLock::new(algos) });

    let app = Router::new()
        .route("/lookup/{algo}/{hash}", get(lookup_handler))
        .route("/algorithms", get(list_algorithms))
        .fallback_service(ServeDir::new("static")) // Serve index.html and assets
        .layer(CorsLayer::permissive())
        .with_state(shared_state);

    let listener = tokio::net::TcpListener::bind("0.0.0.0:3000").await?;
    println!("API listening on http://{}", listener.local_addr()?);
    axum::serve(listener, app).await?;

    Ok(())
}

async fn list_algorithms(State(state): State<Arc<AppState>>) -> impl IntoResponse {
    let rows = sqlx::query_scalar::<_, String>("SELECT name FROM algorithms")
        .fetch_all(&state.pool)
        .await;

    match rows {
        Ok(names) => Json(names).into_response(),
        Err(e) => (StatusCode::INTERNAL_SERVER_ERROR, format!("DB Error: {}", e)).into_response(),
    }
}

async fn lookup_handler(
    State(state): State<Arc<AppState>>,
    Path((algo_name, hash_str)): Path<(String, String)>,
) -> impl IntoResponse {
    let algo = match state.algo(&algo_name).await {
        Ok(a) => a,
        Err((code, msg)) => return reply(code, &hash_str, &algo_name, Some(msg), None),
    };

    // Decodificación flexible: algunos hashes llegan en hex y otros en crudo.
    // Sólo se acepta el hex si la longitud decodificada cuadra.
    let hash_bytes = match hex::decode(&hash_str) {
        Ok(d) if d.len() == algo.module.hash_len => d,
        _ => hash_str.as_bytes().to_vec(),
    };

    if hash_bytes.len() != algo.module.hash_len {
        return reply(
            StatusCode::BAD_REQUEST,
            &hash_str,
            &algo_name,
            Some(format!(
                "Invalid hash length for {}. Expected {} bytes, got {}",
                algo_name,
                algo.module.hash_len,
                hash_bytes.len()
            )),
            None,
        );
    }

    let mut prefix_buf = [0u8; PREFIX_MAX];
    let prefix = match algo.module.prefix(&hash_bytes, &mut prefix_buf) {
        Ok(p) => p,
        Err(e) => {
            return reply(
                StatusCode::INTERNAL_SERVER_ERROR,
                &hash_str,
                &algo_name,
                Some(format!("Failed to generate prefix: {e}")),
                None,
            )
        }
    };

    let table_name = format!("{}_{}", algo_name, prefix);

    // Normalmente el handle ya está abierto. Si la tabla se creó después del
    // arranque se abre ahora, y si no existe el hash no está.
    let result = match algo.tables.get(&table_name) {
        Some(t) => db::lookup(&algo.env, t, &hash_bytes),
        None => match db::open_table(&algo.env, &table_name) {
            Ok(Some(t)) => db::lookup(&algo.env, &t, &hash_bytes),
            Ok(None) => Ok(None),
            Err(e) => Err(e),
        },
    };

    match result {
        Ok(plaintext) => reply(StatusCode::OK, &hash_str, &algo_name, None, plaintext),
        Err(e) => reply(
            StatusCode::INTERNAL_SERVER_ERROR,
            &hash_str,
            &algo_name,
            Some(format!("LMDB error: {e}")),
            None,
        ),
    }
}
