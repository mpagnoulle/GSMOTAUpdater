#include "GSMOTAUpdater.h"
#include <stdarg.h>

// ============================================================================
// Section 1: Initialization & Configuration
// ============================================================================

GSMOTAUpdater::GSMOTAUpdater_Progress GSMOTAUpdater::_progress_callback = nullptr;

/**
 * Constructs a new GSMOTAUpdater object.
 * Initializes the object with default values for its member variables.
 */
GSMOTAUpdater::GSMOTAUpdater()
{
	isInitialized = false;
	isTCPConnected = false;
	wasConnectionLost = false;
	waitingForData = true;
	isHeadersRead = false;
	chunkDownloaded = false;
	isDownloadComplete = false;
	debugEnabled = false;
	fileSize = 0;
	currentChunkByte = 0;
	currentByte = 0;
	rangeStart = 0;
	rangeEnd = 0;
	serverPort = DEFAULT_SERVER_PORT;
	ATResponse = "";
}

/**
 * Initializes the GSMOTAUpdater with the specified parameters.
 *
 * @param server_address [const char*] The address of the server to connect to.
 * @param server_port [int] The port number of the server.
 * @param download_path [const char*] The path where the downloaded file will requested from server.
 * @param file_size [unsigned long] The size of the file to be downloaded.
 * @param serial_AT [HardwareSerial*] The hardware serial object for AT commands.
 * @param file_system [FS*] The file system object for file operations.
 */
void GSMOTAUpdater::init(const char *server_address, int server_port, const char *download_path, unsigned long file_size, HardwareSerial *serial_AT, FS *file_system)
{
	serverAddress = server_address;
	serverPort = server_port;
	downloadPath = download_path;
	fileSize = file_size;
	SerialAT = serial_AT;
	fileSystem = file_system;
	isInitialized = true;
}

/**
 * Enables or disables debug logging.
 *
 * @param enabled [bool] True to enable debug output, false to disable.
 */
void GSMOTAUpdater::setDebug(bool enabled)
{
	debugEnabled = enabled;
}

/**
 * Registers a callback function for download progress updates.
 *
 * @param fn [GSMOTAUpdater_Progress] The callback function to invoke with progress updates.
 */
void GSMOTAUpdater::onDownloadFirmwareProgress(GSMOTAUpdater_Progress fn)
{
	_progress_callback = fn;
}

// ============================================================================
// Section 2: Public API (Core Functionality)
// ============================================================================

/**
 * Downloads a file from a server using GPRS connection.
 *
 * @param fileName [const char*] The name of the file to save in memory with downloaded data.
 * @returns [bool] True if the download is successful, false otherwise.
 */
bool GSMOTAUpdater::download(const char *fileName)
{
	if (!isInitialized)
	{
		logError("class not initialized");
		return false;
	}

	File file = fileSystem->open(fileName, FILE_WRITE);
	rangeEnd = chunkSize;

	while (!isDownloadComplete)
	{
		if (!file)
		{
			file = fileSystem->open(fileName, FILE_WRITE);
			if (!file)
			{
				logError("file not open");
				return false;
			}
		}

		if (wasConnectionLost)
		{
			file.seek(currentChunkByte);
			wasConnectionLost = false;
		}

		String cipStartCmd = "AT+CIPSTART=\"TCP\",\"" + serverAddress + "\"," + String(serverPort);
		if (!sendATCommand(cipStartCmd.c_str(), "OK", TCP_CONNECT_TIMEOUT))
		{
			logError("connection error");
			file.close();
			return false;
		}

		if (!waitForTCPConnection())
		{
			file.close();
			return false;
		}

		// Build HTTP GET request with Range header
		String request = "GET " + downloadPath + " HTTP/1.1\r\nHost: " + serverAddress +
		                 "\r\nRange: bytes=" + String(rangeStart) + "-" + String(rangeEnd) +
		                 "\r\nConnection: keep-alive\r\n\r\n";
		int requestLength = request.length();

		String cipSendCmd = "AT+CIPSEND=" + String(requestLength);
		if (!sendATCommand(cipSendCmd.c_str(), ">", CIPSEND_TIMEOUT))
		{
			logError("CIPSEND failed");
			file.close();
			return false;
		}

		if (!sendATCommand(request.c_str(), "SEND OK", SEND_OK_TIMEOUT))
		{
			logError("SEND failed");
			file.close();
			return false;
		}

		// Process incoming data
		while (isTCPConnected)
		{
			while (SerialAT->available() > 0)
			{
				char c = SerialAT->read();
				ATResponse += c;

				if (waitingForData)
				{
					if (ATResponse.indexOf("+CIPRXGET: 1") != -1)
					{
						waitingForData = false;
						rstATResponse();
						SerialAT->println("AT+CIPRXGET=3,128");
					}
					else if (ATResponse.indexOf("\nCLOSED") != -1)
					{
						connectionClosed();
					}
					continue;
				}

				if (ATResponse.indexOf(F("\nCLOSED")) != -1)
				{
					connectionClosed();
					continue;
				}

				if (!ATResponse.endsWith("\nOK"))
				{
					continue;
				}

				// Response ends with OK - process it
				if (ATResponse.indexOf("+CIPRXGET: 3,0,0") != -1)
				{
					waitingForData = true;
					rstATResponse();
					continue;
				}

				// Process received data
				if (!isHeadersRead)
				{
					// Check for HTTP response and header end
					if (ATResponse.indexOf("48545450") != -1 && ATResponse.indexOf("0D0A0D0A") != -1)
					{
						ATResponse = ATResponse.substring(
							ATResponse.indexOf("0D0A0D0A") + 8,
							ATResponse.indexOf("\nOK", ATResponse.indexOf("0D0A0D0A"))
						);
						writeDataToFile(file, ATResponse);
						isHeadersRead = true;
						rstATResponse();
					}
				}
				else
				{
					ATResponse = ATResponse.substring(ATResponse.indexOf("\r\n\r\n") + 4);
					ATResponse = ATResponse.substring(ATResponse.indexOf("\r\n") + 2, ATResponse.indexOf("\nOK"));
					writeDataToFile(file, ATResponse);
					rstATResponse();
				}

				// Check if current chunk is complete
				if (currentByte >= rangeEnd)
				{
					chunkDownloaded = true;
					currentChunkByte = rangeEnd + 1;
					rangeStart = currentByte;
					rangeEnd = rangeStart + chunkSize;

					if (sendATCommand("AT+CIPCLOSE", "CLOSE OK", CIPCLOSE_TIMEOUT))
					{
						resetConnectionState();
					}
					else
					{
						logError("connection could not be closed");
					}
					break;
				}

				// Check if download is complete
				if (currentByte >= fileSize)
				{
					chunkDownloaded = true;
					isDownloadComplete = true;
					resetConnectionState();
					file.close();
					return true;
				}

				delay(MODEM_READ_DELAY);
				SerialAT->println("AT+CIPRXGET=3,128");
			}
		}
	}

	file.close();
	return false;
}

