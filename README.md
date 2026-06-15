# 🌡️ TempBot — ESP8266/ESP32 Temperature Monitor

โปรเจกต์ติดตามอุณหภูมิผ่าน **ESP8266 / ESP32** เซนเซอร์ **DS18B20 หรือ DHT22** บันทึกลง **Google Sheets** แจ้งเตือน **LINE** + ส่งข้อมูลไป **IoTcenter** — รองรับ Offline Buffer, OTA Update, Multi-Bitmap Animation, Web Config UI

---

## 📁 โครงสร้างโปรเจกต์

```
tempbot/
├── TempBot/                          # Unified firmware (DS18B20 / DHT22 via build flag)
│   ├── TempBot.ino
│   └── Makefile                      # make dht22 / ds18b20 / flash_*
├── PShome01_DHT22/                   # บ้าน (DHT22) — ก่อน unify
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
├── test/                             # Unit tests (host-side C++)
└── releases/
    └── latest/                       # firmware .bin (DHT22 + DS18B20)
        ├── DHT22/
        └── DS18B20/
```

---

## 🔧 Hardware

| ชิ้นส่วน | ค่า |
|---------|------|
| บอร์ด | NodeMCU / Wemos D1 Mini (ESP8266) |
| จอ OLED | SSD1306 128×64 (I2C, addr 0x3C) |
| เซนเซอร์ | DS18B20 หรือ DHT22 |

```
ESP8266 Pinout:
  D1 (GPIO 5) → OLED SCL
  D2 (GPIO 4) → OLED SDA
  D4 (GPIO 2) → DHT22 DATA
  D5 (GPIO14) → DS18B20 DATA
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

| Feature | รองรับ |
|---------|------|
| DS18B20 / DHT22 | ✅ |
| OLED Display | ✅ |
| Multi-Bitmap Animation (cat/chicken/fish/tree) | ✅ |
| WiFiManager + Config Portal | ✅ |
| Web Config UI (`http://<ip>/`) | ✅ |
| Google Sheets Sync | ✅ |
| LINE Notify Alert | ✅ |
| Offline Buffer (LittleFS queue) | ✅ |
| OTA Update | ✅ |
| Boot LINE Notification + Reset Reason | ✅ |
| Factory Reset (Flash btn 5s) | ✅ |
| Temperature Calibration Offset | ✅ |
| Temp threshold sync from GAS | ✅ |
| Static IP | ✅ |
| IoTcenter Integration | ผ่าน GAS |

---

## 🚀 Compile

ใช้โค้ดรวม `TempBot/TempBot.ino` — เลือก sensor ด้วย build flag

### ESP8266 (DHT22 / DS18B20)
```bash
cd TempBot
make ds18b20    # build → releases/latest/DS18B20/firmware.bin
make dht22      # build → releases/latest/DHT22/firmware.bin
make flash_ds18b20  PORT=/dev/cu.wchusbserial1110
```

หรือเรียก `arduino-cli` ตรง ๆ:
```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 \
  --build-property "build.extra_flags=-DSENSOR_DS18B20" TempBot
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
