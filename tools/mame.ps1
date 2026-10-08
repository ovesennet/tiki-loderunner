<#
.SYNOPSIS
    Run Lode Runner on the Tiki-100 under MAME.

.DESCRIPTION
    An alternative to deploy.ps1 / tools\boot.ps1, which use the Djupdal
    emulator. MAME's tiki100 driver emulates the Z80 CTC, the AY-3-8912 and
    the interrupt daisy chain properly, and it can be driven and screenshotted
    from its own Lua engine, which makes verification repeatable instead of
    depending on window focus and sleep timings.

    A scripted run uses no host keyboard or mouse input at all, so it does not
    steal the focus and you can keep working while tests execute. The machine
    is driven by tools\mame-drive.lua through MAME's natural-keyboard API, and
    the startup warning that would otherwise need a synthetic RETURN is
    suppressed with -seconds_to_run.

    Requires a tiki100 ROM set; run tools\make-mame-romset.py once to build
    one from the tiki.rom in this repository.

.PARAMETER NoBuild
    Skip the build step.

.PARAMETER NoDeploy
    Use the existing disk image instead of regenerating it.

.PARAMETER Interactive
    Leave MAME running and do not drive it. Use this to actually play. The
    startup screens are not suppressed in this mode, so dismiss the red ROM
    warning with RETURN before the machine starts.

.PARAMETER AutoStart
    Deploy the game as MENY.COM so the disk boots straight into it, with no
    menu and nothing to type.

.PARAMETER Steps
    Override the automation steps. See tools\mame-drive.lua for the grammar.

.PARAMETER Shot
    Copy the final snapshot to this path.

.PARAMETER Wav
    Record the emulated AY-3-8912 output to this WAV file. Lets sound be
    verified by measuring the recording rather than by listening.

.EXAMPLE
    .\tools\mame.ps1 -Shot build\mame.png
    Build, deploy, boot, start LODERUN and capture the room renderer.

.EXAMPLE
    .\tools\mame.ps1 -NoBuild -AutoStart -Steps "wait:1100;snap;quit"
    Boot a self-starting disk and photograph the title screen.

.EXAMPLE
    .\tools\mame.ps1 -NoBuild -Interactive
    Boot and play.
#>
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [switch]$NoDeploy,
    [switch]$Interactive,
    [switch]$AutoStart,
    [string]$Steps,
    [string]$Shot,
    [string]$Disk,
    [string]$Wav,
    [string]$MamePath
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

Write-Host "=== TIKI LODE RUNNER (MAME) ===" -ForegroundColor Cyan
Write-Host ""

# --- Locate MAME -----------------------------------------------------------
# 0.289's winget build throws during machine start on some systems; 0.287 is
# known good, so prefer it when both are installed.
if (-not $MamePath) {
    $candidates = @(
        $env:MAME_PATH,
        "C:\mame287\mame.exe",
        "C:\mame\mame.exe",
        "$env:LOCALAPPDATA\Programs\mame\mame.exe"
    ) | Where-Object { $_ }
    $MamePath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $MamePath -or -not (Test-Path $MamePath)) {
    throw "MAME not found. Install it (winget install MAMEdev.MAME --location C:\mame287 --version 0.287) or pass -MamePath."
}
Write-Host "MAME:  $MamePath"

# --- ROM set ---------------------------------------------------------------
$romPath = Join-Path $root "roms"
if (-not (Test-Path (Join-Path $romPath "tiki100\tikirom-2.03w.u10"))) {
    Write-Host "Building ROM set from tiki.rom..."
    & python (Join-Path $root "tools\make-mame-romset.py")
    if ($LASTEXITCODE -ne 0) { throw "ROM set generation failed" }
}

