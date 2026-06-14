#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266httpUpdate.h>
#include <time.h>
#include "bitmaps.h"
#include <ESP8266WebServer.h>
#include <tempbot_common.h>
#include <ArduinoJson.h>


#define FIRMWARE_VERSION "1.0.14"

// --- 1. Configuration ---
#define SENSOR_PIN 14        // ขา D5 (สำหรับ DS18B20)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_I2C_ADDR 0x3C 

#define FRAME_DELAY 42
#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 25

// Offline Data Queue
#define QUEUE_FILE        "/queue.csv"
#define DROPPED_FILE      "/dropped.txt"
#define MAX_QUEUE_ENTRIES 1440

// --- 2. Objects ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

char webAppUrl[150] = "";
char timerDelayStr[10] = "10";
char lineToken[200]  = "";   // LINE Messaging API Channel Access Token
char lineGroupId[40] = "";
char minTempAlert[10] = "20.0";
char maxTempAlert[10] = "35.0";
char boardName[32] = "";     // Custom Board Name (เช่น Kitchen, ServerRoom)
char bitmapName[20] = "cat"; // Bitmap set: cat, chicken, fish, tree
char staticIP[16] = "";        // Static IP (empty = DHCP)
char otaPassword[32] = "";   // ArduinoOTA update password
char otaVersionUrl[150] = ""; // URL to version.txt
char otaBinUrl[150] = ""; // URL to firmware .bin
char tempCalibrationStr[10] = "0.0"; // Temperature calibration offset

unsigned long timerDelay = 1800000; // 30 นาที (ค่าเริ่มต้น)
int failedSyncCount = 0;            // นับจำนวนครั้งที่ส่งข้อมูลไม่สำเร็จติดต่อกัน
int droppedEntries = 0;             // นับ entries ที่ถูกลบเมื่อ queue เต็ม

unsigned long lastSensorErrorNotifyTime = 0;
bool isBootNotificationSent = false;

ESP8266WebServer server(80);

float dailyMinTemp = 999.0;
float dailyMaxTemp = -999.0;
int lastDayOfMinMax = -1;
unsigned long lastSyncTimeEpoch = 0;
unsigned long lastSyncTimeMillis = 0;
unsigned long lastTime = 0;

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

void saveConfig() {
  File configFile = LittleFS.open("/config.bin", "w");
  if (configFile) {
    configFile.write((uint8_t*)webAppUrl, sizeof(webAppUrl));
    configFile.write((uint8_t*)timerDelayStr, sizeof(timerDelayStr));
    configFile.write((uint8_t*)lineToken, sizeof(lineToken));
    configFile.write((uint8_t*)minTempAlert, sizeof(minTempAlert));
    configFile.write((uint8_t*)maxTempAlert, sizeof(maxTempAlert));
    configFile.write((uint8_t*)lineGroupId, sizeof(lineGroupId));
    configFile.write((uint8_t*)boardName, sizeof(boardName));
    configFile.write((uint8_t*)bitmapName, sizeof(bitmapName));
    configFile.write((uint8_t*)staticIP, sizeof(staticIP));
    configFile.write((uint8_t*)otaPassword, sizeof(otaPassword));
    configFile.write((uint8_t*)otaVersionUrl, sizeof(otaVersionUrl));
    configFile.write((uint8_t*)otaBinUrl, sizeof(otaBinUrl));
    configFile.write((uint8_t*)tempCalibrationStr, sizeof(tempCalibrationStr));
    configFile.close();
  }
}

void updateDailyMinMax(float temp) {
  time_t now = time(nullptr);
  if (now < 1000000000) {
    // Time not synced yet. Do not update/reset min/max.
    return;
  }
  struct tm* timeinfo = localtime(&now);
  int currentDay = timeinfo->tm_mday;
  
  if (lastDayOfMinMax != currentDay) {
    // Midnight reset!
    dailyMinTemp = temp;
    dailyMaxTemp = temp;
    lastDayOfMinMax = currentDay;
    Serial.println("Daily min/max reset for the new day.");
  } else {
    if (temp < dailyMinTemp) dailyMinTemp = temp;
    if (temp > dailyMaxTemp) dailyMaxTemp = temp;
  }
}

// Web Config HTML and Handlers
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
<label>Static IP:</label><input name='static_ip' value='%s' placeholder='DHCP if empty'><br/>
<hr/>
<h3>Temperature Calibration</h3>
<label>Temp Offset (C):</label><input name='temp_cal' value='%s' placeholder='e.g. -4.29'><br/>
<label>Bitmap:</label><input name='bitmap' value='%s' placeholder='cat, chicken, fish, tree'><br/>
<hr/>
<h3>Auto OTA Settings</h3>
<label>OTA Version URL:</label><input name='ota_version_url' value='%s' size='60'><br/>
<label>OTA Firmware URL:</label><input name='ota_bin_url' value='%s' size='60'><br/>
<input type='submit' value='Save'>
</form>
</body></html>
)HTML";

void handleRoot(){
  char buffer[2048];
  snprintf(buffer, sizeof(buffer), CONFIG_HTML, 
    webAppUrl, timerDelayStr, lineToken, boardName, 
    minTempAlert, maxTempAlert, 
    otaPassword, staticIP, tempCalibrationStr, bitmapName,
    otaVersionUrl, otaBinUrl);
  server.send(200, "text/html", buffer);
}

