// Sensor type: build with -DSENSOR_DHT22 or -DSENSOR_DS18B20
#if !defined(SENSOR_DHT22) && !defined(SENSOR_DS18B20)
#error "Define SENSOR_DHT22 or SENSOR_DS18B20 in build flags"
#endif

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#ifdef SENSOR_DHT22
#include <DHT.h>
#else
#include <OneWire.h>
#include <DallasTemperature.h>
#endif
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266httpUpdate.h>
#include <time.h>
#include "bitmaps.h"
#include <ESP8266WebServer.h>
#include <tempbot_common.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "1.0.16"

#define SENSOR_PIN 14
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_I2C_ADDR 0x3C
#define FRAME_DELAY 42
#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 25
#define QUEUE_FILE   "/queue.csv"
#define DROPPED_FILE "/dropped.txt"
#define MAX_QUEUE_ENTRIES 1440

// --- Sensor objects ---
#ifdef SENSOR_DHT22
DHT dht(SENSOR_PIN, DHT22);
#else
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
#endif
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Config (unified struct, same layout for all boards = 850 bytes) ---
char webAppUrl[150]        = "";
char timerDelayStr[10]     = "10";
char lineToken[200]        = "";
char lineGroupId[40]       = "";
char minTempAlert[10]      = "20.0";
char maxTempAlert[10]      = "35.0";
char minHumidAlert[10]     = "30.0";  // DS18B20: stored but unused
char maxHumidAlert[10]     = "80.0";  // DS18B20: stored but unused
char boardName[32]         = "";
char bitmapName[20]        = "cat";
char staticIP[16]          = "";
char otaPassword[32]       = "";
char otaVersionUrl[150]    = "";
char otaBinUrl[150]        = "";
char tempCalibrationStr[10]= "0.0";

unsigned long timerDelay   = 1800000;
int failedSyncCount        = 0;
int droppedEntries         = 0;

enum AlertState { STATE_NORMAL, STATE_ALERT_LOW, STATE_ALERT_HIGH };
AlertState lastAlertState  = STATE_NORMAL;
unsigned long lastLineNotifyTime = 0;
unsigned long lastSensorErrorNotifyTime = 0;
#ifdef SENSOR_DHT22
AlertState lastHumidAlertState = STATE_NORMAL;
unsigned long lastHumidLineNotifyTime = 0;
#endif
bool isBootNotificationSent = false;

ESP8266WebServer server(80);

float dailyMinTemp = 999.0;
float dailyMaxTemp = -999.0;
#ifdef SENSOR_DHT22
float dailyMinHumid = 999.0;
float dailyMaxHumid = -999.0;
#endif
int lastDayOfMinMax        = -1;
unsigned long lastSyncTimeEpoch   = 0;
unsigned long lastSyncTimeMillis  = 0;
unsigned long lastTime     = 0;

String currentStatus = "STARTING";
float currentTemp    = -999;
#ifdef SENSOR_DHT22
float currentHumid   = -999;
#endif

int8_t shiftX = 0;
int8_t shiftY = 0;

// --- Dropped count helpers ---
int loadDroppedCount() {
  if (!LittleFS.exists(DROPPED_FILE)) return 0;
  File f = LittleFS.open(DROPPED_FILE, "r");
  if (!f) return 0;
  int n = f.readStringUntil('\n').toInt();
  f.close();
  return n;
}

void saveDroppedCount(int count) {
  File f = LittleFS.open(DROPPED_FILE, "w");
  if (f) { f.println(String(count)); f.close(); }
}

// --- Config save/load ---
void saveConfig() {
  File f = LittleFS.open("/config.bin", "w");
  if (!f) return;
  f.write((uint8_t*)webAppUrl,         sizeof(webAppUrl));
  f.write((uint8_t*)timerDelayStr,     sizeof(timerDelayStr));
  f.write((uint8_t*)lineToken,         sizeof(lineToken));
  f.write((uint8_t*)minTempAlert,      sizeof(minTempAlert));
  f.write((uint8_t*)maxTempAlert,      sizeof(maxTempAlert));
  f.write((uint8_t*)lineGroupId,       sizeof(lineGroupId));
  f.write((uint8_t*)boardName,         sizeof(boardName));
  f.write((uint8_t*)bitmapName,        sizeof(bitmapName));
  f.write((uint8_t*)staticIP,          sizeof(staticIP));
  f.write((uint8_t*)minHumidAlert,     sizeof(minHumidAlert));
  f.write((uint8_t*)maxHumidAlert,     sizeof(maxHumidAlert));
  f.write((uint8_t*)otaPassword,       sizeof(otaPassword));
  f.write((uint8_t*)otaVersionUrl,     sizeof(otaVersionUrl));
  f.write((uint8_t*)otaBinUrl,         sizeof(otaBinUrl));
  f.write((uint8_t*)tempCalibrationStr,sizeof(tempCalibrationStr));
  f.close();
}

