#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "bitmaps.h"

// --- 1. Configuration ---
#define SENSOR_PIN 14        // ขา D5 (สำหรับ DS18B20)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_I2C_ADDR 0x3D 

#define FRAME_DELAY 42
#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 25

// Offline Data Queue
#define QUEUE_FILE        "/queue.csv"
#define MAX_QUEUE_ENTRIES 32

// --- 2. Objects ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

char webAppUrl[150] = "";
char timerDelayStr[10] = "30";
char lineToken[200]  = "";   // LINE Messaging API Channel Access Token
char lineGroupId[40] = "";   // LINE Group ID (เช่น C1234abcd...)
char minTempAlert[10] = "20.0";
char maxTempAlert[10] = "35.0";
char boardName[32] = "";     // Custom Board Name (เช่น Kitchen, ServerRoom)
char otaPassword[32] = "";   // ArduinoOTA update password
unsigned long lastTime = 0;
unsigned long timerDelay = 1800000; // 30 นาที (ค่าเริ่มต้น)
int failedSyncCount = 0;            // นับจำนวนครั้งที่ส่งข้อมูลไม่สำเร็จติดต่อกัน

enum AlertState { STATE_NORMAL, STATE_ALERT_LOW, STATE_ALERT_HIGH };
AlertState lastAlertState = STATE_NORMAL;
unsigned long lastLineNotifyTime = 0;
bool isBootNotificationSent = false;

float dailyMinTemp = 999.0;
float dailyMaxTemp = -999.0;
int lastDayOfMinMax = -1;
unsigned long lastSyncTimeEpoch = 0;

String formatTime(time_t epoch, bool includeSeconds) {
  if (epoch < 1000000000) {
    return "--:--";
  }
  struct tm* timeinfo = localtime(&epoch);
  char buffer[10];
  if (includeSeconds) {
    sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    sprintf(buffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  }
  return String(buffer);
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


String getBoardIdentifier() {
  String bName = String(boardName);
  bName.trim();
  if (bName.length() == 0) {
    bName = "BOARD_" + String(ESP.getChipId(), HEX);
    bName.toUpperCase();
  }
  return bName;
}

String currentStatus = "STARTING";
float currentTemp = -999;
bool isConversionRequestIssued = false;
unsigned long conversionStartTime = 0;

int8_t shiftX = 0;
int8_t shiftY = 0;

// --- 3. Bitmap Data ( Angry Cat ) ---
// (Move to bitmaps.h for better project organization)


// --- 4. Display Functions ---

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
    if (temp == DEVICE_DISCONNECTED_C) {
      display.print("ERR");
    } else {
      display.print(temp, 1);
      display.print(" C");
    }
  }
  display.display();
}

