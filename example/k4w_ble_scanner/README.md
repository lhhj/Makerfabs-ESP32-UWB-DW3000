# KKM K4W Tag Detector & Info Reader (ESP32 BLE)

This Arduino sketch scans for KKM/KBeacon/K4W BLE tags, auto-connects, reads standard info (Device Information Service, Battery Service), and lists all services/characteristics so you can identify KKM custom UUIDs for further control.

Why BLE? The K4W tag exposes device info/config over BLE (KBeacon SDK). UWB is used for ranging, not for metadata.

## Requirements
- ESP32 board with BLE (ESP32-WROOM/ESP32-DevKitC)
- Arduino ESP32 core installed
- Serial Monitor at 115200 baud

## What it does
- Scans for BLE devices whose name contains: `KBeacon`, `KKM`, or `K4W` (adjust in code)
- Stops scanning and connects to the first match
- Reads the standard Device Information Service:
  - Manufacturer Name, Model Number, Serial Number, Firmware, Hardware, Software
- Reads Battery Service: Battery Level (%)
- Enumerates all services and characteristics and prints any readable values in hex

This lets you correlate to the KKM iOS SDK (KBeacon) to find the right custom service/characteristic UUIDs.

## How to use
1. Open `k4w_ble_scanner.ino` and upload to ESP32.
2. Open Serial Monitor (115200).
3. Watch scan results until a matching device is found.
4. The sketch will connect, read info, list services/characteristics, then disconnect and resume scanning.

## Adapting filters
Edit these arrays/strings near the top of the sketch:
```cpp
static const char* FILTER_NAMES[] = {"KBeacon", "KKM", "K4W"};
```
Add specific device name or partial name if needed.

## Adding custom reads (KKM proprietary)
Once you observe the UUIDs in the serial output, add targeted reads like:
```cpp
BLEUUID KKM_SVC("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
BLEUUID KKM_CH ("yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy");
BLERemoteService* svc = client->getService(KKM_SVC);
if (svc) {
  auto ch = svc->getCharacteristic(KKM_CH);
  if (ch && ch->canRead()) {
    auto val = ch->readValue();
    // parse val per KKM docs
  }
}
```
Refer to KKM/KBeacon documentation for UUIDs and payloads.

## Troubleshooting
- If connect fails, the tag may require a manual pairing step or be in sleep; try pressing its button.
- If services are empty, increase scan window/duration or move closer.
- If names don’t match, remove the filter or add your device’s advertised name.

## Next steps
- If you want to correlate UWB traffic with BLE, run the DW3000 passive scanner example in parallel on a second board, or integrate a lightweight UWB RX loop.
- Add writes/subscriptions (notify/indicate) for advanced control once you know the custom UUIDs.
