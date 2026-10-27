# Requirements 
- ESP8266 v1 12E 160MHZ NodeMCU / (wemos d1 mini)
- 5v 1-6 Channel Relay
- 8 Female to Female Dupont Wire
- DS3231 RTC Module
- CR2032 Battery
- 5v 1-3a Power Supply

# Arduino Libraries
- ArduinoJson
- NTPClient
- [RTCLib](https://codeload.github.com/adafruit/RTClib/zip/refs/tags/1.14.1) 1.14.1

## Setups
> Download and install 
### ESP8266 Win/Linux Drivers
- CH340G: https://sparks.gogo.co.nz/ch340.html
- CP2102: https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads
- FT232: https://ftdichip.com/wp-content/uploads/2025/03/CDM-v2.12.36.20-Universal-Driver-for-x64-WHQL-Certified.zip
## Flasher
### Android (otg)
- https://play.google.com/store/apps/details?id=io.serialflow.espflash
### Android Termux
- https://github.com/7wp81x/Termux-ESP-Flasher
### Windows
- https://dl.espressif.com/public/flash_download_tool.zip
### Linux
```sh
esptool --port <PORT> write_flash 0x0 esp32-dump-0x0.bin
```
### Win/Linux Browser
- https://g3gg0.github.io/esp32_flasher/flasher.html

# Installation
- Download the firmware and flash
```
firmware: 0x0
```

# WiFi Key
- WiFi SSID: `ESP8266_6CH_Timer_Switch`
- Password: `12345678`

# Activation
> online
- Go to `192.168.4.1 -> wifi` and connect to your home wifi after the NTP synchronized rtc time everything will work
> offline
- Go to `192.168.4.1 -> time` tap sync browser to set the rtc time

# Relay Name
- Double tap relay name to edit
  
# Access
° Direct Access
- mDNS:`esp8266-relay.local`
- Captive Portal: Auto redirect
- Gateway:`192.168.4.1`
- WAN:`192.168.1.123`
- Global:`Enable esp8266 Port Forwarding on your router to access anywhere`

# Reset
- Hold board boot button for 5 seconds to factory reset

# Restart
- Tap board RST button to restart

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

- Wemos D1 Mini
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/wemos-diagram.png">

- ESP8266
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/esp8266-diagram.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic2.jpg">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/esp8266.jpg">
