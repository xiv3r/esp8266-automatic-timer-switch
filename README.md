# Requirements 
- ESP8266 12E 160MHZ
- 8-Channel Relay
- 10 Dupont Wire
- Home Wifi for NTP/RTC sync
- 5vDC Battery (Maintain Power and Timer)(optional)

# Arduino Libraries
- ArduinoJson
- NTPClient

# Features 
- Captive Portal
- Custom wifi settings
- WAN gateway access
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

# Note
- In case the ntp server is inactive to maintain the relay switch time precision control add a 5vdc battery for esp8266 and separate the 5vdc relay power supply adaptor.

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
- Download the [flasher](https://github.com/xiv3r/esp8266-automatic-timer-switch/releases/tag/flasher) and [firmware.bin](https://github.com/xiv3r/esp8266-automatic-timer-switch/raw/refs/heads/main/esp8266-8ch-timer-switch-firmware-0x0.bin) and flash using `0x0` offset

# WiFi Key
- WiFi SSID: `ESP8266_8CH_Smart_Switch`
- Password: `ESP8266-admin`

# Activation
- Go to `wifi settings` and connect to your home wifi after the NTP is synchronized everything will works.

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
