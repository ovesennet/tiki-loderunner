param([switch]$Kill)
$ErrorActionPreference = "SilentlyContinue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
if ($Kill) {
  $procs = Get-Process | Where-Object { $_.MainWindowTitle -like "*TIKI-100 Emul*" }
  foreach ($p in $procs) { Stop-Process -Id $p.Id -Force }
  Start-Sleep 2
}
& "$Root\deploy.ps1" -NoBuild -NoLaunch | Out-Null
Start-Process "$Root\tikiemul.exe" -ArgumentList "-diska `"$Root\dsk\work.dsk`""
Start-Sleep 7
python "$Root\tools\drive.py" key 0x41    # A
Start-Sleep 2
python "$Root\tools\drive.py" key 0x0D    # Enter -> CP/M
Start-Sleep 2
python "$Root\tools\drive.py" type "LODERUN`n"
Start-Sleep 3
python "$Root\tools\drive.py" key 0x20    # Space -> start game
Start-Sleep 1
Write-Host "BOOTED"