# --- Build and deploy ------------------------------------------------------
if (-not $NoBuild) {
    & (Join-Path $root "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not $NoDeploy) {
    & (Join-Path $root "deploy.ps1") -NoBuild -NoLaunch -AutoStart:$AutoStart
    if ($LASTEXITCODE -ne 0) { throw "Deploy failed" }
}

$disk = if ($Disk) {
    if ([System.IO.Path]::IsPathRooted($Disk)) { $Disk } else { Join-Path $root $Disk }
} elseif ($AutoStart) {
    Join-Path $root "dsk\loderun.dsk"
} else {
    Join-Path $root "dsk\work.dsk"
}
if (-not (Test-Path $disk)) { throw "Missing $disk - run without -NoDeploy" }

$snapDir = Join-Path $root "build\mame"
New-Item -ItemType Directory -Force -Path $snapDir | Out-Null
Get-ChildItem $snapDir -Recurse -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force

# --- Default automation ----------------------------------------------------
# tiki100 runs at ~50 Hz, so frame counts are roughly milliseconds x 20.
# Boot CP/M, reach the game (typing LODERUN unless the disk self-boots),
# dismiss its PRESS RETURN prompt and photograph the room renderer.
if (-not $Steps) {
    if ($AutoStart) {
        # A self-booting disk needs no typing to reach the game.
        $Steps = @(
            "wait:1100"     # CP/M boots and runs the game in place of MENY.COM
            "snap"          # title screen
            "post:\n"       # RETURN starts the room viewer
            "wait:200"
            "snap"          # room 1
        ) -join ";"
    } else {
        $Steps = @(
            "wait:900"      # CP/M boots and the TIKO menu appears
            "post:a\n"      # "Avslutt meny, kjor vanlig TIKO"
            "wait:200"
            "post:loderun\n"
            "wait:600"      # 34 KB loads from floppy
            "snap"          # title screen
            "post:\n"       # RETURN starts the room viewer
            "wait:200"
            "snap"          # room 1
        ) -join ";"
    }
}

# --- Launch ----------------------------------------------------------------
# No host keyboard or mouse input is used anywhere in this path, so a run does
# not steal the focus and the machine stays usable while tests execute.
#
# The reconstructed decode PROM does not match MAME's recorded checksum, so
# MAME would normally open with a red "ROMs are incorrect" panel and pause
# until it is acknowledged -- and `-autoboot_script` does not even start until
# that panel is gone, so it cannot be dismissed from Lua. `-seconds_to_run`
# suppresses the startup screens outright, which removes the need for the
# synthetic RETURN that used to be delivered with SetForegroundWindow +
# keybd_event. It also bounds a run that wedges. Everything after that is
# driven from tools\mame-drive.lua, which talks to the emulated keyboard
# through MAME's own natural-keyboard API rather than through Windows.
$mameArgs = @(
    "tiki100"
    "-rompath", $romPath
    "-flop1", $disk
    "-slot1", '""'          # the 8088 and Winchester cards carry their own
    "-slot2", '""'          # ROMs, which we neither have nor need
    "-skip_gameinfo"
    "-window"
    "-nomaximize"
    "-snapshot_directory", $snapDir
)
if ($Wav) {
    $wavPath = if ([System.IO.Path]::IsPathRooted($Wav)) { $Wav } else { Join-Path $root $Wav }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $wavPath) | Out-Null
    Remove-Item $wavPath -ErrorAction SilentlyContinue
    $mameArgs += @("-wavwrite", $wavPath)
}
if (-not $Interactive) {
    # -seconds_to_run counts emulated seconds. Size it from the step list's
    # own waits (the machine runs at ~50 Hz) so a long scripted session is not
    # cut short, and leave room for the boot that precedes the first wait.
    $frames = ([regex]::Matches($Steps, "wait:(\d+)") |
               ForEach-Object { [int]$_.Groups[1].Value } |
               Measure-Object -Sum).Sum
    $seconds = [Math]::Max(60, [int]($frames / 50) + 30)

    $mameArgs += @("-seconds_to_run", $seconds)
    $mameArgs += @("-autoboot_script", (Join-Path $root "tools\mame-drive.lua"))
    $env:TIKI_MAME_STEPS = $Steps
    $env:TIKI_MAME_LOG = Join-Path $root "build\mame-drive.log"
    Remove-Item $env:TIKI_MAME_LOG -ErrorAction SilentlyContinue
}

Write-Host "Disk:  $disk"
Write-Host "Launching..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $MamePath -ArgumentList $mameArgs `
                      -WorkingDirectory (Split-Path -Parent $MamePath) -PassThru

if ($Interactive) {
    Write-Host ""
    Write-Host "MAME is running. Keys: A/D/I/J move, SPACE dig, P pause," -ForegroundColor Green
    Write-Host "',' and '.' change room, ESC (BRYT) quits to CP/M." -ForegroundColor Green
    exit 0
}

# --- Wait for the automation to finish -------------------------------------
# MAME files snapshots under <snapshot_directory>\<system>\, so search deeply.
$expected = ([regex]::Matches($Steps, "(^|;)snap")).Count
# A step list may do its work entirely through peeks, with no snapshot at all,
# so also wait for the machine to exit whenever the steps end with a quit.
$waitsForExit = $Steps -match "(^|;)quit"
# A long scripted session can legitimately take minutes of wall clock, so size
# the deadline from the emulated time the steps ask for rather than fixing it.
# Emulation is not guaranteed to run at or above realtime, so allow generous
# headroom on top.
$budget = if ($Interactive) { 180 } else { [Math]::Max(180, ($seconds * 3) + 60) }
$deadline = (Get-Date).AddSeconds($budget)
do {
    Start-Sleep -Seconds 2
    $shots = @(Get-ChildItem $snapDir -Recurse -Filter *.png -ErrorAction SilentlyContinue |
               Sort-Object Name)
    $pending = ($shots.Count -lt $expected) -or ($waitsForExit -and -not $proc.HasExited)
} while ($pending -and (Get-Date) -lt $deadline -and -not $proc.HasExited)

Write-Host ""
if ($shots.Count -lt $expected) {
    Write-Host "Captured $($shots.Count)/$expected snapshots (timed out)" -ForegroundColor Yellow
} else {
    Write-Host "Captured $($shots.Count) snapshot(s) in $snapDir" -ForegroundColor Green
}
$shots | ForEach-Object { Write-Host "  $($_.Name)" }

if ($Shot -and $shots.Count -gt 0) {
    Copy-Item ($shots | Select-Object -Last 1).FullName $Shot -Force
    Write-Host "Final snapshot -> $Shot" -ForegroundColor Green
}

if (-not $proc.HasExited) {
    Write-Host ""
    Write-Host "MAME is still running (pid $($proc.Id)). Stop-Process -Id $($proc.Id) to close it."
}
