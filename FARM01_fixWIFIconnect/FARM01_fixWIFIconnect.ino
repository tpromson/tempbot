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
#include "bitmaps.h"

// --- 1. Configuration ---
#define SENSOR_PIN 14        // ขา D5 (สำหรับ DS18B20)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_I2C_ADDR 0x3C 

#define FRAME_DELAY 42
#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 22 

// --- 2. Objects ---
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

char webAppUrl[150] = "xxx";
char timerDelayStr[10] = "30";
char lineToken[55] = "";
char minTempAlert[10] = "20.0";
char maxTempAlert[10] = "35.0";
unsigned long lastTime = 0;
unsigned long timerDelay = 1800000; // 30 นาที (ค่าเริ่มต้น)
int failedSyncCount = 0;            // นับจำนวนครั้งที่ส่งข้อมูลไม่สำเร็จติดต่อกัน

enum AlertState { STATE_NORMAL, STATE_ALERT_LOW, STATE_ALERT_HIGH };
AlertState lastAlertState = STATE_NORMAL;
unsigned long lastLineNotifyTime = 0;

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

void updateDisplay(float temp, String status) {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(shiftX, shiftY);
  display.print("STATUS: "); 
  display.println(status);
  display.drawFastHLine(0, 10 + shiftY, 128, WHITE);

  if (temp != DEVICE_DISCONNECTED_C && temp > -50) {
    display.setTextSize(4);
    display.setCursor(5 + shiftX, 20 + shiftY);
    display.print(temp, 1);
    display.setTextSize(2);
    display.print(" C");
  } else {
    display.setTextSize(2);
    display.setCursor(10 + shiftX, 30 + shiftY);
    display.print("SENSOR ERR");
  }
  display.display();
}

// --- 5. Logic Functions ---

void sendData() {
  // ตรวจสอบ WiFi ก่อนส่งทุกครั้ง ถ้าไม่ต่อให้ข้ามไปก่อน
  if (WiFi.status() != WL_CONNECTED) {
    currentStatus = "NO WIFI";
    failedSyncCount++;
    if (failedSyncCount >= 10) {
      Serial.println("Watchdog: Sync failed 10 times consecutively. Rebooting...");
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000);
      ESP.restart();
    }
    return;
  }

  sensors.requestTemperatures(); 
  delay(750); // รอ DS18B20 แปลงค่าความละเอียด 12-bit
  
  float t = sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t == 85.0) {
    currentStatus = "SENS ERR";
    failedSyncCount++;
    if (failedSyncCount >= 10) {
      Serial.println("Watchdog: Sync failed 10 times consecutively. Rebooting...");
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000);
      ESP.restart();
    }
    return;
  }

  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String boardID = "BOARD_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();
  
  String url = String(webAppUrl) + "?temperature=" + String(t, 1) + "&humidity=0&board_id=" + boardID;
  
  bool syncSuccess = false;
  if (http.begin(client, url)) {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000); 
    int httpCode = http.GET();
    if (httpCode == 200) {
      currentStatus = "SYNCED";
      failedSyncCount = 0; // รีเซ็ตตัวนับเมื่อสำเร็จ
      syncSuccess = true;
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
      Serial.println("Watchdog: Sync failed 10 times consecutively. Rebooting...");
      playCatAnimation(2, "WATCHDOG REBOOT");
      delay(1000);
      ESP.restart();
    }
  }
}

void sendLineNotify(String message) {
  if (lineToken[0] == '\0') return; // ข้ามถ้าไม่มี Token
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  if (http.begin(client, "https://notify-api.line.me/api/notify")) {
    http.addHeader("Authorization", "Bearer " + String(lineToken));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String postData = "message=" + message;
    int httpCode = http.POST(postData);
    if (httpCode == 200) {
      Serial.println("LINE Notify: Sent successfully.");
    } else {
      Serial.print("LINE Notify: Failed with code "); Serial.println(httpCode);
    }
    http.end();
  } else {
    Serial.println("LINE Notify: Failed to connect.");
  }
}

