/* BLE + UWB probe for K4W (ESP32-core NimBLE)
   - conservative probing: enumerate writable characteristics,
     try a short list of safe payloads, watch for UWB frames.
   - Use with care. Do not spam.
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "dw3000.h"

// Config
static const uint16_t SCAN_INTERVAL_MS = 2000;
static const uint16_t SCAN_WINDOW_MS   = 1200;
static const uint16_t SCAN_DURATION_S  = 10;
static const char* TARGET_NAME_HINT = "K4W_UWP ";
static const char* TARGET_ADDR = ""; // optional: set MAC to target, else filter by name

// UWB (same as your previous config)
static const uint8_t UWB_PIN_RST = 27;
static const uint8_t UWB_PIN_IRQ = 34;
static const uint8_t UWB_PIN_SS  = 4;
static bool uwb_inited = false;
static uint8_t uwb_rx_buffer[FRAME_LEN_MAX];
static dwt_config_t uwb_config = {
  9, DWT_PLEN_128, DWT_PAC8, 9,9,1, DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
  (129+8-8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static void uwb_start_rx(){ memset(uwb_rx_buffer,0,sizeof(uwb_rx_buffer)); dwt_rxenable(DWT_START_RX_IMMEDIATE); }
static bool uwb_init(){ spiBegin(UWB_PIN_IRQ,UWB_PIN_RST); spiSelect(UWB_PIN_SS); delay(200); dwt_softreset(); delay(200);
  if(dwt_initialise(DWT_DW_INIT)==DWT_ERROR){ Serial.println("[UWB] INIT FAILED"); return false; }
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  if(dwt_configure(&uwb_config)){ Serial.println("[UWB] CONFIG FAIL"); return false;}
  uwb_start_rx(); Serial.println("[UWB] Passive RX started"); return true; }
static void uwb_poll(){
  uint32_t st = dwt_read32bitreg(SYS_STATUS_ID);
  if(st & SYS_STATUS_RXFCG_BIT_MASK){
    uint16_t len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_BIT_MASK;
    if(len && len <= FRAME_LEN_MAX){
      dwt_readrxdata(uwb_rx_buffer, len - FCS_LEN, 0);
      Serial.print("[UWB RAW] ");
      for(int i=0;i<len-FCS_LEN;i++) Serial.printf("%02X ", uwb_rx_buffer[i]);
      Serial.println();
    }
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
  } else if(st & SYS_STATUS_ALL_RX_ERR){
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
  }
}

// NimBLE globals
static NimBLEScan* pScan = nullptr;
static std::string queuedAddr="";
static bool queued=false;

// candidates (small, safe set)
static const uint8_t payloads[][4] = {
  {0x01,0x00,0x00,0x00},
  {0x01,0x01,0x00,0x00},
  {0xA5,0x5A,0x00,0x00},
  {0xFF,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00},
};
static const size_t payloadLens[] = {1,2,2,1,1};

// some ascii tries (C strings)
static const char* asciiCmds[] = {"START","BEGIN","RUN","ON","GO"};

// callback to detect target
class MyScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    std::string name = dev->getName();
    std::string addr = dev->getAddress().toString();
    Serial.printf("[ADV] %s name='%s' rssi=%d\n", addr.c_str(), name.c_str(), dev->getRSSI());
    if(queued) return;
    if(strlen(TARGET_ADDR)>0){
      if(addr==TARGET_ADDR){ queuedAddr=addr; queued=true; if(pScan->isScanning()) pScan->stop(); }
    }else{
      if(name.find(TARGET_NAME_HINT)!=std::string::npos){ queuedAddr=addr; queued=true; if(pScan->isScanning()) pScan->stop(); }
    }
  }
};

static void enumerateAndProbe(const std::string &addr) {
  Serial.printf("[PROBE] connect to %s\n", addr.c_str());
  NimBLEClient* client = NimBLEDevice::createClient();
  NimBLEDevice::setMTU(247);
  if(!client->connect(NimBLEAddress(addr, BLE_ADDR_PUBLIC))){ Serial.println("[PROBE] connect failed"); return; }
  Serial.println("[PROBE] connected: discovering services...");
  const auto &svcs = client->getServices();
  struct Writable { NimBLERemoteCharacteristic* ch; std::string uuid; };
  std::vector<Writable> writableChars;

  for(auto* s : svcs){
    Serial.printf(" SVC %s\n", s->getUUID().toString().c_str());
    const auto &chs = s->getCharacteristics();
    for(auto* ch : chs){
      Serial.printf("   CH %s\n", ch->getUUID().toString().c_str());
      // check if write property present
      if(ch->canWrite() || ch->canWriteNoResponse()){
        Writable w; w.ch = ch; w.uuid = ch->getUUID().toString();
        writableChars.push_back(w);
      }
    }
  }

  if(writableChars.empty()){
    Serial.println("[PROBE] No writable characteristics found (device may expose service after init). Disconnecting.");
    client->disconnect();
    return;
  }

  // For each writable characteristic, send small set of payloads
  for(auto &w : writableChars){
    Serial.printf("[PROBE] Trying on %s\n", w.uuid.c_str());
    // small binary payloads
    for(size_t pi=0; pi < sizeof(payloads)/sizeof(payloads[0]); ++pi){
      size_t len = payloadLens[pi];
      Serial.printf(" write binary (%zu bytes): ", len);
      for(size_t b=0;b<len;b++) Serial.printf("%02X ", payloads[pi][b]);
      Serial.println();
      try {
        w.ch->writeValue(payloads[pi], len, true); // with response
      } catch (...) { Serial.println(" write error"); }
      delay(1000); // wait and watch UWB
    }
    // small ascii tries if char accepts string (try but be conservative)
    for(size_t ai=0; ai < sizeof(asciiCmds)/sizeof(asciiCmds[0]); ++ai){
      Serial.printf(" write ascii: %s\n", asciiCmds[ai]);
      try { w.ch->writeValue((uint8_t*)asciiCmds[ai], strlen(asciiCmds[ai]), true); } catch (...) { Serial.println(" write error"); }
      delay(1000);
    }
  }

  Serial.println("[PROBE] done: disconnecting and resuming scan");
  client->disconnect();
}

void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println("Probe starting...");

  NimBLEDevice::init("");
  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new MyScanCB());
  pScan->setActiveScan(true);
  pScan->setInterval(SCAN_INTERVAL_MS);
  pScan->setWindow(SCAN_WINDOW_MS);
  pScan->start(SCAN_DURATION_S, false);

  // uwb
  uwb_inited = uwb_init();
}

void loop(){
  if(queued){
    // do the probe sequence (blocking) and resume scanning
    std::string addr = queuedAddr;
    queued=false;
    enumerateAndProbe(addr);
    // small cooldown and restart scan
    delay(2000);
    pScan->clearResults();
    pScan->start(SCAN_DURATION_S, false);
  }

  uwb_poll(); // prints RAW if frames arrive
  delay(50);
}
