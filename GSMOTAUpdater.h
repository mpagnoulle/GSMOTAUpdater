#ifndef GSMOTAUPDATER_H
#define GSMOTAUPDATER_H

#include <Arduino.h>
#include <MD5Builder.h>
#include <FS.h>
#include <Update.h>

#define DEBUG 			true

class GSMOTAUpdater
{
	public:
		GSMOTAUpdater();

		int chunkSize = 25000; // bytes
		char md5Hash[33] = ""; // md5 hash for verification

		void init(const char *, int, const char *, unsigned long, HardwareSerial *, FS *);
		bool download(const char *);
		bool verifyMD5(const char *, char *);
		bool performUpdate(const char *);
		typedef std::function<void(unsigned long, unsigned long)> GSMOTAUpdater_Progress;
		void onDownloadFirmwareProgress(GSMOTAUpdater_Progress fn);

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
		static void downloadFirmwareProgress(unsigned long, unsigned long);
		static GSMOTAUpdater_Progress _progress_callback;
};

#endif


#if defined ESP32
	#define HEAP_AVAILABLE() ESP.getFreeHeap()

	#ifdef ESP32
		#define GOTA_LOG_FORMAT(letter, format)  "[" #letter "][%s:%u][H:%u] %s(): " format "\r\n", __FILE__, __LINE__, HEAP_AVAILABLE(), __FUNCTION__

		#if defined DEBUG_ESP_PORT
			#define gota_log_d(format, ...) DEBUG_ESP_PORT.printf(GOTA_LOG_FORMAT(N, format), ##__VA_ARGS__);
			#define gota_log_e(format, ...) DEBUG_ESP_PORT.printf(GOTA_LOG_FORMAT(E, format), ##__VA_ARGS__);

		#else
			#define gota_log_d(format, ...) Serial.printf(GOTA_LOG_FORMAT(N, format), ##__VA_ARGS__);
			#define gota_log_e(format, ...) Serial.printf(GOTA_LOG_FORMAT(E, format), ##__VA_ARGS__);
		#endif
	#endif
#endif
