# Upload UDP UWB Tag with slower baud rate for problematic boards
# Usage: .\upload_slow.ps1 [COM_PORT]

param(
    [string]$Port = "COM9"
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "UDP UWB Tag - Slow Upload" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

# Board FQBN for ESP32-WROOM
$FQBN = "esp32:esp32:esp32:FlashFreq=80,PartitionScheme=default,UploadSpeed=115200"
$DriverLib = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\Dw3000")).Path

Write-Host "Compiling udp_uwb_tag.ino..." -ForegroundColor Yellow
arduino-cli compile --fqbn $FQBN . --warnings none --libraries $DriverLib

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Uploading to $Port at 115200 baud..." -ForegroundColor Yellow
arduino-cli upload -p $Port --fqbn $FQBN . --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Upload failed!" -ForegroundColor Red
    Write-Host "Try holding BOOT button during upload" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Upload successful!" -ForegroundColor Green
Write-Host "The device may need a manual reset if it doesn't start." -ForegroundColor Yellow