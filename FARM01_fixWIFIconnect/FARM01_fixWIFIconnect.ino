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
unsigned long lastTime = 0;
unsigned long timerDelay = 1800000; // 30 นาที (ค่าเริ่มต้น)

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
    return;
  }

  sensors.requestTemperatures(); 
  delay(750); // รอ DS18B20 แปลงค่าความละเอียด 12-bit
  
  float t = sensors.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t == 85.0) {
    currentStatus = "SENS ERR";
    return;
  }

  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String boardID = "BOARD_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();
  
  String url = String(webAppUrl) + "?temperature=" + String(t, 1) + "&humidity=0&board_id=" + boardID;
  
  if (http.begin(client, url)) {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000); 
    int httpCode = http.GET();
    currentStatus = (httpCode == 200) ? "SYNCED" : "ERR " + String(httpCode);
    http.end();
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
  
  // สั่งตั้งค่า WiFi Mode ให้เป็นแบบพยายามต่ออัตโนมัติเมื่อหลุด
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  // 1. อ่านค่าพารามิเตอร์จาก LittleFS
  if (LittleFS.begin()) {
    Serial.println("LittleFS mounted successfully.");
    if (LittleFS.exists("/config.bin")) {
      File configFile = LittleFS.open("/config.bin", "r");
      if (configFile) {
        configFile.readBytes(webAppUrl, sizeof(webAppUrl));
        configFile.readBytes(timerDelayStr, sizeof(timerDelayStr));
        configFile.close();
        Serial.println("Config loaded from LittleFS:");
        Serial.print("URL: "); Serial.println(webAppUrl);
        Serial.print("Delay: "); Serial.println(timerDelayStr);
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
  wm.addParameter(&custom_url);
  wm.addParameter(&custom_delay);
  
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

  File configFile = LittleFS.open("/config.bin", "w");
  if (configFile) {
    configFile.write((uint8_t*)webAppUrl, sizeof(webAppUrl));
    configFile.write((uint8_t*)timerDelayStr, sizeof(timerDelayStr));
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
      isConversionRequestIssued = false; // รีเซ็ตสถานะเพื่อรอรอบถัดไป
      lastUpdate = currentMillis;
    }
  }
}