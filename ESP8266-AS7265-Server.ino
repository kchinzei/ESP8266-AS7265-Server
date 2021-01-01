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

#include "SparkFun_AS7265X.h"
AS7265X sensor;
#include <Wire.h>
#include <elapsedMillis.h>

ESP8266WebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const unsigned long WifiManager_TimeOutSec = 180;
const char *OTAName = "ESP8266";
const char *OTAPassword = "you_must_set_your_pw";
const char *mdnsName = "esp8266";

elapsedMillis sensorElapsed;
const unsigned long DELAY = 1000;

const char *index_html = "/index.html";

void handleRoot() {
  File fp = SPIFFS.open(index_html, "r");
  if (fp) {
    webServer.send(200, "text/html", fp.readString());
    fp.close();
  } else {
    Serial.printf("Fail to load %s", index_html);
  }
}

void handleNotFound() {
  webServer.send(404, "text/plain", "File not found.");
}

void start_Sensor() {
  if(sensor.begin() == false) {
    Serial.println("Sensor does not appear to be connected. Please check wiring. Freezing...");
    while(1);
  }

  // Once the sensor is started we can increase the I2C speed
  // Wire.setClock(400000);

  // sensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_CONTINUOUS); //All 6 channels on all devices
  
  // sensor.setIntegrationCycles(1); 
  // 0 seems to cause the sensors to read very slowly
  // 1*2.8ms = 5.6ms per reading
  // But we need two integration cycles so 89Hz is aproximately the fastest read rate

  sensor.enableIndicator();
  sensor.disableBulb(AS7265x_LED_WHITE);
  sensor.disableBulb(AS7265x_LED_IR);
  sensor.disableBulb(AS7265x_LED_UV);
}

const char SENSOR_JSON[] PROGMEM = R"=====({"vA":%5.1f,"vB":%5.1f,"vC":%5.1f,"vD":%5.1f,"vE":%5.1f,"vF":%5.1f,"vG":%5.1f,"vH":%5.1f,"vI":%5.1f,"vJ":%5.1f,"vK":%5.1f,"vL":%5.1f,"vR":%5.1f,"vS":%5.1f,"vT":%5.1f,"vU":%5.1f,"vV":%5.1f,"vW":%5.1f})=====";

void sensor_loop() {
  char payload[256]; // "v[A-L,R-W]":%5.1f x 18ch nicely fit.
  sensor.disableIndicator();
  sensor.takeMeasurements();
  snprintf_P(payload, sizeof(payload), SENSOR_JSON, sensor.getCalibratedA(),
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
                                                    sensor.getCalibratedW());
  // Serial.println(payload);                                                    
  webSocket.broadcastTXT(payload, strlen(payload));
  sensor.enableIndicator();
}

void startWiFi() {
  WiFiManager wifiManager;

  wifiManager.setTimeout(WifiManager_TimeOutSec);
  if( !wifiManager.autoConnect()) {
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

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
}

void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  Serial.println("WebSocket server started.");
}

void startMDNS() { // Start the mDNS responder
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startServer() {
  webServer.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");
  webServer.serveStatic("/Chart.min.js", SPIFFS, "/Chart.min.js");
  webServer.serveStatic("/style.css", SPIFFS, "/style.css");
  webServer.on("/", handleRoot);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("HTTP server started.");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("");

  startWiFi();
  startOTA();
  startSPIFFS();
  startWebSocket();
  startMDNS();
  startServer();

  start_Sensor();
}

void loop(void) {
  webSocket.loop();
  webServer.handleClient();
  ArduinoOTA.handle();
  
  if (sensorElapsed > DELAY) {
    sensorElapsed = 0;
    sensor_loop();
    // Serial.println("Update sensor");
  }
}
