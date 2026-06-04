//! Build script du kernel :
//! - Compile le kernel ELF
//! - Génère les images disque BIOS et UEFI via le crate `bootloader`
//! - Expose les chemins via env vars pour `cargo run`

use std::{env, path::PathBuf};

fn main() {
    // On ne refait pas le kernel ici (c'est cargo qui s'en charge).
    // Ce script génère les images après que le kernel ELF soit prêt.
    // Il est invoqué par le package `xtask` — voir xtask/src/main.rs.
    //
    // Pour l'instant, on délègue à xtask (cargo xtask run).
    println!("cargo:rerun-if-changed=build.rs");
}
