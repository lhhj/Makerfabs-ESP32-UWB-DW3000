# K4W Tag Discovery Tools - Summary

## 📦 What's Included

This folder contains a complete toolkit for discovering the BLE command that triggers K4W tags to transmit UWB data.

### Core Files

1. **`k4w_ble_discovery.ino`** ⭐ **MAIN DISCOVERY TOOL**
   - Automated BLE/UWB correlation tool
   - Systematically tests 19 different payloads
   - Monitors UWB radio during each BLE write
   - Highlights when UWB packets are detected
   - **This is what you should upload and run**

2. **`k4w_ble_scanner.ino`** (Original)
   - Your original probing code
   - Kept for reference

### Helper Scripts

3. **`upload.ps1`**
   - One-command upload script
   - Usage: `.\upload.ps1 COM3`
   - Compiles and uploads discovery tool

4. **`monitor.ps1`**
   - Serial monitor with automatic logging
   - Usage: `.\monitor.ps1 COM3`
   - Saves output to timestamped log file

### Documentation

5. **`QUICKSTART.md`** 📘 **START HERE**
   - Quick 3-step guide to get running
   - Troubleshooting table
   - Success checklist

6. **`DISCOVERY_GUIDE.md`** 📚 **DETAILED REFERENCE**
   - In-depth explanation of discovery process
   - Advanced techniques (BLE sniffing, etc.)
   - Expected output patterns
   - Manual testing methods

7. **`README.md`**
   - Original documentation

8. **`SUMMARY.md`** (this file)
   - Overview of all files

---

## 🚀 Quick Start (3 Commands)

```powershell
# 1. Upload discovery tool
.\upload.ps1 COM3

# 2. Start monitoring
.\monitor.ps1 COM3

# 3. Power on your K4W tag and watch the output!
```

---

## 🎯 What Problem Does This Solve?

**Problem**: K4W tags need a BLE "connect" signal before they'll transmit UWB ranging data. You don't know what signal to send.

**Solution**: The discovery tool:
1. Scans for K4W tags via BLE
2. Connects and enumerates ALL services/characteristics
3. Subscribes to notifications to catch responses
4. Systematically sends test payloads to every writable characteristic
5. Monitors the UWB radio after each write
6. **Tells you exactly which command triggered UWB transmission**

---

## 📊 Expected Results

When successful, you'll see output like:

```
╔═════════════════════════════════════════════════════════════
║ ⚡ POTENTIAL MATCH DETECTED!
║ Characteristic: 0000fff1-0000-1000-8000-00805f9b34fb
║ Payload: 0x01 0x00 (enable with param)
║ UWB Packets received: 3
╚═════════════════════════════════════════════════════════════
```

This tells you:
- **Which BLE characteristic** to write to
- **What bytes to send** (`0x01 0x00`)
- **Confirmation** that UWB packets were received

---

## 🔧 Configuration

Edit these in `k4w_ble_discovery.ino` if needed:

```cpp
// Line 12-14: Scan settings
static const char* TARGET_NAME_HINT = "K4W";  // Change if tag has different name
static const char* TARGET_ADDR = "";          // Set MAC if you know it

// Line 17-18: Timing
static const uint16_t WAIT_AFTER_WRITE_MS = 2000;     // How long to wait for UWB
static const uint16_t WAIT_AFTER_SUBSCRIBE_MS = 1000; // Notification settling time

// Line 21-24: UWB pins (should match your hardware)
static const uint8_t UWB_PIN_RST = 27;
static const uint8_t UWB_PIN_IRQ = 34;
static const uint8_t UWB_PIN_SS  = 4;
```

---

## 🧪 What Payloads Are Tested?

The tool tests 19 different payloads:

### Binary Commands (10)
- Single bytes: `0x01`, `0xFF`
- Two bytes: `0x01 0x00`, `0x01 0x01`, `0x00 0x01`, `0xA5 0x5A`, `0x55 0xAA`, `0xAA 0x55`
- Four bytes: `0x01 0x00 0x00 0x00`, `0x00 0x00 0x00 0x01`

### ASCII Commands (9)
- `START`, `BEGIN`, `ENABLE`, `UWB`, `ON`, `RUN`, `GO`, `INIT`, `CONNECT`

Each is tested on **every writable characteristic** found on the tag.

---

## 💾 Output Logging

Use `monitor.ps1` to automatically save logs:
- Creates timestamped file: `k4w_discovery_log_20231021_143022.txt`
- Captures all serial output
- Useful for later analysis

Or manually redirect:
```powershell
arduino-cli monitor -p COM3 -c baudrate=115200 | Tee-Object -FilePath log.txt
```

