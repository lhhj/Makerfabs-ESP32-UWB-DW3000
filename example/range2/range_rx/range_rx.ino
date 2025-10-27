#include "dw3000.h"
#include "SPI.h"

extern SPISettings _fastSPI;

// Pins (Makerfabs ESP32-S3 UWB)
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

// Common config
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define ALL_MSG_COMMON_LEN          10
#define ALL_MSG_SN_IDX              2
#define RESP_MSG_POLL_RX_TS_IDX     10
#define RESP_MSG_RESP_TX_TS_IDX     14
#define RESP_MSG_TS_LEN             4
#define ANCHOR_ID_IDX               18
#define ANCHOR_ID                   0xA1   // Anchor A ID
#define POLL_RX_TO_RESP_TX_DLY_UUS  240    // A replies earlier (aligned to tag's 240 µs)

static dwt_config_t config = {
  5, DWT_PLEN_128, DWT_PAC8, 9, 9,
  1, DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
  (129 + 8 - 8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};

static uint8_t rx_poll_msg[] = {0x41,0x88,0,0xCA,0xDE,'W','A','V','E',0xE0,0,0};
static uint8_t tx_resp_msg[] = {0x41,0x88,0,0xCA,0xDE,'V','E','W','A',0xE1,0,0,0,0,0,0,0,0,0,0};

static uint8_t  frame_seq_nb = 0;
static uint8_t  rx_buffer[20];
static uint32_t status_reg = 0;
static uint64_t poll_rx_ts, resp_tx_ts;
extern dwt_txconfig_t txconfig_options;

void setup() {
  UART_init();
  _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);

  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);
  delay(2);

  while (!dwt_checkidlerc()) { UART_puts("IDLE FAILED\r\n"); while(1){} }
  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { UART_puts("INIT FAILED\r\n"); while(1){} }

  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
  if (dwt_configure(&config)) { UART_puts("CONFIG FAILED\r\n"); while(1){} }

  dwt_configuretxrf(&txconfig_options);
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  Serial.begin(115200);
  while(!Serial){ delay(10); }
  Serial.println("Anchor A up");
  Serial.println("ID=0xA1, reply @ 240us");
}

void loop() {
  dwt_rxenable(DWT_START_RX_IMMEDIATE);

  while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
           (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {}

  if (status_reg & SYS_STATUS_RXFCG_BIT_MASK) {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len <= sizeof(rx_buffer)) {
      dwt_readrxdata(rx_buffer, frame_len, 0);
      rx_buffer[ALL_MSG_SN_IDX] = 0;

      if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0) {
        poll_rx_ts = get_rx_timestamp_u64();

        uint32_t resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
        dwt_setdelayedtrxtime(resp_tx_time);

        resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
        resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

        // Embed our Anchor ID at byte 18
        tx_resp_msg[ANCHOR_ID_IDX] = ANCHOR_ID;

        tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
        dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);

        if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
          while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) {}
          dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
          frame_seq_nb++;
        }
      }
    }
  } else {
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
  }
}
