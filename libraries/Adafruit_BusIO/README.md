# Adafruit Bus IO Library — TempBot Project

[![Build Status](https://github.com/adafruit/Adafruit_BusIO/workflows/Arduino%20Library%20CI/badge.svg)](https://github.com/adafruit/Adafruit_BusIO/actions)

## About

This is a **vendored copy** of the [Adafruit Bus IO Library](https://github.com/adafruit/Adafruit_BusIO) bundled with the **TempBot** project.

Adafruit Bus IO is a helper library that abstracts away I2C, SPI, and UART transactions and register access. In the TempBot project, it is used as a **dependency of Adafruit SSD1306** and **Adafruit GFX**, which drive the 128×64 OLED display over I2C.

---

## Role in TempBot

| Component | Interface | Uses Adafruit_BusIO? |
|-----------|-----------|----------------------|
| SSD1306 OLED Display | I2C (SDA=D2, SCL=D1) | ✅ Yes (via Adafruit_SSD1306) |
| DHT22 Sensor | GPIO (D4) | ❌ No (direct GPIO read) |
| ESP8266 WiFi | Built-in | ❌ No |

The OLED is connected to the ESP8266 as follows:

| ESP8266 Pin | OLED Pin |
|-------------|----------|
| D2 (GPIO 4) | SDA |
| D1 (GPIO 5) | SCL |
| 3.3V | VCC |
| GND | GND |

---

## Installation

This library is already included in the `libraries/` folder of the TempBot project. **No separate installation is required.**

If you are setting up a new environment and installing dependencies manually via Arduino IDE:

1. Open **Sketch → Include Library → Manage Libraries...**
2. Search for **Adafruit BusIO** and install it.
3. Also install: **Adafruit SSD1306**, **Adafruit GFX Library**, **DHT sensor library**, and **WiFiManager**.

---

## Library Dependencies in TempBot

```
Adafruit SSD1306
  └── Adafruit GFX Library
        └── Adafruit BusIO  ← this library
```

---

## License

MIT License — Adafruit Industries.  
All text above must be included in any redistribution.

Adafruit invests time and resources providing open source code.  
Please support Adafruit and open-source hardware by purchasing products from [Adafruit](https://www.adafruit.com)!