void handleSave(){
  if(server.hasArg("url")) strncpy(webAppUrl, server.arg("url").c_str(), sizeof(webAppUrl)-1);
  if(server.hasArg("delay")) strncpy(timerDelayStr, server.arg("delay").c_str(), sizeof(timerDelayStr)-1);
  if(server.hasArg("token")) strncpy(lineToken, server.arg("token").c_str(), sizeof(lineToken)-1);
  if(server.hasArg("board")) strncpy(boardName, server.arg("board").c_str(), sizeof(boardName)-1);
  if(server.hasArg("min_temp")) strncpy(minTempAlert, server.arg("min_temp").c_str(), sizeof(minTempAlert)-1);
  if(server.hasArg("max_temp")) strncpy(maxTempAlert, server.arg("max_temp").c_str(), sizeof(maxTempAlert)-1);
  if(server.hasArg("ota_pass")) strncpy(otaPassword, server.arg("ota_pass").c_str(), sizeof(otaPassword)-1);
  if(server.hasArg("static_ip")) strncpy(staticIP, server.arg("static_ip").c_str(), sizeof(staticIP)-1);
  if(server.hasArg("ota_version_url")) strncpy(otaVersionUrl, server.arg("ota_version_url").c_str(), sizeof(otaVersionUrl)-1);
  if(server.hasArg("ota_bin_url")) strncpy(otaBinUrl, server.arg("ota_bin_url").c_str(), sizeof(otaBinUrl)-1);
  if(server.hasArg("temp_cal")) strncpy(tempCalibrationStr, server.arg("temp_cal").c_str(), sizeof(tempCalibrationStr)-1);
  if(server.hasArg("bitmap")) {
    String bmp = server.arg("bitmap");
    bmp.trim();
    if (bmp.length() > 0 && bmp.length() < 20) {
      bmp.toCharArray(bitmapName, 20);
      setBitmap(bitmapName);
    }
  }
  // Save updated config to LittleFS
  saveConfig();
  server.send(200, "text/plain", "Config saved, rebooting...");
  delay(500);
  ESP.restart();
}

void handleQueue() {
  if (!LittleFS.exists(QUEUE_FILE)) {
    server.send(200, "text/plain", "Queue is empty or does not exist.");
    return;
  }
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) {
    server.send(500, "text/plain", "Failed to open queue file.");
    return;
  }
  String content = "Timestamp,Temperature\n";
  while (f.available()) {
    content += f.readStringUntil('\n');
  }
  f.close();
  server.send(200, "text/plain", content);
}

String currentStatus = "STARTING";
float currentTemp = -999;

int8_t shiftX = 0;
int8_t shiftY = 0;

// --- 3. Display Functions ---

void showOnDisplay(String title, String msg, float temp = -999) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.setCursor(0, 18);
  display.println(msg);

  if (temp != -999) {
    display.setTextSize(2);
    display.setCursor(0, 38);
    display.print(temp, 1);
    display.print(" C");
  }
  display.display();
}

void playAnimation(int repetitions, String message) {
  for (int i = 0; i < repetitions; i++) {
    for (int f = 0; f < currentFrameCount; f++) {
      display.clearDisplay();
      display.drawBitmap(32, 0, currentFrames[f], FRAME_WIDTH, FRAME_HEIGHT, WHITE);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(message, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((128 - w) / 2, 56);
      display.print(message);
      display.display();
      delay(FRAME_DELAY);
      ArduinoOTA.handle(); 
      yield(); // ช่วยส่งงานให้ระบบจัดการ WiFi พื้นหลัง
    }
  }
}

void drawWiFiIcon(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) {
    // Draw crossed lines to indicate offline
    display.drawLine(x, y, x + 10, y + 8, WHITE);
    display.drawLine(x + 10, y, x, y + 8, WHITE);
    return;
  }

  int32_t rssi = WiFi.RSSI();
  int bars = 0;
  if (rssi >= -55)      bars = 4;
  else if (rssi >= -70) bars = 3;
  else if (rssi >= -85) bars = 2;
  else if (rssi > -100) bars = 1;

  for (int i = 0; i < 4; i++) {
    int barHeight = (i + 1) * 2;
    int barX = x + (i * 3);
    int barY = y + 8 - barHeight;
    if (i < bars) {
      display.fillRect(barX, barY, 2, barHeight, WHITE);
    } else {
      display.drawRect(barX, barY, 2, barHeight, WHITE);
    }
  }
}

void updateDisplay(float temp, String status) {
  display.clearDisplay();
  
  // 1. แถบแสดงสถานะด้านบน (Top Bar)
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(2 + shiftX, 1 + shiftY);
  
  // แสดงชื่อบอร์ดที่มุมบนซ้าย
  String boardID = getBoardIdentifier();
  display.print(boardID);
  
  // แสดงสถานะที่มุมบนขวา
  display.setCursor(62 + shiftX, 1 + shiftY);
  display.print(status);
  
  drawWiFiIcon(115 + shiftX, shiftY);
  
  // เส้นแบ่งแถบด้านบน
  display.drawFastHLine(0, 10 + shiftY, 128, WHITE);

  if (temp > -100 && temp < 200) {
    updateDailyMinMax(temp);

    // 2. อุณหภูมิปัจจุบัน (ขยับมาตรงกลางจอ)
    display.setTextSize(3);
    display.setCursor(2 + shiftX, 20 + shiftY);
    display.print(temp, 1);
    display.print(" C");
  } else {
    display.setTextSize(2);
    display.setCursor(10 + shiftX, 25 + shiftY);
    display.print("SENSOR ERR");
  }

  // 5. แถบแสดงข้อมูลสลับด้านล่าง (Bottom Bar - no separator line)
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setTextSize(1);
    display.setCursor(2 + shiftX, 56 + shiftY);
    
    int displayState = (millis() / 15000) % 3;
    time_t now = time(nullptr);
    
    if (now < 1000000000) {
      displayState = 0; // Force IP if time not synced
    }

    if (displayState == 0) {
      display.print("IP: ");
      display.print(WiFi.localIP().toString());
    } else if (displayState == 1) {
      String currTime = formatTime(now, false);
      String syncTime = formatTime(lastSyncTimeEpoch, false);
      display.print("Time " + currTime + " | Sync " + syncTime);
    } else if (displayState == 2) {
      if (dailyMinTemp > 500.0 || dailyMaxTemp < -500.0) {
        display.print("T Min/Max: --/--");
      } else {
        display.print("T Min/Max: ");
        display.print(dailyMinTemp, 1);
        display.print("/");
        display.print(dailyMaxTemp, 1);
      }
    }
  } else {
    display.setTextSize(1);
    display.setCursor(2 + shiftX, 56 + shiftY);
    display.print("OFFLINE MODE");
  }

  display.display();
}

