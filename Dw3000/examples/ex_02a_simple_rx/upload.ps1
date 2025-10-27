# K4W Discovery Tool - Upload Script
# Usage: .\upload.ps1 [COM_PORT]
# Example: .\upload.ps1 COM9

param(
    [string]$Port = "COM9"
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "K4W Tag Discovery Tool - Upload" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

# Check if arduino-cli is available
if (!(Get-Command "arduino-cli" -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: arduino-cli not found in PATH" -ForegroundColor Red
    Write-Host "Install from: https://arduino.github.io/arduino-cli/" -ForegroundColor Yellow
    exit 1
}

# Board FQBN for ESP32-WROOM (D1/DevKit style)
$FQBN = "esp32:esp32:esp32:FlashFreq=80,PartitionScheme=default"

Write-Host "Step 1: Checking ESP32 core installation..." -ForegroundColor Yellow
arduino-cli core list | Select-String "esp32"

if ($LASTEXITCODE -ne 0) {
    Write-Host "ESP32 core might not be installed. Installing..." -ForegroundColor Yellow
    arduino-cli core update-index
    arduino-cli core install esp32:esp32
}

Write-Host ""
Write-Host "Step 2: CLEAN BUILD - Removing cache..." -ForegroundColor Yellow
$sketchDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path "$env:LOCALAPPDATA\arduino\sketches") {
    Get-ChildItem "$env:LOCALAPPDATA\arduino\sketches" -Directory | 
        Where-Object { $_.Name -match "[A-F0-9]{32}" } | 
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Build cache cleared" -ForegroundColor Green
}

Write-Host ""
Write-Host "Step 3: Compiling k4w_ble_discovery.ino..." -ForegroundColor Yellow
arduino-cli compile --fqbn $FQBN . --warnings none --verbose --clean

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Compilation failed!" -ForegroundColor Red
    Write-Host "Check the error messages above." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Compilation successful!" -ForegroundColor Green
Write-Host ""

Write-Host "Available COM ports:" -ForegroundColor Yellow
arduino-cli board list

Write-Host ""
Write-Host "Step 4: Uploading to $Port..." -ForegroundColor Yellow
arduino-cli upload -p $Port --fqbn $FQBN . --verbose

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Upload failed!" -ForegroundColor Red
    Write-Host "Make sure:" -ForegroundColor Yellow
    Write-Host "  - ESP32 is connected to $Port" -ForegroundColor Yellow
    Write-Host "  - No other program is using the port" -ForegroundColor Yellow
    Write-Host "  - Boot button is pressed if needed" -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "Upload successful!" -ForegroundColor Green
Write-Host ""
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Next Steps:" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "1. Open Serial Monitor at 115200 baud" -ForegroundColor White
Write-Host "2. Power on your K4W tag (should be in range)" -ForegroundColor White
Write-Host "3. Watch for discovered devices" -ForegroundColor White
Write-Host "4. Check which device sends UWB packets" -ForegroundColor White
Write-Host ""
Write-Host "To open Serial Monitor:" -ForegroundColor Yellow
Write-Host "  arduino-cli monitor -p $Port -c baudrate=115200" -ForegroundColor Cyan
Write-Host ""
