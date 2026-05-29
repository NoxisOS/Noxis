; ─────────────────────────────────────────────────────────────
; asm/ports.asm — x86 port I/O primitives
;
; Purpose: Raw port I/O operations that C cannot express directly.
;          All kernel I/O goes through these functions.
;
; Register convention:
;   cdecl: return in AL/AX/EAX, parameters on stack
;   All functions preserve EBX, ESI, EDI, EBP, ESP
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

global port_byte_in
global port_byte_out
global port_word_in
global port_word_out
global port_dword_in
global port_dword_out
global io_delay

; ── Port byte I/O ────────────────────────────────────────────

; uint8_t port_byte_in(uint16_t port);
port_byte_in:
    mov  dx, [esp + 4]             ; port number
    in   al, dx
    ret

; void port_byte_out(uint16_t port, uint8_t data);
port_byte_out:
    mov  dx, [esp + 4]             ; port number
    mov  al, [esp + 8]             ; data byte
    out  dx, al
    ret

; ── Port word I/O ────────────────────────────────────────────

; uint16_t port_word_in(uint16_t port);
port_word_in:
    mov  dx, [esp + 4]
    in   ax, dx
    ret

; void port_word_out(uint16_t port, uint16_t data);
port_word_out:
    mov  dx, [esp + 4]
    mov  ax, [esp + 8]
    out  dx, ax
    ret

; ── Port dword I/O ───────────────────────────────────────────

; uint32_t port_dword_in(uint16_t port);
port_dword_in:
    mov  dx, [esp + 4]
    in   eax, dx
    ret

; void port_dword_out(uint16_t port, uint32_t data);
port_dword_out:
    mov  dx, [esp + 4]
    mov  eax, [esp + 8]
    out  dx, eax
    ret

; ── I/O delay (writes to unused port 0x80) ───────────────────
io_delay:
    out  0x80, al
    ret
