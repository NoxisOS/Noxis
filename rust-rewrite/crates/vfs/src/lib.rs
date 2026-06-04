//! `vfs` — Virtual File System.
//!
//! Architecture:
//!   VFS (mount table) → trait FileSystem → trait Inode
//!   Implementations: ramfs, noxfs (TODO), procfs, devfs
#![no_std]
extern crate alloc;

pub mod inode;
pub mod vfs;
pub mod ramfs;
pub mod procfs;
pub mod devfs;
pub mod pipe;

pub use vfs::VFS;
