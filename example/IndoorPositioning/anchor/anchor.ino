/*

For ESP32 UWB with DW3000 chip
Adapted to use local DW3000 driver with channel 5 support
Anchor mode for indoor positioning system

*/

#include <SPI.h>
#include "dwt_uwb_driver.h"

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

// UWB Variables
extern dwt_txconfig_t txconfig_options;
static uint8_t rx_poll_hdr[] = {0x41,0x88,0,0xCA,0xDE,'W','A','V','E',0xE0,0,0};
static uint8_t tx_resp_msg[] = {0x41,0x88,0,0xCA,0xDE,'V','E','W','A',0xE1,0,0,0,0,0,0,0,0,0,0};
static uint8_t rx_buffer[64];
static uint64_t poll_rx_ts = 0;
static uint64_t resp_tx_ts = 0;

// Constants
#define SPEED_OF_LIGHT 299702547.0
#define RXFLEN_MASK 0x0000007FUL
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN 4

// Anchor address (can be customized per anchor)
#define ANCHOR_ID 0x01

// Timing variables
long heartbeat_time = 0;

// Function prototypes
void processPolls();
void sendResponse();
float dwt_readrxpower();  // Simplified RX power reading

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("=======================================");
    Serial.println("ESP32 UWB Anchor Starting");
    Serial.println("=======================================");

    // Initialize DW3000
    Serial.println("Initializing DW3000 Anchor...");
    
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);
    delay(100);  // Longer delay for DW3000 to stabilize

    int idle_attempts = 0;
    while (!dwt_checkidlerc()) { 
        idle_attempts++;
        Serial.print("IDLE CHECK FAILED - Attempt ");
        Serial.println(idle_attempts);
        if (idle_attempts > 10) {
            Serial.println("ERROR: DW3000 not responding - continuing anyway");
            break;
        }
        delay(1000);
    }
    
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) { 
        Serial.println("ERROR: DW3000 init failed - continuing anyway"); 
    } else {
        Serial.println("DW3000 driver initialized successfully");
    }

    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    
    if (dwt_configure(&config)) { 
        Serial.println("ERROR: DW3000 config failed - continuing anyway"); 
    } else {
        Serial.println("DW3000 configured successfully");
    }

    dwt_configuretxrf(&txconfig_options);
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
    dwt_setrxtimeout(0);  // No RX timeout

    // Set as anchor
    tx_resp_msg[9] = ANCHOR_ID;  // Set anchor ID in response header
    
    Serial.println("DW3000 initialized: ch=5, PRL=128, code=3/3, 6.8M");
    Serial.print("Running as UWB Anchor #");
    Serial.println(ANCHOR_ID);
    
    // Start listening for polls
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

void loop()
{
    // Process incoming polls from tags
    processPolls();
    
    // Heartbeat every 5 seconds
    if ((millis() - heartbeat_time) > 5000)
    {
        Serial.print("Anchor #");
        Serial.print(ANCHOR_ID);
        Serial.println(" heartbeat - listening for tags");
        heartbeat_time = millis();
    }
}

void processPolls()
{
    uint32_t status_reg = dwt_read32bitreg(SYS_STATUS_ID);
    
    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
    {
        uint32_t frame_len;
        
        // Clear RX good frame event
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
        
        // Read frame length
        frame_len = dwt_read32bitreg(RX_FINFO_ID) & RXFLEN_MASK;
        
        if (frame_len <= sizeof(rx_buffer))
        {
            dwt_readrxdata(rx_buffer, frame_len, 0);
            
            // Check if this is a poll message
            if (memcmp(rx_buffer, rx_poll_hdr, 10) == 0)
            {
                uint8_t tag_seq = rx_buffer[2];  // Get sequence number
                
                Serial.print("Poll from tag, seq=");
                Serial.print(tag_seq);
                Serial.print(", RX power: ");
                Serial.print(dwt_readrxpower());
                Serial.println(" dBm");
                
                sendResponse(tag_seq);
            }
        }
        
        // Re-enable receiver
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    else if (status_reg & SYS_STATUS_ALL_RX_ERR)
    {
        // Clear RX error events
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        
        // Re-enable receiver
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

void sendResponse(uint8_t seq)
{
    // Get poll receive timestamp using DW3000 function
    uint8_t rx_ts_bytes[5];
    dwt_readrxtimestamp(rx_ts_bytes);
    poll_rx_ts = 0;
    for (int i = 4; i >= 0; i--) {
        poll_rx_ts = (poll_rx_ts << 8) + rx_ts_bytes[i];
    }
    
    // Prepare response message
    tx_resp_msg[2] = seq;  // Copy sequence number
    
    // Clear status
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    
    // Write response data
    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
    dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 0);
    
    // Start transmission
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    
    // Wait for TX completion
    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { /* wait */ }
    
    // Get response transmit timestamp using DW3000 function
    uint8_t tx_ts_bytes[5];
    dwt_readtxtimestamp(tx_ts_bytes);
    resp_tx_ts = 0;
    for (int i = 4; i >= 0; i--) {
        resp_tx_ts = (resp_tx_ts << 8) + tx_ts_bytes[i];
    }
    
    // Write timestamps into message for next poll
    resp_tx_ts_to_msg(tx_resp_msg, RESP_MSG_POLL_RX_TS_IDX, poll_rx_ts);
    resp_tx_ts_to_msg(tx_resp_msg, RESP_MSG_RESP_TX_TS_IDX, resp_tx_ts);
    
    Serial.println("Response sent");
}

float dwt_readrxpower()
{
    // Simplified RX power estimation
    uint32_t reg = dwt_read32bitreg(0x12);  // RX_FQUAL register
    return (float)((reg & 0xFFFF0000) >> 16) / 100.0 - 164.0;
}

static void resp_tx_ts_to_msg(uint8_t *ts_msg, uint32_t ts_msg_idx, uint64_t ts)
{
    for (int i = 0; i < RESP_MSG_TS_LEN; i++)
    {
        ts_msg[ts_msg_idx + i] = (uint8_t)(ts >> (i * 8));
    }
}
