//! xtask — build and run helper for Noxis OS.
//!
//! Usage:
//!   cargo xtask build          — compile the kernel
//!   cargo xtask image          — generate BIOS + UEFI disk images
//!   cargo xtask run            — launch QEMU in UEFI mode
//!   cargo xtask run-bios       — launch QEMU in legacy BIOS mode
//!   cargo xtask run-release    — release build + UEFI run

use std::{
    env,
    path::{Path, PathBuf},
    process::{Command, ExitStatus},
};

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let cmd = args.get(0).map(String::as_str).unwrap_or("help");

    match cmd {
        // Userland always built in release to keep ELF sizes small
        "build"         => { build(false); build_userland(true); }
        "build-release" => { build(true);  build_userland(true);  }
        "image"         => { build(false); build_userland(true); image(false); }
        "run"           => { build(false); build_userland(true); image(false); run_uefi(); }
        "run-bios"      => { build(false); build_userland(true); image(false); run_bios(); }
        "run-release"   => { build(true);  build_userland(true);  image(true);  run_uefi(); }
        _ => eprintln!("Usage: cargo xtask [build|image|run|run-bios|run-release]"),
    }
}

// ── Paths ─────────────────────────────────────────────────────────────────────

fn workspace_root() -> PathBuf {
    // This file lives at xtask/src/main.rs → workspace root is ../..
    let manifest = env!("CARGO_MANIFEST_DIR");
    Path::new(manifest).parent().unwrap().to_path_buf()
}

fn kernel_elf(release: bool) -> PathBuf {
    workspace_root()
        .join("target")
        .join("x86_64-unknown-none")
        .join(if release { "release" } else { "debug" })
        .join("kernel")
}

fn out_dir() -> PathBuf {
    let p = workspace_root().join("target").join("imgs");
    std::fs::create_dir_all(&p).ok();
    p
}

// ── Build ─────────────────────────────────────────────────────────────────────

const USERLAND_PROGS: &[&str] = &[
    "nsh", "ls", "cat", "echo", "ps", "mkdir", "rm", "cp", "wc", "grep",
];
// Note: nsh is installed as both "nsh" and looked up on PATH

fn build(release: bool) {
    let root = workspace_root();
    let mut cmd = Command::new("cargo");
    cmd.current_dir(&root)
        .args([
            "build", "--package", "kernel",
            "--target", "x86_64-unknown-none",
            "-Z", "build-std=core,compiler_builtins,alloc",
            "-Z", "build-std-features=compiler-builtins-mem",
        ]);
    if release { cmd.arg("--release"); }
    run_cmd(cmd);
}

fn build_userland(release: bool) {
    let root = workspace_root();
    let mut cmd = Command::new("cargo");
    cmd.current_dir(&root);
    cmd.arg("build");
    for prog in USERLAND_PROGS {
        cmd.args(["--package", prog]);
    }
    cmd.args([
        "--target", "x86_64-unknown-none",
        "-Z", "build-std=core,compiler_builtins,alloc",
        "-Z", "build-std-features=compiler-builtins-mem",
    ]);
    if release { cmd.arg("--release"); }
    run_cmd(cmd);
    println!("[xtask] Userland programs built.");

    // Bundle ELFs into a ramdisk image
    build_ramdisk(release);
}

/// Simple ramdisk format:
///   For each file:
///     [u32 name_len][name bytes][u64 data_len][data bytes]
///   Terminated by name_len = 0xFFFFFFFF
fn build_ramdisk(release: bool) {
    use std::io::Write;
    let out = out_dir().join("initrd.img");
    let mut img: Vec<u8> = Vec::new();

    for prog in USERLAND_PROGS {
        let elf = userland_elf(prog, release);
        if !elf.exists() { continue; }
        let data = std::fs::read(&elf).expect("cannot read elf");
        let name = prog.as_bytes();
        // name_len (u32 LE)
        img.extend_from_slice(&(name.len() as u32).to_le_bytes());
        img.extend_from_slice(name);
        // data_len (u64 LE)
        img.extend_from_slice(&(data.len() as u64).to_le_bytes());
        img.extend_from_slice(&data);
        println!("[xtask] initrd: {} ({} KiB)", prog, data.len() / 1024);
    }
    // Terminator
    img.extend_from_slice(&0xFFFFFFFFu32.to_le_bytes());

    std::fs::write(&out, &img).expect("cannot write initrd.img");
    println!("[xtask] initrd: {} KiB total → {}", img.len() / 1024, out.display());
}

