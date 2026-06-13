# 🌡️ TempBot — ESP8266/ESP32 Temperature Monitor

โปรเจกต์ติดตามอุณหภูมิผ่าน **ESP8266 / ESP32** เซนเซอร์ **DS18B20 หรือ DHT22** บันทึกลง **Google Sheets** แจ้งเตือน **LINE** + ส่งข้อมูลไป **IoTcenter** — รองรับ Offline Buffer, OTA Update, Multi-Bitmap Animation, Web Config UI

---

## 📁 โครงสร้างโปรเจกต์

```
tempbot/
├── PShome01_DHT22/                   # บ้าน (DHT22)
├── Farm02_DHT22/                     # ฟาร์ม #2 (DHT22 — เหมือน PShome01)
├── template_DHT22/                   # Template DHT22 (ต้นแบบ)
├── Farm01_DS18B20/                   # ฟาร์ม #1 (DS18B20)
├── Farm03_DS18B20/                   # ฟาร์ม #3 (DS18B20)
├── Farm04_DS18B20/                   # ฟาร์ม #4 (DS18B20)
├── template_DS18B20/                 # Template DS18B20 (ต้นแบบ)
├── template_ESP32/                   # Template สำหรับ ESP32
├── google_apps_script/
│   └── Code.gs                       # Google Apps Script (+ IoTcenter + LINE Bot)
├── libraries/
│   ├── tempbot_common/               # shared lib (formatTime, urlEncode, sendLineNotify, ฯลฯ)
│   ├── ArduinoJson/                  # JSON parsing
│   ├── WiFiManager/
│   ├── Adafruit_SSD1306/
│   ├── Adafruit_GFX_Library/
│   ├── Adafruit_BusIO/
│   ├── OneWire/                      # DS18B20
│   ├── DallasTemperature/            # DS18B20
│   ├── DHT_sensor_library/           # DHT22
│   └── Adafruit_Unified_Sensor/      # DHT22
└── releases/
    └── v1.0.0/                       # firmware .bin (Compiled)
```

---

## 🔧 Hardware

| ชิ้นส่วน | ESP8266 | ESP32 |
|---------|---------|-------|
| บอร์ด | NodeMCU / Wemos D1 Mini | ESP32 Dev Module |
| จอ OLED | SSD1306 128×64 (I2C, addr 0x3C) | SSD1306 128×64 (I2C, addr 0x3D) |
| เซนเซอร์ | DS18B20 หรือ DHT22 | DS18B20 |

```
ESP8266 Pinout:
  D1 (GPIO 5) → OLED SCL
  D2 (GPIO 4) → OLED SDA
  D4 (GPIO 2) → DHT22 DATA
  D5 (GPIO14) → DS18B20 DATA

ESP32 Pinout:
  D22        → OLED SCL
  D21        → OLED SDA
  D14        → DS18B20 DATA
```

---

## 📦 Libraries (Arduino IDE → Library Manager)

| Library | สำหรับ |
|---------|--------|
| WiFiManager (tzapu) | ทุกบอร์ด |
| Adafruit SSD1306 | ทุกบอร์ด (OLED) |
| Adafruit GFX Library | ทุกบอร์ด (OLED) |
| ArduinoJson (Benoît Blanchon) v7.x | ทุกบอร์ด |
| OneWire (Paul Stoffregen) | DS18B20 |
| DallasTemperature (Miles Burton) | DS18B20 |
| DHT sensor library (Adafruit) | DHT22 |
| Adafruit Unified Sensor | DHT22 |

> `tempbot_common` อยู่ใน `libraries/` แล้ว

---

## ⚙️ Features

| Feature | ESP8266 | ESP32 |
|---------|---------|-------|
| DS18B20 / DHT22 | ✅ | ✅ DS18B20 |
| OLED Display | ✅ | ✅ |
| Multi-Bitmap Animation (cat/chicken/fish/tree) | ✅ | ❌ (single bitmap) |
| WiFiManager + Config Portal | ✅ | ✅ |
| Web Config UI (`http://<ip>/`) | ✅ | ✅ |
| Google Sheets Sync | ✅ | ✅ |
| LINE Notify Alert | ✅ | ✅ |
| Offline Buffer (LittleFS queue) | ✅ | ✅ |
| OTA Update | ✅ | ✅ |
| Boot LINE Notification + Reset Reason | ✅ | ✅ |
| Factory Reset (Flash btn 5s) | ✅ | ✅ |
| Temperature Calibration Offset | ✅ | ✅ |
| Temp threshold sync from GAS | ✅ | ✅ |
| Static IP | ✅ | ❌ |
| IoTcenter Integration | ผ่าน GAS | ผ่าน GAS |

---

## 🚀 Compile

### ESP8266
```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcu PShome01_DHT22
# หรือ
arduino-cli compile --fqbn esp8266:esp8266:nodemcu Farm03_DS18B20
```

### ESP32 (ต้องใช้ huge_app partition)
```bash
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" template_ESP32
```

---

## 🌐 Web Config UI

เปิด `http://<device-ip>/` เพื่อตั้งค่า:

| Field | สำหรับ |
|-------|--------|
| WebApp URL | URL ที่ Deploy จาก Code.gs |
| Sync Delay (min) | ความถี่ส่งข้อมูล |
| LINE Token | Channel Access Token |
| Board Name | ชื่อบอร์ด |
| Min/Max Temp | ค่าแจ้งเตือนอุณหภูมิ |
| OTA Password | รหัสผ่าน OTA |
| Static IP | (ESP8266 เท่านั้น) |
| Temp Calibration | Offset ค่าอุณหภูมิ |
| Bitmap | รูป animation (cat/chicken/fish/tree) — ESP8266 |

---

## 🚀 Deploy Google Apps Script

1. สร้าง **Google Sheet** เปล่า
2. **Extensions → Apps Script** → วาง `google_apps_script/Code.gs`
3. **File → Project Properties → Script Properties** → เพิ่ม:
   - `LINE_TOKEN` = Channel Access Token
   - `IOTCENTER_API_KEY` = (ถ้าใช้ IoTcenter)
4. **Deploy → New deployment → Web app**
   - Execute as: **Me**
   - Access: **Anyone**
5. เอา Web App URL ไปใส่ใน Config UI ของบอร์ด

---

## 📱 LINE Bot Commands

| คำสั่ง | รายละเอียด |
|-------|-----------|
| `temp` / `ล่าสุด` | ข้อมูลล่าสุด |
| `status` / `สถานะ` | สรุปทุกบอร์ด |
| `สรุป` / `report` / `กราฟ` | รายงาน 24 ชม. + กราฟ |
| `ตั้ง max 35` / `ตั้ง min 20` | ตั้งค่าแจ้งเตือน |
| `ดูค่า` | ดูค่าตั้งปัจจุบัน |
| `help` | คำสั่งทั้งหมด |

---

## 📊 Google Sheets Columns

| คอลัมน์ | รายละเอียด |
|---------|-----------|
| Timestamp | เวลาบันทึก (Asia/Bangkok) |
| Board ID | ชื่อบอร์ด |
| Temperature (°C) | อุณหภูมิ |
| Humidity (%) | 0 สำหรับ DS18B20 |
| Data Type | `LIVE` หรือ `BUFFERED` |

---

## 📝 License

MIT License
