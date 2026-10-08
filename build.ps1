# build.ps1 - Build TIKI LODE RUNNER with z88dk
#
# Usage:
#   .\build.ps1              Build loderun.com / loderun.dsk
#   .\build.ps1 -Clean       Clean build outputs first
#   .\build.ps1 -Verbose     Show full compiler output
#
# The level, spawn and glyph tables in src\asm are committed source. They were
# generated once; nothing regenerates them during a build.

param(
    [switch]$Clean,
    [switch]$Verbose
)

# === Configuration ===
$ProjectRoot = $PSScriptRoot
$SrcDir      = "$ProjectRoot\src\c"
$BuildDir    = "$ProjectRoot\build"
$OutputName  = "loderun"

# Ensure z88dk environment
if (-not $env:ZCCCFG) {
    $env:ZCCCFG = "C:\z88dk\lib\config"
}
if ($env:PATH -notlike "*z88dk*") {
    $env:PATH = "C:\z88dk\bin;$env:PATH"
}

# Verify zcc is available
$zcc = Get-Command zcc.exe -ErrorAction SilentlyContinue
if (-not $zcc) {
    Write-Host "ERROR: zcc not found. Is z88dk installed at C:\z88dk?" -ForegroundColor Red
    exit 1
}

# Create build dir
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Clean
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item "$BuildDir\*" -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "=== TIKI LODE RUNNER Build ===" -ForegroundColor Cyan
Write-Host ""

# -- Data tables (committed source, not build products) --
$asmDir = "$ProjectRoot\src\asm"
$missing = @("$asmDir\leveldata.asm", "$asmDir\gfxdata.asm", "$asmDir\trainlevel.asm") |
           Where-Object { -not (Test-Path $_) }
if ($missing) {
    Write-Host "  ERROR: missing data table(s):" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "         $_" -ForegroundColor Red }
    Write-Host "         These are committed source; restore them from git." -ForegroundColor Red
    exit 1
}

# -- Full game build --
Write-Host "Building $OutputName..." -ForegroundColor White

# Remove stale outputs so a compiler failure can't report an old .com as success
Remove-Item "$BuildDir\$OutputName.com","$BuildDir\$OutputName.img","$BuildDir\$OutputName.dsk" -Force -ErrorAction SilentlyContinue

# Collect C source files
$cFiles = @(
    "$SrcDir\main.c",
    "$SrcDir\video.c",
    "$SrcDir\input.c",
    "$SrcDir\room.c",
    "$SrcDir\game.c",
    "$SrcDir\sound.c"
)

# Assembly source files
$asmFiles = @(
    "$asmDir\screen.asm",
    "$asmDir\tiles.asm",
    "$asmDir\sprite.asm",
    "$asmDir\keyboard.asm",
    "$asmDir\sound.asm",
    "$asmDir\gfxdata.asm",
    "$asmDir\leveldata.asm",
    "$asmDir\trainlevel.asm"
)

# Verify all source files exist
$allFiles = $cFiles + $asmFiles
foreach ($f in $allFiles) {
    if (-not (Test-Path $f)) {
        Write-Host "  ERROR: Source file not found: $f" -ForegroundColor Red
        exit 1
    }
}

