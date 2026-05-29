# Skill: Drivers

## Purpose
This skill covers writing hardware device drivers for the Noxis OS. Every driver follows the same pattern: probe → initialize → register ISR → expose interface. All I/O is defensive.

## Key Concepts

### Driver Architecture

```
┌────────────────────────────────────────┐
│              Driver Interface           │
│  (vga_write, kbd_read, ata_read, ...)  │
└────────────────┬───────────────────────┘
                 │
┌────────────────┴───────────────────────┐
│            Driver Internals             │
│  ┌─────────┐ ┌──────────┐ ┌─────────┐ │
│  │ Init    │ │  ISR     │ │  State  │ │
│  │ probe,  │ │ handle   │ │ manage  │ │
│  │ config  │ │ IRQs     │ │ buffers │ │
│  └─────────┘ └──────────┘ └─────────┘ │
└────────────────┬───────────────────────┘
                 │
┌────────────────┴───────────────────────┐
│              HAL Layer                  │
│  port_byte_in/out, pic_send_eoi, etc.  │
└────────────────┬───────────────────────┘
                 │
┌────────────────┴───────────────────────┐
│              Hardware                   │
└────────────────────────────────────────┘
```

### Port I/O (x86)

```c
static inline uint8_t  port_byte_in(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void port_byte_out(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint16_t port_word_in(uint16_t port) {
    uint16_t result;
    __asm__ volatile ("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void port_word_out(uint16_t port, uint16_t data) {
    __asm__ volatile ("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint32_t port_dword_in(uint16_t port) {
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void port_dword_out(uint16_t port, uint32_t data) {
    __asm__ volatile ("outl %0, %1" : : "a"(data), "Nd"(port));
}
```

Note: In the Noxis codebase, these go in `src/hal/ports.c` as proper functions. The inline ASM above is the underlying implementation, but we wrap them in C functions for consistency. **However**, since the conventions say no inline ASM in .c files, we implement `port_byte_in` etc. in a separate `src/asm/ports.asm` file and declare them as `extern` in `src/hal/ports.h`.

### I/O Delay

Some devices need a brief delay between port accesses. On ancient hardware this matters; on QEMU it usually doesn't. We include it for correctness:

```c
static void _io_delay(void) {
    /* Write to an unused port — takes ~1 µs on ISA bus */
    port_byte_out(0x80, 0);
}
```

### VGA Text Mode Driver

**Hardware:** VGA-compatible text mode at physical address 0xB8000.
**Resolution:** 80 columns × 25 rows. Each character = 2 bytes: [ASCII char] [attribute byte].

```
Attribute byte: 0xAB where:
  A = Background color (4 bits: IRGB → intensity, red, green, blue)
  B = Foreground color (4 bits: IRGB)
```

Standard colors:
```c
#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

#define VGA_ENTRY(c, fg, bg)  (((bg) << 4 | (fg)) << 8 | (c))
```

**Scrolling algorithm:**
1. Move lines 1-24 up by one row (memmove)
2. Clear line 24
3. Decrement cursor row

**Cursor control:**
- Position = `row * 80 + col`
- Write high byte to port 0x3D4, low byte to port 0x3D5
- Cursor low register = 0x0F, cursor high register = 0x0E

### PS/2 Keyboard Driver

**Hardware:** PS/2 controller at ports 0x60 (data) and 0x64 (status/command).
**IRQ:** IRQ1 (vector 0x21 after PIC remap).

```
Port 0x64 (Status, read):
  Bit 0: Output buffer full (1 = data available)
  Bit 1: Input buffer full (0 = ready to accept command)

Port 0x60 (Data, read/write):
  Read: Scancode from keyboard
  Write: Command byte to keyboard
```

**Scancode handling:**
- Key press: scancode with bit 7 = 0
- Key release: scancode with bit 7 = 1 (scancode | 0x80)
- The ISR reads the scancode, converts to ASCII using a lookup table, and places it in a circular buffer.

**Circular buffer for keyboard input:**
```c
#define KBD_BUFFER_SIZE 256

typedef struct {
    uint8_t  buffer[KBD_BUFFER_SIZE];
    uint32_t head;       /* Write position */
    uint32_t tail;       /* Read position */
    uint32_t count;      /* Number of items */
} kbd_buffer_t;
```

### PIT Driver (Programmable Interval Timer)

**Hardware:** Intel 8253/8254 at ports 0x40-0x43.
**IRQ:** IRQ0 (vector 0x20 after PIC remap).
**Base frequency:** 1,193,182 Hz (NTSC color burst crystal ÷ 3).

```c
/* Set PIT channel 0 to fire at 'hz' frequency */
void pit_set_frequency(uint32_t hz) {
    uint32_t divisor = 1193182 / hz;

    /* Command byte: Channel 0, lobyte/hibyte, rate generator, binary */
    port_byte_out(0x43, 0x36);

    /* Send divisor (low then high byte) */
    port_byte_out(0x40, (uint8_t)(divisor & 0xFF));
    port_byte_out(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}
```

