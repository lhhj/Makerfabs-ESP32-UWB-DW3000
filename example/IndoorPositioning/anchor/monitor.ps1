# Monitor script for ESP32 UWB Anchor with DW3000
# Channel 5 configuration

$PortPath = "COM11"  # Adjust as needed
$BaudRate = 115200

Write-Host "Starting serial monitor for ESP32 UWB Anchor (DW3000, Channel 5)..." -ForegroundColor Green
Write-Host "Port: $PortPath, Baud: $BaudRate" -ForegroundColor Yellow
Write-Host "Press Ctrl+C to exit" -ForegroundColor Yellow
Write-Host "" 

arduino-cli monitor -p $PortPath -c baudrate=$BaudRate