$sourceList = ($allFiles | ForEach-Object { "`"$_`"" }) -join " "

# Build command:
#   +cpm                    Target CP/M
#   -subtype=tiki100_400k   Tiki-100 400K disk format (also links -ltiki100)
#   -startup=0              Minimal CRT startup
#   -clib=default           Default C library
#   -compiler=sccz80        Use sccz80 compiler (more forgiving; use sdcc for optimized builds)
#   -pragma-define:REGISTER_SP
#                           THE important one. Do not remove it. CP/M loads
#                           the whole .COM image at $0100, and the HIMEM
#                           graphics section rides along in the tail of that
#                           image (from __BSS_END_head up to the image top)
#                           until the CRT relocates it to CRT_ORG_GRAPHICS.
#                           The subtype's default stack sits *inside* that
#                           tail, so the CRT pushes return addresses over the
#                           payload and then faithfully copies the damaged
#                           bytes up to the graphics origin. The result is a
#                           handful of corrupted instructions in tiles.asm or
#                           sprite.asm -- which ones depends purely on the
#                           image size, so the failure looks like a random
#                           "size ceiling": sometimes a black playfield,
#                           sometimes actors drawn at the wrong X. This was
#                           diagnosed by peeking the executing code in RAM and
#                           diffing it against build\loderun.com; exactly the
#                           4 bytes that load just below the old SP differed.
#                           Parking the stack at $CBFE puts it in the CCP
#                           area, which is dead while we run and is restored
#                           by the warm boot we exit through. It is clear of
#                           both the loaded image and the graphics section.
#   -pragma-define:CRT_ORG_GRAPHICS
#                           Origin of the relocated graphics section. It must
#                           sit above the loaded image top ($0100 + file size)
#   -pragma-define:CRT_ORG_GRAPHICS
#                           Origin of the relocated graphics section. It must
#                           sit above the loaded image top ($0100 + file size)
#                           and leave room below the stack for the section
#                           itself. Getting this wrong is silent and baffling,
#                           so it is no longer a magic number: the build does
#                           one pass to learn the image size and the section
#                           size, computes the lowest safe origin, and rebuilds
#                           if the provisional guess was wrong. The origin only
#                           relocates code, it does not change the image size,
#                           so the second pass is final.
#   -create-app             Generate .dsk disk image too
#   -m                      Generate map file
#   -s                      Generate symbol file
#   -I                      Include path for our headers

# Top of the stack, and the floor the graphics origin may never drop below.
# $CBFE is inside the CCP, which is dead while the game runs and is restored
# by the warm boot we exit through. BDOS starts at $CC00.
$StackTop    = 0xCBFE
$OriginFloor = 0xBC80
$MinStack    = 1280     # bytes between the section top and the stack

function Invoke-Zcc([int]$origin) {
    $org = "0x{0:X4}" -f $origin
    $sp  = "0x{0:X4}" -f $StackTop
    $c = "zcc +cpm -subtype=tiki100_400k -startup=0 -clib=default -compiler=sccz80 -pragma-define:CRT_ORG_GRAPHICS=$org -pragma-define:REGISTER_SP=$sp -Cz--container=raw -I`"$SrcDir`" -o `"$BuildDir\$OutputName`" $sourceList -create-app -m -s"
    Write-Host "  $c" -ForegroundColor DarkGray
    $out = Invoke-Expression "$c 2>&1" | Out-String
    if ($Verbose -or $LASTEXITCODE -ne 0) { Write-Host $out }
    return $out
}

# Lowest 16-byte-aligned origin that clears the loaded image, given a .com of
# $size bytes. The image occupies $0100 .. $0100+size-1.
function Get-RequiredOrigin([int]$size) {
    $top = 0x100 + $size
    $org = [math]::Ceiling($top / 16) * 16
    if ($org -lt $OriginFloor) { $org = $OriginFloor }
    return [int]$org
}

function Get-MapValue([string]$name) {
    $line = Select-String -Path "$BuildDir\$OutputName.map" -Pattern "^$name\s+=\s+\`$([0-9A-Fa-f]+)" |
            Select-Object -First 1
    if (-not $line) { return $null }
    return [Convert]::ToInt32($line.Matches[0].Groups[1].Value, 16)
}

$result = Invoke-Zcc $OriginFloor

if (Test-Path "$BuildDir\$OutputName.com") {
    $sz     = (Get-Item "$BuildDir\$OutputName.com").Length
    $wanted = Get-RequiredOrigin $sz

    if ($wanted -ne $OriginFloor) {
        Write-Host ("  Image top is 0x{0:X4}; relocating graphics to 0x{1:X4} and rebuilding." -f (0x100 + $sz), $wanted) -ForegroundColor Yellow
        $result = Invoke-Zcc $wanted
        $sz = (Get-Item "$BuildDir\$OutputName.com").Length
        # The origin must not change the image size. If it did, the layout
        # argument above is void and the result cannot be trusted.
        if ((Get-RequiredOrigin $sz) -gt $wanted) {
            Write-Host "  FAILED - image grew when relocated; layout is unstable" -ForegroundColor Red
            exit 1
        }
    }

    $head = Get-MapValue '__HIMEM_head'
    $tail = Get-MapValue '__HIMEM_END_tail'
    if ($head -ne $null -and $tail -ne $null) {
        $stack = $StackTop - $tail
        Write-Host ("  Layout: image 0x0100-0x{0:X4}, graphics 0x{1:X4}-0x{2:X4}, stack {3} bytes below 0x{4:X4}" -f (0x100 + $sz - 1), $head, $tail, $stack, $StackTop) -ForegroundColor DarkGray
        if ($head -lt (0x100 + $sz)) {
            Write-Host "  FAILED - graphics section overlaps the loaded image" -ForegroundColor Red
            exit 1
        }
        if ($stack -lt $MinStack) {
            Write-Host "  FAILED - only $stack bytes of stack left (need $MinStack). Shrink the image." -ForegroundColor Red
            exit 1
        }
    }
}

if (Test-Path "$BuildDir\$OutputName.com") {
    $sz = (Get-Item "$BuildDir\$OutputName.com").Length
    Write-Host "  OK: $OutputName.com ($sz bytes)" -ForegroundColor Green
    
    # Rename .img to .dsk (raw container produces .img)
    if (Test-Path "$BuildDir\$OutputName.img") {
        Move-Item "$BuildDir\$OutputName.img" "$BuildDir\$OutputName.dsk" -Force
        $dsz = (Get-Item "$BuildDir\$OutputName.dsk").Length
        Write-Host "  OK: $OutputName.dsk ($([math]::Round($dsz/1024))K raw disk image)" -ForegroundColor Green
    }
    
    if (Test-Path "$BuildDir\$OutputName.map") {
        Write-Host "  Map: $BuildDir\$OutputName.map" -ForegroundColor DarkGray
    }
} else {
    Write-Host "  FAILED - .com not produced" -ForegroundColor Red
    if (-not $Verbose) { Write-Host $result }
    exit 1
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
