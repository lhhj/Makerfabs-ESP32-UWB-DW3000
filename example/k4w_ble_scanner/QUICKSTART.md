# Quick Reference: K4W Tag Discovery

## 🚀 Quick Start (3 Steps)

1. **Upload the discovery tool:**
   ```powershell
   cd c:\Users\leh\source\repos\Makerfabs-ESP32-UWB-DW3000\example\k4w_ble_scanner
   .\upload.ps1 COM3  # Replace COM3 with your port
   ```

2. **Start monitoring with logging:**
   ```powershell
   .\monitor.ps1 COM3
   ```

3. **Power on K4W tag and wait for discovery**
   - Tool will scan, connect, and test automatically
   - Look for "⚡ POTENTIAL MATCH DETECTED!" messages

---

## 📊 What You'll See

### Successful Discovery:
```
╔═════════════════════════════════════════════════════════════
║ ⚡ POTENTIAL MATCH DETECTED!
║ Characteristic: 0000fff1-0000-1000-8000-00805f9b34fb
║ Payload: 0x01 0x00 (enable with param)
║ UWB Packets received: 3
╚═════════════════════════════════════════════════════════════
```

### UWB Data Received:
```
╔═══════════════════════════════════════════════════════════════
║ [UWB PACKET RECEIVED] Time: 12345 ms
║ ⏱️  234 ms since last BLE write
║ 📝 Last BLE write: 0000fff1-... <- 0x01 0x00
║ Length: 12 bytes
║ RAW: 41 88 01 23 45 67 89 AB CD EF 12 34
╚═══════════════════════════════════════════════════════════════
```

---

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| **Can't find K4W tag** | • Check tag is powered on<br>• Check battery level<br>• Try changing `TARGET_NAME_HINT` in code<br>• Tag might advertise as "KBeacon" or "KKM" |
| **BLE connects but no UWB** | • Tag might need multiple commands<br>• Try pairing in nRF Connect first<br>• Check UWB antenna connection<br>• Verify UWB channel matches (Channel 5) |
| **Compile errors** | • Install ESP32 core: `arduino-cli core install esp32:esp32`<br>• Update core: `arduino-cli core update-index`<br>• Check NimBLE library is installed |
| **Upload fails** | • Close Arduino IDE / PuTTY / other serial programs<br>• Hold BOOT button during upload<br>• Try different USB cable<br>• Check COM port: `arduino-cli board list` |
| **No serial output** | • Verify baud rate is 115200<br>• Press RESET button on ESP32<br>• Try different serial monitor |

---

## 📝 Manual Compilation (Alternative)

If PowerShell scripts don't work:

```powershell
# Check board is connected
arduino-cli board list

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32 k4w_ble_discovery.ino

# Upload (replace COM3)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32 k4w_ble_discovery.ino

# Monitor
arduino-cli monitor -p COM3 -c baudrate=115200
```

---

## 🎯 Interpreting Results

### Step 1: Find the Match
Look for high correlation between BLE write and UWB packets:
- ✓ UWB within 100-500ms of write = **likely match**
- ✗ UWB after >2 seconds = **probably unrelated**

### Step 2: Document Everything
When you find it, note:
- **Service UUID**: `0000fff0-0000-1000-8000-00805f9b34fb`
- **Characteristic UUID**: `0000fff1-0000-1000-8000-00805f9b34fb`
- **Payload**: `01 00` (hex bytes)
- **Write Type**: With/without response

### Step 3: Verify
Test multiple times:
- Disconnect and reconnect
- Power cycle the tag
- Try with other K4W tags (if available)

### Step 4: Create Working Code
```cpp
// Minimal working example after discovery:

NimBLEClient* client = NimBLEDevice::createClient();
client->connect(NimBLEAddress("XX:XX:XX:XX:XX:XX"));

// Your discovered UUIDs:
NimBLERemoteService* svc = client->getService("YOUR_SERVICE_UUID");
NimBLERemoteCharacteristic* ch = svc->getCharacteristic("YOUR_CHAR_UUID");

// Your discovered payload:
uint8_t cmd[] = {0xXX, 0xXX}; // What you found
ch->writeValue(cmd, sizeof(cmd), true);

// Now poll for UWB data
while(true) {
    uwb_poll();
    delay(10);
}
```

---

## 🔍 Advanced Discovery: BLE Sniffing

If automatic discovery doesn't work, use BLE sniffer:

### Option 1: nRF Connect Mobile App
1. Install nRF Connect (iOS/Android)
2. Scan for K4W tag
3. Connect and explore services
4. Try writing to writable characteristics
5. Watch ESP32 serial monitor for UWB packets

### Option 2: nRF Sniffer (Most Powerful)
1. Get nRF52840 USB Dongle (~$10)
2. Install nRF Sniffer firmware
3. Use with Wireshark to capture BLE traffic
4. Use official K4W app while sniffing
5. Analyze captured packets to find exact command

### Option 3: Web Bluetooth (Chrome)
```javascript
// Test in Chrome browser console
navigator.bluetooth.requestDevice({
    acceptAllDevices: true,
    optionalServices: ['0000fff0-0000-1000-8000-00805f9b34fb']
}).then(device => device.gatt.connect())
  .then(server => {
    // Explore services...
  });
```

---

## 📚 Common K4W/KBeacon UUIDs

If you see these UUIDs, they're likely important:

| UUID | Typical Purpose |
|------|----------------|
| `0000fff0-xxxx` | Custom service (config/control) |
| `0000fff1-xxxx` | Write characteristic (commands) |
| `0000fff2-xxxx` | Notify characteristic (responses) |
| `0000ffe0-xxxx` | Alternative config service |
| `0000180f-xxxx` | Battery Service |
| `0000180a-xxxx` | Device Information |

---

## 💡 Tips for Success

1. **Start Fresh**: Power cycle both ESP32 and K4W tag before testing
2. **Good Logs**: Always save serial output to file for analysis
3. **One Tag at a Time**: Test with only one K4W tag powered on
4. **Be Patient**: Discovery takes 5-10 minutes per tag
5. **Share Findings**: Document and share what you learn!

---

## 📞 Getting Help

If you're stuck:
1. Save your serial monitor log
2. Note your exact hardware (ESP32 model, K4W tag model)
3. Check these resources:
   - Makerfabs ESP32-UWB documentation
   - KBeacon SDK documentation
   - ESP32 forums / Reddit r/esp32
   - GitHub issues on related projects

---

## ✅ Success Checklist

- [ ] ESP32 powers on and initializes UWB successfully
- [ ] K4W tag is discovered during BLE scan
- [ ] BLE connection succeeds
- [ ] Services and characteristics are enumerated
- [ ] Notifications subscribed (if available)
- [ ] Systematic probing completes
- [ ] UWB packets received and correlated with BLE write
- [ ] Working command documented
- [ ] Command verified repeatable

---

## 🎉 What's Next?

After discovery:
1. Create a minimal sketch with just the working command
2. Implement ranging/distance calculation
3. Add multiple tag support
4. Build your application!

Good luck! 🚀
