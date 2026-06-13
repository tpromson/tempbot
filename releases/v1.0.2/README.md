# TempBot Firmware Release v1.0.2

แก้บั๊ก **config corruption** + cosmetic ต่อจาก v1.0.1

## สิ่งที่แก้จาก v1.0.1
- **(สำคัญ) Config corruption** — `setup()` เคยเขียน `/config.bin` แบบ inline โดยลืม `bitmapName` + `staticIP` (794 ไบต์) ไม่ตรงกับ `saveConfig()`/ตัวอ่าน (830 ไบต์) → หลัง reboot ครั้งที่ 2 ฟิลด์ `bitmap/staticIP/otaVersionUrl/otaBinUrl/calibration` เลื่อน offset เพี้ยน (OTA URL พังเอง) แก้โดยให้ `setup()` เรียก `saveConfig()` แทน — แหล่งความจริงเดียว
- เส้นแบ่งจอใช้พิกัด Y ผิด (`10 + shiftX` → `10 + shiftY`)
- DS18B20: `displayState % 4` → `% 3` (มีแค่ 3 state แถบล่างเคยว่าง 15 วิ/รอบ)

## OTA URL (ใช้ raw URL เท่านั้น — อย่าใช้ลิงก์ releases/download ที่เป็น 302)
แทน `RAW` = `https://raw.githubusercontent.com/tpromson/tempbot/v1.0.2/releases/v1.0.2`

| บอร์ด | Version URL | Firmware URL |
|-------|-------------|--------------|
| Farm01_DS18B20 | `RAW/Farm01_DS18B20/version.txt` | `RAW/Farm01_DS18B20/firmware.bin` |
| Farm02_DHT22 | `RAW/Farm02_DHT22/version.txt` | `RAW/Farm02_DHT22/firmware.bin` |
| Farm03_DS18B20 | `RAW/Farm03_DS18B20/version.txt` | `RAW/Farm03_DS18B20/firmware.bin` |
| PShome01_DHT22 | `RAW/PShome01_DHT22/version.txt` | `RAW/PShome01_DHT22/firmware.bin` |

## วิธีทำงาน
บอร์ดดึง `version.txt` → ถ้าไม่ตรงกับ `FIRMWARE_VERSION` ในเครื่อง → ดาวน์โหลด `firmware.bin` มา flash เอง (เช็คตอนบูต + ทุก 12 ชม.)
