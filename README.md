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
- WiFi SSID: `ESP8266_9CH_Smart_Switch`
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
IN8 _____ RX
IN9 _____ TX
GND _____ GND
```

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image2.png">

<details><summary>

# Full Features
</summary>

```
🏗️ Architecture

Component Details
Microcontroller ESP8266 (ESP-12E/NodeMCU)
Storage LittleFS Filesystem
Web Server HTTP on port 80
Time Sync NTP + Browser Time Sync
Networking WiFi + Access Point mode
JSON Library ArduinoJson

---

⚙️ Core Features

1. Relay Control System

Feature Specification
Channels Up to 16 relays (default: 9)
Default GPIO Pins D0(16), D1(5), D2(4), D3(0), D5(14), D6(12), D7(13), RX(3), TX(1)
Logic Levels Configurable Active HIGH/LOW per relay
Manual Control ON/OFF/Auto modes via web interface
State Persistence Saves relay configurations to LittleFS

2. Advanced Scheduling Engine

Each relay supports 8 independent schedules with the following parameters:

Schedule Parameters

· ⏰ Start Time (24-hour format: HH:MM:SS)
· ⏰ Stop Time (24-hour format: HH:MM:SS)
· 📅 Days of Week (Individual day selection: Sun-Sat)
· 📆 Days of Month (Individual day selection: 1-31)
· ✅ Enable/Disable per schedule

Schedule Types

Type Behavior Example
Normal Start < Stop 08:00:00 to 17:00:00
Overnight Start > Stop 22:00:00 to 06:00:00
Always ON Start = Stop Activates during valid days

Day Filtering Logic

· Day of Week: 7-bit mask (bit 0=Sun through bit 6=Sat)
· Day of Month: 31-bit mask (bit 0=Day 1 through bit 30=Day 31)
· Combined AND logic for precise scheduling
· 0x7F = All days of week selected
· 0xFFFFFFFF = All days of month selected

---

🌐 Web Interface

Pages Structure

/
├── /                 - Relay Controls & Schedules
├── /wifi             - WiFi Station Configuration
├── /ntp              - Time & NTP Settings
├── /ap               - Access Point Settings
├── /pins             - GPIO Pin Configuration
└── /system           - System Information & Controls

CSS Styling

· Responsive design (mobile-first)
· Modern card-based UI with shadows
· Color-coded status indicators
· Toast notifications for user feedback
· Smooth transitions and hover effects
· 24-hour time input fields

JavaScript Features

· Real-time clock display (updates every second)
· WiFi signal strength visualization bars
· Network scanning with async results
· AJAX-based API calls (no page reloads)
· Inline editing for relay names
· Interactive schedule configuration
· Confirmation dialogs for destructive actions

---

🔌 API Endpoints

Relay Management

Endpoint Method Description
/api/relays GET Get all relay states and schedules
/api/relay/manual POST Set manual ON/OFF control
/api/relay/reset POST Reset to automatic schedule mode
/api/relay/save POST Save schedule configuration
/api/relay/name POST Update relay display name

Time Management

Endpoint Method Description
/api/time GET Get current time & sync status
/api/time/browser-sync POST Sync time from browser

WiFi Configuration

Endpoint Method Description
/api/wifi GET Get WiFi configuration
/api/wifi POST Save & connect to WiFi
/api/wifi/scan POST/GET Start scan / Get results

NTP Settings

Endpoint Method Description
/api/ntp GET Get NTP configuration
/api/ntp POST Save NTP settings
/api/ntp/sync POST Force immediate sync

AP Settings

Endpoint Method Description
/api/ap GET Get AP configuration
/api/ap POST Save AP settings & restart

Pin Management

Endpoint Method Description
/api/pins GET Get pin configuration
/api/pins POST Save pins & restart

System Controls

Endpoint Method Description
/api/system GET Get system information
/api/system POST Update hostname
/api/reset POST Restart device
/api/factory-reset POST Erase all settings

