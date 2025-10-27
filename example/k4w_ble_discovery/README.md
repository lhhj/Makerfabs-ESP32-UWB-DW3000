# K4W Tag Discovery Tool - Quick Start

## 📁 This is a Separate Folder!

This discovery tool is in its own folder to avoid Arduino compilation conflicts.

## 🚀 Quick Start

1. **Navigate to this folder:**
   ```powershell
   cd c:\Users\leh\source\repos\Makerfabs-ESP32-UWB-DW3000\example\k4w_ble_discovery
   ```

2. **Upload the discovery tool:**
   ```powershell
   .\upload.ps1 COM9  # Replace COM9 with your port
   ```

3. **Start monitoring:**
   ```powershell
   .\monitor.ps1 COM9
   ```

4. **Power on your K4W tag** and watch for results!

## ✨ Key Fixes in This Version

- **Separate folder** - No more conflicts with other .ino files
- **NimBLE 2.x compatible** - Works with your NimBLE-Arduino 2.3.6
- **Fixed namespace collision** - Handles DW3000 vs NimBLE macro conflicts
- **Proper API calls** - Uses `canWrite()`, `canWriteNoResponse()` instead of deprecated methods

## 🎯 What to Look For

When successful, you'll see:

```
╔═════════════════════════════════════════════════════════════
║ ⚡ POTENTIAL MATCH DETECTED!
║ Characteristic: 0000fff1-0000-1000-8000-00805f9b34fb
║ Payload: 0x01 0x00 (enable with param)
║ UWB Packets received: 3
╚═════════════════════════════════════════════════════════════
```

## 📝 Configuration

Edit `k4w_ble_discovery.ino` line 24-25 if needed:

```cpp
static const char* TARGET_NAME_HINT = "K4W";  // Tag name to search for
static const char* TARGET_ADDR = "";           // Or set specific MAC address
```

Common alternatives: `"KBeacon"`, `"KKM"`, `"iBeacon"`

## 🔧 Troubleshooting

### Tag Not Found
- Check battery
- Try different name: `"KBeacon"` or `"KKM"`
- Set exact MAC address if known

### Compile Error
- Make sure you're in the `k4w_ble_discovery` folder (not `k4w_ble_scanner`)
- Only `k4w_ble_discovery.ino` should be in this folder

### Upload Error
- Close Arduino IDE / other serial monitors
- Try holding BOOT button during upload
- Check COM port with: `arduino-cli board list`

## 📚 More Info

See the original `k4w_ble_scanner` folder for:
- `QUICKSTART.md` - Detailed quick start guide
- `DISCOVERY_GUIDE.md` - Complete reference
- `SUMMARY.md` - Overview

## 🆚 Why Two Folders?

Arduino compiles ALL `.ino` files in a folder together, which caused conflicts. This separate folder ensures clean compilation of the discovery tool.

Good luck! 🚀
