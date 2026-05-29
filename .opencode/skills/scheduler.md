# Skill: Scheduler

## Purpose
This skill covers implementing a preemptive multitasking scheduler and context switching for the Noxis OS. The scheduler is called from the PIT interrupt (IRQ0) and performs round-robin task switching.

## Key Concepts

### Process/Thread State

```c
typedef enum {
    PROC_READY    = 0,   /* Ready to run, in ready queue */
    PROC_RUNNING  = 1,   /* Currently executing on CPU */
    PROC_BLOCKED  = 2,   /* Waiting for I/O or event */
    PROC_SLEEPING = 3,   /* Sleeping (timed wait) */
    PROC_ZOMBIE   = 4,   /* Terminated, awaiting cleanup */
} proc_state_t;
```

### CPU Context (Saved/Restored on Switch)

When a process is preempted, the CPU state that must be saved:

```
Offset  Register   Notes
───────────────────────────────────
  0     EIP        Instruction pointer (already on stack from interrupt)
  4     CS         Code segment (already on stack from interrupt)
  8     EFLAGS     Flags (already on stack from interrupt)
 12     User ESP   Stack pointer (already on stack from interrupt if ring 3→0)
 16     User SS    Stack segment (already on stack if ring 3→0)
───────────────────────────────────  <-- These are pushed by CPU on interrupt
 20     GS         Segment register
 24     FS         Segment register
 28     ES         Segment register
 32     DS         Segment register
 36     EDI        General purpose
 40     ESI        General purpose
 44     EBP        Base pointer
 48     EBX        General purpose
 52     EDX        General purpose
 56     ECX        General purpose
 60     EAX        General purpose
───────────────────────────────────  <-- These are pushed by our ISR stub
 64     CR3        Page directory (only if process has own address space)
```

Total: 68 bytes (without CR3) or 72 bytes (with CR3 for full process switch).

### Context Switch Flow

```
1. PIT fires → IRQ0 → isr_stub_32 → isr_handler (C)
2. isr_handler calls scheduler_tick()
3. scheduler_tick() decrements current process quantum
4. If quantum == 0 or process blocked:
   a. Save current context (EIP, ESP, registers)
   b. Move current to ready queue
   c. Pick next process from ready queue
   d. Switch to next process's address space (load CR3 if different)
   e. Restore next process's context
5. Return from ISR → CPU executes next process
```

### ASM Context Switch

The switch itself is in ASM because it must precisely manipulate the stack and registers:

```nasm
; void context_switch(context_t* old, context_t* new)
; Saves current context to *old, loads context from *new
context_switch:
    ; Save old context
    mov  eax, [esp + 4]      ; old context pointer
    mov  [eax + 0],  edi
    mov  [eax + 4],  esi
    mov  [eax + 8],  ebp
    mov  [eax + 12], ebx
    mov  [eax + 16], edx
    mov  [eax + 20], ecx
    mov  [eax + 24], eax     ; save EAX
    ; ... save segment registers, EIP, ESP, EFLAGS ...

    ; Load new context
    mov  eax, [esp + 8]      ; new context pointer
    mov  edi, [eax + 0]
    mov  esi, [eax + 4]
    mov  ebp, [eax + 8]
    ; ... etc ...

    ; Load new address space if different
    mov  eax, [eax + 68]     ; CR3 value
    cmp  eax, [g_current_cr3]
    je   .skip_cr3
    mov  cr3, eax
.skip_cr3:

    ret
```

### Scheduler Algorithm: Round-Robin with Priorities

```c
#define MAX_PRIORITY   0    /* Highest */
#define MIN_PRIORITY   7    /* Lowest */
#define DEFAULT_QUANTUM 10  /* Ticks per quantum */

typedef struct process process_t;

/* Ready queue: one queue per priority level */
static process_t* g_ready_queues[8];  /* Heads of linked lists */

/**
 * @brief Called every PIT tick to manage scheduling
 */
void scheduler_tick(void) {
    process_t* current = g_current_process;
    if (!current) return;

    if (current->state == PROC_RUNNING) {
        if (current->quantum_remaining > 0) {
            current->quantum_remaining--;
        }
        if (current->quantum_remaining == 0) {
            /* Preempt */
            _scheduler_preempt(current);
        }
    }
}
```

## Common Pitfalls

1. **Switching during a switch**: The scheduler must be reentrant-safe. If the PIT fires during a context switch, corrupt state is inevitable. Mask interrupts during the switch or use a `switching` flag.

2. **Stack corruption on switch**: If the stack pointer isn't saved and restored atomically, the wrong stack is used when the ISR returns. ESP must be the very first thing saved and the very last thing restored.

3. **Forgetting to save segment registers**: DS, ES, FS, GS contain user segment selectors. If not restored, the kernel's selectors leak into user mode, or vice versa.

4. **CR3 switch overhead**: Switching address spaces flushes the TLB. Avoid CR3 switches when switching between threads of the same process (threads share the address space).

5. **Idle task**: When no processes are ready, the scheduler must have an idle task that does `hlt` in a loop. Without it, the scheduler picks NULL and crashes.

6. **Blocked process queue vs ready queue**: Blocked processes must be on a separate "wait queue" and moved to "ready queue" when the event fires. Don't poll blocked processes.

7. **Quantum donation**: A process that blocks before its quantum expires should not be penalized — give it a fresh quantum when it wakes up, or its responsiveness suffers.

8. **Race on ready queue**: The PIT ISR adds processes to the ready queue, and the scheduler removes them. If not protected (masking interrupts), this is a classic ABA problem.

## Debugging Tips

- Add a "tick counter" visible on VGA (e.g., increment a character) to visually confirm scheduling is running
- Dump current process name/ID to VGA each context switch to see if processes are actually being scheduled
- If the system freezes after enabling interrupts: check that the PIT ISR correctly sends EOI to the PIC
- Triple fault after context switch: usually means the stack was corrupted or ESP points to invalid memory
- Use QEMU `-d cpu_reset` to trace resets caused by triple faults
- Add a watchdog: if the scheduler hasn't switched in N ticks, trigger a kernel panic with register dump