---

🕐 Time Synchronization

Dual Sync Methods

1. NTP (Network Time Protocol)

· Primary Server: Configurable (default: ph.pool.ntp.org)
· Fallback Servers:
  1. pool.ntp.org
  2. time.nist.gov
  3. time.google.com
· Fallback Trigger: 3 consecutive NTP failures
· Sync Interval: Configurable 1-24 hours

2. Browser Time Sync

· One-click sync from web browser
· Uses Date.now()/1000 for accuracy
· No network connectivity required
· Ideal for air-gapped deployments

RTC Drift Compensation

· Drift Tracking: Measures difference between NTP updates
· Correction Algorithm: Weighted average (0.75 old + 0.25 new)
· Safety Bounds: 0.95 to 1.05 (5% accuracy window)
· Persistence: Saves epoch + drift to config file

Time Zone Handling

· GMT Offset: Configurable in seconds
· Daylight Saving: Additional offset support
· Storage: Raw UTC internally, local time for display
· Default: UTC+8 (28800 seconds)

---

📡 WiFi Features

Station Mode (STA)

· Automatic Reconnection: With rate limiting
· Reconnection Limits:
  · 10 attempts per cycle
  · 20 attempts per hour
  · 1-hour cooldown after max attempts
· Connection Timeout: 15 seconds
· Health Check: Every 5 seconds

Access Point Mode (AP)

· Fallback Network: Always available
· Default SSID: ESP8266_9CH_Timer_Switch
· Default Password: ESP8266-admin
· Configurable:
  · SSID (up to 31 characters)
  · Password (8+ characters or open)
  · Channel (1-13, default: 6)
  · Visibility (Broadcast or Hidden)

WiFi Scan

· Asynchronous scanning
· Results show SSID, RSSI, encryption
· Signal strength visualization
· Click to auto-fill connection form

---

💾 Storage System

LittleFS Configuration Files

File Size Magic Number Content
/system.cfg ~256 bytes 0x1234 System + WiFi config
/ext.cfg ~32 bytes 0xEC Extended settings
/relays.cfg ~2KB N/A All relay configs (16×128)
/pins.cfg ~32 bytes 0x50 GPIO pin assignments

Data Integrity

· Atomic Writes: Write to .tmp → rename
· Magic Numbers: Validation on load
· Version Tracking: EEPROM_VERSION = 7
· Auto-format: Creates fresh FS on corruption
· Dirty Flag: Optimizes write frequency

Configuration Structure

    magic: uint16_t       // 0x1234
    version: uint8_t      // EEPROM version
    sta_ssid[32]          // WiFi SSID
    sta_password[64]      // WiFi Password
    ap_ssid[32]           // AP SSID
    ap_password[32]       // AP Password
    ntp_server[48]        // NTP Server URL
    gmt_offset: long      // Timezone offset (seconds)
    daylight_offset: int  // DST offset (seconds)
    last_rtc_epoch: time_t// Last known time
    rtc_drift: float      // Clock drift factor
    hostname[32]          // mDNS hostname

---

🔔 Status LED

Pattern Meaning
Fast Blink (200ms) No WiFi connection
Slow Blink (1s) WiFi connected, no time sync
Solid ON/OFF Normal operation (time synced)

· Pin: D4 (GPIO2)
· Logic: Active LOW (default)
· Update Rate: Real-time based on status

---

🛡️ Production Features

Memory Management

· Minimum heap for save: 5KB (prevents corruption)
· Minimum heap for NTP: 8KB
· Deferred saves on low memory
· Yield() calls in long operations

Watchdog Protection

· Regular yield() in loops
· Break operations into chunks
· Max 3 relays processed per yield in schedule engine

Error Recovery

· WiFi reconnect state machine
· NTP fallback chain
· LittleFS auto-format on failure
· Configuration defaults on corruption
· Boot loop protection via LED

Connection Rate Limiting

