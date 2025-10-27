/*
  Enhanced BLE + UWB Discovery Tool for K4W Tags (Crash-Hardened)
  Options chosen by user:
    - Channel switching: FULL RE-INIT per channel (B)
    - Frame filtering: RAW (no filtering) (1)

  Fixes vs your original:
    - Safe uwb_poll(): guards against len < FCS_LEN and caps reads to buffer
    - Robust uwb_set_channel(): dwt_softreset + dwt_initialise + dwt_configure each switch
    - Minor correctness tweaks (counts, sizeof array elements, watchdog resets)
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_task_wdt.h"
#include <algorithm>
#include <cctype>

// ---- Include DW3000 last to avoid macro conflicts
#undef READ
#undef WRITE
#include "dwt_uwb_driver.h"

// ===== CONFIG =====
static const uint16_t SCAN_INTERVAL_MS = 2000;
static const uint16_t SCAN_WINDOW_MS   = 1200;
static const uint16_t SCAN_DURATION_S  = 20;
static const char*    TARGET_NAME_HINT = "";   // empty = accept all
static const uint32_t DEFAULT_PASSKEY  = 0000000000000000;    // 000000

// Wait times for UWB observation
static const uint16_t UWB_WAIT_TIME_MS             = 30000; // 30s per channel
static const uint16_t ACTIVATION_COMMAND_DELAY_MS  = 300;
static const uint16_t POST_ACTIVATION_HOLD_MS      = 20000;
static const bool     ENABLE_DEVICE_TESTING        = false;  // watchdog workaround: skip blocking BLE tests

// UWB pins for Makerfabs ESP32-UWB-DW3000
static const uint8_t UWB_PIN_RST = 27;
static const uint8_t UWB_PIN_IRQ = 34; // input only, fine
static const uint8_t UWB_PIN_SS  = 4;

// UWB globals
static bool     uwb_inited = false;
static uint8_t  uwb_rx_buffer[FRAME_LEN_MAX];
static const uint8_t UWB_CHANNELS[] = {5, 9};   // channels to test
static String   currentConnectedDevice = "";
static unsigned long connectionStartTime = 0;
static int      totalUwbPacketsReceived = 0;

// DW3000 config (base) – will override .chan dynamically
static dwt_config_t uwb_config = {
  /*chan*/           5,
  /*txPreambLength*/ DWT_PLEN_128,
  /*rxPAC*/          DWT_PAC8,
  /*txCode*/         9,
  /*rxCode*/         9,
  /*sfdType*/        1,                 // DWT_SFD_IEEE
  /*dataRate*/       DWT_BR_6M8,
  /*phrMode*/        DWT_PHRMODE_STD,
  /*phrRate*/        DWT_PHRRATE_STD,
  /*sfdTimeout*/     (129+8-8),         // preamble length + SFD length - PAC size (typical)
  /*stsMode*/        DWT_STS_MODE_OFF,
  /*stsLength*/      DWT_STS_LEN_64,
  /*pdoaMode*/       DWT_PDOA_M0
};

extern dwt_txconfig_t txconfig_options;
extern dwt_txconfig_t txconfig_options_ch9;

// ===== UWB Helpers =====
static void uwb_start_rx() {
  memset(uwb_rx_buffer, 0, sizeof(uwb_rx_buffer));
  dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static bool uwb_hw_init(uint8_t channel) {
  // SPI + select (once per init is fine; safe to call again)
  spiBegin(UWB_PIN_IRQ, UWB_PIN_RST);
  spiSelect(UWB_PIN_SS);
  delay(2);

  dwt_softreset();
  delay(2);

  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    Serial.println("[UWB] ❌ INIT FAILED");
    return false;
  }

  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

  uwb_config.chan = channel;
  if (channel == 9) {
    uwb_config.txCode = 10;
    uwb_config.rxCode = 10;
  } else {
    uwb_config.txCode = 9;
    uwb_config.rxCode = 9;
  }
  if (dwt_configure(&uwb_config)) {
    Serial.printf("[UWB] ❌ CONFIG FAIL (chan=%u)\n", channel);
    return false;
  }

  const dwt_txconfig_t *rf_cfg = (channel == 9) ? &txconfig_options_ch9 : &txconfig_options;
  dwt_txconfig_t txrf_local = *rf_cfg;
  dwt_configuretxrf(&txrf_local);

  uwb_start_rx();
  Serial.printf("[UWB] ✓ Passive RX started (chan=%u)\n", channel);
  return true;
}