void checkLineAlerts(float temp) {
  if (lineToken[0] == '\0') return; // ข้ามหากไม่ได้กรอก Line Token

  float minT = atof(minTempAlert);
  float maxT = atof(maxTempAlert);
  unsigned long currentMillis = millis();
  
  String boardID = "BOARD_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();

  AlertState newState = STATE_NORMAL;
  if (temp < minT && temp > -50) {
    newState = STATE_ALERT_LOW;
  } else if (temp > maxT) {
    newState = STATE_ALERT_HIGH;
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

// --- 6. Setup ---
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
        if (fileSize >= 235) {
          configFile.readBytes(lineToken, sizeof(lineToken));
          configFile.readBytes(minTempAlert, sizeof(minTempAlert));
          configFile.readBytes(maxTempAlert, sizeof(maxTempAlert));
        } else {
          // ค่าเริ่มต้นสำหรับการแจ้งเตือนหากอัปเกรดมาจากรุ่นเก่า
          lineToken[0] = '\0';
          strcpy(minTempAlert, "20.0");
          strcpy(maxTempAlert, "35.0");
        }
        configFile.close();
        Serial.println("Config loaded from LittleFS:");
        Serial.print("URL: "); Serial.println(webAppUrl);
        Serial.print("Delay: "); Serial.println(timerDelayStr);
        Serial.print("LINE Token: "); Serial.println(lineToken);
        Serial.print("Min Alert: "); Serial.println(minTempAlert);
        Serial.print("Max Alert: "); Serial.println(maxTempAlert);
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
  
  Wire.begin(4, 5); 
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_I2C_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
  
  display.dim(true); // เปิดโหมดประหยัดหน้าจอ (Dim Screen) ยืดอายุหน้าจอ OLED
  display.clearDisplay();
  playCatAnimation(1, "BOOTING...");

  WiFiManager wm;

  // เพิ่มช่องกรอกค่าปรับแต่ง (Custom Parameters)
  WiFiManagerParameter custom_url("url", "Google WebApp URL", webAppUrl, 150);
  WiFiManagerParameter custom_delay("delay", "Sync Delay (Minutes)", timerDelayStr, 10);
  WiFiManagerParameter custom_token("token", "LINE Notify Token", lineToken, 55);
  WiFiManagerParameter custom_min_temp("min_temp", "Min Temp Alert (C)", minTempAlert, 10);
  WiFiManagerParameter custom_max_temp("max_temp", "Max Temp Alert (C)", maxTempAlert, 10);
  
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_min_temp);
  wm.addParameter(&custom_max_temp);
  
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
  strncpy(minTempAlert, custom_min_temp.getValue(), sizeof(minTempAlert));
  strncpy(maxTempAlert, custom_max_temp.getValue(), sizeof(maxTempAlert));

  File configFile = LittleFS.open("/config.bin", "w");
  if (configFile) {
    configFile.write((uint8_t*)webAppUrl, sizeof(webAppUrl));
    configFile.write((uint8_t*)timerDelayStr, sizeof(timerDelayStr));
    configFile.write((uint8_t*)lineToken, sizeof(lineToken));
    configFile.write((uint8_t*)minTempAlert, sizeof(minTempAlert));
    configFile.write((uint8_t*)maxTempAlert, sizeof(maxTempAlert));
    configFile.close();
    Serial.println("Config saved to LittleFS.");
  }

  ArduinoOTA.setHostname(boardID.c_str());
  ArduinoOTA.begin();

  if (WiFi.status() == WL_CONNECTED) {
    sendData();
  }
  lastTime = millis();
}

// --- 7. Loop ---
void loop() {
  ArduinoOTA.handle();
  
  // ตรวจสอบการกดปุ่ม Flash (GPIO 0) ค้างไว้ 5 วินาทีเพื่อ Reset Settings
  static unsigned long flashPressStartTime = 0;
  if (digitalRead(0) == LOW) {
    if (flashPressStartTime == 0) {
      flashPressStartTime = millis();
    }
    unsigned long holdTime = millis() - flashPressStartTime;
    if (holdTime >= 5000) {
      playCatAnimation(2, "FACTORY RESET");
      WiFiManager wm;
      wm.resetSettings();
      if (LittleFS.begin()) {
        LittleFS.remove("/config.bin");
      }
      Serial.println("Settings and LittleFS config reset! Rebooting...");
      delay(1000);
      ESP.restart();
    } else {
      // แสดงตัวนับถอยหลัง 5..1 วินาที
      int countdown = 5 - (holdTime / 1000);
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(5, 10);
      display.println("KEEP PRESSING FLASH");
      display.setCursor(20, 24);
      display.println("TO FACTORY RESET");
      display.setTextSize(3);
      display.setCursor(55, 40);
      display.print(countdown);
      display.display();
      delay(50);
      return; // ข้ามการทำงานรอบปกติของ loop ขณะกดปุ่ม
    }
  } else {
    if (flashPressStartTime != 0) {
      flashPressStartTime = 0;
      updateDisplay(currentTemp, currentStatus); // คืนค่าหน้าจอปกติทันทีเมื่อปล่อยปุ่ม
    }
  }

  // ตรวจสอบสถานะ WiFi สม่ำเสมอ
  checkWiFiConnection();

  unsigned long currentMillis = millis();

  // 1. จัดการส่งข้อมูลไป Google Sheets (ทุก 30 นาที)
  if (currentMillis - lastTime >= timerDelay) {
    if (WiFi.status() == WL_CONNECTED) {
      playCatAnimation(1, "SENDING DATA"); 
      sendData();
      playCatAnimation(1, "DONE!");
    }
    lastTime = currentMillis;
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
    }
    
    // เมื่อเวลาผ่านไปเกิน 750ms นับจากสั่งคำนวณ ให้ดึงค่ามาแสดงผล
    if (isConversionRequestIssued && (currentMillis - conversionStartTime >= 750)) {
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