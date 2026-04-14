# Requirements 
- ESP8266 12E 160MHZ
- 8-Channel Relay
- 10 Dupont Wire

# Arduino Libraries
- ArduinoJson
- NTPClient

# Installation
- Flash the firmware.bin using `0x0` offset

# Schematics
```
RELAY     ESP8266
VCC _____ 5VIN 
IN1 _____ D0 GPIO
IN2 _____ D1 GPIO
IN3 _____ D2 GPIO
IN4 _____ D3 GPIO
IN5 _____ D4 GPIO
IN6 _____ D5 GPIO
IN7 _____ D6 GPIO
IN8 _____ D7 GPIO
GND _____ GND
```
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/img/esp8266_dashboard.jpg">
