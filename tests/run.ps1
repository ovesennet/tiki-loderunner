<#
.SYNOPSIS
    Build and run the game-logic tests on the emulated Tiki.

.DESCRIPTION
    Links game.c and room.c against stubbed video/input and runs the result
    under MAME with no host input, then reads the pass/fail bytes straight
    out of Z80 memory.

    Building with the same sccz80 toolchain as the game is deliberate: the
    tests then cover the compiler's code generation too, which has already
    produced one silent logic inversion in this project.

.EXAMPLE
    .\tests\run.ps1
#>
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [string]$MamePath
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\tests"
$name = "gametest"

# --- Build -----------------------------------------------------------------
if (-not $NoBuild) {
    if (-not $env:ZCCCFG) { $env:ZCCCFG = "C:\z88dk\lib\config" }
    if ($env:PATH -notlike "*z88dk*") { $env:PATH = "C:\z88dk\bin;$env:PATH" }
    if (-not (Get-Command zcc.exe -ErrorAction SilentlyContinue)) {
        throw "zcc not found. Is z88dk installed at C:\z88dk?"
    }

    New-Item -ItemType Directory -Force -Path $build | Out-Null
    Remove-Item "$build\$name.com", "$build\$name.img", "$build\$name.dsk" `
        -Force -ErrorAction SilentlyContinue

    $sources = @(
        "$root\src\c\game.c"
        "$root\src\c\room.c"
        "$root\tests\stubs.c"
        "$root\tests\test_game.c"
        "$root\src\asm\leveldata.asm"
        "$root\src\asm\trainlevel.asm"
    ) | ForEach-Object { "`"$_`"" }

    Write-Host "Building $name.com..." -ForegroundColor Cyan

    # The test build needs the same memory layout care as the game (see the
    # long note in build.ps1). The subtype's default stack sits just above
    # BSS, and as this suite grew the gap shrank to a few hundred bytes --
    # deep call nesting then wrote over test_results/test_count/test_fail,
    # which reads as a suite that stops early with impossible counters.
    # Park the stack in the CCP and place the graphics section clear of the
    # loaded image, exactly as the game build does.
    $StackTop    = 0xCBFE
    $OriginFloor = 0xBC80
    $MinStack    = 1280

    function Invoke-TestZcc([int]$origin) {
        $org = "0x{0:X4}" -f $origin
        $sp  = "0x{0:X4}" -f $StackTop
        # No -create-app: z88dk's disk image is not a bootable TIKO system
        # disk, so the .com is placed on one by deploy.ps1 below.
        $c = "zcc +cpm -subtype=tiki100_400k -startup=0 -clib=default " +
             "-compiler=sccz80 -pragma-define:CRT_ORG_GRAPHICS=$org " +
             "-pragma-define:REGISTER_SP=$sp " +
             "-Cz--container=raw -I`"$root\src\c`" " +
             "-o `"$build\$name`" $($sources -join ' ') -m -s"
        $o = Invoke-Expression "$c 2>&1" | Out-String
        # Without -create-app the linker leaves the binary extensionless.
        if (Test-Path "$build\$name") {
            Move-Item "$build\$name" "$build\$name.com" -Force
        }
        return $o
    }

    $out = Invoke-TestZcc $OriginFloor
    if (-not (Test-Path "$build\$name.com")) {
        Write-Host $out
        throw "test build failed"
    }

    $sz  = (Get-Item "$build\$name.com").Length
    $org = [math]::Ceiling((0x100 + $sz) / 16) * 16
    if ($org -lt $OriginFloor) { $org = $OriginFloor }
    if ($org -ne $OriginFloor) {
        $out = Invoke-TestZcc ([int]$org)
        if (-not (Test-Path "$build\$name.com")) {
            Write-Host $out
            throw "test build failed"
        }
        $sz = (Get-Item "$build\$name.com").Length
    }
    Write-Host "  OK: $name.com ($sz bytes)" -ForegroundColor Green

    $tail = Select-String -Path "$build\$name.map" -Pattern "^__HIMEM_END_tail\s+=\s+\`$([0-9A-Fa-f]+)" |
            Select-Object -First 1
    if ($tail) {
        $end   = [Convert]::ToInt32($tail.Matches[0].Groups[1].Value, 16)
        $stack = $StackTop - $end
        if ($end -lt (0x100 + $sz)) {
            throw ("graphics section ends at 0x{0:X4}, inside the loaded image" -f $end)
        }
        if ($stack -lt $MinStack) {
            throw ("only $stack bytes of stack left (need $MinStack); shrink the test image")
        }
        Write-Host ("  Layout: image 0x0100-0x{0:X4}, graphics ends 0x{1:X4}, stack {2} bytes" -f (0x100 + $sz - 1), $end, $stack) -ForegroundColor DarkGray
    }
}

if (-not (Test-Path "$build\$name.com")) { throw "missing $build\$name.com" }

