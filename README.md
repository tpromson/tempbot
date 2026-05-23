# 🌡️ TempBot — ESP8266 Temperature & Humidity Monitor

โปรเจกต์ติดตามอุณหภูมิและความชื้นผ่าน **ESP8266** บันทึกลง **Google Sheets** พร้อมแจ้งเตือน **LINE Notify** — รองรับ Offline Buffer, OTA Update, และ Web Config UI

---

## 📁 โครงสร้างโปรเจกต์

```
tempbot/
├── Farm02_DHT22/                     # บอร์ดฟาร์ม #2 (DHT22 — อุณหภูมิ + ความชื้น)
│   ├── Farm02_DHT22.ino
│   └── bitmaps.h
├── Farm04_DS18B20/                   # บอร์ดฟาร์ม #4 (DS18B20 — อุณหภูมิอย่างเดียว)
│   ├── Farm04_DS18B20.ino
│   └── bitmaps.h
├── template_DHT22/                   # Template สำหรับเซนเซอร์ DHT22
│   ├── template_DHT22.ino
│   └── bitmaps.h
├── template_DS18B20/                 # Template สำหรับเซนเซอร์ DS18B20
│   ├── template_DS18B20.ino
│   └── bitmaps.h
├── template_ESP32/                   # Template สำหรับ ESP32
│   ├── template_ESP32.ino
│   └── bitmaps.h
├── google_apps_script/
│   └── Code.gs                       # Google Apps Script (บันทึกข้อมูลลง Sheets)
└── libraries/                        # Arduino libraries ที่ใช้ (vendored)
    ├── Adafruit_BusIO/
    ├── Adafruit_Unified_Sensor/
    └── DHT_sensor_library/
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
| Adafruit BusIO | Adafruit |
| DHT sensor library | Adafruit *(DHT22 only)* |
| Adafruit Unified Sensor | Adafruit *(DHT22 only)* |
| OneWire | Paul Stoffregen *(DS18B20 only)* |
| DallasTemperature | Miles Burton *(DS18B20 only)* |

> **หมายเหตุ:** ไลบรารีบางส่วนอยู่ในโฟลเดอร์ `libraries/` แล้ว ไม่ต้องติดตั้งซ้ำ

---

## ⚙️ Features

| Feature | รายละเอียด |
|---------|-----------|
| **Web Config UI** | แก้ไขค่าทุกอย่างผ่านเบราว์เซอร์ที่ `http://<device-ip>/` ไม่ต้องแก้โค้ด |
| **WiFiManager** | ตั้งค่า WiFi ผ่าน Access Point ครั้งแรก |
| **Google Sheets Sync** | ส่งข้อมูลทุก N นาที (ตั้งค่าได้, ค่าเริ่มต้น 10 นาที) |
| **LINE Notify Alert** | แจ้งเตือนเมื่ออุณหภูมิ/ความชื้นเกิน min/max ที่กำหนด |
| **Offline Buffer** | เก็บข้อมูลใน LittleFS เมื่อ WiFi หาย → flush อัตโนมัติเมื่อ WiFi กลับมา |
| **Auto Queue Flush** | ตรวจ queue ทุก loop cycle และส่งย้อนหลังทันทีที่ WiFi พร้อม |
| **WiFi Signal Icon** | แสดงความแรงสัญญาณบนจอ OLED |
| **OLED IP Display** | แสดง IP Address บนจอ OLED เมื่อบูต |
| **OTA Update** | อัปเดตเฟิร์มแวร์ผ่าน WiFi ไม่ต้องต่อ USB (รองรับ Password) |
| **Boot LINE Notification** | แจ้งเตือน LINE เมื่อบอร์ดออนไลน์ พร้อม IP และ Reset Reason |
| **Config Portal** | กดปุ่ม Flash สั้น → เปิด Config Portal (ไม่ล้าง WiFi) |
| **Factory Reset** | กดปุ่ม Flash ค้าง 5 วิ → ล้างทุกอย่าง |
| **Software Watchdog** | Reboot อัตโนมัติถ้า sync ล้มเหลว 10 ครั้งติดกัน |
| **OLED Burn-in Protection** | Pixel shift ทุก 1 นาที ป้องกันจอไหม้ |
| **Temperature Calibration** | ปรับ offset ค่าอุณหภูมิให้ตรงกับอุปกรณ์อ้างอิง |

---

## 🌐 Web Config UI

หลังจากบอร์ดเชื่อมต่อ WiFi สำเร็จ สามารถเปิดเบราว์เซอร์ไปที่ IP ของบอร์ด เช่น `http://192.168.0.106/` เพื่อแก้ไขการตั้งค่าได้ทันที:

