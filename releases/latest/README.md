# TempBot OTA — "latest" pointer (แนะนำให้ใช้)

OTA URL แบบนี้ **ไม่ต้องแก้รายบอร์ดทุกครั้งที่ออกเวอร์ชันใหม่** — ชี้ที่ branch `master`
+ โฟลเดอร์ `latest/<ชนิดเซนเซอร์>/` ตัวเดียว แล้วทุกบอร์ดจะอัปตามอัตโนมัติ

bin แยกตาม **ชนิดเซนเซอร์** (config อยู่ใน LittleFS ของแต่ละบอร์ด ไม่อยู่ใน firmware)
ดังนั้นบอร์ด DS18B20 ทุกตัวใช้ bin เดียวกัน, DHT22 ทุกตัวใช้ bin เดียวกัน

## OTA URL ที่ตั้งในบอร์ด
แทน `RAW` = `https://raw.githubusercontent.com/tpromson/tempbot/master/releases/latest`

| ชนิดเซนเซอร์ | OTA Version URL | OTA Firmware URL |
|---|---|---|
| **DS18B20** (Ps garden, Chicken03, ฟาร์ม DS18B20) | `RAW/DS18B20/version.txt` | `RAW/DS18B20/firmware.bin` |
| **DHT22** (Chicken02, PShome, ฟาร์ม DHT22) | `RAW/DHT22/version.txt` | `RAW/DHT22/firmware.bin` |

> ใช้ `master` (ไม่ใช่ tag `v1.0.x`) — raw จะเสิร์ฟไฟล์ล่าสุดที่ push เข้า master เสมอ

## logic ฝั่ง firmware (v1.0.5+)
เทียบเวอร์ชันด้วย `isNewerVersion()` (semver) → **อัปเฉพาะที่ใหม่กว่า ไม่ downgrade**
ดังนั้นชี้ที่ `latest` ปลอดภัย ไม่วนอัป/ถอย

## วิธีออกเวอร์ชันใหม่ (rollout ทุกบอร์ดในขั้นตอนเดียว)
```bash
# 1. แก้โค้ด + bump FIRMWARE_VERSION (เช่น 1.0.5 -> 1.0.6) ใน TempBot/TempBot.ino
# 2. build bin canonical 2 ชนิด ด้วย Makefile
cd TempBot
make ds18b20    # → ../releases/latest/DS18B20/firmware.bin
make dht22      # → ../releases/latest/DHT22/firmware.bin
# 3. bump version.txt ทั้งสองโฟลเดอร์
echo 1.0.6 > ../releases/latest/DS18B20/version.txt
echo 1.0.6 > ../releases/latest/DHT22/version.txt
# 4. push เข้า master — ทุกบอร์ดจะเช็ค (ตอนบูต + ทุก 12 ชม.) เห็นใหม่กว่า แล้วอัปเอง
git add -f releases/latest && git commit -m "release 1.0.6" && git push origin master
```

## หมายเหตุ
- โฟลเดอร์ `releases/v1.0.x/` แบบเดิม (ฝัง version ใน URL) ยังอยู่เพื่อ archive — แต่ **อย่าตั้ง OTA URL ของบอร์ดไปที่ tag เก่า** (จะเสี่ยง logic เก่าถอยเวอร์ชัน) ใช้ `latest` แทน
- เวอร์ชันเก่าย้อนได้จาก git history เสมอ