// --- 4. Logic Functions ---

// --- 4. Offline Queue Functions ---

int getQueueSize() {
  if (!LittleFS.exists(QUEUE_FILE)) return 0;
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    ESP.wdtFeed();
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 2) count++;
  }
  f.close();
  return count;
}

void queueData(float temp) {
  int size = getQueueSize();
  if (size >= MAX_QUEUE_ENTRIES) {
    droppedEntries++;
    saveDroppedCount(droppedEntries);
    Serial.println("Queue full! Removing oldest entry...");
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      src.readStringUntil('\n'); // skip oldest line
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) dst.println(line);
      }
    }
    src.close();
    dst.close();
    LittleFS.remove(QUEUE_FILE);
    LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    size--;
  }
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (f) {
    time_t now = time(nullptr);
    if (now < 1000000000 && lastSyncTimeEpoch >= 1000000000) {
      unsigned long elapsed = (millis() - lastSyncTimeMillis) / 1000;
      now = lastSyncTimeEpoch + elapsed;
    }
    f.println(String(now) + "," + String(temp, 1));
    f.close();
    Serial.print("Queued. Size: "); Serial.println(size + 1);
  }
}

void flushQueue() {
  if (strlen(webAppUrl) < 10) {
    Serial.println("FlushQueue aborted: No valid WebApp URL.");
    return;
  }
  if (!LittleFS.exists(QUEUE_FILE)) return;

  int entryCount = getQueueSize();
  if (entryCount == 0) { LittleFS.remove(QUEUE_FILE); return; }

  Serial.print("Flushing "); Serial.print(entryCount); Serial.println(" queued entries...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(5, 10);
  display.print("SYNCING OFFLINE DATA");
  display.setCursor(5, 25);
  display.print(entryCount);
  display.print(" buffered entries");
  display.display();

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(4096, 1024);
  String boardID = getBoardIdentifier();
  int sentCount = 0;

  // Pass 1: อ่านและส่งทีละบรรทัด ไม่โหลดทั้งไฟล์เข้า RAM (กัน stack overflow)
  {
    File f = LittleFS.open(QUEUE_FILE, "r");
    while (f.available()) {
      ESP.wdtFeed();
      if (WiFi.status() != WL_CONNECTED) break;

      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() <= 2) continue;

      int firstComma = line.indexOf(',');
      if (firstComma < 0) { sentCount++; continue; }
      int secondComma = line.indexOf(',', firstComma + 1);

      String timestampStr = "", tempStr = "";
      timestampStr = line.substring(0, firstComma);
      if (secondComma < 0) {
        tempStr = line.substring(firstComma + 1);
      } else {
        tempStr = line.substring(firstComma + 1, secondComma);
      }

      String url = String(webAppUrl) + "?temperature=" + tempStr
                 + "&board_id=" + urlEncode(boardID)
                 + "&queued=1";
      if (timestampStr.length() > 0 && timestampStr != "0") {
        url += "&timestamp=" + timestampStr;
      }

      HTTPClient http;
      bool ok = false;
      if (http.begin(client, url)) {
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);
        ESP.wdtDisable();
        int code = http.GET();
        String body = (code == 200) ? http.getString() : "";
        ESP.wdtEnable(8000);
        ok = (code == 200 && body.startsWith("OK"));
        if (!ok) Serial.println("Flush failed: HTTP " + String(code) + " body=" + body.substring(0, 16));
        http.end();
      } else {
        Serial.println("Flush http.begin failed");
      }
      client.stop();

      if (ok) {
        sentCount++;
        display.fillRect(0, 40, 128, 20, BLACK);
        display.setCursor(5, 42);
        display.print("Sent: ");
        display.print(sentCount);
        display.print("/");
        display.print(entryCount);
        display.display();
      } else {
        break;
      }
      ArduinoOTA.handle();
      delay(500);
      yield();
    }
    f.close();
  }

  if (sentCount > 0) {
    lastSyncTimeEpoch = time(nullptr);
    lastSyncTimeMillis = millis();
  }

  if (sentCount >= entryCount) {
    LittleFS.remove(QUEUE_FILE);
    Serial.println("Queue fully flushed!");
    return;
  }

  // Pass 2: เขียนเฉพาะ entries ที่ยังไม่ได้ส่ง → tmp → rename
  {
    File src = LittleFS.open(QUEUE_FILE, "r");
    File dst = LittleFS.open("/qtmp.csv", "w");
    if (src && dst) {
      int lineNum = 0;
      while (src.available()) {
        ESP.wdtFeed();
        String line = src.readStringUntil('\n');
        line.trim();
        if (line.length() <= 2) continue;
        if (lineNum++ < sentCount) continue;
        dst.println(line);
      }
    }
    src.close();
    dst.close();
    LittleFS.remove(QUEUE_FILE);
    LittleFS.rename("/qtmp.csv", QUEUE_FILE);
    Serial.print("Partial flush: "); Serial.print(sentCount);
    Serial.print("/"); Serial.println(entryCount);
  }
}

// --- 5. Logic Functions ---

// --- 5.1 Helper functions ---
bool validateWebAppUrl() {
  if (strlen(webAppUrl) < 10) {
    currentStatus = "NO URL";
    return false;
  }
  return true;
}

bool readSensorData() {
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if (t != DEVICE_DISCONNECTED_C && t > -55.0) t += getTempCalibrationOffset();
  if (t == DEVICE_DISCONNECTED_C || t < -55.0) {
    currentStatus = "SENS ERR";
    failedSyncCount++;
    unsigned long interval = 3600000;
    if (millis() - lastSensorErrorNotifyTime >= interval) {
      notifyViaGAS(String("⚠️ [TempBot Alert]\n") + "Board: " + getBoardIdentifier() + "\nIP: " + WiFi.localIP().toString() + "\nStatus: SENSOR ERROR\nDS18B20 not responding!");
      lastSensorErrorNotifyTime = millis();
    }
    return false;
  }
  currentTemp = t;
  return true;
}

