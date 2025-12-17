#ifndef GSMOTAUPDATER_H
#define GSMOTAUPDATER_H

#include <Arduino.h>
#include <MD5Builder.h>
#include <FS.h>
#include <Update.h>

class GSMOTAUpdater
{
public:
	// Constants - Buffer and chunk sizes
	static constexpr size_t AT_BUFFER_SIZE = 128;
	static constexpr int DEFAULT_CHUNK_SIZE = 25000;
	static constexpr int DEFAULT_SERVER_PORT = 443;

	// Constants - Timeouts (ms)
	static constexpr unsigned long TCP_CONNECT_TIMEOUT = 10000;
	static constexpr unsigned long CIPSEND_TIMEOUT = 5000;
	static constexpr unsigned long CIPCLOSE_TIMEOUT = 5000;
	static constexpr unsigned long SEND_OK_TIMEOUT = 10000;

	// Constants - Delays (ms)
	static constexpr unsigned long MODEM_READ_DELAY = 25;
	static constexpr unsigned long CONNECTION_LOST_DELAY = 250;

	GSMOTAUpdater();

	int chunkSize = DEFAULT_CHUNK_SIZE;

	void init(const char *, int, const char *, unsigned long, HardwareSerial *, FS *);
	bool download(const char *);
	bool verifyMD5(const char *, char *);
	bool performUpdate(const char *);

	typedef std::function<void(unsigned long, unsigned long)> GSMOTAUpdater_Progress;
	void onDownloadFirmwareProgress(GSMOTAUpdater_Progress fn);

	void setDebug(bool enabled);

private:
	HardwareSerial *SerialAT;
	fs::FS *fileSystem = nullptr;

	bool isInitialized;
	bool isTCPConnected;
	bool wasConnectionLost;
	bool waitingForData;
	bool isHeadersRead;
	bool chunkDownloaded;
	bool isDownloadComplete;
	bool debugEnabled = false;

	unsigned long fileSize;
	unsigned long currentChunkByte;
	unsigned long currentByte;
	unsigned long rangeStart;
	unsigned long rangeEnd;

	String serverAddress;
	int serverPort;
	String downloadPath;
	String ATResponse;

	bool sendATCommand(const char *, const char *, unsigned long);
	void writeDataToFile(File &, String &);
	void connectionClosed();
	void rstATResponse();
	void hexStringToBinary(const char*, unsigned char*);
	void resetConnectionState();
	bool waitForTCPConnection();

	void logDebug(const char* format, ...);
	void logError(const char* format, ...);

	static void downloadFirmwareProgress(unsigned long, unsigned long);
	static GSMOTAUpdater_Progress _progress_callback;
};

#endif
