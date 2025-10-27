/*!
 *  @file    uwb_sts_detector.ino
 *  @brief   Passive UWB STS Burst Detector for DW3000 + KKM K4W
 *
 *  Detects encrypted IEEE 802.15.4z STS bursts (no payload decode).
 *  Prints RSSI and timestamp when a burst is received.
 */

#include "dwt_uwb_driver.h"

#define APP_NAME "UWB STS DETECTOR v1.0"

// ESP32 pins
const uint8_t PIN_RST = 27;
const uint8_t PIN_IRQ = 34;
const uint8_t PIN_SS  = 4;

// DW3000 config for STS detection
static dwt_config_t config = {
    9,                  // Channel (KKM tags usually use 9)
    DWT_PLEN_128,       // Preamble
    DWT_PAC8,
    9, 9,
    3,                  // 4z 8-symbol SDF
    DWT_BR_6M8,
    DWT_PHRMODE_EXT,
    DWT_PHRRATE_STD,
    (129 + 8 - 8),
    DWT_STS_MODE_ND,    // <== STS No-Data detect mode
    DWT_STS_LEN_256,    // longer window
    DWT_PDOA_M0
};

static uint32_t total_hits = 0;
static uint32_t last_stats = 0;
#define STATS_INTERVAL_MS 10000

void setup()
{
    UART_init();
    test_run_info((unsigned char *)APP_NAME);
    Serial.println("=== UWB STS Burst Detector ===");

    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(200);

    while (!dwt_checkidlerc()) delay(100);

    dwt_softreset();
    delay(200);

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        Serial.println("ERROR: DW3000 init failed");
        while (1) delay(1000);
    }

    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    if (dwt_configure(&config)) {
        Serial.println("ERROR: configure failed");
        while (1) delay(1000);
    }

    // enable STS detect interrupt
    dwt_write32bitreg(SYS_MASK_ID, SYS_STATUS_RXSTSE_BIT_MASK);

    Serial.println("Listening for STS bursts on channel 9...");
    Serial.println("-------------------------------------------------------------");

    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    last_stats = millis();
}

void loop()
{
    uint32_t status = dwt_read32bitreg(SYS_STATUS_ID);

    if (status & SYS_STATUS_RXSTSE_BIT_MASK) {
        dwt_rxdiag_t diag;
        dwt_readdiagnostics(&diag);

        float rssi_f = 10.0 * log10((float)diag.ipatovPower) - 113.77;
        Serial.print("[");
        Serial.print(millis());
        Serial.print(" ms] STS DETECT | RSSI ≈ ");
        Serial.print(rssi_f, 1);
        Serial.println(" dBm");
        total_hits++;

        // clear flag
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXSTSE_BIT_MASK);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }

    if (millis() - last_stats >= STATS_INTERVAL_MS) {
        Serial.print("\n=== STATS ===  Detections: ");
        Serial.println(total_hits);
        Serial.println("============================\n");
        last_stats = millis();
    }

    delay(1);
}