bool checkWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    queueData(currentTemp);
    currentStatus = "BUFFERED:" + String(getQueueSize());
    failedSyncCount++;
    return false;
  }
  return true;
}

bool syncToGAS(WiFiClientSecure &client) {
  HTTPClient http;
  String url = String(webAppUrl) + "?temperature=" + String(currentTemp, 1) + "&board_id=" + urlEncode(getBoardIdentifier());
  if (!http.begin(client, url)) {
    currentStatus = "ERR HTTP_BEGIN";
    Serial.println("syncToGAS: http.begin failed");
    return false;
  }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  ESP.wdtDisable();                     // กัน soft WDT ตอน GAS ตอบช้า (cold start ~6s)
  int httpCode = http.GET();
  String payload = (httpCode == 200) ? http.getString() : "";
  ESP.wdtEnable(8000);
  if (httpCode != 200 || !payload.startsWith("OK")) {  // ต้องได้ body "OK" จริง ไม่ใช่หน้า error ที่ GAS ห่อเป็น 200
    currentStatus = "ERR " + String(httpCode);
    Serial.println("syncToGAS: HTTP " + String(httpCode) + " body=" + payload.substring(0, 16));
    http.end();
    return false;
  }
  currentStatus = "SYNCED";
  failedSyncCount = 0;
  lastSyncTimeEpoch = time(nullptr);
  lastSyncTimeMillis = millis();
  http.end();
  return true;
}

void fetchAndApplySettings(WiFiClientSecure &client) {
  HTTPClient http;
  String url = String(webAppUrl) + "?get_settings=1&board_id=" + urlEncode(getBoardIdentifier());
  if (!http.begin(client, url)) { Serial.println("fetchSettings: http.begin failed"); return; }
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  ESP.wdtDisable();
  int sc = http.GET();
  ESP.wdtEnable(8000);
  if (sc != 200) { Serial.println("fetchSettings: HTTP != 200"); http.end(); return; }
  String payload = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error: "); Serial.println(err.c_str());
    return;
  }
  bool changed = false;
  if (!doc["maxTemp"].isNull()) {
    String maxStr = String((float)doc["maxTemp"], 1);
    maxStr.toCharArray(maxTempAlert, sizeof(maxTempAlert));
    changed = true;
  }
  if (!doc["minTemp"].isNull()) {
    String minStr = String((float)doc["minTemp"], 1);
    minStr.toCharArray(minTempAlert, sizeof(minTempAlert));
    changed = true;
  }
  if (!doc["bitmap"].isNull() && doc["bitmap"].is<const char*>()) {
    const char* bmp = doc["bitmap"].as<const char*>();
    if (strlen(bmp) > 0 && strlen(bmp) < sizeof(bitmapName)) {
      strncpy(bitmapName, bmp, sizeof(bitmapName) - 1);
      bitmapName[sizeof(bitmapName) - 1] = '\0';
      Serial.print("Bitmap updated from GAS: ");
      Serial.println(bitmapName);
    }
  }
  if (changed) saveConfig();
  Serial.println("Settings updated from GAS");
}

void incrementFailCount() {
  failedSyncCount++;
  Serial.print("Failed sync count = "); Serial.println(failedSyncCount);
}

// --- 5.2 sendData coordinator ---
void sendData() {
  if (!validateWebAppUrl()) return;
  if (!readSensorData()) return;
  if (!checkWiFiConnected()) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(4096, 1024); // จำกัด TLS buffer กัน OOM ตอน handshake ติดกันหลายครั้ง

  if (syncToGAS(client)) {
    if (droppedEntries > 0) {
      String msg = "⚠️ [TempBot] Data Loss\n"
                   "Board: " + getBoardIdentifier() + "\n"
                   "หายระหว่างออฟไลน์: " + String(droppedEntries) + " entries\n"
                   "(queue เต็ม 1440 entries = ~30 วัน ยังไม่พอ)";
      notifyViaGAS(msg);
      droppedEntries = 0;
      saveDroppedCount(0);
    }
    fetchAndApplySettings(client);
    flushQueue();
  } else {
    incrementFailCount();
  }
}


void checkWiFiConnection() {
  static unsigned long lastWiFiCheck = 0;
  unsigned long currentMillis = millis();

  // ตรวจสอบทุกๆ 10 วินาที
  if (currentMillis - lastWiFiCheck >= 10000) {
    lastWiFiCheck = currentMillis;

    if (WiFi.status() != WL_CONNECTED) {
      currentStatus = "RECONNECTING";
      Serial.println("WiFi connection lost. Trying to reconnect...");
      
      // สั่งให้ ESP เชื่อมต่อใหม่ด้วยข้อมูลเดิมที่ WiFiManager บันทึกไว้
      WiFi.begin(); 
    }
  }
}

