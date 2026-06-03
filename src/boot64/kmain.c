/**
 * @file    src/boot64/kmain.c
 * @brief   Minimal 64-bit C kernel — proves the x86_64-elf toolchain,
 *          linking, and the long-mode C calling convention all work.
 */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;

#define VGA ((volatile uint16_t*)0xB8000)

static void puts_at(int row, const char* s, uint8_t attr) {
    volatile uint16_t* p = VGA + row * 80;
    for (int i = 0; s[i]; i++)
        p[i] = ((uint16_t)attr << 8) | (uint8_t)s[i];
}

void kmain64(void) {
    /* Clear the first few rows. */
    for (int i = 0; i < 80 * 5; i++) VGA[i] = 0x0700 | ' ';

    puts_at(0, "Noxis OS  --  x86_64 long mode", 0x0F);  /* white  */
    puts_at(1, "64-bit C kernel running.", 0x0A);         /* green  */
    puts_at(2, "kmain64() reached via SysV ABI call.", 0x0B); /* cyan */

    for (;;) __asm__ __volatile__("hlt");
}
