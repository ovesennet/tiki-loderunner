param([switch]$NoDeploy)
$ErrorActionPreference = "SilentlyContinue"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
$p = Get-Process tikiemul -ErrorAction SilentlyContinue
if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep 3 }
if (-not $NoDeploy) { & "$Root\deploy.ps1" -NoBuild -NoLaunch | Out-Null }
Start-Process "$Root\tikiemul.exe" -ArgumentList "-diska `"$Root\dsk\work.dsk`""
Start-Sleep 9
python "$Root\tools\drive.py" key 0x41 | Out-Null   # A -> exit menu
Start-Sleep 2
python "$Root\tools\drive.py" key 0x0D | Out-Null   # Enter -> CP/M
Start-Sleep 3
python "$Root\tools\drive.py" click 200 300 | Out-Null   # focus window
Start-Sleep 1
python "$Root\tools\drive.py" type "LODERUN`n" | Out-Null
Start-Sleep 4
python "$Root\tools\drive.py" key 0x20 0x20 | Out-Null   # Space -> start
Write-Host "PLAYING"