void loadConfig() {
  if (!LittleFS.exists("/config.bin")) return;
  File f = LittleFS.open("/config.bin", "r");
  if (!f) return;
  size_t sz = f.size();
  f.readBytes(webAppUrl,     sizeof(webAppUrl));
  f.readBytes(timerDelayStr, sizeof(timerDelayStr));
  if (sz >= 840) {
    // unified format (850 bytes): has minHumidAlert/maxHumidAlert between staticIP and otaPassword
    f.readBytes(lineToken,          sizeof(lineToken));
    f.readBytes(minTempAlert,       sizeof(minTempAlert));
    f.readBytes(maxTempAlert,       sizeof(maxTempAlert));
    f.readBytes(lineGroupId,        sizeof(lineGroupId));
    f.readBytes(boardName,          sizeof(boardName));
    f.readBytes(bitmapName,         sizeof(bitmapName));
    f.readBytes(staticIP,           sizeof(staticIP));
    f.readBytes(minHumidAlert,      sizeof(minHumidAlert));
    f.readBytes(maxHumidAlert,      sizeof(maxHumidAlert));
    f.readBytes(otaPassword,        sizeof(otaPassword));
    f.readBytes(otaVersionUrl,      sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,          sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
  } else if (sz >= 820) {
    // Farm03 old format (830 bytes): no humidity fields
    f.readBytes(lineToken,          sizeof(lineToken));
    f.readBytes(minTempAlert,       sizeof(minTempAlert));
    f.readBytes(maxTempAlert,       sizeof(maxTempAlert));
    f.readBytes(lineGroupId,        sizeof(lineGroupId));
    f.readBytes(boardName,          sizeof(boardName));
    f.readBytes(bitmapName,         sizeof(bitmapName));
    f.readBytes(staticIP,           sizeof(staticIP));
    f.readBytes(otaPassword,        sizeof(otaPassword));
    f.readBytes(otaVersionUrl,      sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,          sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
    strcpy(minHumidAlert, "30.0");
    strcpy(maxHumidAlert, "80.0");
  } else if (sz >= 550) {
    // Farm02 older format (has humidity but older field order)
    f.readBytes(lineToken,     sizeof(lineToken));
    f.readBytes(minTempAlert,  sizeof(minTempAlert));
    f.readBytes(maxTempAlert,  sizeof(maxTempAlert));
    f.readBytes(lineGroupId,   sizeof(lineGroupId));
    f.readBytes(boardName,     sizeof(boardName));
    f.readBytes(bitmapName,    sizeof(bitmapName));
    f.readBytes(staticIP,      sizeof(staticIP));
    f.readBytes(minHumidAlert, sizeof(minHumidAlert));
    f.readBytes(maxHumidAlert, sizeof(maxHumidAlert));
    f.readBytes(otaPassword,   sizeof(otaPassword));
    f.readBytes(otaVersionUrl, sizeof(otaVersionUrl));
    f.readBytes(otaBinUrl,     sizeof(otaBinUrl));
    f.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
  } else if (sz >= 472) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    f.readBytes(boardName,    sizeof(boardName));
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
  } else if (sz >= 452) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    f.readBytes(boardName,    sizeof(boardName));
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
  } else if (sz >= 420) {
    f.readBytes(lineToken,    sizeof(lineToken));
    f.readBytes(minTempAlert, sizeof(minTempAlert));
    f.readBytes(maxTempAlert, sizeof(maxTempAlert));
    f.readBytes(lineGroupId,  sizeof(lineGroupId));
    boardName[0] = '\0';
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
  } else {
    lineToken[0] = '\0'; lineGroupId[0] = '\0'; boardName[0] = '\0';
    strcpy(minTempAlert, "20.0"); strcpy(maxTempAlert, "35.0");
    strcpy(minHumidAlert, "30.0"); strcpy(maxHumidAlert, "80.0");
  }
  f.close();
  Serial.printf("Config loaded (%d bytes): URL=%s delay=%s\n", (int)sz, webAppUrl, timerDelayStr);
}

// --- Daily min/max ---
void updateDailyMinMax(float temp
#ifdef SENSOR_DHT22
  , float humid
#endif
) {
  time_t now = time(nullptr);
  if (now < 1000000000) return;
  struct tm* tm = localtime(&now);
  int day = tm->tm_mday;
  if (lastDayOfMinMax != day) {
    dailyMinTemp = temp; dailyMaxTemp = temp;
#ifdef SENSOR_DHT22
    dailyMinHumid = humid; dailyMaxHumid = humid;
#endif
    lastDayOfMinMax = day;
    Serial.println("Daily min/max reset.");
  } else {
    if (temp < dailyMinTemp) dailyMinTemp = temp;
    if (temp > dailyMaxTemp) dailyMaxTemp = temp;
#ifdef SENSOR_DHT22
    if (humid < dailyMinHumid) dailyMinHumid = humid;
    if (humid > dailyMaxHumid) dailyMaxHumid = humid;
#endif
  }
}

// --- Web config HTML ---
#ifdef SENSOR_DHT22
const char CONFIG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><title>TempBot Config</title></head><body>
<h2>TempBot Configuration</h2>
<form method='GET' action='/save'>
<label>WebApp URL:</label><input name='url' value='%s'><br/>
<label>Sync Delay (min):</label><input name='delay' value='%s'><br/>
<label>LINE Token:</label><input name='token' value='%s'><br/>
<label>Board Name:</label><input name='board' value='%s'><br/>
<label>Min Temp (C):</label><input name='min_temp' value='%s'><br/>
<label>Max Temp (C):</label><input name='max_temp' value='%s'><br/>
<label>Min Humid (%):</label><input name='min_humid' value='%s'><br/>
<label>Max Humid (%):</label><input name='max_humid' value='%s'><br/>
<label>OTA Password:</label><input name='ota_pass' value='%s'><br/>
<label>Static IP:</label><input name='static_ip' value='%s'><br/>
<hr/><h3>Calibration &amp; OTA</h3>
<label>Temp Offset (C):</label><input name='temp_cal' value='%s'><br/>
<label>Bitmap:</label><input name='bitmap' value='%s'><br/>
<label>OTA Version URL:</label><input name='ota_version_url' value='%s' size='60'><br/>
<label>OTA Firmware URL:</label><input name='ota_bin_url' value='%s' size='60'><br/>
<input type='submit' value='Save'>
</form></body></html>
)HTML";
#else
const char CONFIG_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><title>TempBot Config</title></head><body>
<h2>TempBot Configuration</h2>
<form method='GET' action='/save'>
<label>WebApp URL:</label><input name='url' value='%s'><br/>
<label>Sync Delay (min):</label><input name='delay' value='%s'><br/>
<label>LINE Token:</label><input name='token' value='%s'><br/>
<label>Board Name:</label><input name='board' value='%s'><br/>
<label>Min Temp (C):</label><input name='min_temp' value='%s'><br/>
<label>Max Temp (C):</label><input name='max_temp' value='%s'><br/>
<label>OTA Password:</label><input name='ota_pass' value='%s'><br/>
<label>Static IP:</label><input name='static_ip' value='%s'><br/>
<hr/><h3>Calibration &amp; OTA</h3>
<label>Temp Offset (C):</label><input name='temp_cal' value='%s'><br/>
<label>Bitmap:</label><input name='bitmap' value='%s'><br/>
<label>OTA Version URL:</label><input name='ota_version_url' value='%s' size='60'><br/>
<label>OTA Firmware URL:</label><input name='ota_bin_url' value='%s' size='60'><br/>
<input type='submit' value='Save'>
</form></body></html>
)HTML";
#endif

