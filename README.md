# GSMOTAUpdater
This class can make it possible for you to update the firmware of your ESP32 over the air by downloading the firmware using a SIM800(C) modem.

## ⚠️ A few things to note
- This class is provided as-is with no guarantee that it will be working with your project without some tweaking done on your side.
- As this class is using AT commands to communicate with the modem, it is not compatible with TinyGSM.
- This has been made to write the downloaded file on an external flash memory chip, if you want to download it in the ESP32's SPIFFS memory, it is possible by passing the right file system, just be mindful of available space.

## How does it work?
1. The class is first initialized with the requirements parameters
2. The download request is being made, the firmware will be downloaded in chunks of 25 000 bytes (**your server need to support range requests**, the size of the chunk can be changed)
3. Once downloaded, you can verify the md5 hash of the downloaded file against a known hash, that is to prevent writing corrupt data to the ESP32 which could result in bricking it (can be solved by flashing it via esptools)
4. If all is good, you can then perform the firmware update with the downloaded file

If the download of a chunk fails due to a loss of connection, the download of that specific chunk will be restarted, after testing, this was the best way to ensure file integrity (compare to resuming to the last written byte).

*Patience is needed as the whole update process can take up to 20 minutes for a file of 1MB.*

## How to use
Here is a quick example on how to use the class:
```cpp
void performFirmwareUpdate() {
  // Initialize the FOTA updater
  fotaUpdater.init(serverAddress, 443, downloadPath, fileSize, & SerialSIM800C, & FileSystem);

  // Adjust chunk size
  fotaUpdater.chunkSize = 25000;

  // Set progress callback
  fotaUpdater.onDownloadFirmwareProgress(onUpdateProgress);

  // Download firmware
  if (fotaUpdater.download(firmwareFile)) { // firmwareFile is the name of the file you want to write to memory
    // Verify MD5 hash
    if (fotaUpdater.verifyMD5(firmwareFile, knownMD5)) {
      // Perform the update
      if (fotaUpdater.performUpdate(firmwareFile)) {
        Serial.println("Flashed successfully!");
        // Reset ESP32 to boot on new firmware
      }
    }
  }
}
```
**Verifying the MD5 checksum is optional but strongly recommended to avoid writing corrupted data to the ESP32.**

You can also download the firmware as a gzip file to be expanded before calling performUpdate using the ESP32-targz library.
