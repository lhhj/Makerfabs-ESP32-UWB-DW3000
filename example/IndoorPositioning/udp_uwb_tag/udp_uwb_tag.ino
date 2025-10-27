/*

For ESP32 UWB with DW3000 chip
Adapted to use local DW3000 driver with channel 5 support

*/

#include <SPI.h>
#include "dwt_uwb_driver.h"
#include <WiFi.h>
#include "link.h"

// ===== Pins (Makerfabs ESP32 UWB) =====
#define PIN_RST 27
#define PIN_IRQ 34
#define PIN_SS  4

// ===== UWB Configuration =====
static dwt_config_t config = {
  5,                 // channel 5
  DWT_PLEN_128,      // preamble length
  DWT_PAC8,          // PAC
  3,                 // TX preamble code
  3,                 // RX preamble code
  1,                 // SFD type
  DWT_BR_6M8,        // data rate 6.8 Mbps
  DWT_PHRMODE_STD,   // PHY header mode
  DWT_PHRRATE_STD,   // PHY header rate
  (129 + 8 - 8),     // SFD timeout
  DWT_STS_MODE_OFF,  // STS disabled
  DWT_STS_LEN_64,    // STS length
  DWT_PDOA_M0        // PDOA off
};

const char *ssid = "Makerfabs";
const char *password = "20160704";
const char *host = "192.168.1.103";
WiFiClient client;

struct MyLink *uwb_data;
int index_num = 0;
long runtime = 0;
long heartbeat_time = 0;
String all_json = "";

// UWB Variables
extern dwt_txconfig_t txconfig_options;
static uint8_t tx_poll_msg[] = {0x41,0x88,0,0xCA,0xDE,'W','A','V','E',0xE0,0,0};
static uint8_t rx_resp_hdr[] = {0x41,0x88,0,0xCA,0xDE,'V','E','W','A',0xE1,0,0};
static uint8_t rx_buffer[64];
static uint8_t seq = 0;

// Constants
#define SPEED_OF_LIGHT 299702547.0
#define RXFLEN_MASK 0x0000007FUL

// Function prototypes
void performRanging();
void processRangeResponse();
float dwt_readrxpower();  // Simplified RX power reading

void setup()
{
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Connected");
    Serial.print("IP Address:");
    Serial.println(WiFi.localIP());

    if (client.connect(host, 80))
    {
        Serial.println("Success");
        client.print(String("GET /") + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n" +
                     "\r\n");
    }

    delay(1000);

    // Initialize DW3000
    Serial.println("Initializing DW3000...");
    
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(2);

    while (!dwt_checkidlerc()) { 
        Serial.println("IDLE FAILED"); 
        delay(1000);
    }
    
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { 
        Serial.println("INIT FAILED"); 
        while(1) delay(1000);
    }

    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    
    if (dwt_configure(&config)) { 
        Serial.println("CONFIG FAILED"); 
        while(1) delay(1000);
    }

    dwt_configuretxrf(&txconfig_options);
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
    dwt_setrxtimeout(0);  // No RX timeout

    Serial.println("DW3000 initialized: ch=5, PRL=128, code=3/3, 6.8M");
    Serial.println("Running as UWB Tag for indoor positioning");

    uwb_data = init_link();
}

void loop()
{
    // Perform UWB ranging
    performRanging();
    
    // Send UDP data every second
    if ((millis() - runtime) > 1000)
    {
        make_link_json(uwb_data, &all_json);
        send_udp(&all_json);
        runtime = millis();
    }
    
    // Heartbeat every 5 seconds
    if ((millis() - heartbeat_time) > 5000)
    {
        Serial.println("UWB Tag heartbeat - WiFi connected, ranging active");
        heartbeat_time = millis();
    }
}

void performRanging()
{
    // Prepare poll message
    tx_poll_msg[2] = seq++; // Sequence number
    
    // Clear status
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
    
    // Send poll message
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    
    // Wait for response (with timeout)
    uint32_t timeout = 5000; // 5ms timeout
    uint32_t start_time = micros();
    
    while ((micros() - start_time) < timeout) {
        uint32_t status = dwt_read32bitreg(SYS_STATUS_ID);
        
        if (status & SYS_STATUS_RXFCG_BIT_MASK) {
            // Good frame received
            processRangeResponse();
            return;
        }
        
        if (status & SYS_STATUS_ALL_RX_ERR) {
            // Clear RX errors
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            break;
        }
    }
    
    // Timeout or error - prepare for next ranging
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    delay(100); // Wait before next poll
}

void processRangeResponse()
{
    // Clear RX flag
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
    
    // Read frame length and data
    uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
    if (frame_len > sizeof(rx_buffer)) frame_len = sizeof(rx_buffer);
    dwt_readrxdata(rx_buffer, frame_len, 0);
    
    // Check if it's a valid response frame
    if (frame_len >= 20) {
        // Extract anchor ID (assuming it's at a specific offset)
        uint16_t anchor_addr = (rx_buffer[6] << 8) | rx_buffer[5]; // Extract from frame
        
        // Calculate distance (simplified version)
        uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
        uint32_t resp_rx_ts = dwt_readrxtimestamplo32();
        
        // Get remote timestamps from payload (simplified)
        uint32_t poll_rx_ts = ((uint32_t)rx_buffer[10]) | ((uint32_t)rx_buffer[11] << 8) | 
                              ((uint32_t)rx_buffer[12] << 16) | ((uint32_t)rx_buffer[13] << 24);
        uint32_t resp_tx_ts = ((uint32_t)rx_buffer[14]) | ((uint32_t)rx_buffer[15] << 8) | 
                              ((uint32_t)rx_buffer[16] << 16) | ((uint32_t)rx_buffer[17] << 24);
        
        // Calculate time of flight and distance
        float clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);
        int32_t rtd_init = (int32_t)(resp_rx_ts - poll_tx_ts);
        int32_t rtd_resp = (int32_t)(resp_tx_ts - poll_rx_ts);
        
        double tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
        double distance = tof * SPEED_OF_LIGHT;
        
        // Get RX power (approximate)
        float rx_power = dwt_readrxpower();
        
        if (distance > 0 && distance < 1000) { // Sanity check
            Serial.print("Range from anchor ");
            Serial.print(anchor_addr, HEX);
            Serial.print(": ");
            Serial.print(distance, 2);
            Serial.print(" m, RX power: ");
            Serial.print(rx_power, 1);
            Serial.println(" dBm");
            
            // Update link data
            fresh_link(uwb_data, anchor_addr, distance, rx_power);
        }
    }
    
    // Re-enable RX for next frame
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

void send_udp(String *msg_json)
{
    if (client.connected())
    {
        client.print(*msg_json);
        Serial.println("UDP send");
    }
}

// Simplified RX power reading function
float dwt_readrxpower()
{
    // Read diagnostic registers for RX power estimation
    uint32_t diag_reg = dwt_read32bitreg(0x0F); // RX_FQUAL_ID register
    
    // Simple conversion to approximate dBm
    // This is a simplified version - actual implementation would be more complex
    float power_estimate = -80.0 + ((diag_reg & 0xFFFF) / 1000.0);
    
    return power_estimate;
}
