//! VFS mount table and path resolution.

use alloc::{string::String, vec::Vec, sync::Arc, collections::BTreeMap};
use sync::Mutex;

use super::inode::{FileSystem, Inode, KResult};
use abi::error::KError;

struct Mount {
    path: String,
    fs:   Arc<dyn FileSystem>,
}

/// Global VFS — mount table + path resolver.
pub struct VFS {
    mounts: Vec<Mount>,
}

impl VFS {
    pub const fn empty() -> Self { Self { mounts: Vec::new() } }

    /// Mount a filesystem at `path`.
    pub fn mount(&mut self, path: &str, fs: Arc<dyn FileSystem>) {
        // Remove existing mount at same path
        self.mounts.retain(|m| m.path != path);
        self.mounts.push(Mount { path: String::from(path), fs });
        // Keep longest-prefix first
        self.mounts.sort_by(|a, b| b.path.len().cmp(&a.path.len()));
    }

    /// Resolve an absolute path to an inode.
    pub fn resolve(&self, path: &str) -> KResult<Arc<dyn Inode>> {
        let (mount, rel) = self.find_mount(path)?;
        self.walk(mount.fs.root(), rel)
    }

    /// Resolve parent directory + final component.
    pub fn resolve_parent(&self, path: &str) -> KResult<(Arc<dyn Inode>, String)> {
        let (dir, name) = split_path(path);
        let parent = if dir.is_empty() || dir == "/" {
            let (mount, _) = self.find_mount(path)?;
            mount.fs.root()
        } else {
            self.resolve(dir)?
        };
        Ok((parent, String::from(name)))
    }

    fn find_mount<'a>(&'a self, path: &'a str) -> KResult<(&'a Mount, &'a str)> {
        for m in &self.mounts {
            if path.starts_with(m.path.as_str()) {
                let rel = &path[m.path.len()..];
                let rel = if rel.is_empty() { "/" } else { rel };
                return Ok((m, rel));
            }
        }
        Err(KError::NoEnt)
    }

    fn walk(&self, mut node: Arc<dyn Inode>, path: &str) -> KResult<Arc<dyn Inode>> {
        for component in path.split('/').filter(|s| !s.is_empty()) {
            node = node.lookup(component)?;
        }
        Ok(node)
    }
}

fn split_path(path: &str) -> (&str, &str) {
    match path.rfind('/') {
        Some(0) => ("/", &path[1..]),
        Some(i) => (&path[..i], &path[i+1..]),
        None    => ("", path),
    }
}

// ── Global singleton ──────────────────────────────────────────────────────────

static GLOBAL_VFS: Mutex<VFS> = Mutex::new(VFS::empty());

pub fn with_vfs<F, R>(f: F) -> R
where F: FnOnce(&mut VFS) -> R {
    f(&mut GLOBAL_VFS.lock())
}
