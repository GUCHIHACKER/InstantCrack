use libloading::{Library, Symbol};
use std::ffi::{c_char, c_int, c_void, CStr};

#[allow(dead_code)]
pub struct HashModule {
    // We keep the library alive so the symbols remain valid
    _lib: Library,
    pub name: String,
    pub hash_len: usize,
    pub max_dbs: usize,
    // Functions
    hash_fn: Symbol<'static, unsafe extern "C" fn(*const c_void, usize, *mut c_void, usize) -> c_int>,
    prefix_fn: Symbol<'static, unsafe extern "C" fn(*const c_void, usize, *mut c_char, usize) -> c_int>,
}

impl HashModule {
    pub unsafe fn load(path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let lib = unsafe { Library::new(path)? };
        
        let name_ptr: Symbol<*const c_char> = unsafe { lib.get(b"MODULE_NAME")? };
        let hash_len_ptr: Symbol<*const c_int> = unsafe { lib.get(b"MODULE_HASH_LEN")? };
        let max_dbs_ptr: Symbol<*const c_int> = unsafe { lib.get(b"MODULE_MAX_DBS")? };

        let name = unsafe { CStr::from_ptr(*name_ptr).to_string_lossy().into_owned() };
        let hash_len = unsafe { **hash_len_ptr as usize };
        let max_dbs = unsafe { **max_dbs_ptr as usize };

        let hash_fn: Symbol<unsafe extern "C" fn(*const c_void, usize, *mut c_void, usize) -> c_int> = unsafe { lib.get(b"module_hash")? };
        let prefix_fn: Symbol<unsafe extern "C" fn(*const c_void, usize, *mut c_char, usize) -> c_int> = unsafe { lib.get(b"module_prefix")? };

        let hash_fn = unsafe { std::mem::transmute(hash_fn) };
        let prefix_fn = unsafe { std::mem::transmute(prefix_fn) };

        Ok(Self {
            _lib: lib,
            name,
            hash_len,
            max_dbs,
            hash_fn,
            prefix_fn,
        })
    }

    #[allow(dead_code)]
    pub fn hash(&self, input: &[u8]) -> Result<Vec<u8>, String> {
        let mut output = vec![0u8; self.hash_len];
        let res = unsafe {
            (self.hash_fn)(
                input.as_ptr() as *const c_void,
                input.len(),
                output.as_mut_ptr() as *mut c_void,
                output.len(),
            )
        };
        if res == 0 {
            Ok(output)
        } else {
            Err("Hash function failed".to_string())
        }
    }

    pub fn prefix(&self, hash: &[u8]) -> Result<String, String> {
        let mut prefix_buf = vec![0i8; 32]; // Accommodate most prefixes
        let res = unsafe {
            (self.prefix_fn)(
                hash.as_ptr() as *const c_void,
                hash.len(),
                prefix_buf.as_mut_ptr(),
                prefix_buf.len(),
            )
        };
        if res == 0 {
            let c_str = unsafe { CStr::from_ptr(prefix_buf.as_ptr()) };
            Ok(c_str.to_string_lossy().into_owned())
        } else {
            Err("Prefix function failed".to_string())
        }
    }
}