# --- Put it on a bootable CP/M disk ---------------------------------------
# z88dk's own .dsk is not a bootable TIKO system disk, so go through the
# project's disk writer, which starts from workbase.dsk.
$disk = Join-Path $build "$name.dsk"
& (Join-Path $root "deploy.ps1") -NoBuild -NoLaunch `
    -ComFile "$build\$name.com" -ComName $name.ToUpper() -OutDisk $disk | Out-Null
if (-not (Test-Path $disk)) { throw "failed to build $disk" }

# --- Locate the result buffer ----------------------------------------------
function Get-Symbol([string]$symbol) {
    $line = Select-String -Path "$build\$name.map" -Pattern "^\s*$symbol\s+=\s+\`$([0-9A-Fa-f]{4})"
    if (-not $line) { throw "symbol $symbol not found in $name.map" }
    return $line.Matches[0].Groups[1].Value
}

$aResults = Get-Symbol "_test_results"
$aCount   = Get-Symbol "_test_count"
$aFail    = Get-Symbol "_test_fail"
$aDone    = Get-Symbol "_test_done"

# --- Check names, in the order the checks run ------------------------------
# Paired with the result bytes positionally, which holds because every check
# sits in straight-line code and the tests run in source order.
$names = @(
    "room 1 spawns three guards"
    "an overlapping guard kills the player"
    "a guard a cell away does not"
    "a guard four pixels away does"
    "the player digs through the floor"
    "the brick is gone from the grid"
    "a guard walks into the hole and is trapped"
    "trapping scores SCORE_TRAP"
    "the guard leaves the hole again"
    "hole dug (crush)"
    "guard trapped (crush)"
    "crushing a trapped guard scores SCORE_KILL"
    "the crushed guard respawns near the top"
    "map gold matches boxes_left at the start"
    "a guard takes a piece of gold off the map"
    "boxes_left ignores guard pickups and drops"
    "every piece of gold is still accounted for"
    "hole dug (gold)"
    "guard trapped (gold)"
    "the dying guard was given gold"
    "the gold leaves the dying guard"
    "boxes_left is unchanged by the kill"
    "the gold is back on the map"
    "no occupancy marker leaks into the terrain"
    "the room starts out running"
    "top row with no gold completes the room"
    "the bonus is LEVEL_BONUS_STEPS x SCORE_BONUS"
    "collecting gold scores SCORE_BOX"
    "collecting gold counts boxes_left down"
    "the busiest room spawns five guards"
    "no two guards ever share a position"
    "no guard leaves the room bounds"
    "occupancy stays balanced with five guards"
    "a guard on the player kills without the cheat"
    "the cheat survives a guard standing on the player"
    "a closing brick kills without the cheat"
    "the cheat survives a closing brick"
    "the cheat lifts the player out of the brick"
    "the training room holds six pieces of gold"
    "the training room has one guard"
    "the training player spawns on open ground"
    "the training room starts out running"
    "the training exit ladder is hidden at first"
    "the training exit ladder reaches the top row"
    "the training room has a solid bedrock floor"
    "climbing works dead on the ladder column"
    "climbing snaps on from 2 px short"
    "climbing snaps on from 2 px past"
    "a half-cell tie snaps toward the ladder when walking into it"
    "a half-cell tie snaps back when walking away from it"
    "a half-cell tie is not taken when walking past the ladder"
    "a whole cell away is still a miss"
    "stepping down snaps on from 2 px short"
    "stepping down snaps back from half a cell past"
    "no climb request embeds the player in a wall"
    "no step-down request embeds the player in a wall"
    "a down request will not snap into a guard's marker"
    "digging works dead on the column"
    "digging snaps on from 2 px past"
    "digging snaps on from 2 px short"
    "a half-cell tie digs the way the player faces"
    "a half-cell tie digs back when facing the other way"
    "no dig request embeds the player in a wall"
    "stepping off a ladder works dead on the rung"
    "stepping off snaps on from 2 px past"
    "stepping off snaps on from 2 px short"
    "a half-cell tie steps off the way the player climbs"
    "a half-cell tie will not step off into a wall"
)

# --- Run -------------------------------------------------------------------
# Boot CP/M, leave the TIKO menu, start the tests and let them finish.
$steps = @(
    "wait:900"
    "post:a\n"
    "wait:200"
    "post:$name\n"
    "wait:9000"     # boot, load the image, then run every check
    "peek:${aDone}:1"
    "peek:${aCount}:1"
    "peek:${aFail}:1"
    "peek:${aResults}:$($names.Count)"
    "quit"
) -join ";"

$args = @{ NoBuild = $true; NoDeploy = $true; Disk = "build\tests\$name.dsk"; Steps = $steps }
if ($MamePath) { $args.MamePath = $MamePath }
& (Join-Path $root "tools\mame.ps1") @args | Out-Null

$log = Get-Content (Join-Path $root "build\mame-drive.log")
function Get-PeekBytes([string]$addr) {
    $m = @($log | Select-String -Pattern "peek $addr = (.+)$")
    if (-not $m) { throw "no peek result for $addr" }
    # The leading comma stops PowerShell unrolling a one-element array into a
    # bare string, which would make indexing return its first character.
    return , @($m[-1].Matches[0].Groups[1].Value.Trim() -split "\s+")
}

function Get-PeekByte([string]$addr) {
    return [Convert]::ToInt32((Get-PeekBytes $addr)[0], 16)
}

$done = Get-PeekByte $aDone
$count = Get-PeekByte $aCount
$fail = Get-PeekByte $aFail
$bytes = Get-PeekBytes $aResults

Write-Host ""
if (-not $done) {
    Write-Host "Tests did not finish (ran $count of $($names.Count) checks)." -ForegroundColor Red
}

for ($i = 0; $i -lt $names.Count; $i++) {
    if ($i -ge $count) {
        Write-Host ("  ....  " + $names[$i]) -ForegroundColor DarkGray
        continue
    }
    if ([Convert]::ToInt32($bytes[$i], 16) -eq 1) {
        Write-Host ("  ok    " + $names[$i]) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL  " + $names[$i]) -ForegroundColor Red
    }
}

Write-Host ""
if ($done -and $fail -eq 0 -and $count -eq $names.Count) {
    Write-Host "all $count checks passed" -ForegroundColor Green
    exit 0
}
Write-Host "$fail failed of $count run" -ForegroundColor Red
exit 1
