# extract_dsk.ps1 — Extract files from a TIKI-100 CP/M disk image
#
# Usage:  .\extract_dsk.ps1 <path-to-dsk-file>
#         .\extract_dsk.ps1 .\dsk\work-bak.dsk
#         .\extract_dsk.ps1 .\dsk\tp310a.dsk

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$DskFile
)

# === TIKI-100 400K DISK PARAMETERS ===
$SECTOR_SIZE       = 512
$SECTORS_PER_TRACK = 10
$SIDES             = 2
$BLOCK_SIZE        = 2048
$DIR_ENTRIES       = 128
$RESERVED_TRACKS   = 1
$DIR_ENTRY_SIZE    = 32
$CPM_RECORD_SIZE   = 128
$CPM_EMPTY         = 0xE5

$DATA_OFFSET = $RESERVED_TRACKS * $SIDES * $SECTORS_PER_TRACK * $SECTOR_SIZE  # 10240 = 0x2800

# =====================================================================
# MAIN
# =====================================================================

if (-not (Test-Path $DskFile)) {
    Write-Host "ERROR: File not found: $DskFile" -ForegroundColor Red
    exit 1
}

$DskFile = (Resolve-Path $DskFile).Path
$dskName = [System.IO.Path]::GetFileNameWithoutExtension($DskFile)
$outDir  = Join-Path ([System.IO.Path]::GetDirectoryName($DskFile)) $dskName

Write-Host ""
Write-Host "=== Extract CP/M Disk Image ===" -ForegroundColor Cyan
Write-Host "  Source: $DskFile" -ForegroundColor White
Write-Host "  Output: $outDir" -ForegroundColor White
Write-Host ""

$disk = [System.IO.File]::ReadAllBytes($DskFile)
Write-Host "  Disk size: $($disk.Length) bytes" -ForegroundColor DarkGray

# Create output folder
if (-not (Test-Path $outDir)) {
    [System.IO.Directory]::CreateDirectory($outDir) | Out-Null
    Write-Host "  Created folder: $outDir" -ForegroundColor DarkGray
}

# --- Read directory entries ---
$entries = @()
for ($s = 0; $s -lt $DIR_ENTRIES; $s++) {
    $off = $DATA_OFFSET + ($s * $DIR_ENTRY_SIZE)
    if ($off + $DIR_ENTRY_SIZE -gt $disk.Length) { break }
    $user = $disk[$off]
    if ($user -eq $CPM_EMPTY) { continue }
    if ($user -gt 0x0F) { continue }

    # Read filename
    $validName = $true
    $fn = ""
    for ($c = 1; $c -le 8; $c++) {
        $b = $disk[$off + $c] -band 0x7F
        if ($b -lt 0x20 -or $b -gt 0x7E) { $validName = $false; break }
        $fn += [char]$b
    }
    if (-not $validName) { continue }

    # Read extension
    $ex = ""
    for ($c = 9; $c -le 11; $c++) {
        $b = $disk[$off + $c] -band 0x7F
        if ($b -lt 0x20 -or $b -gt 0x7E) { $validName = $false; break }
        $ex += [char]$b
    }
    if (-not $validName) { continue }

    $fnTrim = $fn.Trim()
    $exTrim = $ex.Trim()
    if ($fnTrim.Length -eq 0) { continue }

    if ($exTrim.Length -gt 0) {
        $fullName = "$fnTrim.$exTrim"
    } else {
        $fullName = $fnTrim
    }

    $extent = $disk[$off + 12]
    $s2     = $disk[$off + 14]
    $rc     = $disk[$off + 15]

    # Read block list (non-zero blocks only)
    $blks = @()
    for ($c = 16; $c -le 31; $c++) {
        if ($disk[$off + $c] -ne 0) { $blks += $disk[$off + $c] }
    }
    if ($blks.Count -eq 0) { continue }

    $entries += @{
        User     = $user
        FullName = $fullName
        Extent   = $extent
        S2       = $s2
        RC       = $rc
        Blocks   = $blks
    }
}

# Group extents by user + filename
$fileGroups = [ordered]@{}
foreach ($e in $entries) {
    $key = "U$($e.User)_$($e.FullName)"
    if (-not $fileGroups.Contains($key)) {
        $fileGroups[$key] = [System.Collections.ArrayList]@()
    }
    $fileGroups[$key].Add($e) | Out-Null
}

Write-Host ""
Write-Host "Files found: $($fileGroups.Count)" -ForegroundColor White
Write-Host ""

$extracted = 0
foreach ($key in $fileGroups.Keys) {
    $extentList = $fileGroups[$key]

    # Get filename from the key (avoids array indexing issues)
    $fileName = $extentList[0].FullName

    Write-Host "  Processing: $fileName ($($extentList.Count) extents)" -ForegroundColor DarkGray

    # Sort extents by S2*32 + Extent
    $sorted = @($extentList | Sort-Object { ($_.S2 * 32) + $_.Extent })

    # Collect all block numbers in order across all extents
    $allBlockNums = @()
    foreach ($ext in $sorted) {
        foreach ($blk in $ext.Blocks) {
            $allBlockNums += $blk
        }
    }

    if ($allBlockNums.Count -eq 0) { continue }

    # Calculate total file size
    $lastExt = $sorted[$sorted.Count - 1]
    $lastBlockCount = $lastExt.Blocks.Count
    $priorBlockCount = $allBlockNums.Count - $lastBlockCount

    $totalSize = ($priorBlockCount * $BLOCK_SIZE) + ($lastExt.RC * $CPM_RECORD_SIZE)

    # Read all block data into a single byte array
    $rawSize = $allBlockNums.Count * $BLOCK_SIZE
    $rawData = New-Object byte[] $rawSize
    for ($b = 0; $b -lt $allBlockNums.Count; $b++) {
        $blkOff = $DATA_OFFSET + ($allBlockNums[$b] * $BLOCK_SIZE)
        if ($blkOff + $BLOCK_SIZE -le $disk.Length) {
            [Array]::Copy($disk, $blkOff, $rawData, ($b * $BLOCK_SIZE), $BLOCK_SIZE)
        }
    }

    # Clamp to calculated size
    if ($totalSize -gt $rawSize) { $totalSize = $rawSize }
    if ($totalSize -le 0) { continue }

    # Trim trailing 0x1A (CP/M EOF marker) for text files
    $trimmedSize = $totalSize
    while ($trimmedSize -gt 0 -and $rawData[$trimmedSize - 1] -eq 0x1A) {
        $trimmedSize--
    }
    if ($trimmedSize -le 0) { $trimmedSize = $totalSize }

    # Write file
    $finalBytes = New-Object byte[] $trimmedSize
    [Array]::Copy($rawData, 0, $finalBytes, 0, $trimmedSize)

    $outPath = Join-Path $outDir $fileName
    Write-Host "    Writing: $outPath ($trimmedSize bytes)" -ForegroundColor DarkGray
    [System.IO.File]::WriteAllBytes($outPath, $finalBytes)

    Write-Host "  $fileName  ${trimmedSize}B  ($($allBlockNums.Count) blocks, $($sorted.Count) extents)" -ForegroundColor Green
    $extracted++
}

Write-Host ""
Write-Host "Extracted $extracted files to $outDir" -ForegroundColor Cyan
Write-Host "Done!" -ForegroundColor Green