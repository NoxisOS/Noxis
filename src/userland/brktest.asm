; ─────────────────────────────────────────────────────────────
; userland/brktest.asm — exercise the brk() syscall + heap demand paging.
;
;   1. old = brk(0)                 ; query current break
;   2. brk(old + 32 KB)             ; grow the heap by 8 pages
;   3. write a byte in each new page ; each touch demand-faults a page in
;   4. read them back to verify
;   5. print result, exit 0
;
; Absolute addressing (elf_load maps at link address 0x400000).
; sysenter: EAX=#, EBX=arg1, ESI=arg2, EDI=arg3, EDX=ret EIP, ECX=ESP.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1
%define SYS_BRK    20

%macro SYS 0
    lea  edx, [%%ret]
    mov  ecx, esp
    sysenter
%%ret:
%endmacro

%define GROW   0x8000        ; 32 KB = 8 pages

_start:
    ; old = brk(0)
    mov  eax, SYS_BRK
    xor  ebx, ebx
    SYS
    mov  esi, eax            ; esi = old break (heap base)

    ; brk(old + GROW)
    mov  eax, SYS_BRK
    lea  ebx, [esi + GROW]
    SYS
    ; eax = new break; verify it advanced
    lea  edx, [esi + GROW]
    cmp  eax, edx
    jne  .fail

    ; write a sentinel byte at the start of each of the 8 new pages
    mov  edi, esi            ; edi = walking heap pointer
    mov  ecx, 8
.wr:
    mov  byte [edi], 0x5A    ; demand-faults this heap page
    add  edi, 0x1000
    dec  ecx
    jnz  .wr

    ; read them back
    mov  edi, esi
    mov  ecx, 8
.rd:
    cmp  byte [edi], 0x5A
    jne  .fail
    add  edi, 0x1000
    dec  ecx
    jnz  .rd

    ; success
    mov  ebx, 1
    mov  esi, ok
    mov  edi, ok_len
    mov  eax, SYS_WRITE
    SYS
    xor  ebx, ebx
    mov  eax, SYS_EXIT
    SYS
.hang1: jmp .hang1

.fail:
    mov  ebx, 1
    mov  esi, bad
    mov  edi, bad_len
    mov  eax, SYS_WRITE
    SYS
    mov  ebx, 1
    mov  eax, SYS_EXIT
    SYS
.hang2: jmp .hang2

section .data
ok:     db "  brktest: grew heap 32 KB, R/W across 8 pages - OK", 10
ok_len  equ $ - ok
bad:    db "  brktest: FAILED", 10
bad_len equ $ - bad