/**
 * Verifies the MD5 hash of the downloaded file.
 *
 * @param fileName [const char*] The name of the file to verify.
 * @param knownMD5 [char*] The known MD5 hash to compare against.
 * @returns [bool] True if the MD5 hash matches the known MD5 hash, false otherwise.
 */
bool GSMOTAUpdater::verifyMD5(const char *fileName, char *knownMD5)
{
	if (!isInitialized)
	{
		logError("class not initialized");
		return false;
	}

	if (!fileSystem->exists(fileName))
	{
		logError("file not found");
		return false;
	}

	File file = fileSystem->open(fileName, FILE_READ);
	if (!file)
	{
		logError("file not open");
		return false;
	}

	MD5Builder md5;
	md5.begin();

	uint8_t buffer[AT_BUFFER_SIZE];
	int bytesRead = 0;

	while (file.available())
	{
		buffer[bytesRead++] = file.read();

		if (bytesRead == AT_BUFFER_SIZE)
		{
			md5.add(buffer, AT_BUFFER_SIZE);
			bytesRead = 0;
		}
	}

	if (bytesRead > 0)
	{
		md5.add(buffer, bytesRead);
	}

	file.close();
	md5.calculate();

	bool matches = (String(knownMD5) == md5.toString());

	if (matches)
	{
		logDebug("match, %s == %s (calculated)", knownMD5, md5.toString().c_str());
	}
	else
	{
		logError("mismatch, %s != %s (calculated)", knownMD5, md5.toString().c_str());
	}

	return matches;
}

/**
 * Starts the flashing process of the ESP32.
 *
 * @param fileName [const char*] The name of the file containing the firmware.
 * @returns [bool] True if the update was successful, false otherwise.
 */
bool GSMOTAUpdater::performUpdate(const char *fileName)
{
	if (!isInitialized)
	{
		logError("class not initialized");
		return false;
	}

	File file = fileSystem->open(fileName);
	if (!file)
	{
		logError("could not complete, file not found");
		return false;
	}

	size_t updateSize = file.size();
	if (updateSize == 0)
	{
		file.close();
		logError("could not complete, file is empty");
		return false;
	}

	if (!Update.begin(updateSize))
	{
		file.close();
		logError("could not complete, not enough space");
		return false;
	}

	size_t written = Update.writeStream(file);
	if (written != updateSize)
	{
		logError("written only %d/%d", written, updateSize);
	}
	else
	{
		logDebug("written %d/%d successfully", written, updateSize);
	}

	if (!Update.end())
	{
		logError("update end failed");
		return false;
	}

	if (!Update.isFinished())
	{
		logError("update not finished");
		return false;
	}

	logDebug("flashing completed");
	file.close();
	fileSystem->remove(fileName);
	return true;
}

// ============================================================================
// Section 3: Connection Management
// ============================================================================