void playCatAnimation(int repetitions, String message) {
  for (int i = 0; i < repetitions; i++) {
    for (int f = 0; f < FRAME_COUNT; f++) {
      display.clearDisplay();
      display.drawBitmap(32, 0, frames[f], FRAME_WIDTH, FRAME_HEIGHT, WHITE);
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
  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(shiftX, shiftY);
  display.print("STATUS: "); 
  display.print(status);
  
  drawWiFiIcon(115 + shiftX, shiftY);
  
  display.drawFastHLine(0, 10 + shiftY, 128, WHITE);

  if (temp != DEVICE_DISCONNECTED_C && temp > -50 && temp < 125.0) {
    updateDailyMinMax(temp);

    // แสดงอุณหภูมิปัจจุบัน (ลดขนาดตัวอักษรลงเหลือ 3 เพื่อให้มีพื้นที่แสดงค่า Min/Max)
    display.setTextSize(3);
    display.setCursor(5 + shiftX, 16 + shiftY);
    display.print(temp, 1);
    display.setTextSize(2);
    display.print(" C");
    
    // แสดง Min/Max อุณหภูมิของวัน
    display.setTextSize(1);
    display.setCursor(5 + shiftX, 43 + shiftY);
    if (dailyMinTemp > 500.0 || dailyMaxTemp < -500.0) {
      display.print("L: --.- H: --.-");
    } else {
      display.print("L: ");
      display.print(dailyMinTemp, 1);
      display.print("  H: ");
      display.print(dailyMaxTemp, 1);
    }
  } else {
    display.setTextSize(2);
    display.setCursor(10 + shiftX, 30 + shiftY);
    display.print("SENSOR ERR");
  }

  // แสดง IP Address หรือ NTP Time / Last Sync ด้านล่าง
  if (WiFi.status() == WL_CONNECTED) {
    display.setTextSize(1);
    display.setCursor(5 + shiftX, 56 + shiftY);
    
    bool showIP = ((millis() / 4000) % 2 == 0);
    time_t now = time(nullptr);
    if (showIP || now < 1000000000) {
      display.print("IP: ");
      display.print(WiFi.localIP().toString());
    } else {
      String currTime = formatTime(now, true);
      String syncTime = formatTime(lastSyncTimeEpoch, false);
      display.print(currTime);
      display.print(" | Sync ");
      display.print(syncTime);
    }
  }

  display.display();
}

// --- 5. Logic Functions ---

// --- 5. Offline Queue Functions ---

int getQueueSize() {
  if (!LittleFS.exists(QUEUE_FILE)) return 0;
  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 2) count++;
  }
  f.close();
  return count;
}

void queueData(float temp, float humid) {
  int size = getQueueSize();
  if (size >= MAX_QUEUE_ENTRIES) {
    Serial.println("Queue full! Entry dropped.");
    return;
  }
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (f) {
    time_t now = time(nullptr);
    f.println(String(now) + "," + String(temp, 1) + "," + String(humid, 1));
    f.close();
    Serial.print("Queued. Size: "); Serial.println(size + 1);
  }
}

void flushQueue() {
  if (!LittleFS.exists(QUEUE_FILE)) return;

  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return;

  String entries[MAX_QUEUE_ENTRIES];
  int entryCount = 0;
  while (f.available() && entryCount < MAX_QUEUE_ENTRIES) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 2) entries[entryCount++] = line;
  }
  f.close();

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

  String boardID = getBoardIdentifier();

  int sentCount = 0;
  for (int i = 0; i < entryCount; i++) {
    if (WiFi.status() != WL_CONNECTED) break;

    int firstComma = entries[i].indexOf(',');
    if (firstComma < 0) { sentCount++; continue; }
    int secondComma = entries[i].indexOf(',', firstComma + 1);

    String timestampStr = "";
    String tempStr = "";
    String humidStr = "";

    if (secondComma < 0) {
      // Backward compatibility: old format (temp,humid)
      tempStr = entries[i].substring(0, firstComma);
      humidStr = entries[i].substring(firstComma + 1);
    } else {
      // New format (timestamp,temp,humid)
      timestampStr = entries[i].substring(0, firstComma);
      tempStr = entries[i].substring(firstComma + 1, secondComma);
      humidStr = entries[i].substring(secondComma + 1);
    }

    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    String url = String(webAppUrl) + "?temperature=" + tempStr
               + "&humidity=" + humidStr
               + "&board_id=" + urlEncode(boardID)
               + "&queued=1";
    if (timestampStr.length() > 0 && timestampStr != "0") {
      url += "&timestamp=" + timestampStr;
    }

    bool ok = false;
    if (http.begin(client, url)) {
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.setTimeout(10000);
      ok = (http.GET() == 200);
      http.end();
    }

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
      break; // WiFi หลุดหรือ server error → หยุดและลองใหม่รอบหน้า
    }
    ArduinoOTA.handle();
    delay(300);
    yield();
  }

  if (sentCount > 0) {
    lastSyncTimeEpoch = time(nullptr);
  }

  if (sentCount >= entryCount) {
    LittleFS.remove(QUEUE_FILE);
    Serial.println("Queue fully flushed!");
  } else {
    // เขียน entries ที่ยังไม่ได้ส่งกลับไว้
    File fw = LittleFS.open(QUEUE_FILE, "w");
    if (fw) {
      for (int i = sentCount; i < entryCount; i++) fw.println(entries[i]);
      fw.close();
    }
    Serial.print("Partial flush: "); Serial.print(sentCount);
    Serial.print("/"); Serial.println(entryCount);
  }
}