static bool uwb_init() {
  bool ok = uwb_hw_init(/*channel*/ 5);
  if (!ok) return false;
  return true;
}

// Option B: FULL re-init on every channel switch
static bool uwb_set_channel(uint8_t channel) {
  dwt_forcetrxoff();
  delay(2);

  // Full reset & re-init to make the DW3000 happy on mode changes
  if (!uwb_hw_init(channel)) {
    Serial.printf("[UWB] ❌ Failed to set channel %u (re-init path)\n", channel);
    return false;
  }

  Serial.printf("[UWB] 📡 Switched to channel %u\n", channel);
  return true;
}

// Crash-proof poller
static void uwb_poll() {
  uint32_t st = dwt_read32bitreg(SYS_STATUS_ID);

  if (st & SYS_STATUS_RXFCG_BIT_MASK) {
    uint32_t finfo = dwt_read32bitreg(RX_FINFO_ID);
    uint16_t flen  = (uint16_t)(finfo & RX_FINFO_RXFLEN_BIT_MASK);

    // Require at least FCS bytes; drop runts/noise frames
    if (flen >= FCS_LEN) {
      uint16_t payload_len = (uint16_t)(flen - FCS_LEN);
      if (payload_len > FRAME_LEN_MAX) {
        // Truncate rather than overflow
        payload_len = FRAME_LEN_MAX;
      }

      if (payload_len > 0) {
        dwt_readrxdata(uwb_rx_buffer, payload_len, 0);

        totalUwbPacketsReceived++;
        unsigned long now = millis();
        unsigned long timeSinceConnect = (connectionStartTime > 0) ? (now - connectionStartTime) : 0;

        Serial.println();
        Serial.println("╔═════════════════════════════════════════════════════════════════");
        Serial.println("║ ⚡ UWB PACKET RECEIVED!");
        Serial.println("╠═════════════════════════════════════════════════════════════════");
        if (currentConnectedDevice.length() > 0) {
          Serial.printf("║ 📱 BLE Device: %s\n", currentConnectedDevice.c_str());
          Serial.printf("║ ⏱️  Time since connection: %lu ms\n", timeSinceConnect);
        }
        Serial.printf("║ 📊 Total packets received: %d\n", totalUwbPacketsReceived);
        Serial.printf("║ 📏 Length: %u bytes\n", payload_len);
        Serial.print("║ 🔢 RAW: ");
        for (uint16_t i = 0; i < payload_len; i++) {
          Serial.printf("%02X ", uwb_rx_buffer[i]);
        }
        Serial.println();

        bool isPrintable = true;
        for (uint16_t i = 0; i < payload_len; i++) {
          if (uwb_rx_buffer[i] < 32 || uwb_rx_buffer[i] > 126) {
            isPrintable = false;
            break;
          }
        }
        if (isPrintable) {
          Serial.print("║ 📝 ASCII: ");
          for (uint16_t i = 0; i < payload_len; i++) {
            Serial.write(uwb_rx_buffer[i]);
          }
          Serial.println();
        }

        Serial.println("╚═════════════════════════════════════════════════════════════════");
        Serial.println();
      }
    } else {
      // Optional: uncomment to see how often noise/runt frames arrive
      // Serial.println("[UWB] Runt frame discarded");
    }

    // Clear good RX flag and continue listening
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

  } else if (st & SYS_STATUS_ALL_RX_ERR) {
    // Clear all RX error bits and resume
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
  }
}

// ===== BLE Globals & Structures =====
static NimBLEScan* pScan = nullptr;

struct DeviceInfo {
  std::string addr;
  std::string name;
};
static std::vector<DeviceInfo> foundDevices;

