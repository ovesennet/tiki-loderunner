<#
.SYNOPSIS
    Build and run the sound test, then measure MAME's AY output.

.DESCRIPTION
    Links sound.c and sound.asm into a small program that plays every
    effect separated by silence, runs it under MAME with the emulated
    AY-3-8912 recorded to a WAV, and prints the length and frequency of
    each tone it finds.

    This is the only part of the port that memory peeks cannot check: it
    verifies that the PSG is reachable, that the beeper-to-AY pitch
    conversion is right for a 2 MHz AY, and that the cycle-counted
    millisecond delay is accurate on a 4 MHz Z80.

    Uses no host keyboard or mouse input.

.EXAMPLE
    .\tests\run-sound.ps1
#>
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [string]$MamePath
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\tests"
$name = "sndtest"
$wav = Join-Path $root "build\tests\$name.wav"

# --- Build -----------------------------------------------------------------
if (-not $NoBuild) {
    if (-not $env:ZCCCFG) { $env:ZCCCFG = "C:\z88dk\lib\config" }
    if ($env:PATH -notlike "*z88dk*") { $env:PATH = "C:\z88dk\bin;$env:PATH" }
    if (-not (Get-Command zcc.exe -ErrorAction SilentlyContinue)) {
        throw "zcc not found. Is z88dk installed at C:\z88dk?"
    }

    New-Item -ItemType Directory -Force -Path $build | Out-Null
    Remove-Item "$build\$name.com", "$build\$name.dsk" -Force -ErrorAction SilentlyContinue

    $sources = @(
        "$root\src\c\sound.c"
        "$root\tests\test_sound.c"
        "$root\src\asm\sound.asm"
    ) | ForEach-Object { "`"$_`"" }

    Write-Host "Building $name.com..." -ForegroundColor Cyan
    $cmd = "zcc +cpm -subtype=tiki100_400k -startup=0 -clib=default " +
           "-compiler=sccz80 -pragma-define:CRT_ORG_GRAPHICS=0xC000 " +
           "-Cz--container=raw -I`"$root\src\c`" " +
           "-o `"$build\$name`" $($sources -join ' ') -m -s"
    $out = Invoke-Expression "$cmd 2>&1" | Out-String
    if (Test-Path "$build\$name") { Move-Item "$build\$name" "$build\$name.com" -Force }
    if (-not (Test-Path "$build\$name.com")) {
        Write-Host $out
        throw "sound test build failed"
    }
    Write-Host "  OK: $name.com ($((Get-Item "$build\$name.com").Length) bytes)" -ForegroundColor Green
}

if (-not (Test-Path "$build\$name.com")) { throw "missing $build\$name.com" }

# --- Put it on a bootable CP/M disk ---------------------------------------
$disk = Join-Path $build "$name.dsk"
& (Join-Path $root "deploy.ps1") -NoBuild -NoLaunch `
    -ComFile "$build\$name.com" -ComName $name.ToUpper() -OutDisk $disk | Out-Null
if (-not (Test-Path $disk)) { throw "failed to build $disk" }

# --- Run and record --------------------------------------------------------
# The script is about 7 s of sound after a 1 s lead-in; allow for the boot
# and the load as well.
$steps = @(
    "wait:900"
    "post:a\n"
    "wait:200"
    "post:$name\n"
    "wait:900"
    "quit"
) -join ";"

$runArgs = @{
    NoBuild = $true; NoDeploy = $true
    Disk = "build\tests\$name.dsk"; Steps = $steps; Wav = $wav
}
if ($MamePath) { $runArgs.MamePath = $MamePath }
& (Join-Path $root "tools\mame.ps1") @runArgs | Out-Null

if (-not (Test-Path $wav)) { throw "MAME produced no recording at $wav" }

# --- Measure ---------------------------------------------------------------
Write-Host ""
& python (Join-Path $root "tools\check-wav.py") $wav
Write-Host ""
Write-Host "Expected, in order:" -ForegroundColor Cyan
Write-Host "  tally     425 Hz   238 ms"
Write-Host "  victory   200 Hz   150 ms"
Write-Host "            259 Hz   143 ms"
Write-Host "            353 Hz   144 ms"
Write-Host "            399 Hz   140 ms"
Write-Host "            (100 ms rest)"
Write-Host "            353 Hz   113 ms"
Write-Host "            399 Hz   278 ms"
Write-Host "  dig       271/206/166/139 Hz, 11-22 ms each, run together"
Write-Host "  gold      425 Hz    19 ms"
Write-Host "  caught    falling sweep, ~2.9 s total"