void handleRoot() {
  char buf[2048];
#ifdef SENSOR_DHT22
  snprintf(buf, sizeof(buf), CONFIG_HTML,
    webAppUrl, timerDelayStr, lineToken, boardName,
    minTempAlert, maxTempAlert, minHumidAlert, maxHumidAlert,
    otaPassword, staticIP, tempCalibrationStr, bitmapName,
    otaVersionUrl, otaBinUrl);
#else
  snprintf(buf, sizeof(buf), CONFIG_HTML,
    webAppUrl, timerDelayStr, lineToken, boardName,
    minTempAlert, maxTempAlert,
    otaPassword, staticIP, tempCalibrationStr, bitmapName,
    otaVersionUrl, otaBinUrl);
#endif
  server.send(200, "text/html", buf);
}

void handleSave() {
  if (server.hasArg("url"))             strncpy(webAppUrl,         server.arg("url").c_str(),             sizeof(webAppUrl)-1);
  if (server.hasArg("delay"))           strncpy(timerDelayStr,     server.arg("delay").c_str(),           sizeof(timerDelayStr)-1);
  if (server.hasArg("token"))           strncpy(lineToken,         server.arg("token").c_str(),           sizeof(lineToken)-1);
  if (server.hasArg("board"))           strncpy(boardName,         server.arg("board").c_str(),           sizeof(boardName)-1);
  if (server.hasArg("min_temp"))        strncpy(minTempAlert,      server.arg("min_temp").c_str(),        sizeof(minTempAlert)-1);
  if (server.hasArg("max_temp"))        strncpy(maxTempAlert,      server.arg("max_temp").c_str(),        sizeof(maxTempAlert)-1);
#ifdef SENSOR_DHT22
  if (server.hasArg("min_humid"))       strncpy(minHumidAlert,     server.arg("min_humid").c_str(),       sizeof(minHumidAlert)-1);
  if (server.hasArg("max_humid"))       strncpy(maxHumidAlert,     server.arg("max_humid").c_str(),       sizeof(maxHumidAlert)-1);
#endif
  if (server.hasArg("ota_pass"))        strncpy(otaPassword,       server.arg("ota_pass").c_str(),        sizeof(otaPassword)-1);
  if (server.hasArg("static_ip"))       strncpy(staticIP,          server.arg("static_ip").c_str(),       sizeof(staticIP)-1);
  if (server.hasArg("ota_version_url")) strncpy(otaVersionUrl,     server.arg("ota_version_url").c_str(), sizeof(otaVersionUrl)-1);
  if (server.hasArg("ota_bin_url"))     strncpy(otaBinUrl,         server.arg("ota_bin_url").c_str(),     sizeof(otaBinUrl)-1);
  if (server.hasArg("temp_cal"))        strncpy(tempCalibrationStr,server.arg("temp_cal").c_str(),        sizeof(tempCalibrationStr)-1);
  if (server.hasArg("bitmap")) {
    String bmp = server.arg("bitmap"); bmp.trim();
    if (bmp.length() > 0 && bmp.length() < 20) { bmp.toCharArray(bitmapName, 20); setBitmap(bitmapName); }
  }
  saveConfig();
  server.send(200, "text/plain", "Config saved, rebooting...");
  delay(500);
  ESP.restart();
}

void handleQueue() {
  if (!LittleFS.exists(QUEUE_FILE)) { server.send(200, "text/plain", "Queue empty."); return; }
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) { server.send(500, "text/plain", "Failed to open queue."); return; }
#ifdef SENSOR_DHT22
  String content = "Timestamp,Temperature,Humidity\n";
#else
  String content = "Timestamp,Temperature\n";
#endif
  while (f.available()) content += f.readStringUntil('\n');
  f.close();
  server.send(200, "text/plain", content);
}

// --- Display functions ---
void showOnDisplay(String title, String msg, float temp = -999) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0, 0);  display.println(title);
  display.setCursor(0, 18); display.println(msg);
  if (temp != -999) {
    display.setTextSize(2); display.setCursor(0, 38);
    display.print(temp, 1); display.print(" C");
  }
  display.display();
}

void playAnimation(int repetitions, String message) {
  for (int i = 0; i < repetitions; i++) {
    for (int f = 0; f < currentFrameCount; f++) {
      display.clearDisplay();
      display.drawBitmap(32, 0, currentFrames[f], FRAME_WIDTH, FRAME_HEIGHT, WHITE);
      display.setTextSize(1); display.setTextColor(WHITE);
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((128 - w) / 2, 56);
      display.print(message); display.display();
      delay(FRAME_DELAY); ArduinoOTA.handle(); yield();
    }
  }
}

void drawWiFiIcon(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) {
    display.drawLine(x, y, x+10, y+8, WHITE);
    display.drawLine(x+10, y, x, y+8, WHITE);
    return;
  }
  int32_t rssi = WiFi.RSSI();
  int bars = (rssi >= -55) ? 4 : (rssi >= -70) ? 3 : (rssi >= -85) ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int bh = (i+1)*2, bx = x+(i*3), by = y+8-bh;
    if (i < bars) display.fillRect(bx, by, 2, bh, WHITE);
    else          display.drawRect(bx, by, 2, bh, WHITE);
  }
}

