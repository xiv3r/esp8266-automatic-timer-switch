# Requirements 
- ESP8266 v.1 12E 160MHZ
- 5v 1-9 Channel Relay
- Dupont Wire
- Home Wifi for NTP/RTC sync
- 5v 3a Power Supply

`Optional`
- 5vDC Battery (Maintain Power and Timer)

# Arduino Libraries
- ArduinoJson
- NTPClient

# Installation
- Download the [flasher](https://github.com/xiv3r/esp8266-automatic-timer-switch/releases/tag/flasher) and [firmware.bin](https://github.com/xiv3r/esp8266-automatic-timer-switch/raw/refs/heads/main/esp8266-firmware-0x0.bin) and flash using `0x0` offset
```
firmware: 0x0
```

# WiFi Key
- WiFi SSID: `ESP8266_Smart_Switch`
- Password: `ESP8266-admin`

# Activation
- Go to `wifi settings` and connect to your home wifi after the NTP is synchronized everything will work

# Relay Name
- Double click relay name to edit

# Access
° Direct Access
- mDNS:`esp8266relay.local`
- Captive Portal: Auto redirect
- Gateway:`192.168.4.1`
- WAN:`192.168.1.123`
  
° Global:`Enable esp8266 Port Forwarding on your router to access anywhere`

# GPIO Connection 
```
RELAY     ESP8266
VCC _____ 5VIN 
IN1 _____ D0
IN2 _____ D1
IN3 _____ D2
IN4 _____ D3
IN5 _____ D5
IN6 _____ D6
IN7 _____ D7
IN8 _____ D9
IN9 _____ RX
IN10 ____ TX
GND _____ GND
```

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image2.png">

<details><summary>

# Full Features

</summary>


</details>
