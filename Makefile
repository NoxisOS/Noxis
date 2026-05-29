# ─────────────────────────────────────────────────────────────
# Noxis OS Build System
# ─────────────────────────────────────────────────────────────

# ── Shell (Windows cmd) ──────────────────────────────────────
SHELL     = cmd

# ── Toolchain ────────────────────────────────────────────────
CC        = i686-elf-gcc
LD        = i686-elf-ld
AS        = nasm
OBJCOPY   = i686-elf-objcopy

# ── Directories ──────────────────────────────────────────────
BUILD_DIR = build
SRC_DIR   = src

# ── Compiler flags ───────────────────────────────────────────
CFLAGS    = -std=c11 -ffreestanding -nostdlib -nostdinc \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -m32 -march=i686 -mtune=generic \
            -fno-stack-protector -fno-exceptions \
            -fno-asynchronous-unwind-tables \
            -mno-mmx -mno-sse -mgeneral-regs-only \
            -I src

CFLAGS   += -O2

# ── Linker flags ─────────────────────────────────────────────
LDFLAGS   = -T linker.ld -nostdlib -m elf_i386

# ── Assembler flags ──────────────────────────────────────────
ASFLAGS   = -f elf32
BOOTFLAGS = -f bin

# ── Kernel sources ───────────────────────────────────────────
KERNEL_C_OBJS   = build/kernel/early.o
KERNEL_ASM_OBJS = build/asm/kernel_entry.o
KERNEL_OBJS     = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# ── Output files ─────────────────────────────────────────────
KERNEL_ELF  = build/kernel.elf
KERNEL_BIN  = build/kernel.bin
MBR_BIN     = build/mbr.bin
LOADER_BIN  = build/loader.bin
DISK_IMG    = build/noxis.img

# ── Default target ───────────────────────────────────────────
.PHONY: all clean run run-debug
all: $(DISK_IMG)

# ── Disk image (PowerShell) ──────────────────────────────────
$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN)
	@echo BUILD $(DISK_IMG)
	powershell -NoProfile -Command "$$img='$(DISK_IMG)'; $$bytes=2880*512; $$f=New-Object IO.FileStream($$img,'Create'); $$f.SetLength($$bytes); $$f.Close(); $$fs=New-Object IO.FileStream($$img,'Open'); $$fs.Position=0; $$mbr=[IO.File]::ReadAllBytes('$(MBR_BIN)'); $$fs.Write($$mbr,0,$$mbr.Length); $$fs.Position=512; $$ldr=[IO.File]::ReadAllBytes('$(LOADER_BIN)'); $$fs.Write($$ldr,0,$$ldr.Length); $$fs.Position=2560; $$krnl=[IO.File]::ReadAllBytes('$(KERNEL_BIN)'); $$fs.Write($$krnl,0,$$krnl.Length); $$fs.Close(); echo '  -> 1.44 MB floppy image ready'"

# ── Kernel ELF ───────────────────────────────────────────────
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo LD   $@
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# ── Kernel flat binary ───────────────────────────────────────
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo BIN  $@
	$(OBJCOPY) -O binary $< $@

# ── MBR ──────────────────────────────────────────────────────
$(MBR_BIN): src/boot/mbr.asm src/boot/defines.asm
	@echo AS   $@
	if not exist build mkdir build
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

# ── Loader ───────────────────────────────────────────────────
$(LOADER_BIN): src/boot/loader.asm src/boot/defines.asm
	@echo AS   $@
	if not exist build mkdir build
	$(AS) $(BOOTFLAGS) -isrc/boot/ $< -o $@

# ── Kernel C objects ─────────────────────────────────────────
build/kernel/%.o: src/kernel/%.c
	@if not exist build\kernel mkdir build\kernel
	@echo CC   $<
	$(CC) $(CFLAGS) -c $< -o $@

# ── Kernel ASM objects ───────────────────────────────────────
build/asm/%.o: src/asm/%.asm
	@if not exist build\asm mkdir build\asm
	@echo AS   $<
	$(AS) $(ASFLAGS) $< -o $@

# ── Run in QEMU ──────────────────────────────────────────────
QEMU = "D:\Program Files\qemu\qemu-system-i386"

run: $(DISK_IMG)
	@echo RUN  QEMU
	$(QEMU) -fda $(DISK_IMG) -no-reboot -no-shutdown

# ── Run with GDB ─────────────────────────────────────────────
run-debug: $(DISK_IMG)
	@echo RUN  QEMU + GDB on :1234
	start "QEMU" /B $(QEMU) -fda $(DISK_IMG) -no-reboot -no-shutdown -s -S
	@echo Connect: i686-elf-gdb -x .gdbinit

# ── Clean ────────────────────────────────────────────────────
clean:
	@if exist build rmdir /s /q build
