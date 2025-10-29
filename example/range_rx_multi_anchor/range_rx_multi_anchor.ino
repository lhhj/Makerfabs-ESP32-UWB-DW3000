#include "dw3000.h"
#include "SPI.h"
#include <EEPROM.h>   // <-- added

extern SPISettings _fastSPI;

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4

// ----- original example constant (not used for slotting anymore) -----
// #define POLL_RX_TO_RESP_TX_DLY_UUS 450

// ----- multi-anchor slotting (keep these in sync with TAG) -----
#define BASE_REPLY_DLY_US  600U   // headroom after RX before any slot
#define SLOT_DELAY_US     2000U   // 2ms per slot
#define ID_MIN               1
#define ID_MAX              20

// ----- EEPROM ID storage -----
#define EEPROM_SIZE         64
#define EEPROM_ADDR_ID       0
#define DEFAULT_ANCHOR_ID   255    // invalid sentinel -> will fallback

/* Default communication configuration. We use default non-STS DW mode. */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* non-standard 8-symbol SFD */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

static uint8_t rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
// NOTE: we'll append 1 extra byte (ID) at send-time, so don't change this array size:
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[20];
static uint32_t status_reg = 0;
static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;

static uint8_t anchor_id = DEFAULT_ANCHOR_ID;

extern dwt_txconfig_t txconfig_options;

// --- helpers already provided by the MakerFabs library/examples ---
// uint64_t get_rx_timestamp_u64();
// void resp_msg_set_ts(uint8_t *ts_field, uint64_t ts);

#ifndef DWT_TIME_UNITS
#define DWT_TIME_UNITS (1.0/(499.2e6*128.0))
#endif
static inline uint64_t us_to_dwt(uint32_t us) {
  return (uint64_t)((double)us * 1e-6 / DWT_TIME_UNITS);
}

// ----- EEPROM helpers -----
static void save_id_and_reboot(uint8_t id) {
  EEPROM.write(EEPROM_ADDR_ID, id);
  EEPROM.commit();
  Serial.printf("[ANCHOR] Saved ID=%u (valid %d..%d). Rebooting...\r\n", id, ID_MIN, ID_MAX);
  delay(200);
  ESP.restart();
}

static uint8_t load_id_or_fallback() {
  uint8_t id = EEPROM.read(EEPROM_ADDR_ID);
  if (id >= ID_MIN && id <= ID_MAX) return id;
  // fallback hardcoded default if you want (set below); else pick 1
  return 1;
}

static void handle_serial_setid() {
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n'); s.trim();
  if (s.startsWith("SETID")) {
    int sp = s.indexOf(' ');
    if (sp > 0) {
      int v = s.substring(sp+1).toInt();
      if (v >= ID_MIN && v <= ID_MAX) {
        save_id_and_reboot((uint8_t)v);
      } else {
        Serial.printf("[ANCHOR] Invalid ID. Use %d..%d\r\n", ID_MIN, ID_MAX);
      }
    }
  }
}

void setup()
{
  UART_init();
  EEPROM.begin(EEPROM_SIZE);

  // quick “boot window” for SETID if user types early
  if (Serial.available()) handle_serial_setid();

  // Load ID (EEPROM) or fallback
  anchor_id = load_id_or_fallback();
  Serial.printf("[ANCHOR] Using ID=%u (valid %d..%d). Type 'SETID <n>' to change (auto-reboot).\r\n",
                anchor_id, ID_MIN, ID_MAX);

  _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);

  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);

  delay(2);

  while (!dwt_checkidlerc()) {
    UART_puts("IDLE FAILED\r\n");
    while (1);
  }

  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
    UART_puts("INIT FAILED\r\n");
    while (1);
  }

  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

  if (dwt_configure(&config)) {
    UART_puts("CONFIG FAILED\r\n");
    while (1);
  }

  dwt_configuretxrf(&txconfig_options);

  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);

  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  Serial.println("Range TX (ANCHOR)");
  Serial.println("Setup over........");
}

void loop()
{
  handle_serial_setid();

  // Activate RX
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  // Wait for POLL or error
  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) { }

  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
  {
    uint32_t frame_len;

    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

    frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer))
    {
      dwt_readrxdata(rx_buffer, frame_len, 0);

      // Validate POLL header
      rx_buffer[ALL_MSG_SN_IDX] = 0;
      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0)
      {
        uint32_t resp_tx_time32;
        int ret;

        // Poll RX timestamp
        poll_rx_ts = get_rx_timestamp_u64();

        // ---------- SLOT SCHEDULING ----------
        // total delay (us) = base + (ID-1)*slot
        uint32_t total_uus = (uint32_t)(BASE_REPLY_DLY_US + (uint32_t)(anchor_id - 1) * SLOT_DELAY_US);
        // Convert to device time units and program delayed TX (>>8 as per DW docs)
        resp_tx_time32 = (uint32_t)((poll_rx_ts + us_to_dwt(total_uus)) >> 8);
        dwt_setdelayedtrxtime(resp_tx_time32);

        // Response TX timestamp (scheduled + antenna delay)
        resp_tx_ts = (((uint64_t)(resp_tx_time32 & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        // Fill timestamps in the original response frame
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

        // (1) Put ANCHOR ID inside byte [2] of the original frame
        tx_resp_msg[ALL_MSG_SN_IDX] = anchor_id;

        // (2) Keep timestamps already written above
        // NOTHING else changes in tx_resp_msg layout

        // (3) Transmit using original length (no extra byte!)
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);
        ret = dwt_starttx(DWT_START_TX_DELAYED);

        // Append Anchor ID as LAST byte
        //uint8_t resp_len = sizeof(tx_resp_msg);
        //uint8_t tx_buf[sizeof(tx_resp_msg) + 1];
        //memcpy(tx_buf, tx_resp_msg, resp_len);
        //tx_buf[ALL_MSG_SN_IDX] = frame_seq_nb;     // keep seq
        //tx_buf[resp_len++] = anchor_id;           // <-- appended ID

        //dwt_writetxdata(resp_len, tx_buf, 0);
        //dwt_writetxfctrl(resp_len, 0, 1);
        //ret = dwt_starttx(DWT_START_TX_DELAYED);

        if (ret == DWT_SUCCESS)
        {
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { }
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
          frame_seq_nb++;

          Serial.printf("[ANCHOR %u] RESP sent at +%u us\r\n", anchor_id, total_uus);
        }
        else
        {
          Serial.printf("[ANCHOR %u] Missed delayed TX window (slot=%u us)\r\n", anchor_id, total_uus);
        }
      }
    }
  }
  else
  {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}