// Scan CB: collect (optionally filtered) devices
class MyScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    std::string name = dev->getName();
    std::string addr = dev->getAddress().toString();

    bool shouldAdd = false;
    if (strlen(TARGET_NAME_HINT) == 0) {
      shouldAdd = true;
    } else {
      std::string nameLower = name;
      std::string hintLower = TARGET_NAME_HINT;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
      std::transform(hintLower.begin(), hintLower.end(), hintLower.begin(), ::tolower);
      if (nameLower.find(hintLower) != std::string::npos) shouldAdd = true;
    }

    if (shouldAdd) {
      bool alreadyFound = false;
      for (const auto& d : foundDevices) {
        if (d.addr == addr) { alreadyFound = true; break; }
      }

      if (!alreadyFound) {
        Serial.println();
        Serial.println("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.printf("┃ 📡 Found BLE Device!\n");
        Serial.printf("┃ MAC: %s\n", addr.c_str());
        if (name.length() > 0) Serial.printf("┃ Name: '%s'\n", name.c_str());
        else                   Serial.printf("┃ Name: (unnamed)\n");
        Serial.printf("┃ RSSI: %d dBm\n", dev->getRSSI());
        if (dev->haveServiceUUID()) {
          Serial.printf("┃ Services: ");
          for (int i = 0; i < dev->getServiceUUIDCount(); i++) {
            Serial.printf("%s ", dev->getServiceUUID(i).toString().c_str());
          }
          Serial.println();
        }
        Serial.println("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.println();

        DeviceInfo info;
        info.addr = addr;
        info.name = name.length() ? name : "(unnamed)";
        foundDevices.push_back(info);
      }
    }
  }
};

// (Not used but harmless; keeps NimBLE 2.x pairing paths available)
class MySecurityCB : public NimBLEServerCallbacks {
  uint32_t onPassKeyRequest() {
    Serial.printf("🔐 Passkey requested, returning: %06u\n", DEFAULT_PASSKEY);
    return DEFAULT_PASSKEY;
  }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) {
    if (desc->sec_state.encrypted) Serial.println("✅ Authentication successful (encrypted)");
    else                           Serial.println("⚠️ Authentication completed without encryption");
  }
  void onPassKeyNotify(uint32_t pass_key) {
    Serial.printf("🔐 Device passkey notification: %06u\n", pass_key);
  }
  bool onConfirmPIN(uint32_t pass_key) {
    Serial.printf("🔐 Confirm PIN: %06u - accepting\n", pass_key);
    return true;
  }
};

