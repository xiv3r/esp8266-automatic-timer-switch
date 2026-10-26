# Requirements 
- ESP8266 v1 12E 160MHZ NodeMCU / (wemos d1 mini)
- 5v 1-6 Channel Relay
- Dupont Wire
- DS3231 RTC Module
- 5v 2-3a Power Supply

# Arduino Libraries
- ArduinoJson
- NTPClient
- [RTCLib](https://codeload.github.com/adafruit/RTClib/zip/refs/tags/1.14.1) 1.14.1

# Installation
- Download the firmware and flasher

- Flash offset address 
```
firmware: 0x0
```

# WiFi Key
- WiFi SSID: `ESP8266_6CH_Timer_Switch`
- Password: `ESP8266-admin`

# Activation
- Go to `192.168.4.1 -> wifi` and connect to your home wifi after the NTP is synchronized everything will work

# Relay Name
- Double click relay name to edit

# Access
° Direct Access
- mDNS:`esp8266-relay.local`
- Captive Portal: Auto redirect
- Gateway:`192.168.4.1`
- WAN:`192.168.1.123`
  
° Global:`Enable esp8266 Port Forwarding on your router to access anywhere`

# GPIO Connection 
```
9CH  |  ESP8266
VCC _____ 5VIN 
IN1 _____ D0
IN2 _____ D5
IN3 _____ D6
IN4 _____ D7
IN5 _____ RX
IN6 _____ TX
GND _____ GND
```
# DS3231 RTC Module 
```
RTC | ESP8266
SCL → D1
SDA → D2
VCC → 3.3V
GND → GND
```
