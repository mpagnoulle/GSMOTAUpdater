/**
 * GSMOTAUpdater Example - Basic OTA Update via GSM/GPRS
 *
 * This example demonstrates how to perform an over-the-air firmware update
 * using a SIM800C GSM modem. It includes modem initialization, progress
 * callbacks, MD5 verification, and error handling.
 *
 * Hardware Requirements:
 * - ESP32 board
 * - SIM800C GSM modem
 * - Active SIM card with data plan
 * - Storage (SPIFFS, LittleFS, or external flash)
 *
 * Wiring (adjust pins as needed):
 * - ESP32 TX2 (GPIO17) -> SIM800C RX
 * - ESP32 RX2 (GPIO16) -> SIM800C TX
 * - ESP32 GND -> SIM800C GND
 * - SIM800C VCC -> 4.2V power supply (not 3.3V!)
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include <GSMOTAUpdater.h>

// Modem serial configuration
#define MODEM_TX 17
#define MODEM_RX 16
#define MODEM_BAUD 115200

// Server configuration - UPDATE THESE VALUES
const char* SERVER_ADDRESS = "your-server.com";
const int SERVER_PORT = 443;
const char* FIRMWARE_PATH = "/firmware/esp32.bin";
const unsigned long FIRMWARE_SIZE = 123456;  // Size in bytes
const char* FIRMWARE_MD5 = "d41d8cd98f00b204e9800998ecf8427e";  // Expected MD5 hash

// Local file name for downloaded firmware
const char* LOCAL_FIRMWARE_FILE = "/firmware.bin";

// Create instances
HardwareSerial SerialAT(1);
GSMOTAUpdater otaUpdater;

// Progress callback function
void onProgress(unsigned long current, unsigned long total)
{
    int percent = (current * 100) / total;
    Serial.printf("Download progress: %lu / %lu bytes (%d%%)\n", current, total, percent);
}

// Initialize the GSM modem
bool initModem()
{
    Serial.println("Initializing modem...");

    // Basic AT command test
    SerialAT.println("AT");
    delay(1000);

    String response = "";
    unsigned long timeout = millis() + 5000;

    while (millis() < timeout)
    {
        if (SerialAT.available())
        {
            response += (char)SerialAT.read();
        }

        if (response.indexOf("OK") != -1)
        {
            Serial.println("Modem responding");
            break;
        }
    }

    if (response.indexOf("OK") == -1)
    {
        Serial.println("Modem not responding!");
        return false;
    }

    // Enable GPRS - adjust APN for your carrier
    Serial.println("Configuring GPRS...");

    // Set APN (replace with your carrier's APN)
    SerialAT.println("AT+CSTT=\"internet\",\"\",\"\"");
    delay(2000);

    // Bring up wireless connection
    SerialAT.println("AT+CIICR");
    delay(3000);

    // Get local IP address
    SerialAT.println("AT+CIFSR");
    delay(1000);

    // Set to manual data receive mode (required for this library)
    SerialAT.println("AT+CIPRXGET=1");
    delay(500);

    Serial.println("Modem ready");
    return true;
}

void setup()
{
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println("\n\n=================================");
    Serial.println("GSMOTAUpdater Example");
    Serial.println("=================================\n");

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS initialization failed!");
        return;
    }
    Serial.println("SPIFFS initialized");

    // Initialize modem serial
    SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
    delay(1000);

    // Initialize the modem
    if (!initModem())
    {
        Serial.println("Modem initialization failed!");
        return;
    }

    // Configure OTA updater
    otaUpdater.setDebug(true);  // Enable debug output
    otaUpdater.chunkSize = 25000;  // Download in 25KB chunks

    // Initialize with server details
    otaUpdater.init(
        SERVER_ADDRESS,
        SERVER_PORT,
        FIRMWARE_PATH,
        FIRMWARE_SIZE,
        &SerialAT,
        &SPIFFS
    );

    // Register progress callback
    otaUpdater.onDownloadFirmwareProgress(onProgress);

    Serial.println("\nStarting firmware download...");
    Serial.printf("Server: %s:%d%s\n", SERVER_ADDRESS, SERVER_PORT, FIRMWARE_PATH);
    Serial.printf("Expected size: %lu bytes\n", FIRMWARE_SIZE);
    Serial.println("Note: Mid-chunk connection drops are automatically retried");

    // Download the firmware
    if (!otaUpdater.download(LOCAL_FIRMWARE_FILE))
    {
        Serial.println("\nDownload FAILED!");
        Serial.println("Restarting to retry...");
        delay(3000);
        ESP.restart();
    }

    Serial.println("\nDownload complete!");

    // Verify MD5 hash
    Serial.println("Verifying MD5 hash...");
    char md5[33];
    strncpy(md5, FIRMWARE_MD5, sizeof(md5));

    if (!otaUpdater.verifyMD5(LOCAL_FIRMWARE_FILE, md5))
    {
        Serial.println("MD5 verification FAILED!");
        Serial.println("Firmware may be corrupted. Aborting update.");
        SPIFFS.remove(LOCAL_FIRMWARE_FILE);
        return;
    }

    Serial.println("MD5 verification passed!");

    // Perform the update
    Serial.println("\nApplying firmware update...");
    Serial.println("DO NOT power off the device!");

    if (!otaUpdater.performUpdate(LOCAL_FIRMWARE_FILE))
    {
        Serial.println("Update FAILED!");
        return;
    }

    Serial.println("\n=================================");
    Serial.println("Update successful!");
    Serial.println("Restarting in 3 seconds...");
    Serial.println("=================================");

    delay(3000);
    ESP.restart();
}

void loop()
{
    // Nothing to do here - update happens in setup()
}