// --- 5. Config Portal (กดปุ่ม Flash สั้น) ---
void openConfigPortal() {
  currentStatus = "CONFIG MODE";
  updateDisplay(currentTemp, currentStatus);
  playAnimation(1, "CONFIG MODE");

  WiFiManager wm;

  // โหลดค่าปัจจุบันมาแสดงในฟอร์ม ผู้ใช้แก้ไขเฉพาะส่วนที่ต้องการ
  WiFiManagerParameter custom_url("url", "Google WebApp URL", webAppUrl, 150);
  WiFiManagerParameter custom_delay("delay", "Sync Delay (Minutes)", timerDelayStr, 10);
  WiFiManagerParameter custom_token("token", "LINE Channel Access Token", lineToken, 200);
  WiFiManagerParameter custom_groupid("groupid", "LINE Group ID (Cxxxxxxx)", lineGroupId, 40);
  WiFiManagerParameter custom_min_temp("min_temp", "Min Temp Alert (C)", minTempAlert, 10);
  WiFiManagerParameter custom_max_temp("max_temp", "Max Temp Alert (C)", maxTempAlert, 10);
  WiFiManagerParameter custom_board_name("board_name", "Board Name (e.g. Kitchen)", boardName, 32);
  WiFiManagerParameter custom_ota_password("ota_pass", "ArduinoOTA Password", otaPassword, 32);
  WiFiManagerParameter custom_ota_version_url("ota_version_url", "OTA Version URL", otaVersionUrl, 150);
  WiFiManagerParameter custom_ota_bin_url("ota_bin_url", "OTA Firmware URL", otaBinUrl, 150);
  WiFiManagerParameter custom_temp_cal("temp_cal", "Temp Calibration Offset (C)", tempCalibrationStr, 10);
  WiFiManagerParameter custom_bitmap("bitmap", "Bitmap (cat/chicken/fish/tree)", bitmapName, 20);
  WiFiManagerParameter custom_static_ip("static_ip", "Static IP (e.g. 192.168.0.150)", staticIP, 16);
  
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_groupid);
  wm.addParameter(&custom_min_temp);
  wm.addParameter(&custom_max_temp);
  wm.addParameter(&custom_board_name);
  wm.addParameter(&custom_ota_password);
  wm.addParameter(&custom_ota_version_url);
  wm.addParameter(&custom_ota_bin_url);
  wm.addParameter(&custom_temp_cal);
  wm.addParameter(&custom_bitmap);
  wm.addParameter(&custom_static_ip);

  wm.setConfigPortalTimeout(120); // ปิด portal อัตโนมัติใน 2 นาที

  String boardID = "ESP8266_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();

  Serial.println("Opening Config Portal (no WiFi reset)...");
  // startConfigPortal เปิดหน้า config โดยไม่ล้าง WiFi credentials เดิม
  wm.startConfigPortal(boardID.c_str());

  // บันทึกค่าที่ผู้ใช้กรอกใหม่
  if (custom_url.getValue()[0] != '\0') {
    strncpy(webAppUrl, custom_url.getValue(), sizeof(webAppUrl));
  }
  if (custom_delay.getValue()[0] != '\0') {
    strncpy(timerDelayStr, custom_delay.getValue(), sizeof(timerDelayStr));
    unsigned long delayMin = atol(timerDelayStr);
    if (delayMin > 0) timerDelay = delayMin * 60000;
  }
  strncpy(lineToken, custom_token.getValue(), sizeof(lineToken));
  strncpy(lineGroupId, custom_groupid.getValue(), sizeof(lineGroupId));
  strncpy(minTempAlert, custom_min_temp.getValue(), sizeof(minTempAlert));
  strncpy(maxTempAlert, custom_max_temp.getValue(), sizeof(maxTempAlert));
  strncpy(boardName, custom_board_name.getValue(), sizeof(boardName));
  strncpy(otaPassword, custom_ota_password.getValue(), sizeof(otaPassword));
  strncpy(otaVersionUrl, custom_ota_version_url.getValue(), sizeof(otaVersionUrl));
  strncpy(otaBinUrl, custom_ota_bin_url.getValue(), sizeof(otaBinUrl));
  strncpy(tempCalibrationStr, custom_temp_cal.getValue(), sizeof(tempCalibrationStr));
  strncpy(bitmapName, custom_bitmap.getValue(), sizeof(bitmapName));
  if (strlen(bitmapName) > 0) setBitmap(bitmapName);

  // บันทึกลง LittleFS
  saveConfig();

  playAnimation(1, "RESTARTING...");
  delay(500);
  ESP.restart();
}

