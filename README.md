# TempBot — ESP8266 Temperature Monitor

โปรเจกต์ติดตามอุณหภูมิและความชื้นผ่าน ESP8266 บันทึกลง Google Sheets พร้อมแจ้งเตือน LINE Notify

---

## 📁 โครงสร้างโปรเจกต์

```
tempbot/
├── template_fixWIFIconnect/          # สำหรับเซนเซอร์ DS18B20 (อุณหภูมิอย่างเดียว)
│   ├── template_fixWIFIconnect.ino
│   └── bitmaps.h
├── template_DHT22_fixWIFIconnect/    # สำหรับเซนเซอร์ DHT22 (อุณหภูมิ + ความชื้น)
│   ├── template_DHT22_fixWIFIconnect.ino
│   └── bitmaps.h
├── google_apps_script/
│   └── Code.gs                       # Google Apps Script (บันทึกข้อมูลลง Sheets)
└── libraries/                        # Arduino libraries ที่ใช้
```

---

## 🔧 Hardware ที่ต้องการ

| ชิ้นส่วน | รายละเอียด |
|---------|-----------|
| ESP8266 | NodeMCU / Wemos D1 Mini |
| จอ OLED | SSD1306 128×64 (I2C) |
| เซนเซอร์ | DS18B20 หรือ DHT22 |

**การต่อสาย:**

| ESP8266 Pin | DS18B20 | DHT22 | OLED |
|-------------|---------|-------|------|
| D4 (GPIO 2) | DATA    | DATA  | —    |
| D1 (GPIO 5) | —       | —     | SCL  |
| D2 (GPIO 4) | —       | —     | SDA  |
| 3.3V / GND  | VCC/GND | VCC/GND | VCC/GND |

---

## 📦 Libraries ที่ต้องติดตั้ง

ใน Arduino IDE → **Sketch → Include Library → Manage Libraries...**

| Library | ผู้พัฒนา |
|---------|---------|
| WiFiManager | tzapu |
| Adafruit SSD1306 | Adafruit |
| Adafruit GFX Library | Adafruit |
| OneWire | Paul Stoffregen *(DS18B20 only)* |
| DallasTemperature | Miles Burton *(DS18B20 only)* |
| DHT sensor library | Adafruit *(DHT22 only)* |

---

## ⚙️ Features

| Feature | รายละเอียด |
|---------|-----------|
| **WiFiManager** | ตั้งค่า WiFi และ parameters ผ่านหน้าเว็บ ไม่ต้องแก้โค้ด |
| **Google Sheets Sync** | ส่งข้อมูลทุก N นาที (ตั้งค่าได้) |
| **LINE Notify Alert** | แจ้งเตือนเมื่ออุณหภูมิเกิน min/max ที่กำหนด |
| **Offline Buffer** | เก็บข้อมูลใน LittleFS เมื่อ WiFi หาย → ส่งย้อนหลังเมื่อ WiFi กลับมา |
| **WiFi Signal Icon** | แสดงความแรงสัญญาณบนจอ OLED |
| **OTA Update** | อัปเดตเฟิร์มแวร์ผ่าน WiFi ไม่ต้องต่อ USB |
| **Config Portal** | กดปุ่ม Flash สั้น → เปิด Config Portal (ไม่ล้าง WiFi) |
| **Factory Reset** | กดปุ่ม Flash ค้าง 5 วิ → ล้างทุกอย่าง |
| **Software Watchdog** | Reboot อัตโนมัติถ้า sync ล้มเหลว 10 ครั้งติดกัน |
| **OLED Burn-in Protection** | Pixel shift ป้องกันจอไหม้ |

---

## 🚀 การตั้งค่าครั้งแรก

### 1. อัปโหลดโค้ด
- เปิดไฟล์ `.ino` ที่ต้องการใน Arduino IDE
- เลือก Board: **NodeMCU 1.0 (ESP-12E Module)** หรือ **Wemos D1 Mini**
- เลือก Port ที่ถูกต้อง → Upload

### 2. ตั้งค่า WiFi และ Parameters
เมื่อบูตครั้งแรก จอจะแสดง **"WIFI SETUP"** และบอร์ดจะเปิด Access Point:
1. เชื่อมต่อมือถือ/คอมเข้า WiFi ชื่อ `ESP8266_XXXXXX`
2. เปิดเบราว์เซอร์ไปที่ `192.168.4.1`
3. กรอกข้อมูล:

| Field | รายละเอียด |
|-------|-----------|
| WiFi SSID / Password | เครือข่าย WiFi ที่ต้องการ |
| Google WebApp URL | URL จาก Apps Script (ดูขั้นตอนด้านล่าง) |
| Sync Delay (Minutes) | ความถี่การส่งข้อมูล (นาที) |
| LINE Notify Token | Token สำหรับแจ้งเตือน (ว่างได้) |
| Min Temp Alert (°C) | อุณหภูมิต่ำสุดที่แจ้งเตือน |
| Max Temp Alert (°C) | อุณหภูมิสูงสุดที่แจ้งเตือน |

4. กด **Save** → บอร์ด Restart และเชื่อมต่อ WiFi

### 3. ตั้งค่า Google Apps Script
1. เปิด [Google Sheets](https://sheets.google.com) → สร้าง Spreadsheet ใหม่
2. **Extensions → Apps Script** → วางโค้ดจากไฟล์ `google_apps_script/Code.gs`
3. **Deploy → New deployment**
   - Type: **Web app**
   - Execute as: **Me**
   - Who has access: **Anyone** ← สำคัญมาก!
4. Copy **Web App URL** → นำไปใส่ใน Config Portal

---

## 🎛️ การใช้งานปุ่ม Flash (GPIO 0)

| การกด | ผล |
|-------|-----|
| กดแล้วปล่อยภายใน 2 วิ | เปิด Config Portal (WiFi เดิมยังอยู่) |
| กดค้างไว้ 5 วิ | Factory Reset (ลบทุกอย่าง) |

---

## 📊 Google Sheets — คอลัมน์ข้อมูล

| คอลัมน์ | รายละเอียด |
|---------|-----------|
| Timestamp | เวลาที่บันทึก (Asia/Bangkok) |
| Board ID | รหัสบอร์ด เช่น `BOARD_A1B2C3` |
| Temperature (°C) | อุณหภูมิ |
| Humidity (%) | ความชื้น (ว่างสำหรับ DS18B20) |
| Data Type | `LIVE` = ส่งปกติ, `BUFFERED` = มาจาก Offline Queue |

---

## 📱 LINE Notify Setup

1. ไปที่ [notify-bot.line.me](https://notify-bot.line.me)
2. Login → **My page → Generate token**
3. ตั้งชื่อ token → เลือก Group หรือ 1:1 กับ LINE Notify
4. Copy token → ใส่ใน Config Portal

---

## 🔄 OTA Update

```
เงื่อนไข: คอมและบอร์ดอยู่ใน WiFi เดียวกัน
1. Arduino IDE → Tools → Port → เลือก ESP8266 (network port)
2. Upload ตามปกติ
```

---

## 📝 License

MIT License
