# Test upload with different flash settings
# Usage: .\upload_test.ps1 [COM_PORT]

param(
    [string]$Port = "COM9"
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "UDP UWB Tag - Test Upload (QIO->DIO)" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

# Try with DIO flash mode and slower clock
$FQBN = "esp32:esp32:esp32:FlashFreq=40,FlashMode=dio,PartitionScheme=default,UploadSpeed=115200"
$DriverLib = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\Dw3000")).Path

Write-Host "Compiling with DIO flash mode..." -ForegroundColor Yellow
arduino-cli compile --fqbn $FQBN . --warnings none --libraries $DriverLib

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Uploading to $Port with DIO mode..." -ForegroundColor Yellow
arduino-cli upload -p $Port --fqbn $FQBN . --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Upload failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Upload successful! Try monitoring now." -ForegroundColor Green