fn userland_elf(name: &str, release: bool) -> PathBuf {
    workspace_root()
        .join("target")
        .join("x86_64-unknown-none")
        .join(if release { "release" } else { "debug" })
        .join(name)
}

// ── Image ─────────────────────────────────────────────────────────────────────

fn image(release: bool) {
    let elf     = kernel_elf(release);
    let out     = out_dir();
    let bios    = out.join("noxis-bios.img");
    let uefi    = out.join("noxis-uefi.img");
    let ramdisk = out.join("initrd.img");

    println!("[xtask] Generating disk images...");
    println!("        kernel ELF : {}", elf.display());

    let mut bios_boot = bootloader::BiosBoot::new(&elf);
    let mut uefi_boot = bootloader::UefiBoot::new(&elf);

    // Attach ramdisk if it exists
    if ramdisk.exists() {
        println!("        ramdisk    : {}", ramdisk.display());
        bios_boot.set_ramdisk(&ramdisk);
        uefi_boot.set_ramdisk(&ramdisk);
    }

    bios_boot.create_disk_image(&bios).expect("Failed to create BIOS image");
    uefi_boot.create_disk_image(&uefi).expect("Failed to create UEFI image");

    println!("[xtask] BIOS image: {}", bios.display());
    println!("[xtask] UEFI image: {}", uefi.display());
}

// ── Run ───────────────────────────────────────────────────────────────────────

fn qemu() -> PathBuf {
    let candidates = [
        r"D:\Program Files\qemu\qemu-system-x86_64.exe",
        r"C:\Program Files\qemu\qemu-system-x86_64.exe",
        "qemu-system-x86_64",
    ];
    for c in &candidates {
        let p = PathBuf::from(c);
        if p.exists() { return p; }
    }
    PathBuf::from("qemu-system-x86_64")
}

fn run_uefi() {
    let uefi_img = out_dir().join("noxis-uefi.img");
    let ovmf = find_ovmf().expect(
        "OVMF not found. Install the ovmf package or place OVMF.fd in the current directory."
    );

    println!("[xtask] QEMU UEFI with OVMF={}", ovmf.display());

    let pflash = format!("if=pflash,format=raw,readonly=on,file={}", ovmf.to_string_lossy());
    let disk   = format!("format=raw,file={}", uefi_img.display());

    let mut cmd = Command::new(qemu());
    cmd.args([
        "-machine", "q35",
        "-m", "128M",
        "-drive", &pflash,
        "-drive", &disk,
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
    ]);
    run_cmd(cmd);
}

fn run_bios() {
    let bios_img = out_dir().join("noxis-bios.img");
    println!("[xtask] QEMU BIOS");

    let mut cmd = Command::new(qemu());
    cmd.args([
        "-drive", &format!("format=raw,file={}", bios_img.display()),
        "-m", "128M",
        "-serial", "stdio",
        "-display", "none",
        "-no-reboot",
    ]);
    run_cmd(cmd);
}

fn find_ovmf() -> Option<PathBuf> {
    let candidates = [
        // Windows — QEMU bundled EDK2
        r"D:\Program Files\qemu\share\edk2-x86_64-code.fd",
        r"C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        r"D:\Program Files\qemu\OVMF.fd",
        r"C:\Program Files\qemu\OVMF.fd",
        // Local directory
        "OVMF.fd",
        "edk2-x86_64-code.fd",
        // Linux
        "/usr/share/ovmf/OVMF.fd",
        "/usr/share/edk2-ovmf/OVMF.fd",
        "/usr/share/qemu/OVMF.fd",
        "/usr/share/qemu/edk2-x86_64-code.fd",
    ];
    for c in &candidates {
        let p = PathBuf::from(c);
        if p.exists() { return Some(p); }
    }
    None
}

// ── Utility ───────────────────────────────────────────────────────────────────

fn run_cmd(mut cmd: Command) {
    println!("[xtask] {:?}", cmd);
    let status: ExitStatus = cmd.status().expect("Failed to launch command");
    if !status.success() {
        eprintln!("[xtask] Command failed: {:?}", status);
        std::process::exit(1);
    }
}
