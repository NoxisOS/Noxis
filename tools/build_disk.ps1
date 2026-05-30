# ─────────────────────────────────────────────────────────────
# tools/build_disk.ps1 — pack rootfs/ + build/hello.elf as NoxFS
#                        into build/disk.img (1 MB raw image).
#
# NoxFS sector 0:
#   0..3    magic 'NXFS'
#   4..7    num_files (uint32 LE)
#   8..     entries (32 B):
#             name[24]   ASCII, null-padded
#             lba(4)     uint32 LE
#             size(4)    uint32 LE
#
# Files stored contiguously starting at LBA 1, padded to 512 B.
# ─────────────────────────────────────────────────────────────

$ErrorActionPreference = 'Stop'

$SECTOR    = 512
$IMG_BYTES = 1048576
$OUT       = 'build/disk.img'

# Files to ship — order matters for LBA assignment.
$files = @(
    @{ Name = 'motd';       Path = 'rootfs/motd'       },
    @{ Name = 'version';    Path = 'rootfs/version'    },
    @{ Name = 'readme';     Path = 'rootfs/readme'     },
    @{ Name = 'hello.elf';  Path = 'build/hello.elf'   },
    @{ Name = 'echo.elf';   Path = 'build/echo.elf'    },
    @{ Name = 'prompt.elf'; Path = 'build/prompt.elf'  },
    @{ Name = 'fread.elf';  Path = 'build/fread.elf'   },
    @{ Name = 'fork.elf';   Path = 'build/fork.elf'    },
    @{ Name = 'write.elf';  Path = 'build/write.elf'   },
    @{ Name = 'pipe.elf';   Path = 'build/pipe.elf'    }
)

# ── build sector 0 ──────────────────────────────────────────
$s0 = New-Object byte[] $SECTOR

# magic
$magic = [Text.Encoding]::ASCII.GetBytes('NXFS')
[Array]::Copy($magic, 0, $s0, 0, 4)

# num_files
$count = $files.Count
[Array]::Copy([BitConverter]::GetBytes([uint32]$count), 0, $s0, 4, 4)

$entryOff = 8
$nextLba  = 1
$dataStream = New-Object IO.MemoryStream

foreach ($f in $files) {
    if (-not (Test-Path $f.Path)) {
        Write-Error "Missing file: $($f.Path)"
        exit 1
    }
    $bytes = [IO.File]::ReadAllBytes($f.Path)
    $size  = $bytes.Length
    $sectors = [int][Math]::Ceiling([double]$size / [double]$SECTOR)
    if ($sectors -lt 1) { $sectors = 1 }

    # name[24]
    $nameAscii = [Text.Encoding]::ASCII.GetBytes($f.Name)
    $nameLen = [Math]::Min($nameAscii.Length, 23)
    for ($i = 0; $i -lt $nameLen; $i++) {
        $s0[$entryOff + $i] = $nameAscii[$i]
    }
    # lba
    [Array]::Copy([BitConverter]::GetBytes([uint32]$nextLba), 0, $s0, $entryOff + 24, 4)
    # size
    [Array]::Copy([BitConverter]::GetBytes([uint32]$size),    0, $s0, $entryOff + 28, 4)

    $entryOff += 32
    $nextLba  += $sectors

    # append data, padded to sector
    $dataStream.Write($bytes, 0, $size)
    $padding = ($sectors * $SECTOR) - $size
    if ($padding -gt 0) {
        $pad = New-Object byte[] $padding
        $dataStream.Write($pad, 0, $padding)
    }

    Write-Host ("  + {0,-12} {1,7} bytes -> LBA {2}" -f $f.Name, $size, ($nextLba - $sectors))
}

# ── write image ─────────────────────────────────────────────
$fs = New-Object IO.FileStream($OUT, 'Create')
$fs.SetLength($IMG_BYTES)
$fs.Position = 0
$fs.Write($s0, 0, $SECTOR)
$dataStream.Position = 0
$dataStream.WriteTo($fs)
$fs.Dispose()

Write-Host ("  -> $OUT  ($count files, {0} sectors used)" -f ($nextLba - 1))