// --- 6. Setup ---
void setup() {
  Serial.begin(115200);
  
  pinMode(0, INPUT_PULLUP); // ตั้งค่าขาปุ่ม Flash สำหรับ Factory Reset (GPIO 0)

  // สั่งตั้งค่า WiFi Mode ให้เป็นแบบพยายามต่ออัตโนมัติเมื่อหลุด
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  // 1. อ่านค่าพารามิเตอร์จาก LittleFS
  if (LittleFS.begin()) {
    droppedEntries = loadDroppedCount();
    Serial.println("LittleFS mounted successfully.");
    if (LittleFS.exists("/config.bin")) {
      File configFile = LittleFS.open("/config.bin", "r");
      if (configFile) {
        size_t fileSize = configFile.size();
        configFile.readBytes(webAppUrl, sizeof(webAppUrl));
        configFile.readBytes(timerDelayStr, sizeof(timerDelayStr));
        if (fileSize >= 550) {
          // รูปแบบใหม่ล่าสุด: มี tempCalibrationStr
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          configFile.readBytes(boardName, sizeof(boardName));
          configFile.readBytes(bitmapName, sizeof(bitmapName));
          configFile.readBytes(staticIP, sizeof(staticIP));
          configFile.readBytes(otaPassword, sizeof(otaPassword));
    configFile.readBytes(otaVersionUrl, sizeof(otaVersionUrl));
    configFile.readBytes(otaBinUrl, sizeof(otaBinUrl));
    configFile.readBytes(tempCalibrationStr, sizeof(tempCalibrationStr));
        } else if (fileSize >= 472) {
          // รูปแบบที่มี boardName แต่ไม่มี bitmapName/staticIP/otaPassword
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          configFile.readBytes(boardName, sizeof(boardName));
          // skip humidity bytes originally written here
          for (int i = 0; i < 20; i++) configFile.read(); // skip 20 bytes
          bitmapName[0] = '\0';
          staticIP[0] = '\0';
          otaPassword[0] = '\0';
        } else if (fileSize >= 452) {
          // รูปแบบที่มี boardName แต่ไม่มีความชื้น
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          configFile.readBytes(boardName, sizeof(boardName));
        } else if (fileSize >= 420) {
          // รูปแบบใหม่: lineToken 200 bytes + lineGroupId 40 bytes
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          boardName[0] = '\0';
        } else if (fileSize >= 244) {
          configFile.readBytes(lineToken, 64);
          lineToken[63] = '\0';
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          lineGroupId[0] = '\0';
          boardName[0] = '\0';
        } else if (fileSize >= 235) {
          configFile.readBytes(lineToken, 55);
          lineToken[54] = '\0';
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          lineGroupId[0] = '\0';
          boardName[0] = '\0';
        } else {
          lineToken[0] = '\0';
          lineGroupId[0] = '\0';
          boardName[0] = '\0';
          strcpy(minTempAlert, "20.0");
          strcpy(maxTempAlert, "35.0");
        }
        configFile.close();
        Serial.println("Config loaded from LittleFS:");
        Serial.print("URL: "); Serial.println(webAppUrl);
        Serial.print("Delay: "); Serial.println(timerDelayStr);
        Serial.print("LINE Token: "); Serial.println(lineToken[0] ? "[set]" : "[empty]");
        Serial.print("LINE Group: "); Serial.println(lineGroupId);
        Serial.print("Min Alert: "); Serial.println(minTempAlert);
        Serial.print("Max Alert: "); Serial.println(maxTempAlert);
        Serial.print("Board Name: "); Serial.println(boardName);
      }
    }
  } else {
    Serial.println("Failed to mount LittleFS.");
  }

  unsigned long delayMin = atol(timerDelayStr);
  if (delayMin > 0) {
    timerDelay = delayMin * 60000;
  }

  sensors.begin();
  
  Wire.begin(4, 5); // SDA = 4 (D2), SCL = 5 (D1)
  
  uint8_t oledAddr = SCREEN_I2C_ADDR;
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) {
    oledAddr = 0x3C;
    Serial.println("OLED found at 0x3C");
  } else {
    Wire.beginTransmission(0x3D);
    if (Wire.endTransmission() == 0) {
      oledAddr = 0x3D;
      Serial.println("OLED found at 0x3D");
    } else {
      Serial.println("Warning: No OLED found on I2C bus! Check wiring (SDA=D2, SCL=D1).");
    }
  }

  if(!display.begin(SSD1306_SWITCHCAPVCC, oledAddr)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
  
  // display.dim(true); // เปิดโหมดประหยัดหน้าจอ (Dim Screen) ยืดอายุหน้าจอ OLED (บางบอร์ดโคลนอาจจะจอดับเมื่อเปิดใช้บรรทัดนี้ ให้ปิดไว้เป็นค่าเริ่มต้น)
  display.clearDisplay();
  playAnimation(1, "BOOTING...");

  WiFiManager wm;

  // เพิ่มช่องกรอกค่าปรับแต่ง (Custom Parameters)
  WiFiManagerParameter custom_url("url", "Google WebApp URL", webAppUrl, 150);
  WiFiManagerParameter custom_delay("delay", "Sync Delay (Minutes)", timerDelayStr, 10);
  WiFiManagerParameter custom_token("token", "LINE Channel Access Token", lineToken, 200);
  WiFiManagerParameter custom_groupid("groupid", "LINE Group ID (Cxxxxxxx)", lineGroupId, 40);
  WiFiManagerParameter custom_min_temp("min_temp", "Min Temp Alert (C)", minTempAlert, 10);
  WiFiManagerParameter custom_max_temp("max_temp", "Max Temp Alert (C)", maxTempAlert, 10);
  WiFiManagerParameter custom_board_name("board_name", "Board Name (e.g. Kitchen)", boardName, 32);
  WiFiManagerParameter custom_ota_password("ota_pass", "ArduinoOTA Password", otaPassword, 32);
  WiFiManagerParameter custom_ota_version_url("ota_version_url", "OTA Version URL (version.txt)", otaVersionUrl, 150);
  WiFiManagerParameter custom_ota_bin_url("ota_bin_url", "OTA Firmware URL (.bin)", otaBinUrl, 150);
  WiFiManagerParameter custom_temp_cal("temp_cal", "Temp Calibration Offset (C)", tempCalibrationStr, 10);
  WiFiManagerParameter custom_static_ip("static_ip", "Static IP (e.g. 192.168.0.150)", staticIP, 16);

  wm.addParameter(&custom_ota_version_url);
  wm.addParameter(&custom_ota_bin_url);
  wm.addParameter(&custom_temp_cal);

  
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_groupid);
  wm.addParameter(&custom_min_temp);
  wm.addParameter(&custom_max_temp);
  wm.addParameter(&custom_board_name);
  wm.addParameter(&custom_ota_password);
  wm.addParameter(&custom_static_ip);
  
  // ตั้งค่า Config ของ WiFiManager ให้เหมาะกับการจัดการตอนไฟตก
  wm.setConfigPortalTimeout(120); // ถ้าผ่านไป 2 นาทีไม่มีคนมาต่อ AP เพื่อตั้งค่า ให้หลุดจาก setup ไปทำ loop ต่อ (สำคัญมากตอนไฟดับแล้วเราไม่อยู่บ้าน)
  wm.setConnectTimeout(15);       // พยายามต่อกับเร้าเตอร์เดิมตัวละ 15 วินาที
  
  // ถ้ามี staticIP ที่ตั้งไว้ ตั้งค่า Static IP ก่อน connect
  if (strlen(staticIP) > 0) {
    IPAddress ip, gateway, subnet;
    if (ip.fromString(staticIP)) {
      gateway.fromString("192.168.0.1");
      subnet.fromString("255.255.255.0");
      wm.setSTAStaticIPConfig(ip, gateway, subnet);
      Serial.print("Static IP configured: ");
      Serial.println(staticIP);
    }
  }
  
  String boardID = "ESP8266_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();
  
  currentStatus = "WIFI CONNECTING";
  
  // ใช้ autoConnect หากต่อสำเร็จจะไปต่อ หากไม่สำเร็จภายใน Timeout จะหลุดไปทำงานต่อใน loop() เพื่อรอเร้าเตอร์เปิดเสร็จ
  if(!wm.autoConnect(boardID.c_str())) {
    Serial.println("Failed to connect or hit timeout. Continuing to loop...");
    currentStatus = "WIFI TIMEOUT";
  } else {
    playAnimation(1, "WIFI OK!");
    currentStatus = "CONNECTED";
  }
  // Register web config handlers
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_GET, handleSave);
  server.on("/queue", HTTP_GET, handleQueue);
  server.begin();
  Serial.print("Web config server started at ");
  Serial.println(WiFi.localIP());

  // Show IP on OLED for a few seconds
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("IP Address:");
  display.println(WiFi.localIP());
  display.print("FW v");
  display.println(FIRMWARE_VERSION);
  display.display();
  delay(3000);
  display.clearDisplay();

  // ดึงค่าใหม่และบันทึกลงใน LittleFS
  if (custom_url.getValue()[0] != '\0') {
    strncpy(webAppUrl, custom_url.getValue(), sizeof(webAppUrl));
  }
  if (custom_delay.getValue()[0] != '\0') {
    strncpy(timerDelayStr, custom_delay.getValue(), sizeof(timerDelayStr));
    unsigned long delayMin = atol(timerDelayStr);
    if (delayMin > 0) {
      timerDelay = delayMin * 60000;
    }
  }
  strncpy(lineToken, custom_token.getValue(), sizeof(lineToken));
  strncpy(lineGroupId, custom_groupid.getValue(), sizeof(lineGroupId));
  strncpy(minTempAlert, custom_min_temp.getValue(), sizeof(minTempAlert));
  strncpy(maxTempAlert, custom_max_temp.getValue(), sizeof(maxTempAlert));
  strncpy(boardName, custom_board_name.getValue(), sizeof(boardName));
  strncpy(otaPassword, custom_ota_password.getValue(), sizeof(otaPassword));
  strncpy(otaVersionUrl, custom_ota_version_url.getValue(), sizeof(otaVersionUrl));
  strncpy(otaBinUrl, custom_ota_bin_url.getValue(), sizeof(otaBinUrl));
  strncpy(tempCalibrationStr, custom_temp_cal.getValue(), sizeof(tempCalibrationStr));

  saveConfig();

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  setBitmap(bitmapName);
  ArduinoOTA.setHostname(boardID.c_str());
  if (otaPassword[0] != '\0') {
    ArduinoOTA.setPassword(otaPassword);
    Serial.println("ArduinoOTA: Password protection enabled.");
  } else {
    Serial.println("ArduinoOTA: Unprotected.");
  }
  ArduinoOTA.begin();
  Serial.print("OTA ready! IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("OTA port: 8266, Hostname: ");
  Serial.println(boardID.c_str());
  
  // Check for OTA updates on boot (after ArduinoOTA is ready)
  checkForOTAUpdate();

  if (WiFi.status() == WL_CONNECTED) {
    sendData();
  }
  lastTime = millis();
}

