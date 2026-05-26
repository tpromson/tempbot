# TempBot Firmware Releases

## วิธีใช้งาน OTA Update

### 1. ตั้งค่าใน WiFiManager Portal

กรอก URL ดังนี้ในช่อง OTA:

```
OTA Version URL: https://raw.githubusercontent.com/tpromson/tempbot/v1.0.0/PShome01_DHT22/version.txt
OTA Firmware URL: https://raw.githubusercontent.com/tpromson/tempbot/v1.0.0/PShome01_DHT22/firmware.bin
```

### 2. สำหรับบอร์ดอื่นๆ

| บอร์ด | Version URL | Firmware URL |
|-------|-------------|--------------|
| PShome01_DHT22 | `.../v1.0.0/PShome01_DHT22/version.txt` | `.../v1.0.0/PShome01_DHT22/firmware.bin` |
| Farm01_DS18B20 | `.../v1.0.0/Farm01_DS18B20/version.txt` | `.../v1.0.0/Farm01_DS18B20/firmware.bin` |
| Farm02_DHT22 | `.../v1.0.0/Farm02_DHT22/version.txt` | `.../v1.0.0/Farm02_DHT22/firmware.bin` |
| Farm03_DS18B20 | `.../v1.0.0/Farm03_DS18B20/version.txt` | `.../v1.0.0/Farm03_DS18B20/firmware.bin` |
| Farm04_DS18B20 | `.../v1.0.0/Farm04_DS18B20/version.txt` | `.../v1.0.0/Farm04_DS18B20/firmware.bin` |

### 3. Compile Firmware (.bin)

```bash
# ใช้ Arduino CLI
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 PShome01_DHT22/PShome01_DHT22.ino
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --input-dir ./build PShome01_DHT22/PShome01_DHT22.ino

# หรือ Export Binary จาก Arduino IDE
# Sketch > Export Compiled Binary
```

### 4. สร้าง Release ใหม่

```bash
# 1. แก้ไข FIRMWARE_VERSION ใน .ino
# 2. Compile .bin
# 3. สร้าง tag และ release ใหม่
git tag v1.0.1
git push origin v1.0.1
# จากนั้นสร้าง GitHub Release ผ่าน web interface แล้ว upload .bin
```

## การอัปเดตเวอร์ชัน

เมื่อมีเวอร์ชันใหม่ อัปเดต `version.txt` ในโฟลเดอร์ที่เกี่ยวข้อง แล้วสร้าง GitHub Release ใหม่พร้อมไฟล์ `.bin`

## ข้อควรระวัง

- บอร์ดจะตรวจสอบเวอร์ชันใหม่ทุก 12 ชั่วโมง
- ถ้า `version.txt` ตรงกับ `FIRMWARE_VERSION` จะไม่อัปเดต
- การอัปเดตจะรีบูตบอร์ดอัตโนมัติ
