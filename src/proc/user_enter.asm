; ─────────────────────────────────────────────────────────────
; asm/user_enter.asm — enter ring 3 (x86-64) + a ring-3 test program.
; ─────────────────────────────────────────────────────────────
[BITS 64]
global enter_ring3
global user_test_start
global user_test_end

; void enter_ring3(uint64_t entry /*rdi*/, uint64_t user_rsp /*rsi*/)
enter_ring3:
    mov  ax, 0x1B            ; user data selector (rpl 3)
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    push 0x1B                ; SS  = user data
    push rsi                 ; RSP = user stack
    push 0x202               ; RFLAGS (IF=1, reserved bit)
    push 0x23                ; CS  = user code (rpl 3)
    push rdi                 ; RIP = entry
    iretq                    ; → ring 3

; ── Position-independent ring-3 test program ─────────────────
; Copied into a user page at runtime; uses RIP-relative addressing
; so it works at whatever user VA it is mapped to.
user_test_start:
    mov  rax, 1              ; SYS_WRITE
    mov  rdi, 1              ; fd = stdout
    lea  rsi, [rel .msg]     ; buf (RIP-relative)
    mov  rdx, .msg_len       ; len
    syscall

    mov  rax, 0              ; SYS_EXIT
    xor  rdi, rdi            ; code 0
    syscall
.hang:
    jmp  .hang
.msg:     db "Hello from ring 3! syscall/sysret OK", 10
.msg_len  equ $ - .msg
user_test_end:
