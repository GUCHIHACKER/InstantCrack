use libloading::{Library, Symbol};
use std::ffi::{c_char, c_int, c_void, CStr};

pub const PREFIX_MAX: usize = 16;

type HashFn = unsafe extern "C" fn(*const c_void, usize, *mut c_void, usize) -> c_int;
type PrefixFn = unsafe extern "C" fn(*const c_void, usize, *mut c_char, usize) -> c_int;

/// Módulo de hash cargado, compartido entre peticiones. Guarda punteros a
/// función crudos, que son Copy + Send + Sync.
pub struct HashModule {
    // Mantiene la biblioteca viva: los punteros apuntan dentro de ella.
    _lib: Library,
    #[allow(dead_code)]
    pub name: String,
    pub hash_len: usize,
    pub max_dbs: usize,
    hash_fn: HashFn,
    prefix_fn: PrefixFn,
}

unsafe fn read_int(lib: &Library, path: &str, sym: &[u8]) -> Result<c_int, String> {
    // El deref de Symbol reinterpreta la dirección del símbolo como T: *s da el
    // puntero al entero y **s su valor.
    let s: Symbol<*const c_int> = unsafe { lib.get(sym) }
        .map_err(|e| format!("{path} no exporta {}: {e}", String::from_utf8_lossy(sym)))?;
    Ok(unsafe { **s })
}

impl HashModule {
    pub unsafe fn load(path: &str) -> Result<Self, String> {
        let lib = unsafe { Library::new(path) }
            .map_err(|e| format!("no se pudo cargar {path}: {e}"))?;

        // MODULE_NAME es un `const char*`: el símbolo guarda un puntero, así que
        // lleva un nivel más de indirección que los enteros.
        let name = unsafe {
            let s: Symbol<*const *const c_char> = lib
                .get(b"MODULE_NAME")
                .map_err(|e| format!("{path} no exporta MODULE_NAME: {e}"))?;
            CStr::from_ptr(**s).to_string_lossy().into_owned()
        };

        let hash_len = unsafe { read_int(&lib, path, b"MODULE_HASH_LEN")? };
        let max_dbs = unsafe { read_int(&lib, path, b"MODULE_MAX_DBS")? };

        // Se validan antes de pasarlos a usize, donde un negativo se volvería un
        // número enorme al dimensionar buffers.
        if hash_len <= 0 {
            return Err(format!("{path} declara MODULE_HASH_LEN={hash_len}, debe ser positivo"));
        }
        if max_dbs <= 0 {
            return Err(format!("{path} declara MODULE_MAX_DBS={max_dbs}, debe ser positivo"));
        }

        let hash_fn: HashFn = *unsafe { lib.get(b"module_hash") }
            .map_err(|e| format!("{path} no exporta module_hash: {e}"))?;
        let prefix_fn: PrefixFn = *unsafe { lib.get(b"module_prefix") }
            .map_err(|e| format!("{path} no exporta module_prefix: {e}"))?;

        Ok(Self {
            _lib: lib,
            name,
            hash_len: hash_len as usize,
            max_dbs: max_dbs as usize,
            hash_fn,
            prefix_fn,
        })
    }

    #[allow(dead_code)]
    pub fn hash(&self, input: &[u8]) -> Result<Vec<u8>, String> {
        let mut out = vec![0u8; self.hash_len];
        let rc = unsafe {
            (self.hash_fn)(input.as_ptr().cast(), input.len(), out.as_mut_ptr().cast(), out.len())
        };
        if rc == 0 { Ok(out) } else { Err("module_hash falló".into()) }
    }

    /// Escribe el prefijo de tabla en `buf` y devuelve la parte usada. Lo
    /// calcula el módulo, que puede derivarlo de cualquier parte del hash.
    pub fn prefix<'b>(&self, hash: &[u8], buf: &'b mut [u8; PREFIX_MAX]) -> Result<&'b str, String> {
        let rc = unsafe {
            (self.prefix_fn)(hash.as_ptr().cast(), hash.len(), buf.as_mut_ptr().cast(), buf.len())
        };
        if rc != 0 {
            return Err("module_prefix falló".into());
        }
        // Terminador garantizado aunque el módulo se olvide.
        buf[PREFIX_MAX - 1] = 0;
        let end = buf.iter().position(|&b| b == 0).unwrap_or(0);
        std::str::from_utf8(&buf[..end]).map_err(|_| "el prefijo no es UTF-8".to_string())
    }
}
