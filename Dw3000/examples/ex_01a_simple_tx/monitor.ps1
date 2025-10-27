# Serial Monitor with Logging
# Captures serial output to both console and log file
# Usage: .\monitor.ps1 [COM_PORT]

param(
    [string]$Port = "COM9",
    [int]$BaudRate = 115200
)

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = "k4w_discovery_log_$timestamp.txt"

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "K4W Discovery Tool - Serial Monitor" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Port: $Port" -ForegroundColor White
Write-Host "Baud: $BaudRate" -ForegroundColor White
Write-Host "Log:  $logFile" -ForegroundColor White
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Press Ctrl+C to stop monitoring" -ForegroundColor Yellow
Write-Host ""

# Check if arduino-cli is available
if (Get-Command "arduino-cli" -ErrorAction SilentlyContinue) {
    # Use arduino-cli monitor and tee output
    "K4W Discovery Log - Started at $(Get-Date)" | Out-File -FilePath $logFile
    "Port: $Port | Baud: $BaudRate" | Out-File -FilePath $logFile -Append
    "=" * 60 | Out-File -FilePath $logFile -Append
    
    arduino-cli monitor -p $Port -c baudrate=$BaudRate | Tee-Object -FilePath $logFile -Append
} else {
    Write-Host "arduino-cli not found, trying .NET SerialPort..." -ForegroundColor Yellow
    
    # Try using .NET SerialPort directly
    try {
        $port_obj = New-Object System.IO.Ports.SerialPort $Port, $BaudRate
        $port_obj.Open()
        
        "K4W Discovery Log - Started at $(Get-Date)" | Out-File -FilePath $logFile
        "Port: $Port | Baud: $BaudRate" | Out-File -FilePath $logFile -Append
        "=" * 60 | Out-File -FilePath $logFile -Append
        
        Write-Host "Connected! Reading data..." -ForegroundColor Green
        Write-Host ""
        
        while ($true) {
            if ($port_obj.BytesToRead -gt 0) {
                $line = $port_obj.ReadLine()
                Write-Host $line
                $line | Out-File -FilePath $logFile -Append
            }
            Start-Sleep -Milliseconds 10
        }
    }
    catch {
        Write-Host "ERROR: Could not open serial port" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
    finally {
        if ($port_obj -and $port_obj.IsOpen) {
            $port_obj.Close()
        }
    }
}

Write-Host ""
Write-Host "Log saved to: $logFile" -ForegroundColor Green