// Connect, enumerate, attempt activation, hold, then sweep channels
static void testDevice(const std::string &addr, const std::string &name) {
  int packetsBefore = totalUwbPacketsReceived;

  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════════════════════");
  Serial.printf("║ 🔄 TESTING DEVICE\n");
  Serial.printf("║ MAC: %s\n", addr.c_str());
  Serial.printf("║ Name: %s\n", name.c_str());
  Serial.println("╚═══════════════════════════════════════════════════════════════");
  Serial.println();

  NimBLEClient* client = NimBLEDevice::createClient();
  NimBLEAddress bleAddr(addr, BLE_ADDR_PUBLIC);

  Serial.printf("⏳ Connecting to %s...\n", addr.c_str());
  if (!client->connect(bleAddr)) {
    Serial.println("❌ Connection FAILED");
    Serial.println();
    NimBLEDevice::deleteClient(client);
    return;
  }

  Serial.println("✅ CONNECTED!");
  currentConnectedDevice = String(name.c_str()) + " (" + String(addr.c_str()) + ")";
  connectionStartTime = millis();

  // Enumerate services/characteristics
  Serial.println();
  Serial.println("═══ BLE SERVICE DISCOVERY ═══");
  const std::vector<NimBLERemoteService*>& services = client->getServices(true);
  if (!services.empty()) {
    Serial.printf("Found %d service(s):\n", (int)services.size());
    for (auto service : services) {
      esp_task_wdt_reset();
      Serial.println();
      Serial.printf("📦 Service: %s\n", service->getUUID().toString().c_str());

      const std::vector<NimBLERemoteCharacteristic*>& characteristics = service->getCharacteristics(true);
      if (!characteristics.empty()) {
        Serial.printf("   └─ %d characteristic(s):\n", (int)characteristics.size());
        for (auto characteristic : characteristics) {
          esp_task_wdt_reset();
          Serial.printf("      ├─ UUID: %s\n", characteristic->getUUID().toString().c_str());
          Serial.printf("      │  Properties: ");
          if (characteristic->canRead())            Serial.print("READ ");
          if (characteristic->canWrite())           Serial.print("WRITE ");
          if (characteristic->canWriteNoResponse()) Serial.print("WRITE_NR ");
          if (characteristic->canNotify())          Serial.print("NOTIFY ");
          if (characteristic->canIndicate())        Serial.print("INDICATE ");
          Serial.println();

          if (characteristic->canRead()) {
            esp_task_wdt_reset();
            try {
              std::string value = characteristic->readValue();
              Serial.printf("      │  Value (%d bytes): ", (int)value.length());
              for (size_t i = 0; i < value.length() && i < 32; i++) {
                Serial.printf("%02X ", (uint8_t)value[i]);
              }
              if (value.length() > 32) Serial.print("...");
              Serial.println();
              esp_task_wdt_reset();
            } catch (...) {
              Serial.println("      │  (Read failed)");
            }
          }

          if (characteristic->canNotify() || characteristic->canIndicate()) {
            esp_task_wdt_reset();
            try {
              if (characteristic->subscribe(true)) {
                Serial.println("      │  ✓ Subscribed to notifications");
              }
            } catch (...) {
              Serial.println("      │  (Subscribe failed)");
            }
          }
          esp_task_wdt_reset();
        }
      }
      esp_task_wdt_reset();
    }
  }
  Serial.println("═══ END SERVICE DISCOVERY ═══");
  Serial.println();

  bool connectionLost = false;

  // Attempt UWB activation via known service/char (if present)
  Serial.println("═══ ATTEMPTING UWB ACTIVATION ═══");
  NimBLERemoteService* k4wService = client->getService(NimBLEUUID("2e938fd0-6a61-11ed-a1eb-0242ac120002"));
  if (k4wService) {
    NimBLERemoteCharacteristic* writeChar =
      k4wService->getCharacteristic(NimBLEUUID("2e93998a-6a61-11ed-a1eb-0242ac120002"));
    if (writeChar && writeChar->canWrite()) {
      Serial.println("Found K4W write characteristic!");

      uint8_t activationCommands[][8] = {
        {0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x55,0xAA,0x01,0x00,0x00,0x00,0x00,0x00},
        {0xFF,0x01,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x01}, // single byte
        {0x53,0x54,0x41,0x52,0x54,0x00,0x00,0x00}, // "START"
      };

      for (int i = 0; i < 6; i++) {
        esp_task_wdt_reset();
        Serial.printf("🔧 Trying activation command %d...\n", i + 1);
        try {
          int len = (i == 4) ? 1 : 8;
          writeChar->writeValue(activationCommands[i], len, false);
          if (!client->isConnected()) {
            Serial.println("   ⚠️ Device disconnected during activation write");
            connectionLost = true;
            break;
          }

          unsigned long interDelayStart = millis();
          while (client->isConnected() &&
                 millis() - interDelayStart < ACTIVATION_COMMAND_DELAY_MS) {
            if (uwb_inited) uwb_poll();
            delay(25);
            esp_task_wdt_reset();
          }

          if (!client->isConnected()) {
            Serial.println("   ⚠️ Device disconnected before next command");
            connectionLost = true;
            break;
          }
          esp_task_wdt_reset();
        } catch (...) {
          Serial.printf("   Write failed\n");
        }
        if (connectionLost) break;
      }
      if (!connectionLost) {
        Serial.println("✅ Sent all activation commands");
      }
    } else {
      Serial.println("❌ Write characteristic not found or not writable");
    }
  } else {
    Serial.println("❌ K4W service not found");
  }
  Serial.println();

  if (!connectionLost && client->isConnected()) {
    Serial.printf("⏳ Holding connection for %u ms after activation writes...\n", POST_ACTIVATION_HOLD_MS);
    unsigned long holdStart = millis();
    while (client->isConnected() && millis() - holdStart < POST_ACTIVATION_HOLD_MS) {
      if (uwb_inited) uwb_poll();
      delay(50);
      esp_task_wdt_reset();
    }
    if (client->isConnected()) {
      Serial.println("✅ Post-activation hold complete");
    } else {
      Serial.println("⚠️ Device disconnected during post-activation hold");
      connectionLost = true;
    }
  }

  // Channel sweep only if still connected
  if (connectionLost || !client->isConnected()) {
    Serial.println("⚠️ Skipping UWB channel sweep because the device is no longer connected");
  } else {
    const size_t numChannels = sizeof(UWB_CHANNELS) / sizeof(UWB_CHANNELS[0]);
    Serial.printf("🔍 Testing UWB reception on %u channels (%u seconds per channel)...\n",
                  (unsigned)numChannels, (unsigned)(UWB_WAIT_TIME_MS / 1000));
    Serial.println();

    for (size_t ch_idx = 0; ch_idx < numChannels; ch_idx++) {
      uint8_t channel = UWB_CHANNELS[ch_idx];
      int packetsBeforeChannel = totalUwbPacketsReceived;

      Serial.printf("📡 Channel %u - listening...\n", channel);
      if (!uwb_set_channel(channel)) {
        Serial.printf("❌ Failed to switch to channel %u — skipping window\n", channel);
        continue;
      }

      unsigned long waitStart = millis();
      while (millis() - waitStart < UWB_WAIT_TIME_MS) {
        uwb_poll();
        delay(10);
        if (((millis() - waitStart) % 1000) < 20) esp_task_wdt_reset();
      }

      int packetsOnChannel = totalUwbPacketsReceived - packetsBeforeChannel;
      if (packetsOnChannel > 0) {
        Serial.printf("✅ Channel %u: Received %d packet(s)!\n", channel, packetsOnChannel);
      } else {
        Serial.printf("❌ Channel %u: No packets\n", channel);
      }
    }
  }

  int packetsReceived = totalUwbPacketsReceived - packetsBefore;

  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════════════════════");
  Serial.println("║ 📊 TEST RESULTS");
  Serial.println("╠═══════════════════════════════════════════════════════════════");
  Serial.printf("║ Device: %s\n", currentConnectedDevice.c_str());
  Serial.printf("║ UWB Packets Received: %d\n", packetsReceived);

  if (connectionLost || !client->isConnected()) {
    Serial.println("║ ");
    Serial.println("║ ⚠️ Test ended early because the BLE link dropped.");
    Serial.println("║    Results may be incomplete.");
  } else if (packetsReceived > 0) {
    Serial.println("║ ");
    Serial.println("║ ✅ SUCCESS! This device IS sending UWB packets!");
    Serial.println("║    Just connecting to it triggers UWB transmission!");
  } else {
    Serial.println("║ ");
    Serial.println("║ ❌ NO UWB packets detected from this device");
    Serial.println("║    Moving to next device...");
  }

  Serial.println("╚═══════════════════════════════════════════════════════════════");
  Serial.println();

  client->disconnect();
  NimBLEDevice::deleteClient(client);
  currentConnectedDevice = "";

  delay(2000);
}