#ifdef SENSOR_DHT22
void updateDisplay(float temp, float humid, String status) {
#else
void updateDisplay(float temp, String status) {
#endif
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(2+shiftX, 1+shiftY);
  display.print(getBoardIdentifier());
  display.setCursor(62+shiftX, 1+shiftY); display.print(status);
  drawWiFiIcon(115+shiftX, shiftY);
  display.drawFastHLine(0, 10+shiftY, 128, WHITE);

#ifdef SENSOR_DHT22
  if (temp > -100 && humid >= 0) {
    updateDailyMinMax(temp, humid);
    display.setTextSize(3); display.setCursor(2+shiftX, 20+shiftY); display.print(temp, 1);
    display.setTextSize(2); display.setCursor(84+shiftX, 18+shiftY);
    display.print((int)humid); display.print("%");
    int bX=84, bY=40, bW=38, bH=5;
    display.drawRect(bX+shiftX, bY+shiftY, bW, bH, WHITE);
    int fill = constrain((int)(humid/100.0*bW), 0, bW);
    display.fillRect(bX+shiftX, bY+shiftY, fill, bH, WHITE);
  } else {
    display.setTextSize(2); display.setCursor(10+shiftX, 25+shiftY); display.print("SENSOR ERR");
  }
#else
  if (temp > -100 && temp < 200) {
    updateDailyMinMax(temp);
    display.setTextSize(3); display.setCursor(2+shiftX, 20+shiftY);
    display.print(temp, 1); display.print(" C");
  } else {
    display.setTextSize(2); display.setCursor(10+shiftX, 25+shiftY); display.print("SENSOR ERR");
  }
#endif

  if (WiFi.status() == WL_CONNECTED) {
    display.setTextSize(1); display.setCursor(2+shiftX, 56+shiftY);
    time_t now = time(nullptr);
#ifdef SENSOR_DHT22
    int state = (now < 1000000000) ? 0 : (millis()/15000) % 4;
#else
    int state = (now < 1000000000) ? 0 : (millis()/15000) % 3;
#endif
    if (state == 0) { display.print("IP: "); display.print(WiFi.localIP().toString()); }
    else if (state == 1) {
      display.print("Time " + formatTime(now, false) + " | Sync " + formatTime(lastSyncTimeEpoch, false));
    } else if (state == 2) {
      if (dailyMinTemp > 500.0 || dailyMaxTemp < -500.0) display.print("T Min/Max: --/--");
      else { display.print("T Min/Max: "); display.print(dailyMinTemp,1); display.print("/"); display.print(dailyMaxTemp,1); }
    }
#ifdef SENSOR_DHT22
    else if (state == 3) {
      if (dailyMinHumid > 500.0 || dailyMaxHumid < -500.0) display.print("H Min/Max: --/--");
      else { display.print("H Min/Max: "); display.print((int)round(dailyMinHumid)); display.print("/"); display.print((int)round(dailyMaxHumid)); display.print("%"); }
    }
#endif
  } else {
    display.setTextSize(1); display.setCursor(2+shiftX, 56+shiftY); display.print("OFFLINE MODE");
  }
  display.display();
}

// --- Offline queue ---
int getQueueSize() {
  if (!LittleFS.exists(QUEUE_FILE)) return 0;
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    ESP.wdtFeed();
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() > 2) count++;
  }
  f.close();
  return count;
}

#ifdef SENSOR_DHT22
void queueData(float temp, float humid) {
#else
void queueData(float temp) {
#endif
  int size = getQueueSize();
  if (size >= MAX_QUEUE_ENTRIES) {
    droppedEntries++; saveDroppedCount(droppedEntries);
    Serial.println("Queue full! Removing oldest entry...");
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      src.readStringUntil('\n'); // skip oldest
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n'); line.trim();
        if (line.length() > 2) dst.println(line);
      }
    }
    src.close(); dst.close();
    LittleFS.remove(QUEUE_FILE); LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    size--;
  }
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (f) {
    time_t now = time(nullptr);
    if (now < 1000000000 && lastSyncTimeEpoch >= 1000000000)
      now = lastSyncTimeEpoch + (millis() - lastSyncTimeMillis) / 1000;
#ifdef SENSOR_DHT22
    f.println(String(now) + "," + String(temp,1) + "," + String(humid,1));
#else
    f.println(String(now) + "," + String(temp,1));
#endif
    f.close();
    Serial.print("Queued. Size: "); Serial.println(size + 1);
  }
}