// --- 6. Logic Functions ---

void sendData() {
  // ตรวจสอบว่ามี URL ตั้งค่าไว้แล้วก่อน
  if (strlen(webAppUrl) < 10) {
    currentStatus = "NO URL";
    return;
  }

  // อ่านค่าเซนเซอร์เสมอ ไม่ว่า WiFi จะต่ออยู่หรือไม่
  sensors.requestTemperatures();
  delay(750); // รอ DS18B20 แปลงค่า 12-bit

  float t = sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t < -55.0 || t > 125.0) {
    currentStatus = "SENS ERR";
    failedSyncCount++;
    if (failedSyncCount >= 10) {
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000); ESP.restart();
    }
    return;
  }
  currentTemp = t;

  // WiFi ไม่ต่อ → เก็บข้อมูลใน Offline Queue
  if (WiFi.status() != WL_CONNECTED) {
    queueData(t, 0.0);
    int qs = getQueueSize();
    currentStatus = "BUFFERED:" + String(qs);
    failedSyncCount++;
    if (failedSyncCount >= 10) {
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000); ESP.restart();
    }
    return;
  }

  // WiFi ต่ออยู่ → ส่งข้อมูลปกติ
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();

  String boardID = getBoardIdentifier();

  String url = String(webAppUrl) + "?temperature=" + String(t, 1)
             + "&humidity=0&board_id=" + urlEncode(boardID);

  bool syncSuccess = false;
  if (http.begin(client, url)) {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    int httpCode = http.GET();
    if (httpCode == 200) {
      currentStatus = "SYNCED";
      failedSyncCount = 0;
      syncSuccess = true;
      lastSyncTimeEpoch = time(nullptr);
    } else {
      currentStatus = "ERR " + String(httpCode);
    }
    http.end();
  } else {
    currentStatus = "ERR HTTP_BEGIN";
  }

  if (!syncSuccess) {
    failedSyncCount++;
    Serial.print("Watchdog: Failed sync count = "); Serial.println(failedSyncCount);
    if (failedSyncCount >= 10) {
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000); ESP.restart();
    }
  } else {
    // ส่งสำเร็จ → flush ข้อมูลที่ค้างอยู่ใน queue
    flushQueue();
  }
}


