// Tag_B_only_debug.ino — DW3000 initiator (TAG) with ultra-forgiving RX to catch Anchor B @ ~3 ms
#include "dw3000.h"

// ===== Pins (Makerfabs ESP32-S3 UWB) =====
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

// ===== Debug receive strategy =====
// We'll *not* rely on the chip's RX-after-TX timing; we'll force RX on and
// poll for up to 10 ms, clearing transient RX errors as we go.
#define SW_COLLECT_US 10000UL   // 10 ms software window

// ===== Radio config (must match anchors) =====
static dwt_config_t config = {
  5,                 // channel
  DWT_PLEN_128,      // preamble length
  DWT_PAC8,          // PAC
  9,                 // TX preamble code
  9,                 // RX preamble code
  1,                 // SFD type (non-standard 8 symbols)
  DWT_BR_6M8,        // data rate 6.8 Mbps
  DWT_PHRMODE_STD,   // PHY header mode
  DWT_PHRRATE_STD,   // PHY header rate
  (129 + 8 - 8),     // SFD timeout
  DWT_STS_MODE_OFF,  // STS disabled
  DWT_STS_LEN_64,    // STS length
  DWT_PDOA_M0        // PDOA off
};

// ===== Frames =====
static uint8_t tx_poll_msg[] = {0x41,0x88,0,0xCA,0xDE,'W','A','V','E',0xE0,0,0}; // "WAVE", 0xE0
static uint8_t rx_resp_hdr[] = {0x41,0x88,0,0xCA,0xDE,'V','E','W','A',0xE1,0,0}; // "VEWA", 0xE1

// ===== Indices =====
#define ALL_MSG_COMMON_LEN          10
#define ALL_MSG_SN_IDX              2
#define RESP_MSG_POLL_RX_TS_IDX     10
#define RESP_MSG_RESP_TX_TS_IDX     14
#define RESP_MSG_TS_LEN             4
#define ANCHOR_ID_IDX               18     // anchor writes its ID here (B = 0xB2)

// ===== Buffers/State =====
static uint8_t rx_buffer[64];
static uint8_t seq = 0;
extern dwt_txconfig_t txconfig_options;

static inline void resp_msg_get_ts(const uint8_t *p, uint32_t *ts) {
  *ts = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void setup() {
  UART_init();
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.println("\n=== TAG: B-only debug — force RX, 10 ms software window ===");

  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  delay(2);

  while (!dwt_checkidlerc()) { Serial.println("IDLE FAILED"); while(1){} }
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { Serial.println("INIT FAILED"); while(1){} }

  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  if (dwt_configure(&config)) { Serial.println("CONFIG FAILED"); while(1){} }

  dwt_configuretxrf(&txconfig_options);
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  // Disable HW RX timeout — we'll keep RX open and poll in software.
  dwt_setrxtimeout(0);

  Serial.println("Config OK: ch=5, PRL=128, code=9/9, SFD=1, 6.8M");
  Serial.println("Expect Anchor B reply ~3 ms after poll (ID byte at index 18 = 0xB2)");
}

void loop() {
  // --- prepare poll ---
  tx_poll_msg[ALL_MSG_SN_IDX] = seq++;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);

  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);

  Serial.println("\nTAG: TX poll → forcing RX and waiting up to 10 ms");
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  // Force RX on immediately (safety net)
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  // --- software wait: up to 10 ms for any good frame ---
  uint32_t st = 0;
  unsigned long t0 = micros();
  bool got = false;

  while ((micros() - t0) < SW_COLLECT_US) {
    st = dwt_read32bitreg(SYS_STATUS_ID);

    if (st & SYS_STATUS_RXFCG_BIT_MASK) {  // good frame
      got = true;
      break;
    }

    // Clear transient RX errors and keep listening
    if (st & SYS_STATUS_ALL_RX_ERR) {
      dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
      dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
  }

  if (!got) {
    Serial.print("SW TIMEOUT; SYS_STATUS=0x");
    Serial.println((unsigned long)dwt_read32bitreg(SYS_STATUS_ID), HEX);
    delay(500);
    return;
  }

  // --- got a good frame ---
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

  uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
  if (frame_len > sizeof(rx_buffer)) frame_len = sizeof(rx_buffer);
  dwt_readrxdata(rx_buffer, frame_len, 0);

  Serial.print("RX len="); Serial.println(frame_len);
  for (uint32_t i = 0; i < frame_len; i++) {
    if (rx_buffer[i] < 16) Serial.print('0');
    Serial.print(rx_buffer[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  if (frame_len > ANCHOR_ID_IDX) {
    Serial.print("byte[18]=0x"); Serial.println(rx_buffer[ANCHOR_ID_IDX], HEX);
  }

  // --- If header matches expected responder, compute distance ---
  if (frame_len >= 20) {
    // ignore sequence when matching header
    uint8_t hdr_cmp[ALL_MSG_COMMON_LEN];
    memcpy(hdr_cmp, rx_buffer, ALL_MSG_COMMON_LEN);
    hdr_cmp[ALL_MSG_SN_IDX] = 0;

    if (memcmp(hdr_cmp, rx_resp_hdr, ALL_MSG_COMMON_LEN) == 0) {
      // Local timestamps
      uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
      uint32_t resp_rx_ts = dwt_readrxtimestamplo32();

      // Remote timestamps from payload
      uint32_t poll_rx_ts, resp_tx_ts;
      resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
      resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

      // Drift compensation
      float clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

      int32_t rtd_init = (int32_t)(resp_rx_ts - poll_tx_ts);
      int32_t rtd_resp = (int32_t)(resp_tx_ts - poll_rx_ts);

      double tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
      double dist = tof * SPEED_OF_LIGHT;

      // Classify by anchor's programmed delay (helpful sanity print)
      int32_t dt_ticks = (int32_t)(resp_tx_ts - poll_rx_ts);
      double  dt_uus   = dt_ticks * DWT_TIME_UNITS * 1e6;

      Serial.print("anchor_dt≈"); Serial.print((int)dt_uus); Serial.println(" us");

      uint8_t anchor_id = (frame_len > ANCHOR_ID_IDX) ? rx_buffer[ANCHOR_ID_IDX] : 0xFF;
      Serial.print("DIST from A ");
      Serial.print(anchor_id, HEX);
      Serial.print(": ");
      Serial.print(dist, 2);
      Serial.println(" m");
    } else {
      Serial.println("Frame header didn't match expected responder (VEWA/E1).");
    }
  }

  delay(500); // ~2 Hz while debugging
}