// ===== Arduino setup/loop =====
void setup() {
  Serial.begin(115200);
  delay(2000);

  esp_task_wdt_add(NULL); // add current task to watchdog

  Serial.println();
  Serial.println("╔══════════════════════════════════════════════════════════════╗");
  Serial.println("║  K4W Tag BLE/UWB Discovery Tool - Enhanced Multi-Channel     ║");
  Serial.println("║  Enumerates BLE + Tests 7 UWB Channels (30s each)       v3.2 ║");
  Serial.println("╚══════════════════════════════════════════════════════════════╝");
  Serial.println();

  // Initialize UWB (channel 5 default)
  Serial.println("Initializing UWB...");
  uwb_inited = uwb_init();
  if (!uwb_inited) {
    Serial.println("❌ UWB initialization failed - continuing with BLE only");
  }
  Serial.println();

  // Initialize BLE with security
  NimBLEDevice::init("");
  NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityPasskey(DEFAULT_PASSKEY);

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new MyScanCB());
  pScan->setActiveScan(true);
  pScan->setInterval(SCAN_INTERVAL_MS);
  pScan->setWindow(SCAN_WINDOW_MS);

  if (strlen(TARGET_NAME_HINT) > 0) {
    Serial.printf("Scanning for devices with '%s' in name...\n", TARGET_NAME_HINT);
  } else {
    Serial.println("Scanning for ALL BLE devices...");
    Serial.println("⚠️ This will show every BLE device in range!");
    Serial.println("   To filter, edit TARGET_NAME_HINT in the code");
  }
  Serial.println();

  pScan->start(SCAN_DURATION_S, false);
}

