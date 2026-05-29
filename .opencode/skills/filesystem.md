# Skill: Filesystem

## Purpose
This skill covers implementing a filesystem for the Noxis OS. The first target is FAT12/16 because of its simplicity and wide toolchain support. The design separates the VFS layer (generic file operations) from the concrete filesystem implementation.

## Key Concepts

### VFS Layer (Virtual File System)

The VFS provides a uniform interface for all file operations. Everything is a file descriptor:

```c
/**
 * @brief File descriptor abstraction — "everything is a file"
 */
typedef struct vfs_node vfs_node_t;

struct vfs_node {
    uint8_t  name[256];          /* File/directory name */
    uint32_t flags;              /* VFS_FILE, VFS_DIR, VFS_DEVICE, VFS_PIPE */
    uint32_t length;             /* File size in bytes */
    uint32_t inode;              /* Filesystem-specific identifier */

    /* Operations — function pointers for polymorphism */
    uint32_t (*read)  (vfs_node_t*, uint32_t offset, uint32_t size, uint8_t* buf);
    uint32_t (*write) (vfs_node_t*, uint32_t offset, uint32_t size, uint8_t* buf);
    void     (*open)  (vfs_node_t*);
    void     (*close) (vfs_node_t*);
    vfs_node_t* (*finddir)(vfs_node_t*, const uint8_t* name);

    /* Filesystem-specific data */
    void*    fs_data;
};

/* File descriptor table entry */
typedef struct {
    vfs_node_t* node;
    uint32_t    offset;          /* Current read/write position */
    uint32_t    flags;           /* Open flags */
} fd_entry_t;
```

### FAT12/16 Structure

```
┌─────────────┬──────────────┬───────────────┬──────────────┬─────────────┐
│  Boot Sector│  FAT1        │  FAT2 (copy)  │ Root Dir     │ Data Region │
│  1 sector   │  (variable)  │               │ (variable)   │ (rest of    │
│             │              │               │              │  disk)      │
└─────────────┴──────────────┴───────────────┴──────────────┴─────────────┘
```

**Boot Sector (BPB — BIOS Parameter Block):**
```
Offset  Size  Field
0x00    3     Jump instruction
0x03    8     OEM name
0x0B    2     Bytes per sector (always 512 for floppy/FAT12)
0x0D    1     Sectors per cluster
0x0E    2     Reserved sectors count
0x10    1     Number of FATs
0x11    2     Max root directory entries
0x13    2     Total sectors (16-bit — if 0, use 32-bit at 0x20)
0x15    1     Media descriptor
0x16    2     Sectors per FAT
0x18    2     Sectors per track
0x1A    2     Number of heads
0x1C    4     Hidden sectors
0x20    4     Total sectors (32-bit)
```

### FAT Cluster Chain

Each FAT entry is 12 bits (FAT12) or 16 bits (FAT16) and represents one cluster:

- `0x000`: Free cluster
- `0xFF0–0xFF6 (FAT12)` or `0xFFF0–0xFFF6 (FAT16)`: Reserved
- `0xFF7 (FAT12)` or `0xFFF7 (FAT16)`: Bad cluster
- `0xFF8–0xFFF (FAT12)` or `0xFFF8–0xFFFF (FAT16)`: End of cluster chain
- Other values: Next cluster in chain

To read a file:
1. Look up directory entry → get starting cluster
2. Follow FAT chain: entry[N] → next cluster
3. Read data from each cluster's sectors

### Directory Entry (32 bytes)

```
Offset  Size  Field
0x00    11    Short filename (8.3, space-padded)
0x0B    1     Attributes (0x01=readonly, 0x02=hidden, 0x04=system,
                         0x08=volume label, 0x10=subdirectory, 0x20=archive)
0x0C    1     Reserved
0x0D    1     Creation time (tenths of second)
0x0E    2     Creation time
0x10    2     Creation date
0x12    2     Last access date
0x14    2     High word of starting cluster (FAT32, 0 for FAT12/16)
0x16    2     Last modified time
0x18    2     Last modified date
0x1A    2     Starting cluster (low word)
0x1C    4     File size in bytes
```

### Mounting

```c
typedef struct {
    uint8_t  name[32];           /* Mount point name */
    uint32_t device;             /* Device identifier */
    vfs_node_t* root;            /* Root node of filesystem */
    uint32_t fs_type;            /* Filesystem type identifier */
} mount_point_t;

os_status_t vfs_mount(const uint8_t* device, const uint8_t* path, uint32_t fs_type);
```

### Device Files

Devices are exposed as files in the VFS:
- `/dev/vga` — write to display
- `/dev/kbd` — read keyboard input
- `/dev/hda` — read/write raw disk
- `/dev/null` — discard writes, EOF on reads
- `/dev/zero` — infinite zero bytes

## Common Pitfalls

1. **Endianness**: FAT uses little-endian for multi-byte fields. x86 is little-endian so it "just works." If porting to big-endian, byte-swap everything.

2. **FAT12 entry straddling bytes**: 12-bit entries mean 3 bytes = 2 entries. Entry N starts at `N * 3 / 2` bytes. If `N` is even: low 12 bits of the 2-byte word. If `N` is odd: high 12 bits. This is a classic bug.

3. **Long filenames (LFN)**: FAT12/16 with LFN stores long names in hidden directory entries before the short entry. Ignore LFN initially — only handle 8.3 names.

4. **Cluster 0 and 1**: These FAT entries don't correspond to data clusters. The data region starts at cluster 2. Always subtract 2 when calculating cluster→sector mapping.

5. **Root directory size**: In FAT12/16, the root directory is a fixed-size table between FAT2 and the data region. Its size is `MaxRootDirEntries * 32 bytes`. In FAT32, the root is a regular cluster chain.

6. **Sector vs cluster vs byte offsets**: Three coordinate systems. Convert carefully: `sector = (cluster - 2) * sectors_per_cluster + data_region_start`. Then byte offset within sector.

7. **Buffer overflows on read**: Always check `offset + count <= file_size`. Reading past EOF is undefined in FAT.

## Debugging Tips

- Use `hexdump` or `xxd` on the disk image to inspect the FAT structure by hand
- Print directory entries to VGA to verify they're parsed correctly
- Test with a known disk image created by `mkfs.fat` (Linux) or `format` (Windows)
- Verify cluster chain traversal: print each cluster number and its data
- Use QEMU `-drive file=disk.img,format=raw,if=ide` to attach a disk image
