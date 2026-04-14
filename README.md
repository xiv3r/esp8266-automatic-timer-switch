# Requirements 
- ESP8266 12E 160MHZ
- 8-Channel Relay
- 10 Dupont Wire
- Wifi for NTP/RTC sync
- 5vDC Battery (Maintain Power and Timer)(optional)

# Arduino Libraries
- ArduinoJson
- NTPClient

# Features 
- Captive Portal
- Custom wifi settings 
- Manual and Automatic Switch
- Control Doesn't works if time is not synchronized
- Lightweight and More Responsive Web User Interface 
- ESP8266 NTP/RTC Auto synchronization
- Wifi Client mode for NTP/RTC time synchronization
- Support 1--8 Channel Relay
- Each Relay have 4 start and stop schedule a total of 8 schedules
- Persistent data (Survive Power loss)
- Anti-Reset Protection
- Works Offline after NTP is Synchronized 

# Suitable
- Home Automation
- Farms
- Livestocks 
- Buildings
- Water pumping station
- Greenhouses
- Automatic Garden Sprinkler System 
- So much more...

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
