/*
  The MIT License (MIT)
  Copyright (c) Kiyo Chinzei (kchinzei@gmail.com)
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
/*
  ESP8266-AS7265-Server
  Make Asayake to Wake Project.
  Kiyo Chinzei
  https://github.com/kchinzei/ESP8266-AS7265-Server
*/
/*
  This program was modified from
  https://tttapa.github.io/ESP8266/Chap01%20-%20ESP8266.html / GNU GPL 3
  https://web.is.tokushima-u.ac.jp/wp/blog/2019/07/12/esp32-webサーバ上でグラフ表示chart-js/

  About AS7265, see
  https://www.sparkfun.com/products/15050
  https://github.com/sparkfun/SparkFun_AS7265x_Arduino_Library / CC Share-alike 4.0

  You may also need;
  WebSocket (one from Arduino's library manager is too old)
  https://github.com/Links2004/arduinoWebSockets / GNU LESSER 2.1
  WifiManager for Captive Portal / MIT
  https://github.com/tzapu/WiFiManager
  Arduino ESP8266 filesystem uploader to upload files to SPIFFS
  https://github.com/esp8266/arduino-esp8266fs-plugin / GNU GPL 2.0
*/
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>

#include <time.h>

#include "SparkFun_AS7265X.h"
AS7265X sensor;
#include <Wire.h>
#include <elapsedMillis.h>
#include "bufferedFile.h"

ESP8266WebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const char* TimeZoneStr = "JST-9";
const char* NTPServerName1 = "ntp.nict.jp";
const char* NTPServerName2 = "ntp.jst.mfeed.ad.jp";

const unsigned long WifiManager_TimeOutSec = 180;
const char *OTAName = "ESP8266";
const char *OTAPassword = "you_must_set_your_pw";
const char *mdnsName = "esp8266";

/*
 This program runs in two modes, logging mode and idle mode, according to logging = 1/0.
 In logging mode, it samples fast from AS7265X, write to the log file and stop updating the chart.
 In idle mode, it samples slowly (with more integration), periodically update the chart.
 */
int logging = 0;
int loaded = 0; // Is whole page has loaded?

// WebSocket staff
// Mostly based on Pieter's Beginner's Guide, thank you!


void handleNotFound() {
  // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(webServer.uri())) {
    webServer.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path) {
  // send the right file to the client (if it exists)
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = webServer.streamFile(file, contentType);
    file.close();
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Handle received WebSOcket events
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\r\n", num);
      loaded = 0;
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT: {                    // if new text data is received
        if (strncmp((const char *)payload, "ready", length) == 0) {
          loaded = 1;
        } else if (strncmp((const char *)payload, "onToggleLogBtn", length) == 0) {
          if (logging == 0) {
            start_Logging();
          } else {
            end_Logging();
          }
        }
      }
      break;
  }
}

// Maxlen is 32. cf: https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html#spiffs-file-system-limitations
String logFilename = "";

char * WebSocketMsg_LoggingStarted = "loggingStarted";
char * WebSocketMsg_LoggingEnded   = "loggingEnded";
char * WebSocketMsg_EnableDownloadLogFile  = "enableDownloadLogfile";
char * WebSocketMsg_DisableDownloadLogFile = "disableDownloadLogfile";

void notifySocketStatus() {
  char *payload;
  if (logging == 0) {
    payload = WebSocketMsg_LoggingEnded;
    webSocket.broadcastTXT(payload, strlen(payload));
    if (logFilename != "") {
      String s = WebSocketMsg_EnableDownloadLogFile;
      s += " ";
      s += logFilename;
      webSocket.broadcastTXT(s.c_str(), s.length());
    }
  } else {
    payload = WebSocketMsg_LoggingStarted;
    webSocket.broadcastTXT(payload, strlen(payload));
    payload = WebSocketMsg_DisableDownloadLogFile;
    webSocket.broadcastTXT(payload, strlen(payload));
  }
}


// Logging staff

const char SENSOR_JSON[] PROGMEM = R"=====({"vA":%5.1f,"vB":%5.1f,"vC":%5.1f,"vD":%5.1f,"vE":%5.1f,"vF":%5.1f,"vG":%5.1f,"vH":%5.1f,"vI":%5.1f,"vJ":%5.1f,"vK":%5.1f,"vL":%5.1f,"vR":%5.1f,"vS":%5.1f,"vT":%5.1f,"vU":%5.1f,"vV":%5.1f,"vW":%5.1f}%s)=====";
const char SENSOR_CSV[] PROGMEM = R"=====(%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f%s)=====";

elapsedMillis elapsedIdle;
elapsedMillis elapsedLogtime;
const unsigned long IntervalIdle = 1000; // msec
const unsigned long maxLogtime = 10000; // msec

bufferedFile fpLogging;

