# K4W Tag Discovery Guide

## Quick Start
Use `k4w_ble_discovery.ino` to automatically probe the tag and discover the UWB trigger command.

## What the Discovery Tool Does

1. **Scans** for K4W tags
2. **Connects** and enumerates all BLE services/characteristics  
3. **Subscribes** to all notifications to capture responses
4. **Systematically tests** 19 different payloads on every writable characteristic
5. **Monitors UWB radio** during each test
6. **Correlates timing** between BLE writes and UWB packet reception
7. **Highlights matches** when UWB packets are detected after a BLE write

## Expected Output Pattern

When the correct command is found, you'll see:

```
▶ Test 5/19: ASCII: ENABLE
  Data: 45 4E 41 42 4C 45 
  ✓ Write successful
  ⏳ Waiting 2000 ms for response...

╔═══════════════════════════════════════════════════════════════
║ [UWB PACKET RECEIVED] Time: 12345 ms
║ ⏱️  234 ms since last BLE write
║ 📝 Last BLE write: xxxxxxxx-xxxx-...-xxx <- ASCII: ENABLE
║ 📊 Packets since write: 1
║ Length: 12 bytes
║ RAW: 01 23 45 67 89 AB CD EF ...
╚═══════════════════════════════════════════════════════════════

╔═════════════════════════════════════════════════════════════
║ ⚡ POTENTIAL MATCH DETECTED!
║ Characteristic: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
║ Payload: ASCII: ENABLE
║ UWB Packets received: 1
╚═════════════════════════════════════════════════════════════
```

## Manual Testing (If Automatic Discovery Doesn't Work)

### Method 1: Use nRF Connect App
1. Install "nRF Connect" on your phone (iOS/Android)
2. Scan and connect to K4W tag
3. Explore services and try writing to characteristics
4. Have your ESP32 in UWB receive mode watching for packets
5. Note which write caused UWB transmission

### Method 2: Check K4W/KBeacon Documentation
- Search for "KBeacon SDK" or "KKM SDK"
- Look for UWB configuration commands
- Common UUIDs for IoT tags:
  - `0000fff0-xxxx-xxxx-xxxx-xxxxxxxxxxxx` (Custom service)
  - `0000ffe0-xxxx-xxxx-xxxx-xxxxxxxxxxxx` (Config service)

### Method 3: BLE Sniffer
Use a dedicated BLE sniffer (nRF52840 dongle + Wireshark) to capture:
- Official K4W app communication
- Exact command sequences
- Authentication/pairing data

## Common BLE Command Patterns

### Standard Enable Commands
- `0x01` - Enable/Start
- `0x00` - Disable/Stop  
- `0x01 0x00` - Enable with little-endian param
- `0x00 0x01` - Enable with big-endian param

### Configuration Commands
- `0xA5 0x5A` - Test/wake pattern
- `0xFF` - All-on command
- `0x55 0xAA` - Alternating test pattern

### ASCII Commands
If the tag uses text protocol:
- `START`, `STOP`, `BEGIN`, `END`
- `ENABLE`, `DISABLE`
- `ON`, `OFF`
- `UWB`, `RANGE`, `SCAN`

## Troubleshooting

### No UWB Packets Detected
1. **Check UWB initialization**: Look for "✓ Passive RX started" in serial output
2. **Verify tag is in range**: UWB typically works 0-30 meters
3. **Check UWB frequency**: Ensure ESP32 config matches tag (Channel 5)
4. **Tag might need pairing**: Some tags require BLE pairing/bonding first
5. **Multi-step activation**: Tag might need multiple commands in sequence

### BLE Connection Fails
1. **Check tag battery**: Low battery can cause connection issues
2. **Tag might be sleeping**: Try reset button if available
3. **Interference**: Move away from WiFi routers, other BLE devices
4. **Try different scan parameters**: Increase SCAN_WINDOW_MS

### Write Fails
1. **Authentication required**: Some characteristics need pairing
2. **MTU too small**: Code sets MTU to 247, but tag might not support it
3. **Wrong write type**: Try both write-with-response and write-without-response

## Advanced: Multi-Step Sequences

If single commands don't work, the tag might require a sequence:

```cpp
// Example: Initialize, then enable
ch->writeValue(initCmd, initLen, true);
delay(500);
ch->writeValue(enableCmd, enableLen, true);
delay(500);
// Watch for UWB
```

Common sequences:
1. **Authenticate** → **Configure** → **Enable**
2. **Wake** → **Start UWB**
3. **Request config** → **Set params** → **Begin ranging**

## Expected UWB Data from K4W Tags

K4W tags typically transmit:
- **Beacon frames**: Periodic UWB broadcasts with tag ID
- **Ranging frames**: TWR (Two-Way Ranging) protocol
- **Data frames**: May contain sensor data, battery level, etc.

Frame format might be:
- IEEE 802.15.4 standard format
- Custom K4W protocol
- Look for tag ID/MAC address in first bytes

## Interpreting Results

Once you find the command:

1. **Document the exact sequence**:
   - Service UUID
   - Characteristic UUID  
   - Payload bytes (hex)
   - Any required delays
   
2. **Test repeatability**:
   - Disconnect and reconnect
   - Test multiple times
   - Try with different tags
   
3. **Check for parameters**:
   - Can you adjust UWB rate?
   - Set tag ID?
   - Configure ranging mode?

4. **Create a minimal sketch**:
   - Just connect → write command → receive UWB
   - Share your findings!

## Sample Working Code (Once Discovered)

```cpp
// After you discover the command, use this pattern:

NimBLEClient* client = NimBLEDevice::createClient();
client->connect(NimBLEAddress("XX:XX:XX:XX:XX:XX"));

NimBLERemoteService* svc = client->getService("YOUR_SERVICE_UUID");
NimBLERemoteCharacteristic* ch = svc->getCharacteristic("YOUR_CHAR_UUID");

// The magic command you discovered:
uint8_t enableUwb[] = {0xXX, 0xXX, ...}; // Your payload
ch->writeValue(enableUwb, sizeof(enableUwb), true);

// Now UWB should transmit!
// Poll uwb_poll() in loop()
```

## Community Resources

- **K4W Official**: Look for official documentation/SDK
- **Makerfabs Forum**: Share your findings
- **GitHub**: Search for "K4W BLE" or "KBeacon"
- **Discord/Reddit**: IoT, UWB, ESP32 communities

## Safety Notes

⚠️ **Be Respectful**:
- Don't repeatedly hammer the tag (can drain battery)
- Use reasonable delays between tests (code uses 2 seconds)
- Don't brute-force authentication if required
- Follow manufacturer guidelines

📝 **Log Everything**:
- Save serial output to file
- Document what works AND what doesn't
- Share findings to help others

Good luck with your discovery! 🚀
