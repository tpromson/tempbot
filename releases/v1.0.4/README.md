# TempBot Firmware Release v1.0.4

## Hardening (ป้องกัน crash loop จาก GAS ช้า/พัง — root cause จริงที่เจอตอน debug Chicken02)
- **เช็ค response body == "OK" จริง** ไม่ใช่แค่ HTTP 200 — GAS ห่อหน้า error เป็น 200 ทำให้บอร์ดเคยคิดว่า synced ทั้งที่ไม่ได้บันทึก (sync + flush)
- **`ESP.wdtDisable()` รอบ http.GET()** (sync/settings/flush) — กัน soft WDT (3.2s) ตอน GAS cold start ช้า ~6s; ใช้ hardware WDT (~8s) เป็น backstop

## OTA URL (raw)
แทน `RAW` = `https://raw.githubusercontent.com/tpromson/tempbot/v1.0.4/releases/v1.0.4`

| บอร์ด | Version | Firmware |
|---|---|---|
| Farm01_DS18B20 | `RAW/Farm01_DS18B20/version.txt` | `RAW/Farm01_DS18B20/firmware.bin` |
| Farm02_DHT22 | `RAW/Farm02_DHT22/version.txt` | `RAW/Farm02_DHT22/firmware.bin` |
| Farm03_DS18B20 | `RAW/Farm03_DS18B20/version.txt` | `RAW/Farm03_DS18B20/firmware.bin` |
| PShome01_DHT22 | `RAW/PShome01_DHT22/version.txt` | `RAW/PShome01_DHT22/firmware.bin` |

## ประวัติ
- v1.0.1 crash loop fix (TLS) · v1.0.2 config corruption · v1.0.3 version on OLED · v1.0.4 GAS-slow/error hardening
