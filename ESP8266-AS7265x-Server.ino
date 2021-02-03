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
  ESP8266-AS7265x-Server
  Make Asayake to Wake Project.
  Kiyo Chinzei
  https://github.com/kchinzei/ESP8266-AS7265x-Server
*/
/*
  This program was modified from
  https://tttapa.github.io/ESP8266/Chap01%20-%20ESP8266.html / GNU GPL 3
  https://m1cr0lab-esp32.github.io/remote-control-with-websocket/websocket-setup/
  https://web.is.tokushima-u.ac.jp/wp/blog/2019/07/12/esp32-webサーバ上でグラフ表示chart-js/

  About AS7265, see
  https://www.sparkfun.com/products/15050
  https://github.com/sparkfun/SparkFun_AS7265x_Arduino_Library / CC Share-alike 4.0

  You may also need;
  ESPAsyncWebServer
  https://github.com/me-no-dev/ESPAsyncWebServer
  ESPAsyncTCP
  https://github.com/me-no-dev/ESPAsyncTCP
  AsyncWiFiManager
  https://github.com/alanswx/ESPAsyncWiFiManager
  
  Arduino ESP8266 filesystem uploader to upload files to SPIFFS
  https://github.com/esp8266/arduino-esp8266fs-plugin / GNU GPL 2.0
*/
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

#define USE_WIFIMANAGER
#ifdef USE_WIFIMANAGER
#include <ESPAsync_WiFiManager.h>
#else
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
const uint32_t connectTimeoutMs = 5000;
const char* ssid_1     = "ssid";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password_1 = "password";     // The password of the Wi-Fi network
const char* ssid_2     = "ssid2";
const char* password_2 = "pssword2";
#endif

#include <time.h>

#include "SparkFun_AS7265X.h"
AS7265X sensor;
#include <Wire.h>
#include <elapsedMillis.h>
#include "bufferedFile.h"
#include "AS7265x_Bulb.h"
AS7265xBulb ledWhite;
AS7265xBulb ledUV;
AS7265xBulb ledIR;

AsyncWebServer webServer(80);
AsyncWebSocket webSocket("/ws");
DNSServer dnsServer;

const char* TimeZoneStr = "JST-9";
const char* NTPServerName1 = "ntp.nict.jp";
const char* NTPServerName2 = "ntp.jst.mfeed.ad.jp";

const char *OTAName = "ESP8266";
const char *OTAPassword = "you_must_set_your_pw";
const char *mdnsName = "esp8266";

const char *btnMessage = "btnMessage";
const char *toggleUVLEDBtn = "ToggleUVLEDBtn";
const char *toggleWhiteLEDBtn = "ToggleWhiteLEDBtn";
const char *toggleIRLEDBtn = "ToggleIRLEDBtn";
const char *toggleCalBtn = "ToggleCalBtn";

/*
 This program runs in two modes, logging mode and idle mode, according to logging = 1/0.
 In logging mode, it samples fast from AS7265X, write to the log file and stop updating the chart.
 In idle mode, it samples slowly (with more integration), periodically update the chart.
 */
int logging = 0;
int logging_toggled = 0;
int ledUV_toggled = 0;
int ledWhite_toggled = 0;
int ledIR_toggled = 0;
int cal_toggled = 0;
int cal_apply = 1;
void start_Logging();
void end_Logging();

