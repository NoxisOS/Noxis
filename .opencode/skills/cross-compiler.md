# Skill: Cross-Compiler

## Purpose
This skill covers building and using the `i686-elf` cross-compiler toolchain required for Noxis OS development. We build GCC and Binutils from source, targeting a freestanding `i686-elf` environment.

## Key Concepts

### Why a Cross-Compiler?

A native compiler (e.g., `gcc` on Linux) targets the host system. It links against the host's libc, assumes the host's ABI, and may emit code that depends on host OS features. A cross-compiler:
- Targets bare-metal `i686-elf` (no OS)
- Produces ELF32 executables
- Has no dependency on host headers or libraries
- Uses `-ffreestanding` and `-nostdlib` by default through configuration

### Toolchain Components

| Component | Purpose                                              | Version (Tested) |
|-----------|------------------------------------------------------|-------------------|
| Binutils  | Assembler (`as`), Linker (`ld`), object tools        | 2.41              |
| GCC       | C compiler (`gcc`), targets `i686-elf`               | 13.2.0            |

### Build Process (Unix-like)

```bash
# 1. Set environment variables
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# 2. Build Binutils
mkdir build-binutils && cd build-binutils
../binutils-X.Y/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install

# 3. Build GCC
mkdir build-gcc && cd build-gcc
../gcc-X.Y/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c \
    --without-headers \
    --disable-hosted-libstdcxx
make -j$(nproc) all-gcc
make install-gcc
```

### Windows with WSL2 or MSYS2

On Windows, the recommended approaches:
1. **WSL2**: Full Linux environment — follow the Unix build process above
2. **MSYS2**: Install mingw-w64 toolchain and build cross-compiler within MSYS2
3. **Native**: Use prebuilt cross-compiler from OSDev wiki or build with Cygwin

### Compiler Flags

```makefile
# Cross-compiler
CC       = i686-elf-gcc
LD       = i686-elf-ld
AS       = nasm

# C flags
CFLAGS   = -std=c11 -ffreestanding -nostdlib -nostdinc \
           -Wall -Wextra -Werror -Wno-unused-parameter \
           -m32 -march=i686 -mtune=generic \
           -fno-stack-protector -fno-exceptions -fno-rtti \
           -fno-asynchronous-unwind-tables

# Linker flags
LDFLAGS  = -T linker.ld -nostdlib -m elf_i386

# NASM flags
ASMFLAGS = -f elf32

# Include paths (relative to src/)
INCLUDES = -I src/common -I src/hal -I src/mm -I src/drivers \
           -I src/kernel -I src/vfs -I src/proc -I src/syscall
```

### Essential GCC Freestanding Flags Explained

| Flag                         | Why                                                       |
|------------------------------|-----------------------------------------------------------|
| `-ffreestanding`             | Assume no standard library; don't optimize main()         |
| `-nostdlib`                  | Don't link standard library                               |
| `-nostdinc`                  | Don't search standard include directories                 |
| `-fno-stack-protector`       | No stack canaries (need libc support for __stack_chk_fail)|
| `-fno-exceptions`            | No C++ exceptions (C only, but gcc may emit unwinding)    |
| `-fno-rtti`                  | No C++ RTTI                                               |
| `-fno-asynchronous-unwind-tables` | Smaller binary, no .eh_frame section                 |
| `-mno-red-zone`              | No red zone (for kernel, stack is preciously limited)     |
| `-mno-mmx` `-mno-sse`        | No SIMD (requires saving FPU state on context switch)     |
| `-mgeneral-regs-only`        | Only general-purpose registers (no FPU/SSE at all)        |

### Verifying the Toolchain

```bash
# Test the cross-compiler
i686-elf-gcc --version
i686-elf-gcc -dumpmachine         # → i686-elf
i686-elf-gcc -dumpspecs | grep '*startfile'  # → empty (no crt0)

# Test compilation of a freestanding file
echo 'void _start(void) { while(1); }' | \
  i686-elf-gcc -x c - -ffreestanding -nostdlib -o /tmp/test.elf -m32

# Check the output
i686-elf-objdump -f /tmp/test.elf
```

## Common Pitfalls

1. **Wrong target triplet**: `i686-elf`, not `i386-elf`, not `i586-elf`, not `x86_64-elf`. The triplet determines the default CPU architecture and code generation.

2. **GCC build failure "cannot find libgcc"**: You need to build `all-gcc` first, then `all-target-libgcc`. In the initial bootstrap, there are no target headers, so `libgcc` can't be fully built yet. Build with `--without-headers`.

3. **Missing dependencies**: Building GCC requires GMP, MPFR, MPC. Either install them system-wide or use `./contrib/download_prerequisites` in the GCC source directory.

4. **C++ standard library attempt**: `--enable-languages=c` only. Adding `c++` tries to build `libstdc++` which requires OS headers (unistd.h, etc.). Don't do it.

5. **Linker script mismatch**: The cross-compiler emits code assuming the ELF entry point is `_start` (default). Our linker script must define `ENTRY(_start)` or specify the entry symbol explicitly.

6. **Multilib confusion**: `i686-elf-gcc` may build for multiple variants (with/without FPU, etc.). Use `-mno-sse -mno-mmx` to avoid SIMD instructions that require saving FPU state.

7. **Host contamination**: If `gcc` (without prefix) accidentally gets used, it links host libraries. Always check your Makefile uses `$(CC)` = `i686-elf-gcc`, never bare `gcc`.

## Project Integration

The Makefile should check for the cross-compiler at the start:

```makefile
ifndef CROSS
  CC := i686-elf-gcc
else
  CC := $(CROSS)gcc
endif

.PHONY: check-toolchain
check-toolchain:
	@which $(CC) > /dev/null || (echo "ERROR: $(CC) not found in PATH"; exit 1)
```

## Debugging Tips

- If compilation succeeds but linking fails with "undefined reference to...": check your linker script, entry symbol, and that all object files are included
- If QEMU reports "Boot failed: not a bootable disk": the output format is wrong (needs to be raw binary, not ELF, for the bootloader)
- `i686-elf-objdump -h kernel.elf` shows all sections and their addresses — verify sections are at expected addresses
- `i686-elf-nm kernel.elf` lists all symbols — verify `_start` is at the correct address
