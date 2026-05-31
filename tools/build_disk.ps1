# ─────────────────────────────────────────────────────────────
# tools/build_disk.ps1 — create NoxFS v2 image with all files.
# ─────────────────────────────────────────────────────────────

param(
    [string]$Image   = 'build/disk.img',
    [int]   $Inodes  = 32,
    [int]   $SizeMB  = 1
)

$ErrorActionPreference = 'Stop'
$BLKSZ    = 512
$INO_SZ   = 64
$DT_SZ    = 32

$blocks      = ($SizeMB * 1048576) / $BLKSZ
$ino_blks    = [int][Math]::Ceiling(($Inodes * $INO_SZ) / $BLKSZ)
$sb_blk      = 0
$blk_bmp_blk = 1
$ino_bmp_blk = 2
$ino_tbl_blk = 3
$data_start  = $ino_tbl_blk + $ino_blks

$files = @(
    @{Name='motd';       Path='rootfs/motd'},
    @{Name='version';    Path='rootfs/version'},
    @{Name='readme';     Path='rootfs/readme'},
    @{Name='hello.elf';  Path='build/hello.elf'},
    @{Name='echo.elf';   Path='build/echo.elf'},
    @{Name='prompt.elf'; Path='build/prompt.elf'},
    @{Name='fread.elf';  Path='build/fread.elf'},
    @{Name='fork.elf';   Path='build/fork.elf'},
    @{Name='write.elf';  Path='build/write.elf'},
    @{Name='pipe.elf';   Path='build/pipe.elf'},
    @{Name='signal.elf'; Path='build/signal.elf'},
    @{Name='ttytest.elf';Path='build/ttytest.elf'},
    @{Name='pftest.elf'; Path='build/pftest.elf'},
    @{Name='segv.elf';   Path='build/segv.elf'},
    @{Name='init.elf';   Path='build/init.elf'},
    @{Name='brktest.elf';Path='build/brktest.elf'},
    @{Name='fputest.elf';Path='build/fputest.elf'},
    @{Name='systest.elf';Path='build/systest.elf'},
    @{Name='ctest.elf';  Path='build/ctest.elf'},
    @{Name='nsh.elf';    Path='build/nsh.elf'}
)

function W32($a, $o, $v) { [Array]::Copy([BitConverter]::GetBytes([uint32]$v),0,$a,$o,4) }
function W16($a, $o, $v) { [Array]::Copy([BitConverter]::GetBytes([uint16]$v),0,$a,$o,2) }

$img = New-Object byte[] ($blocks * $BLKSZ)
$used_blks = $data_start
$dirents   = New-Object System.Collections.ArrayList

# ── superblock ─────────────────────────────────────────────
$sb_off = $sb_blk * $BLKSZ
W32 $img ($sb_off+0)  0x4E584632
W32 $img ($sb_off+4)  $blocks
W32 $img ($sb_off+8)  $Inodes
W32 $img ($sb_off+12) ($blocks - $data_start)
W32 $img ($sb_off+16) ($Inodes - 1)
W32 $img ($sb_off+20) $blk_bmp_blk
W32 $img ($sb_off+24) $ino_bmp_blk
W32 $img ($sb_off+28) $ino_tbl_blk
W32 $img ($sb_off+32) $data_start
W32 $img ($sb_off+36) 0

# ── block bitmap: mark metadata blocks used ───────────────
$bmp_off = $blk_bmp_blk * $BLKSZ
for ($i = 0; $i -lt $data_start; $i++) {
    $byteIdx = $bmp_off + [int]($i / 8)
    $bitIdx  = $i % 8
    $img[$byteIdx] = $img[$byteIdx] -bor (1 -shl $bitIdx)
}

# ── inode bitmap: mark inode 0 used ───────────────────────
$ibmp_off = $ino_bmp_blk * $BLKSZ
$img[$ibmp_off] = 1

# ── block allocator ───────────────────────────────────────
function AllocBlock {
    $bo = $blk_bmp_blk * $BLKSZ
    for ($b = $data_start; $b -lt $blocks; $b++) {
        $byteIdx = $bo + [int]($b / 8)
        $bitIdx  = $b % 8
        if (($img[$byteIdx] -band (1 -shl $bitIdx)) -eq 0) {
            $img[$byteIdx] = $img[$byteIdx] -bor (1 -shl $bitIdx)
            $script:used_blks++
            return $b
        }
    }
    Write-Error "Out of blocks"
    exit 1
}

