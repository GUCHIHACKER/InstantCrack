use lmdb::{EnvBuilder, DbFlags};
use std::path::Path;

pub fn lookup_hash(db_path: &str, table_name: &str, hash_bytes: &[u8]) -> Result<Option<String>, String> {
    let path = Path::new(db_path);
    let env = EnvBuilder::new()
        .max_dbs(256)
        .open(path, 0)
        .map_err(|e| format!("Failed to open LMDB env: {:?}", e))?;

    let db_handle = env.get_db(table_name, DbFlags::empty())
        .map_err(|e| format!("Failed to open DB {:?}: {}", e, table_name))?;

    let txn = env.get_reader()
        .map_err(|e| format!("Failed to start transaction: {:?}", e))?;

    // In lmdb-rs 0.7.6, we must bind the handle to the transaction
    let db = txn.bind(&db_handle);

    match db.get::<&[u8]>(&hash_bytes) {
        Ok(value) => {
            let plaintext = String::from_utf8_lossy(value).into_owned();
            Ok(Some(plaintext))
        }
        Err(lmdb::MdbError::NotFound) => Ok(None),
        Err(e) => Err(format!("LMDB get error: {:?}", e)),
    }
}