// WebSocket staff
// https://github.com/me-no-dev/ESPAsyncWebServer#async-websocket-plugin

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  switch (type) {
    case WS_EVT_CONNECT: {
        end_Logging();
        Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
        client->ping();
      }
      break;
    case WS_EVT_DISCONNECT: {
        end_Logging();
        Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
      }
      break;
    case WS_EVT_ERROR:
      Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
      break;
    case WS_EVT_PONG:
      Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
      break;
    case WS_EVT_DATA: {
      //data packet
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if(info->final && info->index == 0 && info->len == len) {
        //the whole message is in a single frame and we got all of it's data
        //Serial.printf("ws[%s][%u] %s-message[%llu] : '%s'\n", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len, (info->opcode == WS_TEXT)? std::string((const char*) data, len).c_str():"binary");
        if(info->opcode == WS_TEXT) {
          if (strncmp((const char *)data, "onToggleLogBtn", len) == 0) {
              logging_toggled = 1;
          } else if (strncmp((const char *)data, "onToggleUVLEDBtn", len) == 0) {
              ledUV_toggled = 1;
          } else if (strncmp((const char *)data, "onToggleWhiteLEDBtn", len) == 0) {
              ledWhite_toggled = 1;
          } else if (strncmp((const char *)data, "onToggleIRLEDBtn", len) == 0) {
              ledIR_toggled = 1;
          } else if (strncmp((const char *)data, "onToggleCalBtn", len) == 0) {
              cal_toggled = 1;
          } else if (strncmp((const char *)data, "init", len) == 0) {
            notifyLoggingStatus();
            notifyLEDBtn(&ledWhite);
            notifyLEDBtn(&ledIR);
            notifyLEDBtn(&ledUV);
            notifyCalBtn();
          }
        }
      } else {
        // Todo:
        // message is comprised of multiple frames or the frame is split into multiple packets
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

void notifyLoggingStatus() {
  char *payload;
  if (logging == 0) {
    payload = WebSocketMsg_LoggingEnded;
    webSocket.textAll(payload, strlen(payload));
    if (logFilename != "") {
      String s = WebSocketMsg_EnableDownloadLogFile;
      s += " ";
      s += logFilename;
      webSocket.textAll(s.c_str(), s.length());
    }
  } else {
    payload = WebSocketMsg_LoggingStarted;
    webSocket.textAll(payload, strlen(payload));
    payload = WebSocketMsg_DisableDownloadLogFile;
    webSocket.textAll(payload, strlen(payload));
  }
}


// Logging staff
const char SENSOR_JSON[] PROGMEM = R"=====({"vA":%5.1f,"vB":%5.1f,"vC":%5.1f,"vD":%5.1f,"vE":%5.1f,"vF":%5.1f,"vG":%5.1f,"vH":%5.1f,"vR":%5.1f,"vI":%5.1f,"vS":%5.1f,"vJ":%5.1f,"vT":%5.1f,"vU":%5.1f,"vV":%5.1f,"vW":%5.1f,"vK":%5.1f,"vL":%5.1f})=====";
const char SENSOR_CSV[] PROGMEM = "%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f\r\n";
const char SENSOR_CSV_HEADER[] PROGMEM = "410,435,460,485,510,535,560,585,610,645,680,705,730,760,810,860,900,940\r\n";

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
  fpLogging.write_P((const uint8_t *)SENSOR_CSV_HEADER, strlen_P(SENSOR_CSV_HEADER));

  // After some experiments it seems that sampling speed isn't fast as expected
  // by setting it to 1, 9. Reason unclear - because of many tasks in loop()?
  // sensor.setIntegrationCycles(1);  // 2 integration; 5.6 msec per cycle
  // sensor.setIntegrationCycles(9);  // 10 integration; 28 msec per cycle
  sensor.setIntegrationCycles(49); // 50 integration; 140 msec per cycle
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_CONTINUOUS); //All 6 channels on all devices

  elapsedLogtime = 0;
  logging = 1;
  notifyLoggingStatus();
}

void end_Logging() {
  if (logging == 0) return;
  fpLogging.close();
  // Serial.printf("Log %ld samples in file %s\r\n", logging, logFilename.c_str());
  // listDir();
  sensor.setIntegrationCycles(49); // 50 integration; 56 msec per cycle
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default
  logging = 0;
  notifyLoggingStatus();
}

void notifyLEDBtn(AS7265xBulb *bulb) {
  char buf[64];
  const char *btn = toggleWhiteLEDBtn;

  switch (bulb->getBulbtype()) {
    case AS7265x_LED_IR:
      btn = toggleIRLEDBtn;
      break;
    case AS7265x_LED_UV:
      btn = toggleUVLEDBtn;
      break;
  }
  sprintf(buf, "%s %s %s %d", btnMessage, btn, bulb->getBulbtypeString(), bulb->getCurrentIndex());
  webSocket.textAll(buf, strlen(buf));
}

void notifyCalBtn() {
  char buf[64];
  const char* s = cal_apply? "Cal":"Raw";
  sprintf(buf, "%s %s %s", btnMessage, toggleCalBtn, s);
  webSocket.textAll(buf, strlen(buf));
}

void sensor_loop() {
  if (logging_toggled > 0) {
      if (logging == 0)
          start_Logging();
      else
          end_Logging();
      logging_toggled = 0;
  }
  if (logging) {
    if (sensor.dataAvailable()) {
        elapsedIdle = 0;
        const char *s = sensor_sampling(SENSOR_CSV);
        fpLogging.write((const uint8_t *)s, strlen(s));
        logging++;
    }
    if (elapsedLogtime > maxLogtime) {
      end_Logging();
    }
  } else {
    if (ledUV_toggled > 0) {
      ledUV.toggleUp();
      notifyLEDBtn(&ledUV);
      ledUV_toggled = 0;
    }
    if (ledWhite_toggled > 0) {
      ledWhite.toggleUp();
      notifyLEDBtn(&ledWhite);
      ledWhite_toggled = 0;
    }
    if (ledIR_toggled > 0) {
      ledIR.toggleUp();
      notifyLEDBtn(&ledIR);
      ledIR_toggled = 0;
    }
    if (cal_toggled > 0) {
      cal_apply = !cal_apply;
      notifyCalBtn();
      cal_toggled = 0;
    }
    // When doing log saving, it stops updating webSocket.
    if ((elapsedIdle > IntervalIdle) && sensor.dataAvailable()) {
      elapsedIdle = 0;
      const char *s = sensor_sampling(SENSOR_JSON);
      sensor.enableIndicator();
      webSocket.textAll(s, strlen(s));
      sensor.disableIndicator();
      sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default
    }
  }
}

const char *sensor_sampling(const char *formatstr) {
  static char _payload[360]; // Possible max is 4294967296.0 = 2^32, and format will safely fit.
  if (cal_apply)
    snprintf_P(_payload, sizeof(_payload), formatstr,
             sensor.getCalibratedA(),
             sensor.getCalibratedB(),
             sensor.getCalibratedC(),
             sensor.getCalibratedD(),
             sensor.getCalibratedE(),
             sensor.getCalibratedF(),
             sensor.getCalibratedG(),
             sensor.getCalibratedH(),
             sensor.getCalibratedR(),
             sensor.getCalibratedI(),
             sensor.getCalibratedS(),
             sensor.getCalibratedJ(),
             sensor.getCalibratedT(),
             sensor.getCalibratedU(),
             sensor.getCalibratedV(),
             sensor.getCalibratedW(),
             sensor.getCalibratedK(),
             sensor.getCalibratedL());
  else
    snprintf_P(_payload, sizeof(_payload), formatstr,
             (float) sensor.getA(),
             (float) sensor.getB(),
             (float) sensor.getC(),
             (float) sensor.getD(),
             (float) sensor.getE(),
             (float) sensor.getF(),
             (float) sensor.getG(),
             (float) sensor.getH(),
             (float) sensor.getR(),
             (float) sensor.getI(),
             (float) sensor.getS(),
             (float) sensor.getJ(),
             (float) sensor.getT(),
             (float) sensor.getU(),
             (float) sensor.getV(),
             (float) sensor.getW(),
             (float) sensor.getK(),
             (float) sensor.getL());
  return _payload;
}

void start_Sensor() {
  if (sensor.begin() == false) {
    Serial.println("Sensor does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }

  // Once the sensor is started we can increase the I2C speed
  Wire.setClock(400000);
  sensor.setIntegrationCycles(49); // 50 integration; 140 msec per cycle, default
  sensor.setGain(AS7265X_GAIN_64X); // default
  sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT); // default

  sensor.disableIndicator();
  ledUV.init(&sensor, AS7265x_LED_UV, AS7265X_LED_CURRENT_LIMIT_12_5MA);
  ledWhite.init(&sensor, AS7265x_LED_WHITE, AS7265X_LED_CURRENT_LIMIT_25MA);
  ledIR.init(&sensor, AS7265x_LED_IR, AS7265X_LED_CURRENT_LIMIT_25MA);
}


// Other startup routines.
// Mostly based on Pieter's Beginner's Guide, thank you!

#ifdef USE_WIFIMANAGER
void startWiFi() {
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer);
  // ESPAsync_wifiManager.resetSettings();   //reset saved settings
  ESPAsync_wifiManager.autoConnect();
  delay(5000);

  if (WiFi.status() != WL_CONNECTED) {
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

#else
void startWiFi() {
  WiFi.mode(WIFI_STA);
  
  wifiMulti.addAP(ssid_1, password_1);   // add Wi-Fi networks you want to connect to
  wifiMulti.addAP(ssid_2, password_2);

  Serial.println("WifiMulti Connecting");
  while (wifiMulti.run(connectTimeoutMs) != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
    delay(250);
    Serial.print('.');
  }
  Serial.println("\r\n");
  Serial.print("WiFi connected: ");
  Serial.print(WiFi.SSID());
  Serial.print(" ");
  Serial.println(WiFi.localIP());
}
#endif

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
  listDir();
}

void startWebSocket() {
  webSocket.onEvent(onEvent);
  webServer.addHandler(&webSocket);
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
  webServer.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  webServer.begin();
  Serial.println("HTTP server started.");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("");

  startSPIFFS();
  cleanupLogfiles();

  startWiFi();
  startOTA();
  startMDNS();
  startNTP();

  startWebSocket();
  startServer();
  start_Sensor();
}

void loop(void) {
  ArduinoOTA.handle();
  if (elapsedIdle > IntervalIdle)
    webSocket.cleanupClients();
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