String urlEncode(String str) {
  String encoded = "";
  char buf[4];
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

void sendLineNotify(String message) {
  String tokenStr = String(lineToken);
  tokenStr.trim();
  String groupIdStr = String(lineGroupId);
  groupIdStr.trim();

  if (tokenStr.length() == 0 || groupIdStr.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (http.begin(client, "https://api.line.me/v2/bot/message/push")) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + tokenStr);

    // สร้าง JSON body (escape " และ \ ในข้อความ)
    String safeMsg = message;
    safeMsg.replace("\\", "\\\\");
    safeMsg.replace("\"", "\\\"");
    safeMsg.replace("\n", "\\n");
    safeMsg.replace("\r", "\\r");
    String body = "{\"to\":\"" + groupIdStr + "\","
                  "\"messages\":[{\"type\":\"text\",\"text\":\"" + safeMsg + "\"}]}";

    Serial.print("LINE API Token Len: "); Serial.println(tokenStr.length());
    Serial.print("LINE API Group ID:  "); Serial.println(groupIdStr);
    Serial.print("LINE API Payload:   "); Serial.println(body);

    int httpCode = http.POST(body);
    if (httpCode == 200) {
      Serial.println("LINE API: Message sent successfully.");
    } else {
      Serial.print("LINE API: Failed, code "); Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("LINE API: Failed to connect.");
  }
}

void checkLineAlerts(float temp) {
  if (lineToken[0] == '\0' || lineGroupId[0] == '\0') return;

  float minT = atof(minTempAlert);
  float maxT = atof(maxTempAlert);
  unsigned long currentMillis = millis();
  
  String boardID = getBoardIdentifier();

  float hysteresisT = 0.5;
  AlertState newState = STATE_NORMAL;
  if (temp <= minT && temp > -50) {
    newState = STATE_ALERT_LOW;
  } else if (temp >= maxT) {
    newState = STATE_ALERT_HIGH;
  } else {
    if (lastAlertState == STATE_ALERT_LOW && temp < minT + hysteresisT) {
      newState = STATE_ALERT_LOW;
    } else if (lastAlertState == STATE_ALERT_HIGH && temp > maxT - hysteresisT) {
      newState = STATE_ALERT_HIGH;
    } else {
      newState = STATE_NORMAL;
    }
  }

  // ส่งแจ้งเตือนเมื่อเกิดการเปลี่ยนสถานะ หรือถ้ายืนระยะอยู่ในสถานะแจ้งเตือนเดิมเกิน 30 นาที ให้ส่งซ้ำ
  if (newState != lastAlertState || 
      (newState != STATE_NORMAL && (currentMillis - lastLineNotifyTime >= 1800000))) {
    
    String message = "";
    if (newState == STATE_ALERT_LOW) {
      message = "\n⚠️ [ALERT] Low Temp!\nTemp: " + String(temp, 1) + " C\nMin Limit: " + String(minT, 1) + " C\nBoard: " + boardID;
    } else if (newState == STATE_ALERT_HIGH) {
      message = "\n⚠️ [ALERT] High Temp!\nTemp: " + String(temp, 1) + " C\nMax Limit: " + String(maxT, 1) + " C\nBoard: " + boardID;
    } else if (newState == STATE_NORMAL && lastAlertState != STATE_NORMAL) {
      message = "\n✅ [RESOLVED] Temp returned to normal.\nTemp: " + String(temp, 1) + " C\nRange: " + String(minT, 1) + " - " + String(maxT, 1) + " C\nBoard: " + boardID;
    }

    if (message != "") {
      Serial.print("LINE Alert triggering. State change from ");
      Serial.print(lastAlertState); Serial.print(" to "); Serial.println(newState);
      sendLineNotify(message);
      lastLineNotifyTime = currentMillis;
    }
    lastAlertState = newState;
  }
}

// ฟังก์ชันคอยตรวจเช็คและต่อ WiFi ใหม่แบบอัตโนมัติ (ไม่บล็อกลูป)
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

// --- 6. Config Portal (กดปุ่ม Flash สั้น) ---
void openConfigPortal() {
  currentStatus = "CONFIG MODE";
  updateDisplay(currentTemp, currentStatus);
  playCatAnimation(1, "CONFIG MODE");

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
  
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_groupid);
  wm.addParameter(&custom_min_temp);
  wm.addParameter(&custom_max_temp);
  wm.addParameter(&custom_board_name);
  wm.addParameter(&custom_ota_password);

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

  // บันทึกลง LittleFS
  File configFile = LittleFS.open("/config.bin", "w");
  if (configFile) {
    configFile.write((uint8_t*)webAppUrl, sizeof(webAppUrl));
    configFile.write((uint8_t*)timerDelayStr, sizeof(timerDelayStr));
    configFile.write((uint8_t*)lineToken, sizeof(lineToken));
    configFile.write((uint8_t*)minTempAlert, sizeof(minTempAlert));
    configFile.write((uint8_t*)maxTempAlert, sizeof(maxTempAlert));
    configFile.write((uint8_t*)lineGroupId, sizeof(lineGroupId));
    configFile.write((uint8_t*)boardName, sizeof(boardName));
    configFile.write((uint8_t*)otaPassword, sizeof(otaPassword));
    configFile.close();
    Serial.println("Config saved after portal.");
  }

  playCatAnimation(1, "RESTARTING...");
  delay(500);
  ESP.restart();
}