// --- Auto OTA Update ---
void checkForOTAUpdate() {
  if (strlen(otaVersionUrl) == 0 || strlen(otaBinUrl) == 0) {
    Serial.println("OTA URLs not set, skipping OTA check.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping OTA check.");
    return;
  }

  Serial.println("Checking for OTA firmware update...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.println("Checking for");
  display.setCursor(10, 32);
  display.println("firmware update...");
  display.display();

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(16384, 512);  // github ส่ง TLS record ใหญ่ — RX ต้องใหญ่พอ ไม่งั้นดาวน์โหลด .bin ค้าง (stream read timeout)
  HTTPClient http;
  if (!http.begin(client, otaVersionUrl)) {
    Serial.println("Failed to begin HTTP for OTA version.");
    return;
  }
  ESP.wdtDisable();
  int httpCode = http.GET();
  String latestVersion = (httpCode == 200) ? http.getString() : "";
  ESP.wdtEnable(8000);
  http.end();
  if (httpCode != 200) {
    Serial.printf("OTA version check failed, HTTP code %d\n", httpCode);
    return;
  }
  latestVersion.trim();

  Serial.printf("Current: %s, Latest: %s\n", FIRMWARE_VERSION, latestVersion.c_str());

  if (isNewerVersion(latestVersion, String(FIRMWARE_VERSION))) {  // อัปเฉพาะที่ใหม่กว่า ไม่ downgrade
    Serial.printf("New firmware version %s available. Updating...\n", latestVersion.c_str());

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("OTA UPDATE");
    display.setCursor(10, 25);
    display.print("v");
    display.print(FIRMWARE_VERSION);
    display.print(" -> v");
    display.println(latestVersion);
    display.setCursor(10, 45);
    display.println("Downloading...");
    display.display();

    client.stop(); // คืน connection ก่อน reuse ใน ESPhttpUpdate
    ESP.wdtDisable(); // ปิด WDT ตลอด download+flash (616KB ใช้ > 8 วิ)
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, otaBinUrl);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("OTA Update Failed: %s (%d)\n", ESPhttpUpdate.getLastErrorString().c_str(), ESPhttpUpdate.getLastError());
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(10, 20);
        display.println("OTA FAILED!");
        display.setCursor(10, 35);
        display.println(ESPhttpUpdate.getLastErrorString().c_str());
        display.display();
        delay(3000);
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("No OTA updates available.");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("OTA Update successful, rebooting...");
        break; // Device will reboot automatically
    }
  } else {
    Serial.println("Firmware is up to date.");
  }
}

