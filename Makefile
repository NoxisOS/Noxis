# ─────────────────────────────────────────────────────────────
# Noxis OS Build System
# ─────────────────────────────────────────────────────────────

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
            -I $(SRC_DIR)/common \
            -I $(SRC_DIR)/kernel

# Debug build (uncomment for debugging)
# CFLAGS   += -g -O0

# Release build
CFLAGS   += -O2

# ── Linker flags ─────────────────────────────────────────────
LDFLAGS   = -T linker.ld -nostdlib -m elf_i386

# ── Assembler flags ──────────────────────────────────────────
ASFLAGS   = -f elf32
BOOTFLAGS = -f bin

# ── Kernel sources ───────────────────────────────────────────
KERNEL_C_SRCS   = $(wildcard $(SRC_DIR)/kernel/*.c)
KERNEL_ASM_SRCS = $(wildcard $(SRC_DIR)/asm/*.asm)
KERNEL_C_OBJS   = $(patsubst $(SRC_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS = $(patsubst $(SRC_DIR)/asm/%.asm,$(BUILD_DIR)/asm/%.o,$(KERNEL_ASM_SRCS))
KERNEL_OBJS     = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# ── Bootloader sources ───────────────────────────────────────
MBR_SRC    = $(SRC_DIR)/boot/mbr.asm
LOADER_SRC = $(SRC_DIR)/boot/loader.asm
BOOT_DEFS  = $(SRC_DIR)/boot/defines.asm

# ── Output files ─────────────────────────────────────────────
KERNEL_ELF  = $(BUILD_DIR)/kernel.elf
KERNEL_BIN  = $(BUILD_DIR)/kernel.bin
MBR_BIN    = $(BUILD_DIR)/mbr.bin
LOADER_BIN = $(BUILD_DIR)/loader.bin
DISK_IMG    = $(BUILD_DIR)/noxis.img

# ── Default target ───────────────────────────────────────────
.PHONY: all clean run run-debug
all: $(DISK_IMG)

# ── Check toolchain ──────────────────────────────────────────
check-toolchain:
	@which $(CC) > /dev/null 2>/dev/null || (echo "ERROR: $(CC) not found."; exit 1)
	@which $(AS) > /dev/null 2>/dev/null || (echo "ERROR: $(AS) not found."; exit 1)

# ── Disk image ───────────────────────────────────────────────
# Sector layout: 0=Stage1, 1-4=Stage2, 5+=Kernel
$(DISK_IMG): $(MBR_BIN) $(LOADER_BIN) $(KERNEL_BIN)
	@echo "BUILD disk image (1.44 MB floppy)"
	dd if=/dev/zero of=$(DISK_IMG) bs=512 count=2880 2>/dev/null
	dd if=$(MBR_BIN) of=$(DISK_IMG) bs=512 count=1 conv=notrunc 2>/dev/null
	dd if=$(LOADER_BIN) of=$(DISK_IMG) bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN) of=$(DISK_IMG) bs=512 seek=5 conv=notrunc 2>/dev/null

# ── Kernel ELF ───────────────────────────────────────────────
$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@echo "LD   $@"
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# ── Kernel flat binary ───────────────────────────────────────
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "BIN  $@"
	$(OBJCOPY) -O binary $< $@

# ── MBR (Master Boot Record) ─────────────────────────────────
$(MBR_BIN): $(MBR_SRC) $(BOOT_DEFS)
	@echo "AS   $@"
	$(AS) $(BOOTFLAGS) -i$(SRC_DIR)/boot/ $< -o $@

# ── Loader (enters protected mode, loads kernel) ─────────────
$(LOADER_BIN): $(LOADER_SRC) $(BOOT_DEFS)
	@echo "AS   $@"
	$(AS) $(BOOTFLAGS) -i$(SRC_DIR)/boot/ $< -o $@

# ── Kernel C objects ─────────────────────────────────────────
$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(dir $@)
	@echo "CC   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ── Kernel ASM objects ───────────────────────────────────────
$(BUILD_DIR)/asm/%.o: $(SRC_DIR)/asm/%.asm
	@mkdir -p $(dir $@)
	@echo "AS   $<"
	$(AS) $(ASFLAGS) $< -o $@

# ── Run in QEMU ──────────────────────────────────────────────
run: $(DISK_IMG)
	@echo "RUN  QEMU (floppy)"
	qemu-system-i386 -fda $(DISK_IMG) -no-reboot -no-shutdown

# ── Run with GDB debugging ───────────────────────────────────
run-debug: $(DISK_IMG)
	@echo "RUN  QEMU (debug mode, GDB on :1234)"
	qemu-system-i386 -fda $(DISK_IMG) -no-reboot -no-shutdown -s -S &
	@echo "Connect with: i686-elf-gdb -x .gdbinit"

# ── Clean ────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)

# ── Ensure build directories exist ───────────────────────────
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/kernel
	mkdir -p $(BUILD_DIR)/asm