void flushQueue() {
  if (strlen(webAppUrl) < 10) return;
  if (!LittleFS.exists(QUEUE_FILE)) return;

  int entryCount = getQueueSize();
  if (entryCount == 0) { LittleFS.remove(QUEUE_FILE); return; }

  Serial.print("Flushing "); Serial.print(entryCount); Serial.println(" queued entries...");
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(5, 10); display.print("SYNCING OFFLINE DATA");
  display.setCursor(5, 25); display.print(entryCount); display.print(" buffered entries");
  display.display();

  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(4096, 1024);
  String boardID = getBoardIdentifier();
  int sentCount = 0;

  // Pass 1: stream line-by-line
  {
    File f = LittleFS.open(QUEUE_FILE, "r");
    while (f.available()) {
      ESP.wdtFeed();
      if (WiFi.status() != WL_CONNECTED) break;
      String line = f.readStringUntil('\n'); line.trim();
      if (line.length() <= 2) continue;

      int c1 = line.indexOf(',');
      if (c1 < 0) { sentCount++; continue; }
#ifdef SENSOR_DHT22
      int c2 = line.indexOf(',', c1+1);
      String ts = "", t = "", h = "";
      if (c2 < 0) { t = line.substring(0, c1); h = line.substring(c1+1); }
      else { ts = line.substring(0, c1); t = line.substring(c1+1, c2); h = line.substring(c2+1); }
      String url = String(webAppUrl) + "?temperature=" + t + "&humidity=" + h
                 + "&board_id=" + urlEncode(boardID) + "&queued=1";
      if (ts.length() > 0 && ts != "0") url += "&timestamp=" + ts;
#else
      int c2 = line.indexOf(',', c1+1);
      String ts = line.substring(0, c1);
      String t  = (c2 < 0) ? line.substring(c1+1) : line.substring(c1+1, c2);
      String url = String(webAppUrl) + "?temperature=" + t
                 + "&board_id=" + urlEncode(boardID) + "&queued=1";
      if (ts.length() > 0 && ts != "0") url += "&timestamp=" + ts;
#endif
      HTTPClient http; bool ok = false;
      if (http.begin(client, url)) {
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setTimeout(10000);
        ESP.wdtDisable(); int code = http.GET();
        String body = (code == 200) ? http.getString() : "";
        ESP.wdtEnable(8000);
        ok = (code == 200 && body.startsWith("OK"));
        if (!ok) Serial.println("Flush failed: HTTP " + String(code));
        http.end();
      }
      client.stop();
      if (ok) {
        sentCount++;
        display.fillRect(0, 40, 128, 20, BLACK);
        display.setCursor(5, 42); display.print("Sent: "); display.print(sentCount);
        display.print("/"); display.print(entryCount); display.display();
      } else break;
      ArduinoOTA.handle(); delay(500); yield();
    }
    f.close();
  }

  if (sentCount > 0) { lastSyncTimeEpoch = time(nullptr); lastSyncTimeMillis = millis(); }

  if (sentCount >= entryCount) { LittleFS.remove(QUEUE_FILE); Serial.println("Queue fully flushed!"); return; }

  // Pass 2: write unsent entries back
  {
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      int lineNum = 0;
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n'); line.trim();
        if (line.length() <= 2) continue;
        if (lineNum++ < sentCount) continue;
        dst.println(line);
      }
    }
    src.close(); dst.close();
    LittleFS.remove(QUEUE_FILE); LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    Serial.printf("Partial flush: %d/%d\n", sentCount, entryCount);
  }
}

// --- Logic helpers ---
bool validateWebAppUrl() {
  if (strlen(webAppUrl) < 10) { currentStatus = "NO URL"; return false; }
  return true;
}

bool readSensorData() {
#ifdef SENSOR_DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) t += getTempCalibrationOffset();
  if (isnan(t) || isnan(h)) {
    currentStatus = "SENS ERR"; failedSyncCount++;
    if (millis() - lastSensorErrorNotifyTime >= 3600000UL) {
      notifyViaGAS("⚠️ [TempBot Alert]\nBoard: " + getBoardIdentifier() + "\nDHT22 not responding!");
      lastSensorErrorNotifyTime = millis();
    }
    return false;
  }
  currentTemp = t; currentHumid = h;
#else
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C && t > -55.0) t += getTempCalibrationOffset();
  if (t == DEVICE_DISCONNECTED_C || t < -55.0) {
    currentStatus = "SENS ERR"; failedSyncCount++;
    if (millis() - lastSensorErrorNotifyTime >= 3600000UL) {
      notifyViaGAS("⚠️ [TempBot Alert]\nBoard: " + getBoardIdentifier() + "\nDS18B20 not responding!");
      lastSensorErrorNotifyTime = millis();
    }
    return false;
  }
  currentTemp = t;
#endif
  return true;
}

bool checkWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
#ifdef SENSOR_DHT22
    queueData(currentTemp, currentHumid);
#else
    queueData(currentTemp);
#endif
    currentStatus = "BUFFERED:" + String(getQueueSize());
    failedSyncCount++;
    return false;
  }
  return true;
}

bool syncToGAS(WiFiClientSecure &client) {
  HTTPClient http;
#ifdef SENSOR_DHT22
  String url = String(webAppUrl) + "?temperature=" + String(currentTemp,1)
             + "&humidity=" + String(currentHumid,1)
             + "&board_id=" + urlEncode(getBoardIdentifier());
#else
  String url = String(webAppUrl) + "?temperature=" + String(currentTemp,1)
             + "&board_id=" + urlEncode(getBoardIdentifier());
#endif
  if (!http.begin(client, url)) { currentStatus = "ERR HTTP_BEGIN"; return false; }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setTimeout(10000);
  ESP.wdtDisable();
  int httpCode = http.GET();
  String payload = (httpCode == 200) ? http.getString() : "";
  ESP.wdtEnable(8000);
  if (httpCode != 200 || !payload.startsWith("OK")) {
    currentStatus = "ERR " + String(httpCode);
    Serial.println("syncToGAS: HTTP " + String(httpCode));
    http.end(); return false;
  }
  currentStatus = "SYNCED"; failedSyncCount = 0;
  lastSyncTimeEpoch = time(nullptr); lastSyncTimeMillis = millis();
  http.end(); return true;
}

void fetchAndApplySettings() {
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(4096, 1024);
  HTTPClient http;
  String url = String(webAppUrl) + "?get_settings=1&board_id=" + urlEncode(getBoardIdentifier());
  if (!http.begin(client, url)) return;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setTimeout(10000);
  ESP.wdtDisable(); int sc = http.GET(); ESP.wdtEnable(8000);
  if (sc != 200) { http.end(); Serial.println("fetchSettings HTTP: " + String(sc)); return; }
  String payload = http.getString(); http.end();
  Serial.println("fetchSettings payload: " + payload);
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return;
  bool changed = false;
  if (!doc["maxTemp"].isNull()) { String s = String((float)doc["maxTemp"],1); s.toCharArray(maxTempAlert, sizeof(maxTempAlert)); changed = true; }
  if (!doc["minTemp"].isNull()) { String s = String((float)doc["minTemp"],1); s.toCharArray(minTempAlert, sizeof(minTempAlert)); changed = true; }
  if (!doc["bitmap"].isNull() && doc["bitmap"].is<const char*>()) {
    const char* bmp = doc["bitmap"].as<const char*>();
    if (strlen(bmp) > 0 && strlen(bmp) < sizeof(bitmapName)) {
      strncpy(bitmapName, bmp, sizeof(bitmapName)-1);
      setBitmap(bitmapName);
      changed = true;
    }
  }
  if (changed) saveConfig();
}