/**
 * Sends an AT command to the SIM800C and waits for the expected response.
 *
 * @param command [const char*] The AT command to send.
 * @param expectedResponse [const char*] The expected response from the SIM800C.
 * @param timeout [unsigned long] The maximum time to wait for the response, in milliseconds.
 * @returns [bool] True if the expected response is received within the timeout period, false otherwise.
 */
bool GSMOTAUpdater::sendATCommand(const char *command, const char *expectedResponse, unsigned long timeout)
{
	if (!isInitialized)
	{
		logError("class not initialized");
		return false;
	}

	SerialAT->println(command);
	unsigned long startTime = millis();

	while (millis() - startTime < timeout)
	{
		if (!SerialAT->available())
		{
			continue;
		}

		ATResponse += (char)SerialAT->read();

		if (ATResponse.endsWith(expectedResponse))
		{
			rstATResponse();
			return true;
		}
	}

	rstATResponse();
	return false;
}

/**
 * Waits for TCP connection to be established.
 *
 * @returns [bool] True if connection established, false on failure or timeout.
 */
bool GSMOTAUpdater::waitForTCPConnection()
{
	unsigned long startTime = millis();

	while (millis() - startTime < TCP_CONNECT_TIMEOUT)
	{
		while (SerialAT->available() > 0)
		{
			ATResponse += (char)SerialAT->read();

			if (ATResponse.indexOf(F("CONNECT OK")) != -1)
			{
				logDebug("connected to server");
				isTCPConnected = true;
				rstATResponse();
				return true;
			}

			if (ATResponse.indexOf(F("CONNECT FAIL")) != -1)
			{
				rstATResponse();
				logError("could not connect to server");
				return false;
			}
		}
	}

	logError("connection timeout");
	return false;
}

/**
 * Handles the event when the TCP connection is closed or lost.
 * Resets the necessary variables before attempting to reconnect.
 */
void GSMOTAUpdater::connectionClosed()
{
	logDebug("connection closed/lost");
	rangeStart = currentChunkByte;
	currentByte = rangeStart;
	rangeEnd = rangeStart + chunkSize;
	resetConnectionState();
	wasConnectionLost = true;
	rstATResponse();
	delay(CONNECTION_LOST_DELAY);
}

/**
 * Resets connection state flags to their default values.
 */
void GSMOTAUpdater::resetConnectionState()
{
	isTCPConnected = false;
	waitingForData = true;
	isHeadersRead = false;
}

/**
 * Clears the AT response buffer.
 */
void GSMOTAUpdater::rstATResponse()
{
	ATResponse = "";
}

// ============================================================================
// Section 4: Utilities
// ============================================================================

/**
 * Writes binary data to a file.
 *
 * @param file [File&] The file object to write the data to.
 * @param hexString [String&] The hexadecimal string returned from the modem.
 */
void GSMOTAUpdater::writeDataToFile(File &file, String &hexString)
{
	if (hexString.length() <= 1)
	{
		return;
	}

	size_t binaryLength = strlen(hexString.c_str()) / 2;
	unsigned char binaryData[binaryLength];
	hexStringToBinary(hexString.c_str(), binaryData);

	file.write(binaryData, sizeof(binaryData));
	currentByte += sizeof(binaryData);

	downloadFirmwareProgress(currentByte, fileSize);
}

/**
 * Converts a hexadecimal string to binary data.
 *
 * @param hexString [const char*] The hexadecimal string to convert.
 * @param binaryData [unsigned char*] The output buffer to store the binary data.
 */
void GSMOTAUpdater::hexStringToBinary(const char* hexString, unsigned char* binaryData)
{
	size_t len = strlen(hexString);
	for (size_t i = 0; i < len; i += 2)
	{
		const char tmpString[] = {hexString[i], hexString[i + 1]};
		sscanf(tmpString, "%2hhx", &binaryData[i / 2]);
	}
}

/**
 * Invokes the registered progress callback with current download progress.
 *
 * @param progress [unsigned long] Current bytes downloaded.
 * @param total [unsigned long] Total bytes to download.
 */
void GSMOTAUpdater::downloadFirmwareProgress(unsigned long progress, unsigned long total)
{
	if (_progress_callback != nullptr)
	{
		_progress_callback(progress, total);
	}
}

/**
 * Logs a debug message if debug is enabled.
 *
 * @param format [const char*] Printf-style format string.
 * @param ... Variadic format arguments.
 */
void GSMOTAUpdater::logDebug(const char* format, ...)
{
	if (!debugEnabled) return;

	char buffer[256];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	Serial.printf("[D][GSMOTAUpdater][H:%u] %s\n", ESP.getFreeHeap(), buffer);
}

/**
 * Logs an error message if debug is enabled.
 *
 * @param format [const char*] Printf-style format string.
 * @param ... Variadic format arguments.
 */
void GSMOTAUpdater::logError(const char* format, ...)
{
	if (!debugEnabled) return;

	char buffer[256];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	Serial.printf("[E][GSMOTAUpdater][H:%u] %s\n", ESP.getFreeHeap(), buffer);
}
