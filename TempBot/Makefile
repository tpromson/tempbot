FQBN    = esp8266:esp8266:nodemcuv2
SKETCH  = TempBot
PORT   ?= /dev/cu.wchusbserial1110
VERSION := $(shell grep FIRMWARE_VERSION version.h | cut -d '"' -f 2)

DHT22_OUT  = ../releases/latest/DHT22
DS18B20_OUT= ../releases/latest/DS18B20

.PHONY: all dht22 ds18b20 flash_dht22 flash_ds18b20 verify_release

all: dht22 ds18b20

dht22:
	@test -n "$(VERSION)"
	arduino-cli compile --fqbn $(FQBN) --libraries ../libraries \
		--build-property "build.extra_flags=-DSENSOR_DHT22" \
		--output-dir $(DHT22_OUT) .
	cp $(DHT22_OUT)/$(SKETCH).ino.bin $(DHT22_OUT)/firmware.bin
	@printf '%s\n' "$(VERSION)" > $(DHT22_OUT)/version.txt

ds18b20:
	@test -n "$(VERSION)"
	arduino-cli compile --fqbn $(FQBN) --libraries ../libraries \
		--build-property "build.extra_flags=-DSENSOR_DS18B20" \
		--output-dir $(DS18B20_OUT) .
	cp $(DS18B20_OUT)/$(SKETCH).ino.bin $(DS18B20_OUT)/firmware.bin
	@printf '%s\n' "$(VERSION)" > $(DS18B20_OUT)/version.txt

verify_release:
	@test -n "$(VERSION)"
	@test "`cat $(DHT22_OUT)/version.txt`" = "$(VERSION)"
	@test "`cat $(DS18B20_OUT)/version.txt`" = "$(VERSION)"
	@strings $(DHT22_OUT)/firmware.bin | grep -Fx "$(VERSION)"
	@strings $(DS18B20_OUT)/firmware.bin | grep -Fx "$(VERSION)"

flash_dht22: dht22
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) \
		--input-dir $(DHT22_OUT)

flash_ds18b20: ds18b20
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) \
		--input-dir $(DS18B20_OUT)
