# GSMOTAUpdater

A PlatformIO/Arduino library for updating ESP32 firmware over-the-air using a SIM800C GSM modem.

[![PlatformIO Registry](https://badges.registry.platformio.org/packages/mpagnoulle/library/GSMOTAUpdater.svg)](https://registry.platformio.org/libraries/mpagnoulle/GSMOTAUpdater)

## Features

- Download firmware via GPRS using HTTP range requests
- Resumable chunk-based downloads (default 25KB chunks)
- MD5 hash verification before flashing
- Progress callback support
- Runtime debug logging control
- Works with any filesystem (SPIFFS, LittleFS, external flash)

## Installation

### PlatformIO (recommended)

Add to your `platformio.ini`:

```ini
lib_deps =
    mpagnoulle/GSMOTAUpdater@^1.0.0
```

### Arduino IDE

1. Download this repository as ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library

## Important Notes

- This library uses AT commands directly - **not compatible with TinyGSM**
- Your server must support HTTP range requests
- Download can take ~20 minutes for a 1MB file over GPRS
- Mid-chunk connection drops are automatically retried
- Initial connection failures return `false` - handle by restarting or retrying

## Quick Example

```cpp
#include <GSMOTAUpdater.h>
#include <SPIFFS.h>

GSMOTAUpdater otaUpdater;
HardwareSerial SerialAT(1);

void performFirmwareUpdate() {
    // Initialize
    otaUpdater.init(serverAddress, 443, downloadPath, fileSize, &SerialAT, &SPIFFS);
    otaUpdater.setDebug(true);
    otaUpdater.chunkSize = 25000;
    otaUpdater.onDownloadFirmwareProgress(onProgress);

    // Download firmware (mid-chunk drops are auto-retried)
    if (!otaUpdater.download(firmwareFile)) {
        Serial.println("Download failed!");
        ESP.restart();  // Restart and try again
    }

    // Verify MD5 (strongly recommended)
    if (!otaUpdater.verifyMD5(firmwareFile, knownMD5)) {
        Serial.println("MD5 mismatch!");
        ESP.restart();
    }

    // Flash the firmware
    if (!otaUpdater.performUpdate(firmwareFile)) {
        Serial.println("Update failed!");
        ESP.restart();
    }

    Serial.println("Update successful!");
    ESP.restart();
}
```

## API Reference

### Methods

| Method | Description |
|--------|-------------|
| `init(server, port, path, size, serial, fs)` | Initialize with server details and hardware |
| `download(filename)` | Download firmware to file. Returns `false` on failure |
| `verifyMD5(filename, hash)` | Verify file MD5 against known hash |
| `performUpdate(filename)` | Flash the firmware and delete file |
| `setDebug(enabled)` | Enable/disable debug output (default: disabled) |
| `onDownloadFirmwareProgress(callback)` | Set progress callback `void(current, total)` |

### Properties

| Property | Default | Description |
|----------|---------|-------------|
| `chunkSize` | 25000 | Download chunk size in bytes |

## How It Works

1. Initialize with server address, port, download path, file size, serial port, and filesystem
2. Firmware downloads in chunks using HTTP Range requests
3. If connection drops mid-chunk, it automatically retries that chunk
4. If initial connection fails, returns `false` - restart or retry as needed
5. Verify MD5 hash before flashing (recommended to prevent bricking)
6. Flash the firmware using ESP32's Update library

## Hardware Setup

```
ESP32 TX (GPIO17) → SIM800C RX
ESP32 RX (GPIO16) → SIM800C TX
ESP32 GND         → SIM800C GND
4.2V Power        → SIM800C VCC (NOT 3.3V!)
```

## Optional: GZIP Support

You can download gzip-compressed firmware and decompress before flashing using the [ESP32-targz](https://github.com/tobozo/ESP32-targz) library.

## License

GPL-3.0
