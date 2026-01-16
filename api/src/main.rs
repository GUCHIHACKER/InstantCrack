use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::IntoResponse,
    routing::get,
    Json, Router,
};
use serde::{Deserialize, Serialize};
use sqlx::sqlite::SqlitePool;
use std::sync::Arc;
use tower_http::cors::CorsLayer;
use tower_http::services::ServeDir;

mod db;
mod modules;

use crate::modules::HashModule;

struct AppState {
    pool: SqlitePool,
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

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Database setup - assuming manage_algos.py has been run
    let db_url = "sqlite:api.db";
    let pool = SqlitePool::connect(db_url).await.expect("Failed to connect to api.db. Please run ./manage_algos.py first.");

    let shared_state = Arc::new(AppState { pool });

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

async fn list_algorithms(
    State(state): State<Arc<AppState>>,
) -> impl IntoResponse {
    let rows = sqlx::query_scalar::<_, String>(
        "SELECT name FROM algorithms"
    )
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
    let hash_str_for_resp = hash_str.clone();

    // 1. Get algorithm config from SQLite
    let algo_config = sqlx::query_as::<_, AlgorithmEntry>(
        "SELECT name, db_path, module_path FROM algorithms WHERE name = ?"
    )
    .bind(&algo_name)
    .fetch_optional(&state.pool)
    .await;

    let config = match algo_config {
        Ok(Some(c)) => c,
        Ok(None) => return (StatusCode::NOT_FOUND, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: Some("Algorithm not found in registry".to_string()),
        })).into_response(),
        Err(e) => return (StatusCode::INTERNAL_SERVER_ERROR, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: Some(format!("Database error: {}", e)),
        })).into_response(),
    };

    // 2. Load module
    let module = match unsafe { HashModule::load(&config.module_path) } {
        Ok(m) => m,
        Err(e) => return (StatusCode::INTERNAL_SERVER_ERROR, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: Some(format!("Failed to load module: {}", e)),
        })).into_response(),
    };

    // 3. Modular hash decoding:
    // Some hashes are hex-encoded results, some are raw strings.
    // We try to decode from hex ONLY if the length matches expectations.
    let hash_bytes = if let Ok(decoded) = hex::decode(&hash_str) {
        if decoded.len() == module.hash_len {
            decoded
        } else {
            // Hex decoded but length mismatch, maybe it's the raw string that's the hash?
            hash_str.as_bytes().to_vec()
        }
    } else {
        // Not valid hex, use raw bytes
        hash_str.as_bytes().to_vec()
    };

    if hash_bytes.len() != module.hash_len {
         return (StatusCode::BAD_REQUEST, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name.clone(),
            plaintext: None,
            error: Some(format!("Invalid hash length for {}. Expected {} bytes, got {}", algo_name, module.hash_len, hash_bytes.len())),
        })).into_response();
    }

    // 4. Get prefix using module
    let prefix = match module.prefix(&hash_bytes) {
        Ok(p) => p,
        Err(e) => return (StatusCode::INTERNAL_SERVER_ERROR, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: Some(format!("Failed to generate prefix: {}", e)),
        })).into_response(),
    };

    // 5. Lookup in LMDB
    let table_name = format!("{}_{}", algo_name, prefix);
    match db::lookup_hash(&config.db_path, &table_name, &hash_bytes) {
        Ok(Some(plaintext)) => Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: Some(plaintext),
            error: None,
        }).into_response(),
        Ok(None) => Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: None,
        }).into_response(),
        Err(e) => (StatusCode::INTERNAL_SERVER_ERROR, Json(LookupResponse {
            hash: hash_str_for_resp,
            algorithm: algo_name,
            plaintext: None,
            error: Some(format!("LMDB error: {}", e)),
        })).into_response(),
    }
}
