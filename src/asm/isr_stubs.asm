; ─────────────────────────────────────────────────────────────
; asm/isr_stubs.asm — Interrupt Service Routine entry stubs
;
; Purpose: 48 entry points (0-47) for every interrupt/exception.
;          Each stub follows the canonical pattern: push dummy
;          error code (if CPU didn't), push vector, jump to common.
;
; CPU exceptions 0-31: some push error code, some don't.
; IRQ handlers 32-47: never push error code.
;
; isr_common saves full CPU context, calls C dispatcher,
; restores context, and executes iret.
; ─────────────────────────────────────────────────────────────

section .text
[BITS 32]

; ── Exports ──────────────────────────────────────────────────
global isr_common

; ── Stubs are auto-generated via macros ──────────────────────

; Exception with NO error code (CPU doesn't push one)
%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push dword 0                   ; dummy error code
    push dword %1                  ; interrupt vector
    jmp  isr_common
%endmacro

; Exception WITH error code (CPU pushes it)
%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    ; error code already on stack
    push dword %1                  ; interrupt vector
    jmp  isr_common
%endmacro

; ── CPU Exceptions (0–31) ────────────────────────────────────
ISR_NOERR  0     ; #DE  Divide Error
ISR_NOERR  1     ; #DB  Debug
ISR_NOERR  2     ; NMI  Non-Maskable Interrupt
ISR_NOERR  3     ; #BP  Breakpoint
ISR_NOERR  4     ; #OF  Overflow
ISR_NOERR  5     ; #BR  Bound Range Exceeded
ISR_NOERR  6     ; #UD  Invalid Opcode
ISR_NOERR  7     ; #NM  Device Not Available
ISR_ERR    8     ; #DF  Double Fault
ISR_NOERR  9     ;      Coprocessor Segment Overrun
ISR_ERR   10     ; #TS  Invalid TSS
ISR_ERR   11     ; #NP  Segment Not Present
ISR_ERR   12     ; #SS  Stack-Segment Fault
ISR_ERR   13     ; #GP  General Protection Fault
ISR_ERR   14     ; #PF  Page Fault
ISR_NOERR 15     ;      Intel Reserved
ISR_NOERR 16     ; #MF  x87 Floating-Point
ISR_ERR   17     ; #AC  Alignment Check
ISR_NOERR 18     ; #MC  Machine Check
ISR_NOERR 19     ; #XM  SIMD Floating-Point
ISR_NOERR 20     ; #VE  Virtualization Exception
ISR_NOERR 21     ; #CP  Control Protection
ISR_NOERR 22     ;      Intel Reserved
ISR_NOERR 23     ;      Intel Reserved
ISR_NOERR 24     ;      Intel Reserved
ISR_NOERR 25     ;      Intel Reserved
ISR_NOERR 26     ;      Intel Reserved
ISR_NOERR 27     ;      Intel Reserved
ISR_NOERR 28     ; #HV  Hypervisor Injection
ISR_NOERR 29     ; #VC  VMM Communication
ISR_ERR   30     ; #SX  Security Exception
ISR_NOERR 31     ;      Intel Reserved

; ── IRQ Handlers (32–47) ─────────────────────────────────────
; Remapped PIC: master=0x20, slave=0x28
ISR_NOERR 32     ; IRQ0  PIT Timer
ISR_NOERR 33     ; IRQ1  Keyboard
ISR_NOERR 34     ; IRQ2  Cascade (slave PIC)
ISR_NOERR 35     ; IRQ3  COM2
ISR_NOERR 36     ; IRQ4  COM1
ISR_NOERR 37     ; IRQ5  LPT2
ISR_NOERR 38     ; IRQ6  Floppy
ISR_NOERR 39     ; IRQ7  LPT1 / Spurious
ISR_NOERR 40     ; IRQ8  CMOS RTC
ISR_NOERR 41     ; IRQ9  Free
ISR_NOERR 42     ; IRQ10 Free
ISR_NOERR 43     ; IRQ11 Free
ISR_NOERR 44     ; IRQ12 PS/2 Mouse
ISR_NOERR 45     ; IRQ13 FPU
ISR_NOERR 46     ; IRQ14 Primary ATA
ISR_NOERR 47     ; IRQ15 Secondary ATA

; ── Common ISR handler ───────────────────────────────────────
isr_common:
    ; Save all general-purpose registers
    pushad                         ; EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI

    ; Save segment registers
    push ds
    push es
    push fs
    push gs

    ; Load kernel data segment into all segment regs
    mov  ax, 0x10                  ; kernel data selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Pass pointer to the stack frame to C handler
    push esp                       ; isr_frame_t* frame
    extern isr_handler
    call isr_handler
    add  esp, 4                    ; clean argument

    ; Restore segment registers
    pop  gs
    pop  fs
    pop  es
    pop  ds

    ; Restore general-purpose registers
    popad

    ; Remove error code and vector from stack
    add  esp, 8

    ; Return from interrupt
    iret