void loop() {
  static int  deviceIndex = 0;
  static bool scanComplete = false;

  esp_task_wdt_reset();

  if (!scanComplete && !pScan->isScanning()) {
    scanComplete = true;
    Serial.println();
    Serial.printf("📡 Scan complete! Found %d device(s)\n", (int)foundDevices.size());

    if (!foundDevices.empty()) {
      Serial.println();
      Serial.println("═══════════════════════════════════════════════════════════");
      Serial.println("║ DISCOVERED DEVICES:");
      Serial.println("╠═══════════════════════════════════════════════════════════");
      for (size_t i = 0; i < foundDevices.size(); i++) {
        Serial.printf("║ [%d] %s\n", (int)i + 1, foundDevices[i].name.c_str());
        Serial.printf("║     MAC: %s\n", foundDevices[i].addr.c_str());
      }
      Serial.println("╚═══════════════════════════════════════════════════════════");
    }
    Serial.println();

    if (foundDevices.empty()) {
      Serial.println("❌ No BLE devices found!");
      Serial.println("   Rescanning in 10 seconds...");
      for (int i = 0; i < 10; i++) { delay(1000); esp_task_wdt_reset(); }
      deviceIndex = 0;
      scanComplete = false;
      pScan->clearResults();
      pScan->start(SCAN_DURATION_S, false);
      return;
    }

    Serial.println("⏳ Starting device testing in 5 seconds...");
    Serial.println("   (Press RESET to skip testing and rescan)");
    for (int i = 0; i < 5; i++) { delay(1000); esp_task_wdt_reset(); }
  }

  if (scanComplete && !ENABLE_DEVICE_TESTING && deviceIndex < (int)foundDevices.size()) {
    Serial.println("⚠️ Device testing temporarily disabled (watchdog workaround).");
    Serial.println("   Rescanning in 10 seconds...");
    for (int i = 0; i < 10; i++) { delay(1000); esp_task_wdt_reset(); }
    deviceIndex = 0;
    scanComplete = false;
    foundDevices.clear();
    pScan->clearResults();
    pScan->start(SCAN_DURATION_S, false);
    return;
  }

  if (scanComplete && deviceIndex < (int)foundDevices.size()) {
    auto& device = foundDevices[deviceIndex];

    Serial.println();
    Serial.printf("🔍 Testing device %d/%d\n", deviceIndex + 1, (int)foundDevices.size());
    Serial.println();

    testDevice(device.addr, device.name);

    deviceIndex++;

    if (deviceIndex >= (int)foundDevices.size()) {
      Serial.println();
      Serial.println("═══════════════════════════════════════════════════════════");
      Serial.printf("║ Tested all %d devices. Rescanning in 10 seconds...\n", (int)foundDevices.size());
      Serial.println("═══════════════════════════════════════════════════════════");
      Serial.println();

      for (int i = 0; i < 10; i++) { delay(1000); esp_task_wdt_reset(); }
      deviceIndex = 0;
      scanComplete = false;
      foundDevices.clear();
      pScan->clearResults();
      pScan->start(SCAN_DURATION_S, false);
    }
  }

  if (uwb_inited) {
    uwb_poll();
  }

  delay(50);
}
