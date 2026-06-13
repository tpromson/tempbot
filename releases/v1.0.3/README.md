# TempBot Firmware Release v1.0.3

## สิ่งที่เพิ่มจาก v1.0.2
- **แสดงเวอร์ชัน firmware บนจอ OLED** — หน้าจอบูต (ตอนเปิดเครื่อง โชว์ ~3 วินาที) เพิ่มบรรทัด `FW v1.0.3` ต่อจาก IP Address เพื่อให้รู้ว่าบอร์ดอยู่เวอร์ชันไหน (กดปุ่ม RST เพื่อดูได้ทุกเมื่อ)

## OTA URL (raw URL เท่านั้น)
แทน `RAW` = `https://raw.githubusercontent.com/tpromson/tempbot/v1.0.3/releases/v1.0.3`

| บอร์ด | Version URL | Firmware URL |
|-------|-------------|--------------|
| Farm01_DS18B20 | `RAW/Farm01_DS18B20/version.txt` | `RAW/Farm01_DS18B20/firmware.bin` |
| Farm02_DHT22 | `RAW/Farm02_DHT22/version.txt` | `RAW/Farm02_DHT22/firmware.bin` |
| Farm03_DS18B20 | `RAW/Farm03_DS18B20/version.txt` | `RAW/Farm03_DS18B20/firmware.bin` |
| PShome01_DHT22 | `RAW/PShome01_DHT22/version.txt` | `RAW/PShome01_DHT22/firmware.bin` |

## ประวัติแก้บั๊ก
- v1.0.1 — แก้ crash loop ตอน flush offline queue (TLS OOM)
- v1.0.2 — แก้ config corruption (setup เขียน config ลืม bitmap/staticIP) + cosmetic
- v1.0.3 — แสดงเวอร์ชันบนจอ
