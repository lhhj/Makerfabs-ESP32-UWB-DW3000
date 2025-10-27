/*! ----------------------------------------------------------------------------
 *  @file    tag_discovery.ino
 *  @brief   UWB Tag Discovery Example
 *
 *  @description
 *  This example demonstrates how to discover UWB tags in the vicinity.
 *  It works by:
 *  1. Sending out discovery beacon frames periodically
 *  2. Listening for responses from nearby UWB tags/devices
 *  3. Parsing received frames and extracting device information
 *  4. Displaying discovered tags with their details via Serial
 *
 *  The example uses a combination of active scanning (sending beacons)
 *  and passive listening to detect various types of UWB devices.
 *
 * @attention
 *
 * Copyright 2025 Makerfabs & Community
 *
 * All rights reserved.
 *
 */

#include "dwt_uwb_driver.h"
#include <SPI.h>

#define APP_NAME "UWB TAG DISCOVERY v1.0"

// Connection pins for ESP32
const uint8_t PIN_RST = 27; // reset pin
const uint8_t PIN_IRQ = 34; // irq pin
const uint8_t PIN_SS = 4;   // spi select pin

/* Default communication configuration for discovery */
static dwt_config_t config = {
    9,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    10,                /* TX preamble code. Used in TX only. */
    10,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* STS disabled */
    DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       /* PDOA mode off */
};

/* Discovery beacon frame - broadcasts to discover nearby devices */
static uint8_t discovery_beacon[] = {
    0xC1,           // Frame type: Discovery beacon
    0,              // Sequence number (will be incremented)
    0xFF, 0xFF,     // Broadcast PAN ID
    0xFF, 0xFF,     // Broadcast address
    'D', 'I', 'S', 'C', 'O', 'V', 'E', 'R'  // Discovery payload
};

/* Buffer to store received frames */
static uint8_t rx_buffer[FRAME_LEN_MAX];

/* Discovery intervals and timeouts */
#define DISCOVERY_BEACON_INTERVAL_MS 2000   // Send beacon every 2 seconds
#define LISTEN_DURATION_MS 1000             // Listen for responses for 1 second
#define MAX_DISCOVERED_DEVICES 20           // Maximum devices to track
#define STATUS_INTERVAL_MS 5000             // Heartbeat print interval

/* Structure to store discovered device information */
struct DiscoveredDevice {
    uint8_t device_id[8];       // Device identifier
    uint8_t frame_type;         // Type of frame received
    int16_t rssi;               // Signal strength (dBm)
    uint16_t first_path_power;  // First path power
    uint32_t last_seen;         // Timestamp when last seen
    uint16_t frame_count;       // Number of frames received from this device
    bool is_active;             // Device entry is active
};

/* Array to store discovered devices */
static DiscoveredDevice discovered_devices[MAX_DISCOVERED_DEVICES];
static uint8_t device_count = 0;

/* Timing variables */
static uint32_t last_beacon_time = 0;
static uint32_t scan_start_time = 0;
static bool is_scanning = false;
static uint8_t beacon_sequence = 0;
static uint32_t last_status_print = 0;

/* Statistics */
static uint32_t total_frames_received = 0;
static uint32_t beacons_sent = 0;

/* External TX configuration */
extern dwt_txconfig_t txconfig_options_ch9;
extern SPISettings _fastSPI;

void setup()
{
    UART_init();
    test_run_info((unsigned char *)APP_NAME);
    
    _fastSPI = SPISettings(16000000L, MSBFIRST, SPI_MODE0);

    Serial.println("=== UWB Tag Discovery System ===");
    Serial.println("Initializing DW3000...");

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    /* Reset DW IC */
    spiBegin(PIN_IRQ, PIN_RST);
    spiSelect(PIN_SS);

    delay(2); // Allow DW3000 to reach IDLE_RC

    while (!dwt_checkidlerc()) // Need to make sure DW IC is in IDLE_RC before proceeding
    {
        Serial.println("ERROR: DW3000 not in IDLE state");
        delay(1000);
    }

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        Serial.println("ERROR: DW3000 initialization failed");
        while (1) delay(1000);
    }

    // Enable LEDs for visual feedback
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    // Configure DW IC
    if (dwt_configure(&config))
    {
        Serial.println("ERROR: DW3000 configuration failed");
        while (1) delay(1000);
    }

    /* Configure the TX spectrum parameters */
    dwt_configuretxrf(&txconfig_options_ch9);

    /* Enable on-board LNA/PA for better link budget */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    Serial.println("DW3000 initialized successfully!");
    Serial.println("Starting tag discovery...");
    Serial.println("Format: [Time] Device: ID | Type: XX | RSSI: XXX dBm | FPP: XXXX | Count: XX");
    Serial.println("========================================================================");

    // Initialize discovered devices array
    memset(discovered_devices, 0, sizeof(discovered_devices));
    
    last_beacon_time = millis();
    last_status_print = last_beacon_time;
}

