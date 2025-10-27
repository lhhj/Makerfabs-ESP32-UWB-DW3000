/*!
 *  @file    uwb_rssi_monitor_sts_sniffer.ino
 *  @brief   Passive UWB STS energy / RSSI monitor for Makerfabs DW3000
 *
 *  Detects UWB bursts from STS-enabled (802.15.4z) tags such as K4W_nRF52xx.
 *  Prints noise-floor RSSI every second and signal RSSI when detected.
 */

#include "dw3000.h"

#define APP_NAME "UWB RSSI MONITOR (STS SNIFFER)"
#define SERIAL_BAUD 115200

// Makerfabs pin mapping
const uint8_t PIN_RST = 27;
const uint8_t PIN_IRQ = 34;
const uint8_t PIN_SS  = 4;

// ----- DW3000 configuration (802.15.4z sniff mode) -----
static dwt_config_t config = {
    5,                 // Channel (try 9 if you see nothing)
    DWT_PLEN_256,      // Longer preamble helps detect STS frames
    DWT_PAC16,         // Larger PAC for STS bursts
    9,                 // TX preamble code
    9,                 // RX preamble code
    1,                 // SFD type (standard)
    DWT_BR_6M8,        // 6.8 Mb/s
    DWT_PHRMODE_STD,
    DWT_PHRRATE_STD,
    (129 + 8 - 8),     // SFD timeout
    DWT_STS_MODE_ND,   // 🔹 STS sniff mode (non-deterministic)
    DWT_STS_LEN_64,    // 64-symbol STS
    DWT_PDOA_M0
};

// Timing
uint32_t total_hits = 0;
uint32_t last_stats = 0;
uint32_t last_noise_print = 0;
#define STATS_INTERVAL_MS 10000
#define NOISE_INTERVAL_MS 1000

// ----------------------------------------------------
void setup()
{
    Serial.begin(SERIAL_BAUD);
    UART_init();
    test_run_info((unsigned char *)APP_NAME);
    Serial.println("=== UWB STS RSSI Monitor (Makerfabs DW3000) ===");

    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(200);

    while (!dwt_checkidlerc()) delay(100);
    dwt_softreset();
    delay(200);

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) {
        Serial.println("ERROR: init failed");
        while (1) delay(1000);
    }

    dwt_setxtaltrim(8);
    delay(50);
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    if (dwt_configure(&config)) {
        Serial.println("ERROR: configure failed");
        while (1) delay(1000);
    }

    // Enable diagnostic recording
    dwt_configure_rx_diagnostics(DWT_DIAG_ALL);

    Serial.println("DW3000 ready – listening in 802.15.4z STS sniff mode");
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    last_stats = millis();
    last_noise_print = millis();
}

// ----------------------------------------------------
void loop()
{
    uint32_t status = dwt_read32bitreg(SYS_STATUS_ID);

    // If a frame or RX error occurred, grab diagnostics
    if (status & (SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR)) {
        dwt_rxdiag_t diag;
        dwt_readdiagnostics(&diag);

        float rssi_f = 10.0 * log10((float)diag.ipatovPower + 1) - 113.77;

        Serial.print("[");
        Serial.print(millis());
        Serial.print(" ms]  Burst detected (STS) | RSSI ≈ ");
        Serial.print(rssi_f, 1);
        Serial.println(" dBm");

        total_hits++;

        // Clear RX status bits and restart receiver
        dwt_write32bitreg(SYS_STATUS_ID,
                          SYS_STATUS_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }

    // Periodic noise-floor measurement
    if (millis() - last_noise_print >= NOISE_INTERVAL_MS) {
        dwt_rxdiag_t diag;
        dwt_readdiagnostics(&diag);
        float rssi_f = 10.0 * log10((float)diag.ipatovPower + 1) - 113.77;
        Serial.print("[");
        Serial.print(millis());
        Serial.print(" ms]  Noise floor ≈ ");
        Serial.print(rssi_f, 1);
        Serial.println(" dBm");
        last_noise_print = millis();
    }

    // Periodic summary
    if (millis() - last_stats >= STATS_INTERVAL_MS) {
        Serial.print("\n=== STATS ===  Total detections: ");
        Serial.println(total_hits);
        Serial.println("============================\n");
        last_stats = millis();
    }

    delay(1);
}