| Field | รายละเอียด |
|-------|-----------|
| WebApp URL | URL ของ Google Apps Script |
| Sync Delay (min) | ความถี่การส่งข้อมูล (นาที) |
| LINE Token | Channel Access Token สำหรับ LINE Notify |
| Board Name | ชื่อบอร์ด เช่น `Farm02`, `Kitchen` |
| Min Temp (°C) | อุณหภูมิต่ำสุดที่แจ้งเตือน |
| Max Temp (°C) | อุณหภูมิสูงสุดที่แจ้งเตือน |
| Min Humid (%) | ความชื้นต่ำสุดที่แจ้งเตือน |
| Max Humid (%) | ความชื้นสูงสุดที่แจ้งเตือน |

กด **Save** → บอร์ดบันทึกค่าลง LittleFS และ Restart อัตโนมัติ

---

## 🚀 การตั้งค่าครั้งแรก

### 1. อัปโหลดโค้ด
- เปิดไฟล์ `.ino` ที่ต้องการใน Arduino IDE
- เลือก Board: **NodeMCU 1.0 (ESP-12E Module)** หรือ **Wemos D1 Mini**
- เลือก Port ที่ถูกต้อง → Upload

### 2. ตั้งค่า WiFi ครั้งแรก
เมื่อบูตครั้งแรก บอร์ดจะเปิด Access Point:
1. เชื่อมต่อมือถือ/คอมเข้า WiFi ชื่อ `ESP8266_XXXXXX`
2. เปิดเบราว์เซอร์ไปที่ `192.168.4.1`
3. กรอก WiFi SSID / Password และค่า parameters ต่างๆ
4. กด **Save** → บอร์ด Restart และเชื่อมต่อ WiFi

### 3. ตั้งค่า Google Apps Script
1. เปิด [Google Sheets](https://sheets.google.com) → สร้าง Spreadsheet ใหม่
2. **Extensions → Apps Script** → วางโค้ดจากไฟล์ `google_apps_script/Code.gs`
3. **Deploy → New deployment**
   - Type: **Web app**
   - Execute as: **Me**
   - Who has access: **Anyone** ← สำคัญมาก!
4. Copy **Web App URL** → นำไปใส่ใน Web Config UI หรือ Config Portal

---

## 🎛️ การใช้งานปุ่ม Flash (GPIO 0)

| การกด | ผล |
|-------|-----|
| กดแล้วปล่อยภายใน 2 วิ | เปิด Config Portal (WiFi เดิมยังอยู่) |
| กดค้างไว้ 5 วิ | Factory Reset (ลบทุกอย่าง พร้อม Reboot) |

---

## 📊 Google Sheets — คอลัมน์ข้อมูล

| คอลัมน์ | รายละเอียด |
|---------|-----------|
| Timestamp | เวลาที่บันทึก (Asia/Bangkok) |
| Board ID | ชื่อหรือรหัสบอร์ด เช่น `Farm02` หรือ `BOARD_A1B2C3` |
| Temperature (°C) | อุณหภูมิ |
| Humidity (%) | ความชื้น (ว่างสำหรับ DS18B20) |
| Data Type | `LIVE` = ส่งปกติ, `BUFFERED` = มาจาก Offline Queue |

---

## 📱 LINE Notify Setup

1. ไปที่ [notify-bot.line.me](https://notify-bot.line.me)
2. Login → **My page → Generate token**
3. ตั้งชื่อ token → เลือก Group หรือ 1:1 กับ LINE Notify
4. Copy token → ใส่ใน Web Config UI (`http://<device-ip>/`)

**การแจ้งเตือนที่รองรับ:**
- 🚀 Boot notification (เมื่อบอร์ดออนไลน์)
- 🌡️ อุณหภูมิสูง/ต่ำเกินค่าที่กำหนด
- 💧 ความชื้นสูง/ต่ำเกินค่าที่กำหนด

---

## 🔄 OTA Update

```
เงื่อนไข: คอมและบอร์ดอยู่ใน WiFi เดียวกัน
1. Arduino IDE → Tools → Port → เลือก ESP8266 (network port)
2. Upload ตามปกติ
```

> หากตั้ง OTA Password ไว้ Arduino IDE จะขอ password ก่อน upload

---

## 🗄️ Offline Queue

เมื่อ WiFi หาย บอร์ดจะ:
1. **บันทึก** ข้อมูลลงใน `/queue.csv` บน LittleFS (สูงสุด 32 รายการ)
2. เมื่อ WiFi กลับมา → **flush อัตโนมัติ** ทุก loop cycle
3. Google Sheets จะได้รับข้อมูลย้อนหลังพร้อม `timestamp` ที่ถูกต้อง

---

## 📝 License

MIT License