---

## 🔍 Troubleshooting Quick Reference

| Issue | Fix |
|-------|-----|
| **Tag not found** | Check battery, try "KBeacon" or "KKM" as TARGET_NAME_HINT |
| **Connection fails** | Move closer, remove interference, try pairing first |
| **No UWB packets** | Check antenna, verify channel 5, ensure UWB init succeeded |
| **Upload fails** | Close other programs using COM port, try BOOT button |
| **Compile errors** | Install ESP32 core: `arduino-cli core install esp32:esp32` |

See **QUICKSTART.md** for detailed troubleshooting.

---

## 🎓 Learning Path

1. **First Time?** → Read **QUICKSTART.md**
2. **Want Details?** → Read **DISCOVERY_GUIDE.md**  
3. **Having Issues?** → Check troubleshooting sections in both guides
4. **Advanced User?** → Check DISCOVERY_GUIDE.md section on BLE sniffing

---

## 🤝 Contributing / Sharing

If you successfully discover the K4W command:

1. **Document it clearly**:
   ```
   Service UUID: 0000fff0-0000-1000-8000-00805f9b34fb
   Characteristic UUID: 0000fff1-0000-1000-8000-00805f9b34fb
   Command: 0x01 0x00
   Result: Tag begins transmitting UWB beacon every ~100ms
   ```

2. **Test with multiple tags** (if possible)

3. **Share with the community**:
   - Create a GitHub issue/PR
   - Post in Makerfabs forums
   - Update this README

4. **Create a minimal example**:
   ```cpp
   // Working K4W UWB Enable Command
   // Discovered: [Your Name], [Date]
   
   NimBLEClient* client = NimBLEDevice::createClient();
   client->connect(NimBLEAddress(tagAddress));
   
   NimBLERemoteService* svc = client->getService("0000fff0-...");
   NimBLERemoteCharacteristic* ch = svc->getCharacteristic("0000fff1-...");
   
   uint8_t enableCmd[] = {0x01, 0x00};
   ch->writeValue(enableCmd, 2, true);
   
   // Tag now transmits UWB!
   ```

---

## 🛠️ Dependencies

- **Hardware**: ESP32-WROOM + DW3000 UWB module
- **Software**: 
  - arduino-cli (or Arduino IDE)
  - ESP32 Arduino core
  - NimBLE library (included with ESP32 core)
  - DW3000 library (assumed present from your project)

---

## 📝 Notes

- Discovery process takes 5-10 minutes per tag
- Tool is conservative: 2-second wait between tests
- UWB must initialize successfully (watch for "✓ Passive RX started")
- Some tags may require pairing/bonding before commands work
- Multi-step sequences not yet automated (see manual methods in guide)

---

## 🔗 Related Resources

- **Makerfabs ESP32-UWB**: https://github.com/Makerfabs/Makerfabs-ESP32-UWB-DW3000
- **KBeacon SDK**: Search for official KKM/KBeacon documentation
- **NimBLE**: https://github.com/h2zero/NimBLE-Arduino
- **DW3000**: Qorvo DW3000 documentation

---

## ❓ Still Stuck?

1. Check `QUICKSTART.md` troubleshooting section
2. Review `DISCOVERY_GUIDE.md` for advanced techniques
3. Save your serial log and examine for clues
4. Try alternative methods (nRF Connect app, BLE sniffer)
5. Search for K4W/KBeacon documentation online
6. Ask in ESP32 / IoT communities with your logs

---

## ✨ Success Story Template

Once you discover the command, create a file like this:

**`K4W_WORKING_COMMAND.md`**:
```markdown
# K4W Tag - Working BLE Command

**Discovered by**: [Your Name]
**Date**: [Date]
**Tag Model**: [K4W model/version]
**Firmware**: [If known]

## Working Configuration

**Service UUID**: `0000fff0-0000-1000-8000-00805f9b34fb`
**Characteristic UUID**: `0000fff1-0000-1000-8000-00805f9b34fb`
**Command**: `0x01 0x00`
**Write Type**: With response

## Behavior

After sending command:
- Tag immediately begins UWB transmission
- Beacon rate: ~10 Hz (every 100ms)
- Frame format: [describe if known]

## Code Snippet

[Your minimal working code here]

## Notes

[Any additional observations]
```

---

## 🎉 Good Luck!

You now have everything you need to discover the K4W BLE command. The discovery tool will do most of the work automatically. Watch for the "⚡ POTENTIAL MATCH DETECTED!" message!

**Start here**: Upload `k4w_ble_discovery.ino` and monitor the output! 🚀
