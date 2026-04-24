# Requirements 
- ESP8266 12E 160MHZ
- 1-11 Channel Relay
- 11 Dupont Wire
- 1 10K ohms resistor
- Home Wifi for NTP/RTC sync
- 5v 3a Power Supply

`Optional`
- 5vDC Battery (Maintain Power and Timer)

# Arduino Libraries
- ArduinoJson
- NTPClient

# Installation
- Download the [flasher](https://github.com/xiv3r/esp8266-automatic-timer-switch/releases/tag/flasher) and [firmware.bin](https://github.com/xiv3r/esp8266-automatic-timer-switch/raw/refs/heads/main/esp8266-11ch-firmware-0x0.bin) and flash using `0x0` offset

# WiFi Key
- WiFi SSID: `ESP8266_11CH_Smart_Switch`
- Password: `ESP8266-admin`

# Activation
- Go to `wifi settings` and connect to your home wifi after the NTP is synchronized everything will work

# Relay Name
- Double click relay name to edi

# Access
° Direct Access
- mDNS:`esp32-16ch-timer.local`
- Captive Portal: Auto redirect
- Gateway:`192.168.4.1`
- WAN:`192.168.1.123`
  
° Global:`Enable esp8266 Port Forwarding for on your router to access anywhere`

# GPIO Connection 
Note: `For 11 channel relay just connect the 10K ohms resistor from D8 to GND`
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
-- Extra Relay
IN1 _____ D8 GPIO
IN2 _____ RX GPIO3
IN3 _____ TX GPIO1
GND _____ GND
```
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/esp8266-img.png">

<details><summary>

# Full Features

</summary>

## 🌐 Connectivity
```
WiFi Station (STA) Connects to home WiFi network
WiFi Access Point (AP) Built-in AP for initial setup (ESP8266_11CH_Timer_Switch)
mDNS Access via hostname.local (configurable)
Captive Portal Auto-redirects to setup page when connecting to AP
DNS Server Built-in DNS for captive portal functionality
Async WiFi Scan Non-blocking network scanning from web UI
Auto-Reconnect Retries up to 10 times with 5-minute cooldown
```

## ⏰ Time & Scheduling
```
NTP Time Sync Multiple fallback servers (ph.pool.ntp.org → pool.ntp.org → time.nist.gov → time.google.com)
RTC Drift Compensation Software RTC with automatic drift adjustment
EEPROM Time Persistence Saves epoch across reboots
Timezone Support Configurable GMT offset + DST offset
Auto-Sync Interval Configurable 1–24 hours
Manual Sync Force NTP sync from web UI
```

## 🔌 Relay Control (11 Channels)
```
Manual Override ON/OFF/Auto modes per relay
Custom Relay Names 15-character max, double-click to edit
Active-Low Support Configurable relay logic
Pin Mapping D0–D8 + GPIO3 (RX) + GPIO1 (TX)
```

## 📅 Schedule Engine (8 schedules per relay)
```
Per-Schedule Enable/Disable Individual schedule toggles
Start/Stop Times Hour, minute, second granularity
Same-Day Windows Normal start < stop scheduling
Overnight Windows Start > stop wraps across midnight
Always-ON Detection Start == stop interpreted as 24/7 ON
Overnight Badge UI indicator for wrap-around schedules
Visual Indicators "🌙 Overnight" / "● Always ON" badges
```

## 🖥️ Web Interface
```
Relays (/) Relay grid, manual control, schedule editing, name editing
WiFi (/wifi) STA settings, network scan with RSSI bars, connection status
Time (/ntp) NTP server, GMT/DST offsets, sync interval, manual sync
AP (/ap) AP SSID/password, channel (1–13), hidden SSID toggle
System (/system) Info dashboard, hostname config, restart, factory reset

- UI Features

· Live clock display with WiFi/NTP status dots
· RSSI signal strength bars (1–4 bars)
· Toast notifications for actions
· Mobile-responsive CSS grid layout
· Auto-refresh every 60 seconds
```

## 🔧 Configuration Management
```
EEPROM Storage 2048 bytes with magic number validation
Version Migration Auto-upgrades from v1 → v2 → v3 → v4
ExtConfig Partition Extended settings at offset 1024
Factory Reset Erases all EEPROM, reverts to defaults
Safe Relay Default All relays OFF on boot
```

## 🛡️ Security & Stability
```
Non-Blocking Loop No delay() calls; state machines for WiFi/NTP
WiFi Backoff 5-minute cooldown after 10 failed attempts
Watchdog Safe Proper yield behavior
JSON Parsing Memory-efficient manual parsing + StaticJsonDocument
```

## 📡 API Endpoints
```
/api/relays GET Get all relay states and schedules
/api/relay/manual POST Set manual override (ON/OFF)
/api/relay/reset POST Return to auto/schedule mode
/api/relay/save POST Save schedules for a relay
/api/relay/name POST Update relay custom name
/api/time GET Current time, WiFi/NTP status
/api/wifi GET/POST Get/save STA settings
/api/wifi/scan POST/GET Start async scan / get results
/api/ntp GET/POST Get/save NTP settings
/api/ntp/sync POST Force NTP sync
/api/ap GET/POST Get/save AP settings
/api/system GET/POST System info / save hostname
/api/reset POST Soft restart
/api/factory-reset POST Full factory reset
```

## 🔄 Captive Portal Detection
```
OS/Platform Probe Path
iOS / macOS /hotspot-detect.html, /library/test/success.html
Android / Chrome /generate_204
Firefox /success.txt, /canonical.html
Windows (NCSI) /connecttest.txt, /ncsi.txt, /redirect
```

## 📊 System Information Dashboard
```
· STA IP address
· AP IP address
· Free heap memory (KB)
· Uptime (hours/minutes/seconds)
· mDNS hostname
· WiFi RSSI with quality descriptor (Excellent/Good/Fair/Weak)
· NTP last sync time
· Current NTP server
```

## 🧠 Advanced Features
```
RTC Drift Learning Adjusts internal clock based on NTP vs. millis() drift (clamped 0.90–1.10)
EEPROM Wear Reduction Only writes on config changes
Schedule Engine Optimization Early exit when ON condition found
WiFi State Machine Non-blocking connection attempts
Hidden AP Support Toggle SSID broadcast on/off
```

## 📦 Default Settings
```
Setting Default Value
AP SSID ESP8266_11CH_Timer_Switch
AP Password ESP8266-admin
AP Channel 6
NTP Server ph.pool.ntp.org
GMT Offset 28800 (UTC+8)
DST Offset 0
Sync Interval 1 hour
Hostname esp8266relay
Relay Names "Relay 1" through "Relay 11"
```

## 🔌 Pinout Reference
```
Relay GPIO NodeMCU Label
1 16 D0
2 5 D1
3 4 D2
4 0 D3
5 2 D4
6 14 D5
7 12 D6
8 13 D7
9 15 D8
10 3 RX
11 1 TX
```
</details>