# ── root dir inode (inode 0) ──────────────────────────────
$root_data_blk = AllocBlock
$r_ino_off = $ino_tbl_blk * $BLKSZ
W16 $img ($r_ino_off + 0)  0x4000    # mode = dir
W32 $img ($r_ino_off + 10) $root_data_blk
W16 $img ($r_ino_off + 54) 1         # links

# ── inject files ──────────────────────────────────────────
$ino_id = 1
foreach ($f in $files) {
    if (-not (Test-Path $f.Path)) { Write-Warning "skip $($f.Path)"; continue }
    if ($ino_id -ge $Inodes) { Write-Error "Too many files"; exit 1 }

    $data = [IO.File]::ReadAllBytes($f.Path)
    $sz   = $data.Length
    $need = [Math]::Max(1, [int][Math]::Ceiling($sz / $BLKSZ))

    $fb = @()
    for ($j = 0; $j -lt $need; $j++) { $fb += AllocBlock }

    for ($j = 0; $j -lt $need; $j++) {
        $dest = $fb[$j] * $BLKSZ
        $src  = $j * $BLKSZ
        $len  = [Math]::Min($BLKSZ, $sz - $src)
        [Array]::Copy($data, $src, $img, $dest, $len)
    }

    # write file inode
    $f_ino_off = ($ino_tbl_blk * $BLKSZ) + ($ino_id * $INO_SZ)
    W16 $img ($f_ino_off + 0)  0x8000       # mode = file
    W32 $img ($f_ino_off + 6)  $sz          # size
    for ($j = 0; $j -lt [Math]::Min($need, 10); $j++) {
        W32 $img ($f_ino_off + 10 + ($j * 4)) $fb[$j]
    }
    if ($need -gt 10) {
        $ind_blk = AllocBlock
        W32 $img ($f_ino_off + 50) $ind_blk
        $ind_off = $ind_blk * $BLKSZ
        for ($j = 10; $j -lt $need; $j++) {
            W32 $img ($ind_off + (($j - 10) * 4)) $fb[$j]
        }
    }
    W16 $img ($f_ino_off + 54) 1            # links

    # mark inode in bitmap
    $ibyte = $ibmp_off + [int]($ino_id / 8)
    $ibit  = $ino_id % 8
    $img[$ibyte] = $img[$ibyte] -bor (1 -shl $ibit)

    # build dirent
    $nb = [Text.Encoding]::ASCII.GetBytes($f.Name)
    $de = New-Object byte[] $DT_SZ
    W32 $de 0  $ino_id
    W16 $de 4  $DT_SZ
    $de[6] = [Math]::Min($nb.Length, 23)
    $de[7] = 1
    [Array]::Copy($nb, 0, $de, 8, [Math]::Min($nb.Length, 24))
    [void]$dirents.Add($de)

    Write-Host ("  + {0,-14} {1,7} bytes  inode {2}" -f $f.Name, $sz, $ino_id)
    $ino_id++
}

# ── write root dir entries (may span several blocks) ──────
# 32-byte dirents pack 16 to a 512-byte block with no straddling. Each
# dirent must be written into the block its byte offset maps to AND that
# block must be the one recorded in the inode's block pointers — writing
# them contiguously from block 0 while pointing blocks[1] elsewhere would
# silently drop the 17th+ entry.
$dir_sz    = $dirents.Count * $DT_SZ
$root_need = [Math]::Max(1, [int][Math]::Ceiling($dir_sz / $BLKSZ))

$root_blocks = @($root_data_blk)          # block 0 (already allocated)
for ($i = 1; $i -lt $root_need; $i++) {
    $eb = AllocBlock
    W32 $img ($r_ino_off + 10 + ($i * 4)) $eb
    $root_blocks += $eb
}

for ($i = 0; $i -lt $dirents.Count; $i++) {
    $boff   = $i * $DT_SZ
    $bidx   = [int]($boff / $BLKSZ)
    $within = $boff % $BLKSZ
    $dest   = ($root_blocks[$bidx] * $BLKSZ) + $within
    [Array]::Copy($dirents[$i], 0, $img, $dest, $DT_SZ)
}
W32 $img ($r_ino_off + 6) $dir_sz   # root inode size

# ── update superblock counters ────────────────────────────
W32 $img ($sb_off + 12) ($blocks - $used_blks)
W32 $img ($sb_off + 16) ($Inodes - $ino_id)

# ── write ─────────────────────────────────────────────────
[IO.File]::WriteAllBytes($Image, $img)
Write-Host ("  -> $Image  ({0} inodes used, {1} blocks free)" -f ($ino_id - 1), ($blocks - $used_blks))