void loop()
{
    uint32_t current_time = millis();
    
    // Send discovery beacon periodically
    if (!is_scanning && (current_time - last_beacon_time >= DISCOVERY_BEACON_INTERVAL_MS))
    {
        send_discovery_beacon();
        start_listening();
        last_beacon_time = current_time;
    }
    
    // Listen for incoming frames
    if (is_scanning)
    {
        check_for_frames();
        
        // Stop scanning after listen duration
        if (current_time - scan_start_time >= LISTEN_DURATION_MS)
        {
            is_scanning = false;
            print_discovery_summary();
        }
    }
    
    if (current_time - last_status_print >= STATUS_INTERVAL_MS)
    {
        Serial.print("[");
        Serial.print(current_time);
        Serial.print("] Listening ");
        Serial.print(is_scanning ? "(active scan)" : "(idle)" );
        Serial.print(" | Beacons: ");
        Serial.print(beacons_sent);
        Serial.print(" | Frames: ");
        Serial.print(total_frames_received);
        Serial.print(" | Devices: ");
        Serial.println(device_count);
        last_status_print = current_time;
    }

    delay(10); // Small delay to prevent overwhelming the system
}

void send_discovery_beacon()
{
    // Update sequence number
    discovery_beacon[1] = beacon_sequence++;
    
    // Write frame data to DW3000 TX buffer
    dwt_writetxdata(sizeof(discovery_beacon), discovery_beacon, 0);
    dwt_writetxfctrl(sizeof(discovery_beacon) + FCS_LEN, 0, 0);
    
    // Start transmission
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    
    // Wait for transmission to complete
    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK))
    {
        // Wait for TX complete
    }
    
    // Clear TX complete flag
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    
    beacons_sent++;
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] Beacon sent #");
    Serial.println(beacons_sent);
}

void start_listening()
{
    // Clear RX buffer
    memset(rx_buffer, 0, sizeof(rx_buffer));
    
    // Start reception
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    
    is_scanning = true;
    scan_start_time = millis();
}

