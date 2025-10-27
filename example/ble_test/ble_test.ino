/* Simple BLE Scanner Test
   This is a minimal scanner to verify BLE is working
   and show ALL devices in range
*/

#include <NimBLEDevice.h>

class SimpleScanCallback : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    Serial.printf("[DEVICE] %s | RSSI: %d dBm | Name: '%s'\n", 
                  dev->getAddress().toString().c_str(),
                  dev->getRSSI(),
                  dev->getName().c_str());
    
    // Show service count
    Serial.printf("  Services: %d\n", dev->getServiceUUIDCount());
    
    // Show manufacturer data if present
    if(dev->haveManufacturerData()){
      std::string mfg = dev->getManufacturerData();
      Serial.printf("  Manufacturer data (%d bytes): ", mfg.length());
      for(size_t i = 0; i < mfg.length() && i < 20; i++){
        Serial.printf("%02X ", (uint8_t)mfg[i]);
      }
      Serial.println();
    }
    Serial.println();
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║  Simple BLE Scanner Test              ║");
  Serial.println("║  Scanning for ALL BLE devices...      ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  NimBLEDevice::init("");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new SimpleScanCallback());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  
  Serial.println("Starting 30-second scan...\n");
  pScan->start(30, false);
  Serial.println("\n✓ Scan complete! Restarting scan...\n");
}

void loop() {
  // Restart scan every 30 seconds
  static unsigned long lastScan = 0;
  if(millis() - lastScan > 30000){
    lastScan = millis();
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->start(30, false);
  }
  delay(100);
}