Common frequencies:
- 1000 Hz → divisor 1193 (good for millisecond precision)
- 100 Hz → divisor 11932 (good for scheduling)
- 18.2 Hz → divisor 65535 (original PC timer rate, bad precision)

### ATA PIO Driver

**Hardware:** ATA controller at ports 0x1F0-0x1F7 (primary) or 0x170-0x177 (secondary).
**IRQ:** IRQ14 (primary), IRQ15 (secondary) — vectors 0x2E, 0x2F.

```
Ports (Primary):
  0x1F0: Data register (16-bit)
  0x1F1: Features / Error
  0x1F2: Sector count
  0x1F3: LBA low
  0x1F4: LBA mid
  0x1F5: LBA high
  0x1F6: Drive/Head (0xE0 for master, 0xF0 for slave; OR with LBA bits 24-27)
  0x1F7: Command / Status (write = send command, read = get status)

Status register (0x1F7 read):
  Bit 7: BSY (busy)
  Bit 6: DRDY (drive ready)
  Bit 3: DRQ (data request — ready for transfer)
  Bit 0: ERR (error)

Commands:
  0x20: Read sectors with retry
  0x30: Write sectors with retry
  0xEC: Identify drive
```

**Read sector procedure:**
1. Wait for BSY=0 and DRDY=1
2. Write sector count, LBA bytes, drive/head to ports
3. Write READ command (0x20) to command port
4. Wait for BSY=0 and DRQ=1
5. Read 256 words (512 bytes) from data port using `rep insw`
6. Repeat for multi-sector reads

## Common Pitfalls

1. **Race conditions on I/O**: Reading status port and acting on it must happen atomically with respect to interrupts. Mask the device's IRQ during critical sections if needed.

2. **BSY/DRQ polling loops**: Always add a timeout counter to polling loops. Hardware can hang. An infinite loop in the kernel = dead system.

3. **PIC EOI ordering for IRQ8-15**: Always send EOI to slave first (`outb(0xA0, 0x20)`), then master (`outb(0x20, 0x20)`). If you do master first, the slave's interrupt is still pending and may trigger again.

4. **Keyboard ghosting / typematic**: The keyboard sends make/break codes at typematic rate. Don't treat every scancode as a new keypress — check bit 7.

5. **VGA cursor off-screen**: Writing past (24, 79) writes to video memory outside the visible area. Always clamp cursor position.

6. **PIT divisor = 0**: Division by zero → hardware interprets divisor 0 as 65536. Use this intentionally if you need the slowest rate.

7. **ATA drive selection**: Bit 4 of the drive/head register selects master (0) or slave (1). Using the wrong value reads the wrong drive.

8. **Memory-mapped I/O vs Port-mapped I/O**: x86 uses separate I/O address space for most legacy devices. Use `inb`/`outb`, not memory dereferences. Exception: VGA text buffer is memory-mapped at 0xB8000.

## Driver Skeleton Pattern

Every driver in Noxis follows this pattern:

```c
/**
 * @file    drivers/vga.c
 * @brief   VGA text mode driver — 80×25 character display
 * @author  Noxis Team
 * @date    2026-05-29
 */

#include <drivers/vga.h>
#include <hal/ports.h>
#include <common/types.h>

/* ── constants ─────────────────────────────────────────────── */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BUFFER  ((volatile uint16_t*)0xC00B8000) /* mapped to phys 0xB8000 */

/* ── file-scope state ──────────────────────────────────────── */
static uint32_t g_vga_row;
static uint32_t g_vga_col;
static uint8_t  g_vga_color;

/* ── private functions ─────────────────────────────────────── */
static uint8_t _vga_make_color(uint8_t fg, uint8_t bg) {
    return (bg << 4) | (fg & 0x0F);
}

static uint16_t _vga_make_entry(uint8_t c, uint8_t color) {
    return ((uint16_t)color << 8) | (uint16_t)c;
}

/* ── public functions ──────────────────────────────────────── */

/**
 * @brief Initializes the VGA text mode driver
 * @param fg  Default foreground color
 * @param bg  Default background color
 * @return OS_OK
 */
os_status_t vga_init(uint8_t fg, uint8_t bg) {
    g_vga_row = 0;
    g_vga_col = 0;
    g_vga_color = _vga_make_color(fg, bg);
    _vga_clear_screen();
    return OS_OK;
}

/* ... put_char, write, clear, set_cursor, scroll etc. ... */
```

## Debugging Tips

- VGA: Write 'X' to `*(uint16_t*)0xC00B8000` (virtual kernel address) to verify VGA buffer is mapped
- Keyboard: Dump raw scancodes to VGA to see what the hardware is actually sending
- PIT: Toggle a VGA character each tick to verify timer frequency visually
- ATA: Use `info qtree` in QEMU monitor to see attached block devices
- Use `-d int` in QEMU to trace interrupt delivery
- QEMU `-serial stdio` to see kernel debug output on host terminal (requires serial driver)
