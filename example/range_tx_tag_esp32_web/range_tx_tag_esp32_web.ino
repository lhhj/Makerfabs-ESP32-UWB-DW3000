#include "dw3000.h"

#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

#define RNG_DELAY_MS 1000
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

#define ALL_MSG_COMMON_LEN 10
#define ALL_MSG_SN_IDX 2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4
#define POLL_TX_TO_RESP_RX_DLY_UUS 240
#define RESP_RX_TIMEOUT_UUS 120000

// ===== Added for multi-anchor =====
#define MAX_ANCHORS 20
#define MULTICOLLECT_WINDOW_MS 60 // collect replies for 60 ms per pool

static float distances_m[MAX_ANCHORS];
static uint32_t last_seen_ms[MAX_ANCHORS];

// Keep your existing config EXACTLY as-is
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* non-standard SFD */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

// Frames from the original example (unchanged)
static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// NOTE: we will compare only the first ALL_MSG_COMMON_LEN bytes of rx_resp_msg,
// and then allow an extra trailing byte at the end which is Anchor ID.

static uint8_t frame_seq_nb = 0;

// Bumped buffer a bit to allow the extra anchor ID byte
static uint8_t rx_buffer[32];

static uint32_t status_reg = 0;
static double tof;
static double distance;
extern dwt_txconfig_t txconfig_options;


// If SPEED_OF_LIGHT and DWT_TIME_UNITS aren’t already defined by your libs, define them:
#ifndef SPEED_OF_LIGHT
#define SPEED_OF_LIGHT 299702547.0
#endif
#ifndef DWT_TIME_UNITS
#define DWT_TIME_UNITS (1.0/ (499.2e6 * 128.0))
#endif

void setup()
{
  // Clear arrays
  for (int i = 0; i < MAX_ANCHORS; i++) { distances_m[i] = 0.0f; last_seen_ms[i] = 0; }

  UART_init();

  spiBegin(PIN_IRQ, PIN_RST);
  spiSelect(PIN_SS);

  delay(2); // Time needed for DW3000 to start up

  while (!dwt_checkidlerc())
  {
    UART_puts("IDLE FAILED\r\n");
    while (1) ;
  }

  if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
  {
    UART_puts("INIT FAILED\r\n");
    while (1) ;
  }

  // Keep LED behavior same as your working example
  dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

  // Configure DW as in original
  if (dwt_configure(&config))
  {
    UART_puts("CONFIG FAILED\r\n");
    while (1) ;
  }

  dwt_configuretxrf(&txconfig_options);

  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);

  dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
  dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);

  dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

  Serial.println("Range RX (TAG) — multi-anchor enabled");
  Serial.println("Setup over........");
}
void loop()
{
  // --- Send POLL (unchanged) ---
  tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
  dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);  // offset 0
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);           // ranging frame
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

  // We let the DW3000 auto-enable RX after the configured delay,
  // but we also plan to re-enable RX after each frame for a full window.
  const uint32_t t_start = millis();
  bool first_frame_seen = false;

  // Collect for a fixed window
  while ((millis() - t_start) < MULTICOLLECT_WINDOW_MS)
  {
    // Poll for good RX frame or error/timeout
    uint32_t st;
    do {
      st = dwt_read32bitreg(SYS_STATUS_ID);
    } while (!(st & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))
             && ((millis() - t_start) < MULTICOLLECT_WINDOW_MS));

    // If window expired, break
    if ((millis() - t_start) >= MULTICOLLECT_WINDOW_MS) break;

    if (st & SYS_STATUS_RXFCG_BIT_MASK)
    {
      // --- A frame arrived ---
      dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);

      uint16_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
      if (frame_len > sizeof(rx_buffer)) frame_len = sizeof(rx_buffer);
      dwt_readrxdata(rx_buffer, frame_len, 0);

      // Validate header against common part only
      //rx_buffer[ALL_MSG_SN_IDX] = 0;
      //bool header_ok = (frame_len >= ALL_MSG_COMMON_LEN) &&
      //                 (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0);
      // --- read anchor ID BEFORE zeroing ---
      uint8_t anchor_id = rx_buffer[ALL_MSG_SN_IDX];

      // --- now zero it for header compare as before ---
      rx_buffer[ALL_MSG_SN_IDX] = 0;

      bool header_ok = (frame_len >= ALL_MSG_COMMON_LEN) &&
                      (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0);
      if (header_ok)
      {
        // Anchor ID is the LAST byte (appended by anchors)
        //uint8_t anchor_id = 255;
        //if (frame_len > ALL_MSG_COMMON_LEN) {
        //  anchor_id = rx_buffer[frame_len - 1];
        //}

        // ---- SS-TWR math (unaltered from your working example) ----
        uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
        int32_t rtd_init, rtd_resp;
        float clockOffsetRatio;

        // Read local TX/RX timestamps
        poll_tx_ts = dwt_readtxtimestamplo32();
        resp_rx_ts = dwt_readrxtimestamplo32();

        clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

        // Extract remote timestamps from the frame (same indices)
        resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
        resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

        rtd_init = resp_rx_ts - poll_tx_ts;
        rtd_resp = resp_tx_ts - poll_rx_ts;

        tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
        distance = tof * SPEED_OF_LIGHT;

        // Print per-anchor as it arrives (PRINT A)
        if (anchor_id < MAX_ANCHORS) {
          distances_m[anchor_id] = (float)distance;
          last_seen_ms[anchor_id] = millis();
          Serial.print("[TAG] A");
          Serial.print(anchor_id);
          Serial.print(" = ");
          Serial.print(distance, 2);
          Serial.println(" m");
        } else {
          char dist_str[32];
          snprintf(dist_str, sizeof(dist_str), "DIST: %3.2f m", distance);
          test_run_info((unsigned char *)dist_str);
        }

        first_frame_seen = true;
      }

      // Re-enable RX immediately to keep collecting
      dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    else
    {
      // Error or timeout — clear and keep listening until the window ends
      dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
      dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
  }

  // Done with this poll round
  frame_seq_nb++;

  // Small gap between polls if you like (optional)
  Sleep(RNG_DELAY_MS);
}