void start_Logging() {
  if (logging == 1) return;
 
  if (logFilename.length() > 0 && SPIFFS.exists(logFilename))
      SPIFFS.remove(logFilename);
  logFilename = "/";
  logFilename += getCurrentTimeStr();
  logFilename += ".csv";
  fpLogging.open(logFilename);

  sensor.setIntegrationCycles(9); // 10 integration; 56 msec per cycle
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_CONTINUOUS); //All 6 channels on all devices
  
  elapsedLogtime = 0;

  logging = 1;
  notifySocketStatus();  
}

void end_Logging() {
  if (logging == 0) return;
  fpLogging.close();
  // Serial.printf("Log %ld samples in file %s\r\n", logging, logFilename.c_str());
  // listDir();
  sensor.setIntegrationCycles(49); // 50 integration; 56 msec per cycle
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default
  logging = 0;
  notifySocketStatus();
}

void sensor_loop() {
  if (logging) {
    if (sensor.dataAvailable()) {
        const char *s = sensor_sampling(SENSOR_CSV);
        fpLogging.write((const uint8_t *)s, strlen(s));
        logging++;
    }
    if (elapsedLogtime > maxLogtime) {
      end_Logging();
    }
  } else {
    // When doing log saving, it stops updating webSocket.
    // if ((elapsedIdle > IntervalIdle) && sensor.dataAvailable()) {
    if (sensor.dataAvailable()) {
      elapsedIdle = 0;
      const char *s = sensor_sampling(SENSOR_JSON);
      sensor.enableIndicator();
      if (loaded) webSocket.broadcastTXT(s, strlen(s));
      sensor.disableIndicator();
      sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default
    }
  }
}

const char *sensor_sampling(const char *formatstr) {
  static char _payload[360]; // Possible max is 4294967296.0 = 2^32, and format will safely fit.
  snprintf_P(_payload, sizeof(_payload), formatstr,
             sensor.getCalibratedA(),
             sensor.getCalibratedB(),
             sensor.getCalibratedC(),
             sensor.getCalibratedD(),
             sensor.getCalibratedE(),
             sensor.getCalibratedF(),
             sensor.getCalibratedG(),
             sensor.getCalibratedH(),
             sensor.getCalibratedI(),
             sensor.getCalibratedJ(),
             sensor.getCalibratedK(),
             sensor.getCalibratedL(),
             sensor.getCalibratedR(),
             sensor.getCalibratedS(),
             sensor.getCalibratedT(),
             sensor.getCalibratedU(),
             sensor.getCalibratedV(),
             sensor.getCalibratedW(),
             "\r\n");
  return _payload;
}

void start_Sensor() {
  if (sensor.begin() == false) {
    Serial.println("Sensor does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }

  // Once the sensor is started we can increase the I2C speed
  Wire.setClock(400000);
  sensor.setIntegrationCycles(9); // 10 integration; 56 msec per cycle
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default
  // 

  // sensor.setIntegrationCycles(1);
  // 0 seems to cause the sensors to read very slowly
  // 1*2.8ms = 5.6ms per reading
  // But we need two integration cycles so 89Hz is aproximately the fastest read rate

  sensor.enableIndicator();
  sensor.disableBulb(AS7265x_LED_WHITE);
  sensor.disableBulb(AS7265x_LED_IR);
  sensor.disableBulb(AS7265x_LED_UV);
}


// Other startup routines.
// Mostly based on Pieter's Beginner's Guide, thank you!


void startWiFi() {
  WiFiManager wifiManager;

  wifiManager.setTimeout(WifiManager_TimeOutSec);
  if ( !wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  Serial.print("IP \"");
  Serial.print(WiFi.localIP());
  Serial.println("\" connected\r\n");
  Serial.println("\r\n");
}

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  // ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

void startSPIFFS() {
  SPIFFS.begin();
  Serial.println("SPIFFS started.");
  // listDir();
}

void startWebSocket() {
  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  Serial.println("WebSocket server started.");
}

void startMDNS() {
  MDNS.begin(mdnsName);
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startNTP() {
  configTzTime(TimeZoneStr, NTPServerName1, NTPServerName2);   // esp8266/arduino 2.7.0 or later.
}

void startServer() {
  // We don't use .serveStatic() and .on(). Instead every access is done by onNotFound().
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("HTTP server started.");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("");

  startSPIFFS();
  cleanupLogfiles();

  startWiFi();
  startOTA();
  startWebSocket();
  startMDNS();
  startNTP();

  startServer();
  start_Sensor();
}

void loop(void) {
  webSocket.loop();
  webServer.handleClient();
  ArduinoOTA.handle();
  sensor_loop();
}

void listDir() {
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {                      // List the file system contents
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
  }
  Serial.println("");
}

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename) {
  // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  else if (filename.endsWith(".csv")) return "text/csv";
  return "text/plain";
}

void cleanupLogfiles() {
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {                      // List the file system contents
    String fileName = dir.fileName();
    if (fileName.endsWith(".csv")) {
      SPIFFS.remove(fileName);
    }
  }  
}

const char *getCurrentTimeStr() {
  static char _tstr[32];
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  sprintf(_tstr, "%04d%02d%02d-%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  return _tstr;
}
