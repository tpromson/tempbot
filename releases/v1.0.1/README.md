# TempBot Firmware Release v1.0.1

แก้บั๊ก **crash loop ขณะ flush offline queue** (heap หมดเพราะ TLS buffer ไม่จำกัด + reuse client หลายตัวพร้อมกัน)

## สิ่งที่แก้จาก v1.0.0
- ใส่ `client.setBufferSizes(4096, 1024)` กลับให้ทุก `WiFiClientSecure` (sendData / flushQueue / OTA) → กัน OOM ตอน TLS handshake ติดกันหลายครั้ง
- `fetchAndApplySettings()` ใส่ `setFollowRedirects` + `setTimeout` → ดึง min/max/bitmap จาก GAS ได้จริง (เดิม 302 → HTTP != 200)
- `flushQueue()` เพิ่ม `client.stop()` ระหว่างแต่ละรายการ → คืน socket/RAM กัน OOM สะสม

## วิธีตั้งค่า OTA ใน Config Portal

> ⚠️ ใช้ **raw URL** เท่านั้น (ตอบ 200 ตรงๆ) — อย่าใช้ลิงก์ GitHub Release `releases/download/...`
> เพราะ firmware v1.0.1 ยังไม่เปิด follow-redirects ESP จะโหลด .bin ที่เป็น 302 ไม่ได้

| บอร์ด | OTA Version URL | OTA Firmware URL |
|-------|-----------------|------------------|
| Farm01_DS18B20 | `https://raw.githubusercontent.com/tpromson/tempbot/v1.0.1/releases/v1.0.1/Farm01_DS18B20/version.txt` | `https://raw.githubusercontent.com/tpromson/tempbot/v1.0.1/releases/v1.0.1/Farm01_DS18B20/firmware.bin` |
| Farm02_DHT22 | `.../v1.0.1/releases/v1.0.1/Farm02_DHT22/version.txt` | `.../v1.0.1/releases/v1.0.1/Farm02_DHT22/firmware.bin` |
| Farm03_DS18B20 | `.../v1.0.1/releases/v1.0.1/Farm03_DS18B20/version.txt` | `.../v1.0.1/releases/v1.0.1/Farm03_DS18B20/firmware.bin` |
| PShome01_DHT22 | `.../v1.0.1/releases/v1.0.1/PShome01_DHT22/version.txt` | `.../v1.0.1/releases/v1.0.1/PShome01_DHT22/firmware.bin` |

(แทน `.../` ด้วย `https://raw.githubusercontent.com/tpromson/tempbot`)

## วิธีทำงานของ OTA
1. บอร์ดดึง `version.txt` → ถ้าเลข **ไม่ตรง** กับ `FIRMWARE_VERSION` ในเครื่อง → ดาวน์โหลด `firmware.bin` มา flash เอง
2. เช็คตอนบูต + ทุก 12 ชั่วโมง

## รีบิลด์ .bin เอง
```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 --libraries ./libraries \
  --output-dir /tmp/build Farm01_DS18B20/Farm01_DS18B20.ino
cp /tmp/build/Farm01_DS18B20.ino.bin releases/v1.0.1/Farm01_DS18B20/firmware.bin
```