void sendData() {
  if (!validateWebAppUrl()) return;
  if (!readSensorData()) return;
  if (!checkWiFiConnected()) return;
  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(4096, 1024);
  if (syncToGAS(client)) {
    if (droppedEntries > 0) {
      notifyViaGAS("⚠️ [TempBot] Data Loss\nBoard: " + getBoardIdentifier() + "\nหายระหว่างออฟไลน์: " + String(droppedEntries) + " entries");
      droppedEntries = 0; saveDroppedCount(0);
    }
    fetchAndApplySettings();
    flushQueue();
  } else { failedSyncCount++; }
}

void checkWiFiConnection() {
  static unsigned long last = 0;
  if (millis() - last >= 10000) {
    last = millis();
    if (WiFi.status() != WL_CONNECTED) { currentStatus = "RECONNECTING"; WiFi.begin(); }
  }
}

// --- Config portal (short press) ---
void openConfigPortal() {
  currentStatus = "CONFIG MODE";
#ifdef SENSOR_DHT22
  updateDisplay(currentTemp, currentHumid, currentStatus);
#else
  updateDisplay(currentTemp, currentStatus);
#endif
  playAnimation(1, "CONFIG MODE");

  WiFiManager wm;
  WiFiManagerParameter p_url("url", "Google WebApp URL", webAppUrl, 150);
  WiFiManagerParameter p_delay("delay", "Sync Delay (Minutes)", timerDelayStr, 10);
  WiFiManagerParameter p_token("token", "LINE Channel Access Token", lineToken, 200);
  WiFiManagerParameter p_groupid("groupid", "LINE Group ID", lineGroupId, 40);
  WiFiManagerParameter p_min_temp("min_temp", "Min Temp Alert (C)", minTempAlert, 10);
  WiFiManagerParameter p_max_temp("max_temp", "Max Temp Alert (C)", maxTempAlert, 10);
#ifdef SENSOR_DHT22
  WiFiManagerParameter p_min_humid("min_humid", "Min Humid Alert (%)", minHumidAlert, 10);
  WiFiManagerParameter p_max_humid("max_humid", "Max Humid Alert (%)", maxHumidAlert, 10);
#endif
  WiFiManagerParameter p_board("board_name", "Board Name", boardName, 32);
  WiFiManagerParameter p_ota_pass("ota_pass", "ArduinoOTA Password", otaPassword, 32);
  WiFiManagerParameter p_ota_ver("ota_version_url", "OTA Version URL", otaVersionUrl, 150);
  WiFiManagerParameter p_ota_bin("ota_bin_url", "OTA Firmware URL", otaBinUrl, 150);
  WiFiManagerParameter p_cal("temp_cal", "Temp Calibration Offset (C)", tempCalibrationStr, 10);
  WiFiManagerParameter p_bitmap("bitmap", "Bitmap (cat/chicken/fish/tree)", bitmapName, 20);
  WiFiManagerParameter p_ip("static_ip", "Static IP (empty=DHCP)", staticIP, 16);

  wm.addParameter(&p_url); wm.addParameter(&p_delay); wm.addParameter(&p_token);
  wm.addParameter(&p_groupid); wm.addParameter(&p_min_temp); wm.addParameter(&p_max_temp);
#ifdef SENSOR_DHT22
  wm.addParameter(&p_min_humid); wm.addParameter(&p_max_humid);
#endif
  wm.addParameter(&p_board); wm.addParameter(&p_ota_pass);
  wm.addParameter(&p_ota_ver); wm.addParameter(&p_ota_bin);
  wm.addParameter(&p_cal); wm.addParameter(&p_bitmap); wm.addParameter(&p_ip);
  wm.setConfigPortalTimeout(120);

  String boardID = "ESP8266_" + String(ESP.getChipId(), HEX); boardID.toUpperCase();
  wm.startConfigPortal(boardID.c_str());

  if (p_url.getValue()[0])   strncpy(webAppUrl,    p_url.getValue(),     sizeof(webAppUrl));
  if (p_delay.getValue()[0]) { strncpy(timerDelayStr, p_delay.getValue(), sizeof(timerDelayStr)); unsigned long m=atol(timerDelayStr); if(m>0) timerDelay=m*60000; }
  strncpy(lineToken,         p_token.getValue(),    sizeof(lineToken));
  strncpy(lineGroupId,       p_groupid.getValue(),  sizeof(lineGroupId));
  strncpy(minTempAlert,      p_min_temp.getValue(), sizeof(minTempAlert));
  strncpy(maxTempAlert,      p_max_temp.getValue(), sizeof(maxTempAlert));
#ifdef SENSOR_DHT22
  strncpy(minHumidAlert,     p_min_humid.getValue(),sizeof(minHumidAlert));
  strncpy(maxHumidAlert,     p_max_humid.getValue(),sizeof(maxHumidAlert));
#endif
  strncpy(boardName,         p_board.getValue(),    sizeof(boardName));
  strncpy(otaPassword,       p_ota_pass.getValue(), sizeof(otaPassword));
  strncpy(otaVersionUrl,     p_ota_ver.getValue(),  sizeof(otaVersionUrl));
  strncpy(otaBinUrl,         p_ota_bin.getValue(),  sizeof(otaBinUrl));
  strncpy(tempCalibrationStr,p_cal.getValue(),      sizeof(tempCalibrationStr));
  strncpy(bitmapName,        p_bitmap.getValue(),   sizeof(bitmapName));
  if (strlen(bitmapName) > 0) setBitmap(bitmapName);
  saveConfig();
  playAnimation(1, "RESTARTING...");
  delay(500); ESP.restart();
}

