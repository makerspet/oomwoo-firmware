# Windows side of the OOMWOO CH32 firmware setup: the flashing tools.
# Builds run in WSL2 (mcu/scripts/setup.sh); flashing runs on Windows, where
# the USB probes are.
#
#   powershell -ExecutionPolicy Bypass -File mcu\scripts\setup-windows.ps1
#
# Installs into %LOCALAPPDATA%\oomwoo\wch and adds it to the user PATH:
#   - wlink 0.1.3, 32-bit build: works with WCH's own WCH-Link driver, no Zadig
#   - wchisp 0.3.0: bootloader flashing (serial needs no driver; USB needs
#     WinUSB via Zadig and the VC++ runtime)
param(
  [string]$Dest = "$env:LOCALAPPDATA\oomwoo\wch",
  [switch]$SkipPath
)
$ErrorActionPreference = 'Stop'

$tools = @(
  @{ Name = 'wlink'; Url = 'https://github.com/ch32-rs/wlink/releases/download/v0.1.3/wlink-v0.1.3-win-x86.zip';
     Sha = 'c51e2f060cf71abfb773ac5efa0003377e4cb4256f7bd18869b5f9c28c3b683c' },
  @{ Name = 'wchisp'; Url = 'https://github.com/ch32-rs/wchisp/releases/download/v0.3.0/wchisp-v0.3.0-win-x64.zip';
     Sha = 'eba605bbc62f217f6454e7236d04ef1b8a6b4396dd7ce8dc26fc83016213c3aa' }
)

New-Item -ItemType Directory -Force $Dest | Out-Null
foreach ($t in $tools) {
  $exe = Join-Path $Dest "$($t.Name).exe"
  if (Test-Path $exe) { Write-Host "$($t.Name): already installed"; continue }
  $zip = Join-Path $env:TEMP "$($t.Name)-oomwoo.zip"
  Invoke-WebRequest -Uri $t.Url -OutFile $zip -UseBasicParsing
  $sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
  if ($sha -ne $t.Sha) { throw "$($t.Name): checksum mismatch ($sha)" }
  $tmp = Join-Path $env:TEMP "$($t.Name)-oomwoo"
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
  Expand-Archive $zip $tmp
  Copy-Item (Get-ChildItem $tmp -Recurse -Filter "$($t.Name).exe" | Select-Object -First 1).FullName $exe
  Remove-Item -Recurse -Force $tmp, $zip
  Write-Host "$($t.Name): installed"
}
& (Join-Path $Dest 'wlink.exe') --version
& (Join-Path $Dest 'wchisp.exe') --version

if (-not $SkipPath) {
  $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
  if (($userPath -split ';') -notcontains $Dest) {
    [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $Dest), 'User')
    Write-Host "Added $Dest to the user PATH (open a new terminal)."
  }
}

$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
if (-not $wsl) {
  Write-Host "`nWSL2 is not installed. Run 'wsl --install -d Ubuntu-24.04', reboot, then in Ubuntu:"
} else {
  Write-Host "`nNext, in WSL2 (Ubuntu), from the repository:"
}
Write-Host "  mcu/scripts/setup.sh"
Write-Host "`nProbe driver: install WCH-LinkUtility (or WCH's WCHLink driver) so wlink sees the WCH-LinkE."