· Hourly cap: 20 reconnection attempts
· Cooldown: 1 hour after exceeding limit
· Counter reset: Every hour

---

🔒 Security Features

Authentication

· AP Password protection
· Hidden SSID option
· Captive portal detection bypass

Network

· mDNS support (hostname.local)
· Captive portal handling
· Microsoft NCSI compatibility
· Android/iOS hotspot detection

---

📊 System Information Dashboard

Displayed Metrics

· STA IP Address
· AP IP Address
· Free Heap Memory (KB)
· Uptime (hours:minutes:seconds)
· mDNS Hostname (.local address)
· WiFi RSSI (dBm with quality rating)
· NTP Last Sync Time
· NTP Server Address

Quality Indicators

RSSI Range Signal Quality
≥ -50 dBm Excellent
-50 to -60 dBm Good
-60 to -70 dBm Fair
< -70 dBm Weak

---

🔧 Maintenance Features

Device Control

· Soft Restart: Via API (saves state first)
· Factory Reset: Erases all configuration files
· Configuration Backup: All files in LittleFS

Debug Support

· Serial output for schedule processing
· Comprehensive logging
· State tracking for WiFi, NTP, RTC

---

📱 Responsive Design

Breakpoints

· Desktop: 1200px max width, card grid
· Tablet: Flexible grid (min 340px cards)
· Mobile (<500px): Single column layout

Mobile Optimizations

· Smaller day/month buttons
· Reduced font sizes
· Stacked input fields
· Touch-friendly targets (min 44px)

---

🎯 Use Cases

Home Automation

· Lighting schedules (sunset/sunrise patterns)
· Garden irrigation timers
· Pool pump control
· Holiday lighting automation

Industrial Applications

· Equipment runtime scheduling
· Shift-based machine control
· Energy management systems
· Process automation timers

Agricultural

· Greenhouse lighting cycles
· Hydroponic pump schedules
· Livestock feeding timers
· Climate control systems

---

🔄 Power Management

Relay State on Boot

· All relays initialized to OFF state
· Pin configured as OUTPUT
· Active LOW: HIGH = OFF (most relay modules)

Idle Power Consumption

· WiFi connected: ~70-80mA
· AP + STA mode: ~80-100mA
· Deep sleep compatible (future enhancement)

---

📈 Performance

Metric Value
Web Server Response <50ms (local)
Schedule Processing <100ms (9 relays × 8 schedules)
Config Save Time <200ms (atomic write)
NTP Sync Time 1-5 seconds
WiFi Scan Time 2-3 seconds

---

🆘 Troubleshooting

Common Issues & Solutions

Problem Solution
Schedules not triggering Check Time Zone GMT offset
 Verify WiFi connection (NTP sync needed)
 Use Browser Time Sync if no internet
Relay stays ON/OFF Check Manual Override status
 Verify Active LOW/HIGH setting
Web interface slow Check free heap memory
 Reduce max relays if needed
WiFi disconnects Check signal strength (RSSI)
 Reduce number of reconnect attempts

---

🔄 Version History

Version Changes
v7 24-hour time format, month day scheduling
v6 Dynamic GPIO pin management
v5 Browser time sync, RTC drift compensation
v4 Multiple NTP servers with fallback
v3 Non-blocking WiFi reconnect
v2 LittleFS migration from EEPROM
v1 Initial release

---

📝 License & Credits

· Author: github.com/xiv3r
· Libraries: ESP8266WiFi, ArduinoJson, NTPClient
· Version: Production-Ready v7
· Target Platform: ESP8266 (ESP-12E/F, NodeMCU, Wemos D1)

---

🔮 Future Enhancements (Roadmap)

· MQTT integration for Home Assistant
· WebSocket real-time updates
· OTA (Over-The-Air) firmware updates
· Energy monitoring (current sensors)
· Telegram Bot notifications
· IFTTT webhook support
· CSV schedule import/export
· Multi-language web interface
· Temperature/humidity sensor support
· Android/iOS app development

---

```

</details>
