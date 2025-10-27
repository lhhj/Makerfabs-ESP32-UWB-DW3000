# Upload DEBUG version of UDP UWB Tag
# Usage: .\upload_debug.ps1 [COM_PORT]

param(
    [string]$Port = "COM9"
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "UDP UWB Tag - DEBUG VERSION Upload" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

# Board FQBN for ESP32-WROOM
$FQBN = "esp32:esp32:esp32:FlashFreq=40,FlashMode=dio,PartitionScheme=default,UploadSpeed=115200"
$DriverLib = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\Dw3000")).Path

# Temporarily rename files
if (Test-Path "udp_uwb_tag.ino") {
    Rename-Item "udp_uwb_tag.ino" "udp_uwb_tag.ino.bak"
}
Rename-Item "udp_uwb_tag_debug.ino" "udp_uwb_tag.ino"

Write-Host "Compiling DEBUG version..." -ForegroundColor Yellow
arduino-cli compile --fqbn $FQBN . --warnings none --libraries $DriverLib

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Compilation failed!" -ForegroundColor Red
    # Restore files
    Rename-Item "udp_uwb_tag.ino" "udp_uwb_tag_debug.ino"
    if (Test-Path "udp_uwb_tag.ino.bak") {
        Rename-Item "udp_uwb_tag.ino.bak" "udp_uwb_tag.ino"
    }
    exit 1
}

Write-Host ""
Write-Host "Uploading DEBUG version to $Port..." -ForegroundColor Yellow
arduino-cli upload -p $Port --fqbn $FQBN . --verbose

# Restore files
Rename-Item "udp_uwb_tag.ino" "udp_uwb_tag_debug.ino"
if (Test-Path "udp_uwb_tag.ino.bak") {
    Rename-Item "udp_uwb_tag.ino.bak" "udp_uwb_tag.ino"
}

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Upload failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "DEBUG version uploaded successfully!" -ForegroundColor Green
Write-Host "This version has extensive logging to help identify the problem." -ForegroundColor Yellow