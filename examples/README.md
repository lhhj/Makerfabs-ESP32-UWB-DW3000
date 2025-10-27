# UWB Tag Discovery Examples

This directory contains examples for discovering and monitoring UWB devices using the DW3000 library.

## Examples

### 1. Tag Discovery (`tag_discovery/`)

An active scanning example that discovers UWB tags in the vicinity by:
- Sending discovery beacon frames periodically
- Listening for responses from nearby UWB devices
- Tracking discovered devices with their signal strength and activity
- Displaying comprehensive device information

**Features:**
- Active discovery using beacon frames
- Device tracking and statistics
- Signal strength (RSSI) measurement
- Device timeout and cleanup
- Visual feedback via LEDs

**Use this when:** You want to actively discover UWB devices and get them to respond to your discovery requests.

### 2. Passive Scanner (`passive_scanner/`)

A passive monitoring example that:
- Continuously listens for UWB transmissions
- Analyzes frame types and sources
- Monitors UWB activity without transmitting
- Provides detailed statistics

**Features:**
- Pure passive scanning (no transmissions)
- Frame type identification (Beacon, Data, ACK, etc.)
- Signal strength monitoring
- Traffic statistics and analysis
- Real-time activity monitoring

**Use this when:** You want to monitor existing UWB communication without interfering or when you need to analyze UWB traffic patterns.

## Hardware Requirements

- ESP32 board
- DW3000 UWB module (Makerfabs ESP32 UWB)
- Connections:
  - RST pin: GPIO 27
  - IRQ pin: GPIO 34
  - SPI CS pin: GPIO 4

## Getting Started

1. Install the DW3000 library in your Arduino IDE
2. Connect your ESP32 UWB board
3. Choose the appropriate example based on your needs
4. Upload the code and open Serial Monitor (115200 baud)
5. Monitor the output for discovered devices

## Output Format

### Tag Discovery Output
```
[Time] Device: DEVICEID | Type: XX | RSSI: XXX dBm | FPP: XXXX | Count: XX
```

### Passive Scanner Output
```
[Time] Type: BEACON | Len: XX | RSSI: XXX dBm | Source: XXXXXXXX
```

## Configuration

Both examples operate on UWB channel 5 by default. You can modify the channel in the `config` structure:

```cpp
static dwt_config_t config = {
    9,                /* Channel number (change to 9 for channel 9) */
    // ... other settings
};
```

## Understanding the Output

### Signal Strength (RSSI)
- Values closer to 0 dBm indicate stronger signals
- Typical range: -40 dBm (very strong) to -100 dBm (very weak)
- Values below -90 dBm may indicate poor signal quality

### Frame Types
- **BEACON**: Discovery or synchronization frames
- **DATA**: Data transmission frames
- **ACK**: Acknowledgment frames
- **UNK**: Unknown or custom frame types

### Device ID
- 8-byte identifier extracted from received frames
- May represent MAC address, device serial, or custom identifier
- Used to track unique devices

## Multi-Channel Scanning

To scan multiple channels, you can modify the examples to cycle through different channels:

```cpp
uint8_t channels[] = {1, 2, 3, 4, 5, 7, 9};
// Cycle through channels and reconfigure DW3000 for each
```

## Important Notes

1. **Regulatory Compliance**: Ensure your usage complies with local regulations for UWB spectrum
2. **Privacy**: Be mindful of privacy when scanning for devices
3. **Range**: Detection range depends on transmit power, environment, and antenna setup
4. **Interference**: UWB operates in shared spectrum; expect some interference
5. **Power**: Active scanning consumes more power than passive scanning

## Troubleshooting

### No Devices Detected
- Check antenna connections
- Verify other UWB devices are actually transmitting
- Try different channels (1, 2, 3, 4, 5, 7, 9)
- Check distance (start with devices close by)

### High Error Rate
- Poor antenna connection
- Interference from other devices
- Wrong channel configuration
- Hardware issues

### Poor RSSI Values
- Increase distance between devices
- Check antenna orientation
- Verify antenna connections
- Consider environmental factors (walls, metal objects)

## Further Development

These examples can be extended to:
- Implement proper 802.15.4 frame parsing
- Add ranging functionality to measure distances
- Create a mesh network discovery protocol
- Log discovered devices to SD card or network
- Implement device filtering and classification
- Add web interface for monitoring

## License

These examples are provided under the same license as the DW3000 library.