// --- OTA update ---
void checkForOTAUpdate() {
  if (strlen(otaVersionUrl) == 0 || strlen(otaBinUrl) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(10, 20); display.println("Checking for");
  display.setCursor(10, 32); display.println("firmware update..."); display.display();

  WiFiClientSecure client; client.setInsecure(); client.setBufferSizes(16384, 512);
  HTTPClient http;
  if (!http.begin(client, otaVersionUrl)) return;
  ESP.wdtDisable(); int code = http.GET();
  String latest = (code == 200) ? http.getString() : "";
  ESP.wdtEnable(8000); http.end();
  if (code != 200) return;
  latest.trim();
  Serial.printf("OTA: current=%s latest=%s\n", FIRMWARE_VERSION, latest.c_str());

  if (!isNewerVersion(latest, String(FIRMWARE_VERSION))) { Serial.println("Up to date."); return; }

  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(10, 10); display.println("OTA UPDATE");
  display.setCursor(10, 25); display.print("v"); display.print(FIRMWARE_VERSION); display.print(" -> v"); display.println(latest);
  display.setCursor(10, 45); display.println("Downloading..."); display.display();

  client.stop();
  ESP.wdtDisable();
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, otaBinUrl);
  if (ret == HTTP_UPDATE_FAILED) {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(10, 20); display.println("OTA FAILED!");
    display.setCursor(10, 35); display.println(ESPhttpUpdate.getLastErrorString().c_str());
    display.display(); delay(3000);
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  WiFi.setAutoConnect(true); WiFi.setAutoReconnect(true);

  if (LittleFS.begin()) {
    droppedEntries = loadDroppedCount();
    loadConfig();
  } else {
    Serial.println("LittleFS mount failed.");
  }

  unsigned long dm = atol(timerDelayStr);
  if (dm > 0) timerDelay = dm * 60000;

#ifdef SENSOR_DHT22
  dht.begin();
#else
  sensors.begin();
#endif

  Wire.begin(4, 5);
  uint8_t oledAddr = SCREEN_I2C_ADDR;
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) oledAddr = 0x3C;
  else { Wire.beginTransmission(0x3D); if (Wire.endTransmission() == 0) oledAddr = 0x3D; }
  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) { Serial.println("SSD1306 failed"); for(;;); }
  display.clearDisplay(); playAnimation(1, "BOOTING...");

  WiFiManager wm;
  WiFiManagerParameter p_url("url", "Google WebApp URL", webAppUrl, 150);
  WiFiManagerParameter p_delay("delay", "Sync Delay (Minutes)", timerDelayStr, 10);
  WiFiManagerParameter p_token("token", "LINE Channel Access Token", lineToken, 200);
  WiFiManagerParameter p_groupid("groupid", "LINE Group ID", lineGroupId, 40);
  WiFiManagerParameter p_min_temp("min_temp", "Min Temp Alert (C)", minTempAlert, 10);
  WiFiManagerParameter p_max_temp("max_temp", "Max Temp Alert (C)", maxTempAlert, 10);
#ifdef SENSOR_DHT22
  WiFiManagerParameter p_min_humid("min_humid", "Min Humid Alert (%)", minHumidAlert, 10);
  WiFiManagerParameter p_max_humid("max_humid", "Max Humid Alert (%)", maxHumidAlert, 10);
#endif
  WiFiManagerParameter p_board("board_name", "Board Name", boardName, 32);
  WiFiManagerParameter p_ota_pass("ota_pass", "ArduinoOTA Password", otaPassword, 32);
  WiFiManagerParameter p_ota_ver("ota_version_url", "OTA Version URL", otaVersionUrl, 150);
  WiFiManagerParameter p_ota_bin("ota_bin_url", "OTA Firmware URL", otaBinUrl, 150);
  WiFiManagerParameter p_cal("temp_cal", "Temp Calibration Offset (C)", tempCalibrationStr, 10);
  WiFiManagerParameter p_ip("static_ip", "Static IP (empty=DHCP)", staticIP, 16);

  wm.addParameter(&p_ota_ver); wm.addParameter(&p_ota_bin); wm.addParameter(&p_cal);
  wm.addParameter(&p_url); wm.addParameter(&p_delay); wm.addParameter(&p_token);
  wm.addParameter(&p_groupid); wm.addParameter(&p_min_temp); wm.addParameter(&p_max_temp);
#ifdef SENSOR_DHT22
  wm.addParameter(&p_min_humid); wm.addParameter(&p_max_humid);
#endif
  wm.addParameter(&p_board); wm.addParameter(&p_ota_pass); wm.addParameter(&p_ip);
  wm.setConfigPortalTimeout(120); wm.setConnectTimeout(15);

  if (strlen(staticIP) > 0) {
    IPAddress ip, gw, sn;
    if (ip.fromString(staticIP)) { gw.fromString("192.168.0.1"); sn.fromString("255.255.255.0"); wm.setSTAStaticIPConfig(ip, gw, sn); }
  }

  String boardID = "ESP8266_" + String(ESP.getChipId(), HEX); boardID.toUpperCase();
  currentStatus = "WIFI CONNECTING";
  if (!wm.autoConnect(boardID.c_str())) { currentStatus = "WIFI TIMEOUT"; }
  else { playAnimation(1, "WIFI OK!"); currentStatus = "CONNECTED"; }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_GET, handleSave);
  server.on("/queue", HTTP_GET, handleQueue);
  server.begin();

  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0,0); display.println("IP Address:");
  display.println(WiFi.localIP()); display.print("FW v"); display.println(FIRMWARE_VERSION);
  display.display(); delay(3000); display.clearDisplay();

  if (p_url.getValue()[0])   strncpy(webAppUrl,    p_url.getValue(),     sizeof(webAppUrl));
  if (p_delay.getValue()[0]) { strncpy(timerDelayStr, p_delay.getValue(), sizeof(timerDelayStr)); unsigned long m=atol(timerDelayStr); if(m>0) timerDelay=m*60000; }
  strncpy(lineToken,         p_token.getValue(),    sizeof(lineToken));
  strncpy(lineGroupId,       p_groupid.getValue(),  sizeof(lineGroupId));
  strncpy(minTempAlert,      p_min_temp.getValue(), sizeof(minTempAlert));
  strncpy(maxTempAlert,      p_max_temp.getValue(), sizeof(maxTempAlert));
