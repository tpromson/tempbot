#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoOTA.h>
#include "bitmaps.h"

// --- 1. Configuration ---
#define SENSOR_PIN 14        // ขา D5 (สำหรับ DHT22)
#define DHTTYPE DHT22        // ชนิดเซนเซอร์ DHT22 (AM2302)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_I2C_ADDR 0x3C 

#define FRAME_DELAY 42
#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 22 

// --- 2. Objects ---
DHT dht(SENSOR_PIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String webAppUrl = "https://script.google.com/macros/s/AKfycbxP9ItR-dSp_mmQSZaXOJXY3NSm3THfRlf0owGKGMcune2YQCi4kmqexS1vfcOLKE9oqw/exec";
unsigned long lastTime = 0;
unsigned long timerDelay = 1800000; // 30 นาที

String currentStatus = "STARTING";
float currentTemp = -999;
float currentHumid = -999;

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

void updateDisplay(float temp, float humid, String status) {
  display.clearDisplay();
  
  // แถบแสดงสถานะด้านบน
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("STATUS: "); 
  display.println(status);
  display.drawFastHLine(0, 10, 128, WHITE);

  if (temp > -100 && humid >= 0) {
    // วาดเส้นแบ่งครึ่งหน้าจอแนวตั้ง
    display.drawFastVLine(64, 10, 54, WHITE);
    
    // คอลัมน์ซ้าย: แสดงอุณหภูมิ (Temperature)
    display.setTextSize(1);
    display.setCursor(5, 16);
    display.print("TEMP");
    
    display.setTextSize(2);
    display.setCursor(5, 32);
    display.print(temp, 1);
    display.setTextSize(1);
    display.print(" C");
    
    // คอลัมน์ขวา: แสดงความชื้น (Humidity)
    display.setTextSize(1);
    display.setCursor(72, 16);
    display.print("HUMID");
    
    display.setTextSize(2);
    display.setCursor(72, 32);
    display.print(humid, 1);
    display.setTextSize(1);
    display.print(" %");
  } else {
    display.setTextSize(2);
    display.setCursor(10, 30);
    display.print("SENSOR ERR");
  }
  display.display();
}

// --- 4. Logic Functions ---

void sendData() {
  // ตรวจสอบ WiFi ก่อนส่งทุกครั้ง ถ้าไม่ต่อให้ข้ามไปก่อน
  if (WiFi.status() != WL_CONNECTED) {
    currentStatus = "NO WIFI";
    return;
  }

  // อ่านค่าจากเซนเซอร์ DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  if (isnan(t) || isnan(h)) {
    currentStatus = "SENS ERR";
    return;
  }

  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String boardID = "BOARD_" + String(ESP.getChipId(), HEX);
  boardID.toUpperCase();
  
  // ส่งค่าทั้งอุณหภูมิและความชื้นจริงที่วัดได้ไปยัง Google Sheets
  String url = webAppUrl + "?temperature=" + String(t, 1) + "&humidity=" + String(h, 1) + "&board_id=" + boardID;
  
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

// --- 5. Setup ---
void setup() {
  Serial.begin(115200);
  
  // สั่งตั้งค่า WiFi Mode ให้เป็นแบบพยายามต่ออัตโนมัติเมื่อหลุด
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  dht.begin();
  
  Wire.begin(4, 5); 
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_I2C_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
  
  display.clearDisplay();
  playCatAnimation(1, "BOOTING...");

  WiFiManager wm;
  
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

  ArduinoOTA.setHostname(boardID.c_str());
  ArduinoOTA.begin();

  if (WiFi.status() == WL_CONNECTED) {
    sendData();
  }
  lastTime = millis();
}

// --- 6. Loop ---
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

  // 2. อ่านค่าเซนเซอร์แบบ Non-blocking และอัปเดตหน้าจอ (ทุก 2 วินาที)
  static unsigned long lastUpdate = 0;
  if (currentMillis - lastUpdate >= 2000) {
    
    // ดึงค่าอุณหภูมิและความชื้นจากเซนเซอร์ DHT22
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (isnan(t) || isnan(h)) {
      currentTemp = -999;
      currentHumid = -999;
    } else {
      currentTemp = t;
      currentHumid = h;
    }
    
    // ถ้าเชื่อมต่อ WiFi ได้ปกติ แต่อยู่ในช่วงพักรอส่งข้อมูล ให้คงสถานะ SYNCED หรือ CONNECTED ไว้
    if (WiFi.status() == WL_CONNECTED && currentStatus == "RECONNECTING") {
      currentStatus = "CONNECTED";
    }
    
    updateDisplay(currentTemp, currentHumid, currentStatus);
    lastUpdate = currentMillis;
  }
}
