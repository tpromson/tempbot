FQBN    = esp8266:esp8266:nodemcuv2
SKETCH  = TempBot
PORT   ?= /dev/cu.wchusbserial1110

DHT22_OUT  = ../releases/latest/DHT22
DS18B20_OUT= ../releases/latest/DS18B20
VER_FILE   = ../releases/latest/$(SENSOR)/version.txt

.PHONY: all dht22 ds18b20 flash_dht22 flash_ds18b20 version

all: dht22 ds18b20

dht22:
	arduino-cli compile --fqbn $(FQBN) \
		--build-property "build.extra_flags=-DSENSOR_DHT22" \
		--output-dir $(DHT22_OUT) .
	cp $(DHT22_OUT)/$(SKETCH).ino.bin $(DHT22_OUT)/firmware.bin

ds18b20:
	arduino-cli compile --fqbn $(FQBN) \
		--build-property "build.extra_flags=-DSENSOR_DS18B20" \
		--output-dir $(DS18B20_OUT) .
	cp $(DS18B20_OUT)/$(SKETCH).ino.bin $(DS18B20_OUT)/firmware.bin

flash_dht22: dht22
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) \
		--input-dir $(DHT22_OUT)

flash_ds18b20: ds18b20
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) \
		--input-dir $(DS18B20_OUT)