// --- 6. Loop ---
unsigned long lastOTACheck = 0;
const unsigned long OTA_CHECK_INTERVAL = 12UL * 60 * 60 * 1000; // 12 hours

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  // Rest of loop code unchanged
  // If Wi‑Fi just came back up and we have buffered entries, flush them
  static bool lastWiFiConnected = false;
  bool currentWiFiConnected = (WiFi.status() == WL_CONNECTED);
  if (currentWiFiConnected && !lastWiFiConnected) {
    int qs = getQueueSize();
    if (qs > 0) {
      Serial.print("Flushing offline queue of "); Serial.print(qs); Serial.println(" entries after reconnection");
      flushQueue();
    }
  }
  lastWiFiConnected = currentWiFiConnected;
  
  // ตรวจสอบการกดปุ่ม Flash (GPIO 0)
  // กดแล้วปล่อยภายใน 2 วินาที → เปิด Config Portal (ไม่ล้าง WiFi)
  // กดค้างไว้ 5 วินาที          → Factory Reset (ลบทุกอย่าง)
  static unsigned long flashPressStartTime = 0;
  static bool flashActionTaken = false;

  if (digitalRead(0) == LOW) {
    if (flashPressStartTime == 0) {
      flashPressStartTime = millis();
      flashActionTaken = false;
    }
    unsigned long holdTime = millis() - flashPressStartTime;

    if (holdTime >= 5000 && !flashActionTaken) {
      // ค้างครบ 5 วินาที → Factory Reset
      flashActionTaken = true;
      playAnimation(2, "FACTORY RESET");
      WiFiManager wm;
      wm.resetSettings();
      if (LittleFS.begin()) {
        LittleFS.remove("/config.bin");
      }
      Serial.println("Factory reset! Rebooting...");
      delay(1000);
      ESP.restart();
    } else if (holdTime >= 2000) {
      // ค้าง 2-5 วินาที → แสดงนับถอยหลัง Factory Reset
      int countdown = 5 - (int)(holdTime / 1000);
      if (countdown < 1) countdown = 1;
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(5, 5);
      display.println("KEEP HOLDING:");
      display.setCursor(5, 17);
      display.println("FACTORY RESET IN...");
      display.setTextSize(3);
      display.setCursor(55, 35);
      display.print(countdown);
      display.display();
      delay(50);
      return;
    } else {
      // กดค้างไว้ < 2 วินาที → แสดงคำแนะนำ
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(2, 5);
      display.println("RELEASE = CONFIG");
      display.setCursor(2, 17);
      display.println("HOLD 5s  = RESET");
      display.drawFastHLine(0, 28, 128, WHITE);
      display.setTextSize(2);
      display.setCursor(10, 38);
      display.print("CONFIG?");
      display.display();
      delay(50);
      return;
    }
  } else {
    if (flashPressStartTime != 0) {
      unsigned long holdTime = millis() - flashPressStartTime;
      bool wasShortPress = (holdTime < 2000 && !flashActionTaken);
      flashPressStartTime = 0;
      flashActionTaken = false;
      if (wasShortPress) {
        // ปล่อยปุ่มก่อน 2 วินาที → เปิด Config Portal
        openConfigPortal();
      } else {
        updateDisplay(currentTemp, currentStatus);
      }
    }
  }

  // ตรวจสอบสถานะ WiFi สม่ำเสมอ
  checkWiFiConnection();

  // ส่ง Line Boot Notification ครั้งแรกที่เชื่อมต่อสำเร็จ
  if (!isBootNotificationSent && WiFi.status() == WL_CONNECTED) {
    String resetReason = ESP.getResetReason();
    isBootNotificationSent = true;
    if (!resetReason.startsWith("Software Watchdog") && !resetReason.startsWith("Exception")) {
      String boardID = getBoardIdentifier();
      String message = "\n🚀 [BOOT] Board Online!\n"
                       "Name: " + boardID + "\n"
                       "IP: " + WiFi.localIP().toString() + "\n"
                       "Reset Reason: " + resetReason;
      Serial.println("Sending Boot Notification to LINE...");
      notifyViaGAS(message);
    } else {
      Serial.println("Boot Notification skipped: " + resetReason);
    }
  }

  unsigned long currentMillis = millis();

  // 1. จัดการส่งข้อมูลไป Google Sheets (ทุก 30 นาที)
  if (currentMillis - lastTime >= timerDelay) {
    playAnimation(1, "SENDING DATA"); 
    sendData();
    playAnimation(1, "DONE!");
    lastTime = currentMillis; // รีเซ็ตตัวจับเวลา เพื่อรออีก 30 นาทีรอบถัดไป
  }

  // 2. ขยับตำแหน่งหน้าจอเพื่อป้องกันจอเบิร์น (ทุก 1 นาที)
  static unsigned long lastShiftTime = 0;
  if (currentMillis - lastShiftTime >= 60000) {
    static int shiftState = 0;
    shiftState = (shiftState + 1) % 5;
    switch (shiftState) {
      case 0: shiftX = 0;  shiftY = 0;  break;
      case 1: shiftX = 1;  shiftY = 1;  break;
      case 2: shiftX = -1; shiftY = -1; break;
      case 3: shiftX = 2;  shiftY = -1; break;
      case 4: shiftX = -2; shiftY = 1;  break;
    }
    updateDisplay(currentTemp, currentStatus);
    lastShiftTime = currentMillis;
  }

  // 3. อ่านค่าเซนเซอร์แบบ Non-blocking และอัปเดตหน้าจอ (ทุก 2 วินาที)
  static unsigned long lastUpdate = 0;
  if (currentMillis - lastUpdate >= 2000) {
    
    // อ่านค่าอุณหภูมิจากเซนเซอร์ DS18B20
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    
    // --- ปรับค่า Calibration Offset ---
    if (t != DEVICE_DISCONNECTED_C && t > -55.0) {
      t = t + getTempCalibrationOffset(); 
    }
    
    if (t == DEVICE_DISCONNECTED_C || t < -55.0) {
      currentTemp = -999;
    } else {
      currentTemp = t;
    }
    
    // ถ้าเชื่อมต่อ WiFi ได้ปกติ แต่อยู่ในช่วงพักรอส่งข้อมูล ให้คงสถานะ SYNCED หรือ CONNECTED ไว้
    if (WiFi.status() == WL_CONNECTED && (currentStatus == "RECONNECTING" || currentStatus == "SENS ERR")) {
      currentStatus = "CONNECTED";
    }
    
    updateDisplay(currentTemp, currentStatus);

    lastUpdate = currentMillis;
  }

  // 4. ตรวจสอบ OTA อัปเดตเป็นระยะ (ทุก 12 ชั่วโมง)
  if (strlen(otaVersionUrl) > 0 && strlen(otaBinUrl) > 0) {
    unsigned long nowMillis = millis();
    if (nowMillis - lastOTACheck >= OTA_CHECK_INTERVAL) {
      lastOTACheck = nowMillis;
      checkForOTAUpdate();
    }
  }
}