// --- 7. Setup ---
void setup() {
  Serial.begin(115200);
  
  pinMode(0, INPUT_PULLUP); // ตั้งค่าขาปุ่ม Flash สำหรับ Factory Reset (GPIO 0)

  // สั่งตั้งค่า WiFi Mode ให้เป็นแบบพยายามต่ออัตโนมัติเมื่อหลุด
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  // 1. อ่านค่าพารามิเตอร์จาก LittleFS
  if (LittleFS.begin()) {
    Serial.println("LittleFS mounted successfully.");
    if (LittleFS.exists("/config.bin")) {
      File configFile = LittleFS.open("/config.bin", "r");
      if (configFile) {
        size_t fileSize = configFile.size();
        configFile.readBytes(webAppUrl, sizeof(webAppUrl));
        configFile.readBytes(timerDelayStr, sizeof(timerDelayStr));
        if (fileSize >= 484) {
          // รูปแบบใหม่ล่าสุด: มี otaPassword
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          configFile.readBytes(boardName, sizeof(boardName));
          configFile.readBytes(otaPassword, sizeof(otaPassword));
        } else if (fileSize >= 452) {
          // รูปแบบที่มี boardName แต่ไม่มี otaPassword
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          configFile.readBytes(boardName, sizeof(boardName));
          otaPassword[0] = '\0';
        } else if (fileSize >= 420) {
          // รูปแบบใหม่: lineToken 200 bytes + lineGroupId 40 bytes
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          configFile.readBytes(lineGroupId, sizeof(lineGroupId));
          boardName[0] = '\0';
        } else if (fileSize >= 244) {
          // รูปแบบกลาง: lineToken 64 bytes (LINE Notify เดิม)
          configFile.readBytes(lineToken, 64);
          lineToken[63] = '\0';
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
          lineGroupId[0] = '\0';
          boardName[0] = '\0';
        } else if (fileSize >= 235) {
          // รูปแบบเก่า: lineToken 55 bytes
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
  sensors.setWaitForConversion(false); // ปิดการบล็อก เพื่อใช้เทคนิค Non-blocking ใน loop()
  
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
  playCatAnimation(1, "BOOTING...");

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
  
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_groupid);
  wm.addParameter(&custom_min_temp);
  wm.addParameter(&custom_max_temp);
  wm.addParameter(&custom_board_name);
  wm.addParameter(&custom_ota_password);
  
  // ตั้งค่า Config ของ WiFiManager ให้เหมาะกับการจัดการตอนไฟตก
  wm.setConfigPortalTimeout(120); // ถ้าผ่านไป 2 นาทีไม่มีคนมาต่อ AP เพื่อตั้งค่า ให้หลุดจาก setup ไปทำ loop ต่อ (สำคัญมากตอนไฟดับแล้วเราไม่อยู่บ้าน)
  wm.setConnectTimeout(15);       // พยายามต่อกับเร้าเตอร์เดิมตัวละ 15 วินาที
  
  String boardID = "ESP8266_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();
  
  currentStatus = "WIFI CONNECTING";
  
  // ใช้ autoConnect หากต่อสำเร็จจะไปต่อ หากไม่สำเร็จภายใน Timeout จะหลุดไปทำงานต่อใน loop() เพื่อรอเร้าเตอร์เปิดเสร็จ
  if(!wm.autoConnect(boardID.c_str())) {
    Serial.println("Failed to connect or hit timeout. Continuing to loop...");
    currentStatus = "WIFI TIMEOUT";
  } else {
    playCatAnimation(1, "WIFI OK!");
    currentStatus = "CONNECTED";
  }

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

  File configFile = LittleFS.open("/config.bin", "w");
  if (configFile) {
    configFile.write((uint8_t*)webAppUrl, sizeof(webAppUrl));
    configFile.write((uint8_t*)timerDelayStr, sizeof(timerDelayStr));
    configFile.write((uint8_t*)lineToken, sizeof(lineToken));
    configFile.write((uint8_t*)minTempAlert, sizeof(minTempAlert));
    configFile.write((uint8_t*)maxTempAlert, sizeof(maxTempAlert));
    configFile.write((uint8_t*)lineGroupId, sizeof(lineGroupId));
    configFile.write((uint8_t*)boardName, sizeof(boardName));
    configFile.write((uint8_t*)otaPassword, sizeof(otaPassword));
    configFile.close();
    Serial.println("Config saved to LittleFS.");
  }

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.setHostname(boardID.c_str());
  if (otaPassword[0] != '\0') {
    ArduinoOTA.setPassword(otaPassword);
    Serial.println("ArduinoOTA: Password protection enabled.");
  } else {
    Serial.println("ArduinoOTA: Unprotected.");
  }
  ArduinoOTA.begin();

  if (WiFi.status() == WL_CONNECTED) {
    sendData();
  }
  lastTime = millis();
}

// --- 7. Loop ---
void loop() {
  ArduinoOTA.handle();
  
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
      playCatAnimation(2, "FACTORY RESET");
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
    String boardID = getBoardIdentifier();
    String resetReason = ESP.getResetReason();
    String ipAddr = WiFi.localIP().toString();
    
    String message = "\n🚀 [BOOT] Board Online!\n"
                     "Name: " + boardID + "\n"
                     "IP: " + ipAddr + "\n"
                     "Reset Reason: " + resetReason;
                     
    Serial.println("Sending Boot Notification to LINE...");
    sendLineNotify(message);
    isBootNotificationSent = true;
  }

  unsigned long currentMillis = millis();

  // 1. จัดการส่งข้อมูลไป Google Sheets (ทุก 30 นาที)
  if (currentMillis - lastTime >= timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      playCatAnimation(1, "SENDING DATA"); 
      sendData();
      playCatAnimation(1, "DONE!");
      lastTime = currentMillis; // รีเซ็ตเฉพาะเมื่อ WiFi ต่ออยู่และส่งได้
    }
    // ถ้า WiFi ยังไม่ต่อ จะ loop กลับมาลองส่งใหม่ทันทีในรอบถัดไป
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
    
    if (!isConversionRequestIssued) {
      sensors.requestTemperatures(); // ส่งคำสั่งให้เซนเซอร์เริ่มคำนวณอุณหภูมิ (ใช้เวลา ~750ms บอร์ดไม่ควรรอค้าง)
      conversionStartTime = currentMillis;
      isConversionRequestIssued = true;
    } else if (currentMillis - conversionStartTime >= 750) {
      // เมื่อเวลาผ่านไปเกิน 750ms นับจากสั่งคำนวณ ให้ดึงค่ามาแสดงผล
      currentTemp = sensors.getTempCByIndex(0);
      
      // ถ้าเชื่อมต่อ WiFi ได้ปกติ แต่อยู่ในช่วงพักรอส่งข้อมูล ให้คงสถานะ SYNCED หรือ CONNECTED ไว้
      if (WiFi.status() == WL_CONNECTED && currentStatus == "RECONNECTING") {
        currentStatus = "CONNECTED";
      }
      
      updateDisplay(currentTemp, currentStatus);
      
      // ตรวจสอบสถานะและแจ้งเตือน Line Notify
      if (currentTemp != DEVICE_DISCONNECTED_C && currentTemp > -50) {
        checkLineAlerts(currentTemp);
      }
      
      isConversionRequestIssued = false; // รีเซ็ตสถานะเพื่อรอรอบถัดไป
      lastUpdate = currentMillis;
    }
  }
}