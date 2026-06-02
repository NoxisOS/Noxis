#!/usr/bin/env bash
# tools/linux/build_disk.sh
# Creates a 1 MB NoxFS v2 disk image and injects ELF files at the root.
#
# Usage:
#   tools/linux/build_disk.sh <out> [name:path ...]
#
# Example:
#   tools/linux/build_disk.sh build/disk.img \
#       ctest.elf:build/ctest.elf            \
#       nsh.elf:build/nsh.elf

set -e

# ── constants (must match noxfs.h) ─────────────────────────────
BLKSZ=512
INO_SZ=64
DT_SZ=32
INODES=64
DISK_MB=1
NOXFS_MAGIC="4e584632"  # "NXF2" little-endian

DISK_SIZE=$(( DISK_MB * 1024 * 1024 ))
N_BLOCKS=$(( DISK_SIZE / BLKSZ ))
INO_BLKS=$(( (INODES * INO_SZ + BLKSZ - 1) / BLKSZ ))
SB_BLK=0
BLK_BMP_BLK=1
INO_BMP_BLK=2
INO_TBL_BLK=3
DATA_START=$(( INO_TBL_BLK + INO_BLKS ))

# ── arguments ──────────────────────────────────────────────────
if [ -z "$1" ]; then
    echo "usage: build_disk.sh <out> [name:path ...]" >&2
    exit 1
fi
OUT=$1
shift

# ── helpers ────────────────────────────────────────────────────
# Work on a temp binary file via Python (portable little-endian writes)
TMPPY=$(mktemp /tmp/mkdisk_XXXXXX.py)

cat > "$TMPPY" << 'PYEOF'
import sys, struct, os, math

BLKSZ      = 512
INO_SZ     = 64
DT_SZ      = 32
NOXFS_MAGIC= 0x4E584632
NOXFS_DIRECT = 10

def main():
    args = sys.argv[1:]
    out  = args[0]
    files= args[1:]  # "name:path"

    INODES     = 64
    DISK_MB    = 1
    DISK_SIZE  = DISK_MB * 1024 * 1024
    N_BLOCKS   = DISK_SIZE // BLKSZ
    INO_BLKS   = math.ceil(INODES * INO_SZ / BLKSZ)
    SB_BLK     = 0
    BLK_BMP_BLK= 1
    INO_BMP_BLK= 2
    INO_TBL_BLK= 3
    DATA_START = INO_TBL_BLK + INO_BLKS

    img = bytearray(DISK_SIZE)

    blk_bmp = bytearray(BLKSZ)
    ino_bmp = bytearray(BLKSZ)

    used_blks = DATA_START
    used_inos = 1  # root = inode 0

    # mark metadata blocks used
    for i in range(DATA_START):
        blk_bmp[i // 8] |= (1 << (i % 8))
    # mark root inode used
    ino_bmp[0] |= 1

    next_blk = [DATA_START]
    def alloc_blk():
        for b in range(next_blk[0], N_BLOCKS):
            if not (blk_bmp[b // 8] >> (b % 8)) & 1:
                blk_bmp[b // 8] |= (1 << (b % 8))
                next_blk[0] = b + 1
                return b
        sys.exit("mkdisk: out of disk blocks")

    # root dir data block
    root_data_blk = alloc_blk()
    used_blks += 1

    # root inode (inode 0)
    r_off = INO_TBL_BLK * BLKSZ
    struct.pack_into('<H', img, r_off + 0,  0x41ED)       # mode = dir | 0755
    struct.pack_into('<I', img, r_off + 10, root_data_blk)
    struct.pack_into('<H', img, r_off + 54, 1)            # links

    dirents = []

    for spec in files:
        colon = spec.index(':')
        fname = spec[:colon]
        fpath = spec[colon+1:]
        if not os.path.exists(fpath):
            print(f"  ! skip {fpath}")
            continue

        fdata = open(fpath, 'rb').read()
        fsz   = len(fdata)
        n_blks= max(1, math.ceil(fsz / BLKSZ))

        ino_id = used_inos
        used_inos += 1

        fb = []
        for _ in range(n_blks):
            b = alloc_blk()
            used_blks += 1
            fb.append(b)

        for j, b in enumerate(fb):
            chunk = fdata[j*BLKSZ:(j+1)*BLKSZ]
            img[b*BLKSZ : b*BLKSZ + len(chunk)] = chunk

        f_off = INO_TBL_BLK * BLKSZ + ino_id * INO_SZ
        struct.pack_into('<H', img, f_off + 0,  0x81ED)  # file | 0755 (ELFs executable)
        struct.pack_into('<I', img, f_off + 6,  fsz)
        for j in range(min(n_blks, NOXFS_DIRECT)):
            struct.pack_into('<I', img, f_off + 10 + j*4, fb[j])
        if n_blks > NOXFS_DIRECT:
            ind = alloc_blk()
            used_blks += 1
            struct.pack_into('<I', img, f_off + 50, ind)
            for j in range(NOXFS_DIRECT, n_blks):
                struct.pack_into('<I', img, ind*BLKSZ + (j-NOXFS_DIRECT)*4, fb[j])
        struct.pack_into('<H', img, f_off + 54, 1)

        ino_bmp[ino_id // 8] |= (1 << (ino_id % 8))

        de = bytearray(DT_SZ)
        struct.pack_into('<I', de, 0, ino_id)
        struct.pack_into('<H', de, 4, DT_SZ)
        nb = fname.encode('ascii')[:23]
        de[6] = len(nb)
        de[7] = 1  # FT_FILE
        de[8:8+len(nb)] = nb
        dirents.append(de)

        print(f"  + {fname:<16} {fsz:>7} bytes  inode {ino_id}")

    # write root dirents
    dir_sz   = len(dirents) * DT_SZ
    root_need= max(1, math.ceil(dir_sz / BLKSZ))
    rblocks  = [root_data_blk]
    for i in range(1, root_need):
        b = alloc_blk()
        used_blks += 1
        rblocks.append(b)
        struct.pack_into('<I', img, r_off + 10 + i*4, b)
    for i, de in enumerate(dirents):
        off    = i * DT_SZ
        bidx   = off // BLKSZ
        within = off % BLKSZ
        dst    = rblocks[bidx] * BLKSZ + within
        img[dst:dst+DT_SZ] = de
    struct.pack_into('<I', img, r_off + 6, dir_sz)

    # write bitmap blocks into image
    img[BLK_BMP_BLK*BLKSZ : BLK_BMP_BLK*BLKSZ + BLKSZ] = blk_bmp
    img[INO_BMP_BLK*BLKSZ : INO_BMP_BLK*BLKSZ + BLKSZ] = ino_bmp

    # superblock
    sb = SB_BLK * BLKSZ
    struct.pack_into('<IIIIIIIIII', img, sb,
        NOXFS_MAGIC,
        N_BLOCKS,
        INODES,
        N_BLOCKS - used_blks,
        INODES   - used_inos,
        BLK_BMP_BLK,
        INO_BMP_BLK,
        INO_TBL_BLK,
        DATA_START,
        0,  # root_ino = 0
    )

    open(out, 'wb').write(img)
    print(f"  -> {out}  ({used_inos-1} inodes used, {N_BLOCKS - used_blks} blocks free)")

main()
PYEOF

python3 "$TMPPY" "$OUT" "$@"
rm -f "$TMPPY"