#ifdef SENSOR_DHT22
  strncpy(minHumidAlert,     p_min_humid.getValue(),sizeof(minHumidAlert));
  strncpy(maxHumidAlert,     p_max_humid.getValue(),sizeof(maxHumidAlert));
#endif
  strncpy(boardName,         p_board.getValue(),    sizeof(boardName));
  strncpy(otaPassword,       p_ota_pass.getValue(), sizeof(otaPassword));
  strncpy(otaVersionUrl,     p_ota_ver.getValue(),  sizeof(otaVersionUrl));
  strncpy(otaBinUrl,         p_ota_bin.getValue(),  sizeof(otaBinUrl));
  strncpy(tempCalibrationStr,p_cal.getValue(),      sizeof(tempCalibrationStr));
  saveConfig();

  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov");
  setBitmap(bitmapName);
  ArduinoOTA.setHostname(boardID.c_str());
  if (otaPassword[0]) ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.begin();
  checkForOTAUpdate();
  if (WiFi.status() == WL_CONNECTED) sendData();
  lastTime = millis();
}

// --- Loop ---
unsigned long lastOTACheck = 0;
const unsigned long OTA_CHECK_INTERVAL = 12UL * 60 * 60 * 1000;

void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  static bool lastWiFiConnected = false;
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow && !lastWiFiConnected && getQueueSize() > 0) flushQueue();
  lastWiFiConnected = wifiNow;

  // Button: short press = config portal, 5s hold = factory reset
  static unsigned long pressStart = 0;
  static bool actionTaken = false;
  if (digitalRead(0) == LOW) {
    if (pressStart == 0) { pressStart = millis(); actionTaken = false; }
    unsigned long held = millis() - pressStart;
    if (held >= 5000 && !actionTaken) {
      actionTaken = true;
      playAnimation(2, "FACTORY RESET");
      WiFiManager wm; wm.resetSettings();
      if (LittleFS.begin()) LittleFS.remove("/config.bin");
      delay(1000); ESP.restart();
    } else if (held >= 2000) {
      int cd = 5 - (int)(held/1000); if (cd < 1) cd = 1;
      display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
      display.setCursor(5,5); display.println("KEEP HOLDING:");
      display.setCursor(5,17); display.println("FACTORY RESET IN...");
      display.setTextSize(3); display.setCursor(55,35); display.print(cd);
      display.display(); delay(50); return;
    } else {
      display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
      display.setCursor(2,5); display.println("RELEASE = CONFIG");
      display.setCursor(2,17); display.println("HOLD 5s  = RESET");
      display.drawFastHLine(0,28,128,WHITE);
      display.setTextSize(2); display.setCursor(10,38); display.print("CONFIG?");
      display.display(); delay(50); return;
    }
  } else {
    if (pressStart != 0) {
      unsigned long held = millis() - pressStart;
      bool shortPress = (held < 2000 && !actionTaken);
      pressStart = 0; actionTaken = false;
      if (shortPress) openConfigPortal();
#ifdef SENSOR_DHT22
      else updateDisplay(currentTemp, currentHumid, currentStatus);
#else
      else updateDisplay(currentTemp, currentStatus);
#endif
    }
  }

  checkWiFiConnection();

  if (!isBootNotificationSent && WiFi.status() == WL_CONNECTED) {
    String reason = ESP.getResetReason();
    isBootNotificationSent = true;
    if (!reason.startsWith("Software Watchdog") && !reason.startsWith("Exception")) {
      notifyViaGAS("\n🚀 [BOOT] Board Online!\nName: " + getBoardIdentifier() +
                   "\nIP: " + WiFi.localIP().toString() + "\nReset Reason: " + reason);
    } else {
      Serial.println("Boot notification skipped: " + reason);
    }
  }

  unsigned long now = millis();

  if (now - lastTime >= timerDelay) {
    playAnimation(1, "SENDING DATA");
    sendData();
    playAnimation(1, "DONE!");
    lastTime = now;
  }

  static unsigned long lastShift = 0;
  if (now - lastShift >= 60000) {
    static int ss = 0; ss = (ss+1)%5;
    switch(ss){ case 0: shiftX=0; shiftY=0; break; case 1: shiftX=1; shiftY=1; break;
                case 2: shiftX=-1;shiftY=-1;break; case 3: shiftX=2; shiftY=-1;break;
                case 4: shiftX=-2;shiftY=1; break; }
#ifdef SENSOR_DHT22
    updateDisplay(currentTemp, currentHumid, currentStatus);
#else
    updateDisplay(currentTemp, currentStatus);
#endif
    lastShift = now;
  }

  static unsigned long lastUpdate = 0;
  if (now - lastUpdate >= 2000) {
#ifdef SENSOR_DHT22
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) t += getTempCalibrationOffset();
    if (isnan(t) || isnan(h)) { currentTemp = -999; currentHumid = -999; }
    else { currentTemp = t; currentHumid = h; }
    if (WiFi.status() == WL_CONNECTED && (currentStatus == "RECONNECTING" || currentStatus == "SENS ERR"))
      currentStatus = "CONNECTED";
    updateDisplay(currentTemp, currentHumid, currentStatus);
#else
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55.0) t += getTempCalibrationOffset();
    currentTemp = (t == DEVICE_DISCONNECTED_C || t < -55.0) ? -999 : t;
    if (WiFi.status() == WL_CONNECTED && (currentStatus == "RECONNECTING" || currentStatus == "SENS ERR"))
      currentStatus = "CONNECTED";
    updateDisplay(currentTemp, currentStatus);
#endif
    lastUpdate = now;
  }

  if (strlen(otaVersionUrl) > 0 && strlen(otaBinUrl) > 0) {
    if (now - lastOTACheck >= OTA_CHECK_INTERVAL) { lastOTACheck = now; checkForOTAUpdate(); }
  }
}