void check_for_frames()
{
    uint32_t status_reg = dwt_read32bitreg(SYS_STATUS_ID);
    
    // Check if frame received successfully
    if (status_reg & SYS_STATUS_RXFCG_BIT_MASK)
    {
        // Get frame length
        uint16_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_BIT_MASK;
        
        if (frame_len <= FRAME_LEN_MAX && frame_len > 2)
        {
            // Read the received frame
            dwt_readrxdata(rx_buffer, frame_len - FCS_LEN, 0);
            
            // Process the received frame
            process_received_frame(frame_len - FCS_LEN);
            
            total_frames_received++;
        }
        
        // Clear RX good frame flag
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
        
        // Re-enable reception for more frames
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    else if (status_reg & SYS_STATUS_ALL_RX_ERR)
    {
        // Handle RX errors - clear error flags and restart reception
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

void process_received_frame(uint16_t frame_len)
{
    if (frame_len < 2) return; // Too short to be valid
    
    // Extract basic frame information
    uint8_t frame_type = rx_buffer[0];
    uint8_t sequence_num = rx_buffer[1];
    
    // Skip our own beacon frames
    if (frame_type == 0xC1 && frame_len == sizeof(discovery_beacon) - FCS_LEN)
    {
        // This might be our own beacon, check payload
        if (memcmp(&rx_buffer[4], &discovery_beacon[4], 8) == 0)
        {
            return; // Skip our own beacon
        }
    }
    
    // Get signal quality information
    int16_t rssi = get_rssi();
    uint16_t first_path_power = get_first_path_power();
    
    // Extract device identifier (use first 8 bytes as device ID)
    uint8_t device_id[8];
    int id_len = min(8, (int)(frame_len - 2));
    memcpy(device_id, &rx_buffer[2], id_len);
    if (id_len < 8) {
        memset(&device_id[id_len], 0, 8 - id_len);
    }
    
    // Find or create device entry
    int device_index = find_or_create_device(device_id);
    
    if (device_index >= 0)
    {
        // Update device information
        DiscoveredDevice* device = &discovered_devices[device_index];
        device->frame_type = frame_type;
        device->rssi = rssi;
        device->first_path_power = first_path_power;
        device->last_seen = millis();
        device->frame_count++;
        
        // Print device information
        print_device_info(device_index);
    }
}

int find_or_create_device(uint8_t* device_id)
{
    // First, try to find existing device
    for (int i = 0; i < device_count; i++)
    {
        if (discovered_devices[i].is_active && 
            memcmp(discovered_devices[i].device_id, device_id, 8) == 0)
        {
            return i; // Found existing device
        }
    }
    
    // Create new device entry if space available
    if (device_count < MAX_DISCOVERED_DEVICES)
    {
        DiscoveredDevice* device = &discovered_devices[device_count];
        memcpy(device->device_id, device_id, 8);
        device->is_active = true;
        device->frame_count = 0;
        return device_count++;
    }
    
    return -1; // No space for new device
}

void print_device_info(int device_index)
{
    DiscoveredDevice* device = &discovered_devices[device_index];
    
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] Device: ");
    
    // Print device ID in hex
    for (int i = 0; i < 8; i++)
    {
        if (device->device_id[i] < 0x10) Serial.print("0");
        Serial.print(device->device_id[i], HEX);
    }
    
    Serial.print(" | Type: 0x");
    if (device->frame_type < 0x10) Serial.print("0");
    Serial.print(device->frame_type, HEX);
    
    Serial.print(" | RSSI: ");
    Serial.print(device->rssi);
    Serial.print(" dBm");
    
    Serial.print(" | FPP: ");
    Serial.print(device->first_path_power);
    
    Serial.print(" | Count: ");
    Serial.println(device->frame_count);
}

int16_t get_rssi()
{
    // Read diagnostic data from DW3000
    dwt_rxdiag_t diagnostics;
    dwt_readdiagnostics(&diagnostics);
    
    // Use Ipatov power for RSSI calculation
    if (diagnostics.ipatovPower > 0)
    {
        // Convert to dBm (approximation based on DW3000 characteristics)
        float rssi_f = 10.0 * log10((float)diagnostics.ipatovPower) - 113.77;
        return (int16_t)rssi_f;
    }
    return -100; // Default very low RSSI
}

uint16_t get_first_path_power()
{
    // Read diagnostic data from DW3000
    dwt_rxdiag_t diagnostics;
    dwt_readdiagnostics(&diagnostics);
    
    // Return Ipatov power as first path power indicator
    return (uint16_t)(diagnostics.ipatovPower & 0xFFFF);
}

void print_discovery_summary()
{
    Serial.println("\n--- Discovery Summary ---");
    Serial.print("Total devices discovered: ");
    Serial.println(device_count);
    Serial.print("Total frames received: ");
    Serial.println(total_frames_received);
    Serial.print("Beacons sent: ");
    Serial.println(beacons_sent);
    
    if (device_count > 0)
    {
        Serial.println("\nActive devices:");
        for (int i = 0; i < device_count; i++)
        {
            if (discovered_devices[i].is_active)
            {
                print_device_info(i);
            }
        }
    }
    else
    {
        Serial.println("No devices discovered in this scan cycle.");
    }
    
    Serial.println("========================\n");
    
    // Clean up old devices (optional - remove devices not seen for a while)
    cleanup_old_devices();
}

void cleanup_old_devices()
{
    uint32_t current_time = millis();
    const uint32_t DEVICE_TIMEOUT_MS = 30000; // 30 seconds
    
    for (int i = 0; i < device_count; i++)
    {
        if (discovered_devices[i].is_active && 
            (current_time - discovered_devices[i].last_seen) > DEVICE_TIMEOUT_MS)
        {
            Serial.print("Device timed out: ");
            for (int j = 0; j < 8; j++)
            {
                if (discovered_devices[i].device_id[j] < 0x10) Serial.print("0");
                Serial.print(discovered_devices[i].device_id[j], HEX);
            }
            Serial.println();
            
            discovered_devices[i].is_active = false;
        }
    }
}

/*****************************************************************************************************************************************************
 * NOTES:
 *
 * 1. This example uses a simple discovery protocol where it sends beacon frames and listens for any responses.
 *    In a real application, you might want to implement a more sophisticated discovery protocol.
 *
 * 2. The RSSI calculation is simplified and may need calibration for accurate readings.
 *
 * 3. Device identification is based on the frame content. In practice, you might want to use proper
 *    device identifiers or MAC addresses.
 *
 * 4. This example demonstrates passive scanning by listening to all UWB traffic in the area.
 *    Be aware of privacy and regulatory considerations when implementing such functionality.
 *
 * 5. The discovery works on channel 5. You may want to scan multiple channels for complete coverage.
 *
 * 6. Frame filtering could be enhanced to detect specific frame types like ranging frames, data frames, etc.
 *
 ****************************************************************************************************************************************************/