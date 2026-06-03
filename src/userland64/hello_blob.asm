; ─────────────────────────────────────────────────────────────
; src/userland64/hello_blob.asm — embed the built hello.elf into the
; kernel image so the ELF64 loader can run it without a filesystem yet.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global hello_elf_start
global hello_elf_end

section .rodata
hello_elf_start:
    incbin "build/hello.elf"
hello_elf_end:
