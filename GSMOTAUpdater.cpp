#include "GSMOTAUpdater.h"

GSMOTAUpdater::GSMOTAUpdater_Progress GSMOTAUpdater::_progress_callback = nullptr;

/**
 * Constructs a new GSMOTAUpdater object.
 * 
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
	fileSize = 0;
	currentChunkByte = 0;
	currentByte = 0;
	rangeStart = 0;
	rangeEnd = 0;
	serverPort = 443;
	ATResponse = "";
}

/**
 * Initializes the GSMOTAUpdater with the specified parameters.
 *
 * @param server_address The address of the server to connect to.
 * @param server_port The port number of the server.
 * @param download_path The path where the downloaded file will requested from server.
 * @param file_size The size of the file to be downloaded.
 * @param serial_AT The hardware serial object for AT commands.
 * @param file_system The file system object for file operations.
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
 * Downloads a file from a server using GPRS connection.
 *
 * @param fileName The name of the file to save in memory with downloaded data.
 *
 * @returns True if the download is successful, false otherwise.
 */
bool GSMOTAUpdater::download(const char *fileName)
{
	if(!isInitialized) {
		gota_log_e("class not initialized");
		return false;
	}

	File file = fileSystem->open(fileName, FILE_WRITE);
	rangeEnd = chunkSize;

	while (isDownloadComplete == false)
	{
		if (!file)
		{
			file = fileSystem->open(fileName, FILE_WRITE);
			if (!file) {
				gota_log_e("file not open");
				return false;
			}
		}

		if (wasConnectionLost)
		{
			file.seek(currentChunkByte);
			wasConnectionLost = false;
		}

		if (sendATCommand(("AT+CIPSTART=\"TCP\",\"" + String(serverAddress) + "\",443").c_str(), "OK", 10000))
		{
			// Waiting for TCP to connect
			while (isTCPConnected == false)
			{
				while (SerialAT->available() > 0)
				{
					char c = SerialAT->read();
					ATResponse += c;

					if (ATResponse.indexOf(F("CONNECT OK")) != -1)
					{
						#if DEBUG
							gota_log_d("connected to server");
						#endif
						isTCPConnected = true;
						rstATResponse();
					}
					else if (ATResponse.indexOf(F("CONNECT FAIL")) != -1)
					{
						isTCPConnected = false;
						rstATResponse();
						gota_log_e("could connect to server");
						return false;
					}
				}
			}

			// Once connected, we make the GET request to fetch the file.
			std::string request = ("GET " + downloadPath + " HTTP/1.1\r\nHost: " + serverAddress + "\r\nRange: bytes=" + String(rangeStart) + "-" + String(rangeEnd) + "\r\nConnection: keep-alive\r\n\r\n").c_str();
			int requestLength = request.length();

			std::string cipSendReq = ("AT+CIPSEND=" + String(requestLength)).c_str();

			if (sendATCommand(cipSendReq.c_str(), ">", 5000))
			{
				if (sendATCommand(request.c_str(), "SEND OK", 10000))
				{
					while (isTCPConnected)
					{
						while (SerialAT->available() > 0)
						{
							char c = SerialAT->read();
							ATResponse += c;
							//Serial.print(c);

							if (waitingForData)
							{
								if (ATResponse.indexOf("+CIPRXGET: 1") != -1)
								{ // Data has been received by the modem.
									waitingForData = false;
									rstATResponse();
									SerialAT->println("AT+CIPRXGET=3,128"); // Send command to fetch 128 bytes of data from the modem.
								}
								else if (ATResponse.indexOf("\nCLOSED") != -1)
								{
									connectionClosed();
								}
							}
							else if (ATResponse.endsWith("\nOK"))
							{
								if (ATResponse.indexOf("+CIPRXGET: 3,0,0") != -1)
								{ // The modem no longer has data for us, we need to wait for it to receive it.
									waitingForData = true;
									rstATResponse();
								}
								else if (ATResponse.indexOf("\nCLOSED") != -1)
								{
									connectionClosed();
								}
								else
								{ // We got data, time for processing.
									if (!isHeadersRead)
									{ // We remove the headers and write the bit of data we got.
										if(ATResponse.indexOf("48545450") /* = HTTP */ != -1 && ATResponse.indexOf("0D0A0D0A") /* = \r\n\r\n */ != -1) {
											ATResponse = ATResponse.substring(ATResponse.indexOf("0D0A0D0A") + 8, ATResponse.indexOf("\nOK", ATResponse.indexOf("0D0A0D0A")));
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

									if (currentByte >= rangeEnd)
									{
										chunkDownloaded = true;
										currentChunkByte = rangeEnd + 1;
										rangeStart = currentByte;
										rangeEnd = rangeStart + chunkSize;

										if (sendATCommand(F("AT+CIPCLOSE"), F("CLOSE OK"), 5000))
										{
											isTCPConnected = false;
											waitingForData = true;
											isHeadersRead = false;
											break;
										}
										else
										{
											gota_log_e("connection could be closed");
											break;
										}
									}

									if (currentByte >= fileSize)
									{
										chunkDownloaded = true;
										isDownloadComplete = true;
										isTCPConnected = false;
										waitingForData = true;
										isHeadersRead = false;
										file.close();

										return true;
									}

									delay(25); // Give the modem a tiny bit of time, otherwise it does not like it and some data ends up missing.
									SerialAT->println("AT+CIPRXGET=3,128");
								}
							}
							else if (ATResponse.indexOf(F("\nCLOSED")) != -1)
							{
								connectionClosed();
							}
						}
					}
				}
			}
		}
		else
		{
			gota_log_e("connection error");
		}
	}

	file.close();
	return false;
}

/**
 * Sends an AT command to the SIM800C and waits for the expected response.
 *
 * @param command The AT command to send.
 * @param expectedResponse The expected response from the SIM800C.
 * @param timeout The maximum time to wait for the response, in milliseconds.
 *
 * @returns True if the expected response is received within the timeout period, false otherwise.
 */
bool GSMOTAUpdater::sendATCommand(const char *command, const char *expectedResponse, unsigned long timeout)
{
	if(!isInitialized) {
		gota_log_e("class not initialized");
		return false;
	}

	SerialAT->println(command);
	unsigned long startTime = millis();

	while (millis() - startTime < timeout)
	{
		if (SerialAT->available())
		{
			char c = SerialAT->read();
			ATResponse += c;

			if (ATResponse.endsWith(expectedResponse))
			{
				rstATResponse();
				return true;
			}
		}
	}

	rstATResponse();
	return false;
}


/**
 * Writes binary data to a file.
 *
 * @param file The file object to write the data to.
 * @param hexString The hexadecimal string returned from the modem.
 */
void GSMOTAUpdater::writeDataToFile(File &file, String &hexString)
{
	if (hexString.length() > 1) {
		size_t binaryLength = strlen(hexString.c_str()) / 2;
		unsigned char binaryData[binaryLength];
		hexStringToBinary(hexString.c_str(), binaryData);

		file.write(binaryData, sizeof(binaryData));
		currentByte += sizeof(binaryData);

		downloadFirmwareProgress(currentByte, fileSize);

		/*Serial.print(currentByte);
		Serial.print(" / ");
		Serial.print(rangeStart);
		Serial.print("-");
		Serial.print(rangeEnd);
		Serial.print(" / ");
		Serial.print(fileSize);
		Serial.print(" / ");
		Serial.println(sizeof(binaryData));*/
	}
}

/**
 * Handles the event when the TCP connection is closed or lost.
 * Resets the necessary variables before attempting to reconnect.
 */
void GSMOTAUpdater::connectionClosed()
{
	gota_log_d("connection closed/lost");
	rangeStart = currentChunkByte;
	currentByte = rangeStart;
	rangeEnd = rangeStart + chunkSize;
	isTCPConnected = false;
	waitingForData = true;
	isHeadersRead = false;
	wasConnectionLost = true;
	rstATResponse();
	delay(250);
}

/**
 * Verifies the MD5 hash of the downloaded file.
 *
 * @param fileName The name of the file to verify.
 * @param knownMD5 The known MD5 hash to compare against.
 *
 * @returns True if the MD5 hash matches the known MD5 hash, False otherwise.
 */
bool GSMOTAUpdater::verifyMD5(const char *fileName, char *knownMD5)
{
	if(!isInitialized) {
		gota_log_e("class not initialized");
		return false;
	}

	if (fileSystem->exists(fileName))
	{
		File file;
		MD5Builder md5;

		uint8_t bufferSize = 128;
    uint8_t buffer[bufferSize];
    int bytesRead = 0;
    unsigned long currentBytes = 0;

		file = fileSystem->open(fileName, FILE_READ);
		md5.begin();

		if (!file)
		{
			gota_log_e("file not open");
			return false;
		}

    while (file.available()) {
      buffer[bytesRead] = file.read();
      bytesRead++;

      if (bytesRead == bufferSize) {
        md5.add(buffer, bufferSize);
        currentBytes += bufferSize;
        bytesRead = 0;
      }
    }

    if (bytesRead > 0) {
      md5.add(buffer, bytesRead);
      currentBytes += bytesRead;
    }

		file.close();
		md5.calculate();

		if (String(knownMD5) == md5.toString())
		{
			#if DEBUG
				gota_log_d("match, %s == %s (calculated)", knownMD5, md5.toString().c_str());
			#endif

			return true;
		}
		else
		{
			gota_log_e("mismatch, %s != %s (calculated)", knownMD5, md5.toString().c_str());
			return false;
		}
	}
	else
	{
		gota_log_e("file not found");
		return false;
	}
}

/**
 * Expand the gzip file
 *
 * @return True if expansion succesful
 */
bool GSMOTAUpdater::expandFile(const char *src, const char *dest) {
	if(!isInitialized) {
		gota_log_e("Class: Not initialized");
		return false;
	}

	bool ret = false;
	GzUnpacker *GZUnpacker = new GzUnpacker();

	GZUnpacker->haltOnError(false);
	GZUnpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn); // prevent the partition from exploding, recommended
	GZUnpacker->setGzProgressCallback(BaseUnpacker::defaultProgressCallback); // targzNullProgressCallback or defaultProgressCallback
	GZUnpacker->setLoggerCallback(BaseUnpacker::targzPrintLoggerCallback);
	GZUnpacker->setPsram(true);

	if(!GZUnpacker->gzExpander(*fileSystem, src, *fileSystem, dest)) {
		Serial.printf("gzExpander failed with return code #%d", GZUnpacker->tarGzGetError() );
		//setError(GOTA_GZIP_FAIL);
	} else {
		ret = true;
		fileSystem->remove(src); 
	}

	delete GZUnpacker;
	
	return ret;
}


/**
 * Starts the flashing process of the ESP32.
 *
 * @param fileName The name of the file containing the firmware.
 *
 * @returns True if the update was successful, false otherwise.
 */
bool GSMOTAUpdater::performUpdate(const char *fileName) {
	if(!isInitialized) {
		gota_log_e("class not initialized");
		return false;
	}

	File file = fileSystem->open(fileName);
	if (file) {
		size_t updateSize = file.size();

		if (updateSize > 0) {
			if (Update.begin(updateSize)) {    
				size_t written = Update.writeStream(file);

				if (written == updateSize) {
					#if DEBUG
						gota_log_d("written %d/%d successfully", written, updateSize);
					#endif
				}
				else {
					gota_log_e("written only %d/%d.", written, updateSize);
				}

				if (Update.end()) {
					if (Update.isFinished()) {
						#if DEBUG
							gota_log_d("flashing completed");
						#endif
						file.close();
						fileSystem->remove(fileName); 
						return true;
					}
					else {
						gota_log_e("could not complete (1)");
						return false;
					}
				}
				else {
					gota_log_e("could not complete (2)");
					return false;
				}

			}
			else
			{
				gota_log_e("could not complete, not enough space");
				return false;
			}
		}
		else {
			gota_log_e("could not complete, file is empty");
			return false;
		}

		file.close();        
	}
	else {
		gota_log_e("could not complete, file not found");
		return false;
	}

	return false;
}

void GSMOTAUpdater::onDownloadFirmwareProgress(GSMOTAUpdater_Progress fn)
{		
    _progress_callback = fn;
}

void GSMOTAUpdater::downloadFirmwareProgress(unsigned long progress, unsigned long total)
{
    if (_progress_callback != nullptr)
        _progress_callback(progress, total);
}

void GSMOTAUpdater::rstATResponse()
{
	ATResponse = "";
}

/**
 * Converts a hexadecimal string to binary data.
 *
 * @param hexString The hexadecimal string to convert.
 * @param binaryData The output buffer to store the binary data.
 */
void GSMOTAUpdater::hexStringToBinary(const char* hexString, unsigned char* binaryData) {
    size_t len = strlen(hexString);
    for (size_t i = 0; i < len; i += 2) {
				const char tmpString[] = {hexString[i],  hexString[i+ 1]};
        sscanf(tmpString, "%2hhx", &binaryData[i / 2]);
    }
}