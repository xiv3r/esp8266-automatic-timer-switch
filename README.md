# Requirements 
- ESP8266 v.1 12E 160MHZ
- 5v 1-9 Channel Relay
- Dupont Wire
- Home Wifi for NTP/RTC sync
- 5v 3-5a Power Supply

`Optional`
- 5v UPS (Maintain Power and Timer)

# Arduino Libraries
- ArduinoJson
- NTPClient
- RTClib 1.14.1

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

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic-1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic-2.png">

<details><summary>

# Full Features
</summary>

```
---

🔌 ESP8266 9-CHANNEL RELAY SMART SWITCH — COMPLETE FEATURE SPECIFICATION

---

📊 SYSTEM ARCHITECTURE

Core Components

Component Details
MCU ESP8266 (ESP-12E/F or NodeMCU)
Maximum Relays 16 (default 9)
Storage LittleFS (SPIFFS replacement)
Web Server ESP8266WebServer on port 80
DNS Server DNSServer on port 53 (captive portal)
NTP Client WiFiUDP-based, configurable server pool
mDNS ESP8266mDNS for .local name resolution
JSON Parsing ArduinoJson library (StaticJsonDocument + DynamicJsonDocument)
Time Library NTPClient with custom drift compensation

Memory Management

· Minimum heap for saves: 5,120 bytes (5 KB)
· Minimum heap for NTP: 8,192 bytes (8 KB)
· Deferred saves when memory is low
· Atomic file operations with .tmp file strategy
· Dynamic JSON allocation for API responses

---

🔌 RELAY MANAGEMENT SYSTEM

Relay Configuration

MAX_RELAYS: 16 (compile-time constant)
Default relays: 9
Available GPIO pins: D0(16), D1(5), D2(4), D3(0), D5(14), D6(12), D7(13), RX(3), TX(1)
Reserved pins: D4(GPIO2/LED), D8(GPIO15/boot)

Per-Relay Data Structure

Each relay stores:

· Name: 16-character string (customizable)
· GPIO Pin: Assigned ESP8266 GPIO number
· Active Logic: HIGH (relay ON when GPIO HIGH) or LOW (relay ON when GPIO LOW)
· Manual Override Flag: Boolean indicating manual control
· Manual State: Desired state when in manual mode
· 8 Timer Schedules: Each containing:
  · Start time (hour, minute, second)
  · Stop time (hour, minute, second)
  · Enabled flag
  · Day-of-week bitmask (7 bits)
  · Day-of-month bitmask (31 bits)

Relay States

State Description Web UI Badge
OFF Relay de-energized Red "OFF" badge
ON Relay energized Green "ON" badge
MANUAL Override active Orange "MANUAL" badge

Control Methods

1. Web Interface: ON/OFF/AUTO buttons
2. REST API: JSON POST endpoints
3. Automatic Schedule: Time-based activation
4. Manual Override: Temporary or permanent override

---

⏱️ SCHEDULING ENGINE

Schedule Structure (8 per relay)

Schedule[0..7] {
    startHour[0-23], startMinute[0-59], startSecond[0-59]
    stopHour[0-23], stopMinute[0-59], stopSecond[0-59]
    enabled: boolean
    days: 7-bit bitmask (Sun=bit0, Mon=bit1, ... Sat=bit6)
    monthDays: 31-bit bitmask (Day1=bit0, Day2=bit1, ... Day31=bit30)
}

Schedule Types

Type Condition Behavior UI Indicator
Always ON start == stop Relay permanently ON on selected days Green "Always ON" badge
Day Schedule start < stop ON from start to stop Normal time range
Overnight Schedule start > stop ON from start to midnight, then midnight to stop Purple moon badge "Overnight"
Disabled enabled = false Schedule ignored No badge

Day-of-Week Selection

Sunday    (bit 0) -> 0x01
Monday    (bit 1) -> 0x02
Tuesday   (bit 2) -> 0x04
Wednesday (bit 3) -> 0x08
Thursday  (bit 4) -> 0x10
Friday    (bit 5) -> 0x20
Saturday  (bit 6) -> 0x40
Everyday  -> 0x7F (all bits set)

Day-of-Month Selection

Individual days 1-31 via bitmask
0x00000000: Ignored (schedule applies regardless of month day)
0xFFFFFFFF: All month days
Example: Days 1,15,30 = bits 0,14,29 set

Schedule Processing Algorithm


1. Get current epoch time (with drift compensation)
2. Apply GMT offset + daylight offset -> local time
3. Extract hour, minute, second, weekday, month day
4. For each relay:
   a. If manual override active -> use manual state
   b. Else check all 8 schedules:
      - Skip if disabled
      - Skip if day-of-week doesn't match
      - Skip if month-day doesn't match (when monthDays != 0)
      - Check if current time falls within ON period
      - Break on first matching schedule
5. Set relay GPIO accordingly

---

🌐 WIFI CONNECTIVITY

WiFi Modes

Mode Description Use Case
AP Only Access Point mode Initial setup, no external network
STA Only Station mode Connected to home/office WiFi
AP+STA Dual mode (default) Both AP and station simultaneously

Station (STA) Configuration

· SSID: Up to 32 characters
· Password: Up to 64 characters (WPA/WPA2)
· Connection timeout: 15 seconds
· Max reconnect attempts: 10 before cooldown
· Reconnect cooldown: 5 minutes (300 seconds)
· Hourly rate limit: 20 connection attempts/hour
· 1-hour cooldown if hourly limit exceeded
· Status check interval: Every 5 seconds
· Auto-reconnect: Enabled when disconnected

Access Point (AP) Configuration

· SSID: Up to 32 characters (default: "ESP8266_9CH_Timer_Switch")
· Password: Up to 32 characters (default: "ESP8266-admin")
· Channel: 1-13 (default: 6)
· Hidden SSID: Yes/No option
· AP IP: 192.168.4.1
· DNS Server: Redirects all queries to AP IP (captive portal)

WiFi Scanning

· Trigger: Via web UI "Scan" button
· Async scanning: Non-blocking
· Results: Up to 20 networks displayed
· Information per network:
  · SSID name
  · Signal strength (RSSI in dBm)
  · Encryption type (open or secured)
  · Signal strength bars (1-4)
· UI Interaction: Click network to auto-fill SSID

Signal Strength Classification

RSSI Range Bars Rating
≥ -50 dBm ▂▄▆█ 4 bars Excellent
-60 to -50 ▂▄▆ 3 bars Good
-70 to -60 ▂▄ 2 bars Fair
< -70 dBm ▂ 1 bar Weak

mDNS (Multicast DNS)

· Hostname: Configurable (default: "esp8266relay")
· Service: HTTP on port 80
· Access: http://[hostname].local
· Auto-restart: On hostname change
· Auto-start: After WiFi connection

---

🕐 TIME SYNCHRONIZATION SYSTEM

NTP Configuration

· Primary Server: Configurable (default: "ph.pool.ntp.org")
· Fallback Chain:
  1. ph.pool.ntp.org (Philippines)
  2. pool.ntp.org (Global)
  3. time.nist.gov (NIST)
  4. time.google.com (Google)
· Fallback Trigger: After 3 consecutive failures
· Retry interval: 30 seconds on failure
· Sync interval: Configurable 1-24 hours (default: 1 hour)
· GMT Offset: Configurable in seconds (default: 28800 = UTC+8)
· Daylight Saving Offset: Additional seconds (default: 0)

Internal RTC (Software Clock)

· Drift compensation: Weighted exponential moving average
  new_drift = old_drift × 0.75 + measured_drift × 0.25
  drift bounds: [0.95, 1.05]

· Persistence: Saved to LittleFS on sync
· Recovery: Loaded on boot if epoch > 1,000,000,000
· Interpolation: current_time = last_sync_epoch + (elapsed_ms × drift / 1000)
· Millisecond rollover handling: Properly handles 49.7-day overflow

Browser Time Sync (Fallback)

· Function: POST /api/time/browser-sync
· Input: Browser's Date.now() / 1000 (Unix timestamp)
· Validation: Timestamp between 1,000,000,000 and 2,000,000,000
· Use case: When NTP servers unreachable but web access available
· Drift calculation: Same as NTP sync

Time Display

· Format: 24-hour HH:MM:SS
· Update frequency: Every 1 second via AJAX
· Time source priority:
  1. Drift-compensated internal RTC
  2. Browser time (manual sync)
  3. Default "--:--:--"

---

💾 PERSISTENT STORAGE (LittleFS)

File Structure

File Size Content Magic Number
/system.cfg ~168 bytes SystemConfig struct 0x1234
/ext.cfg ~32 bytes ExtConfig struct 0xEC
/relays.cfg ~6,400 bytes 16 × RelayConfig structs (in system.cfg)
/pins.cfg ~32 bytes PinConfig struct 0x50

SystemConfig Structure

struct SystemConfig {
    uint16_t magic;           // 0x1234
    uint8_t  version;         // 7 (current)
    char     sta_ssid[32];    // Station SSID
    char     sta_password[64];// Station password
    char     ap_ssid[32];     // AP SSID
    char     ap_password[32]; // AP password
    char     ntp_server[48];  // NTP server address
    long     gmt_offset;      // GMT offset in seconds
    int      daylight_offset; // DST offset in seconds
    time_t   last_rtc_epoch;  // Last known epoch time
    float    rtc_drift;       // Clock drift factor
    char     hostname[32];    // mDNS hostname
};

ExtConfig Structure

struct ExtConfig {
    uint8_t magic;            // 0xEC
    uint8_t ap_channel;       // 1-13
    uint8_t ntp_sync_hours;   // 1-24
    uint8_t ap_hidden;        // 0 or 1
    uint8_t reserved[28];     // Future expansion
};

PinConfig Structure

struct PinConfig {
    uint8_t  magic;           // 0x50
    uint8_t  numRelays;       // 1-16
    bool     globalActiveLow; // Default relay logic
    uint8_t  reserved[29];    // Future expansion
};

File Operations

· Atomic writes: Write to .tmp file, delete original, rename
· Size verification: ±32 bytes tolerance
· Format on failure: Auto-format LittleFS if mount fails
· Auto-save delay: 30 seconds after modification
· Low-memory deferral: Skips saves when free heap < 5KB
· Pending save retry: Attempted on next save cycle

Version Migration

· Current version: 7
· Migration: Auto-updates version field on load
· Backward compatibility: Resets to defaults if magic invalid

---

🖥️ WEB INTERFACE

Pages Overview

Page URL Purpose
Relays / Main control panel for all relays and schedules
WiFi /wifi Station network configuration and scanning
Time/NTP /ntp NTP server, timezone, sync settings
AP /ap Access Point SSID, password, channel, visibility
Pins /pins GPIO pin assignments and relay count
System /system Device info, hostname, restart, factory reset

Common UI Elements

· Header: Logo, navigation tabs, real-time clock, status dots
· Status Dots:
  · 🟢 Green: WiFi connected / Time synced
  · 🔴 Red: WiFi disconnected
  · 🟡 Yellow: Time not synced
· Toast Notifications:
  · Green: Success messages
  · Red: Error messages
  · Auto-dismiss after 3 seconds
· Responsive Design:
  · Desktop: Multi-column grid
  · Mobile (<500px): Single column, smaller controls

Relay Control Page

· Relay Cards: One card per relay
· Controls:
  · ON button (green)
  · OFF button (red)
  · AUTO button (gray)
  · Double-click name to rename
· Schedule List: 8 expandable schedule entries
  · Enable/disable checkbox
  · Start time selector (HH:MM:SS)
  · Stop time selector (HH:MM:SS)
  · Day-of-week selector (7 clickable chips)
  · Day-of-month selector (31 small clickable chips)
  · Overnight/Always-ON badges
· Save Button: Blue save button per relay

WiFi Configuration Page

· Status Alert:
  · Blue info bar when connected (shows IP, RSSI bars)
  · Yellow warning when disconnected
· SSID Input: Text field with Scan button
· Network List:
  · Shows after scan
  · Sorted by signal strength
  · Click to auto-fill SSID
  · Shows lock icon for secured networks
  · RSSI bars visualization

Time/NTP Configuration Page

· NTP Server: Text input
· GMT Offset: Number input (seconds)
· DST Offset: Number input (seconds)
· Sync Interval: 1-24 hours selector
· Buttons:
  · Save NTP Settings
  · Sync Now
  · Sync from Browser

AP Configuration Page

· Warning: Yellow alert about AP restart
· SSID: Text input (max 31 chars)
· Password: Password input (8+ chars or blank)
· Channel: Dropdown (1-13)
· Hidden SSID: Yes/No dropdown

Pins Configuration Page

· Global Logic: Active LOW/HIGH selector
· Relay Table:
  · Relay number
  · Name input
  · Pin selector dropdown (shows used/available)
  · Active logic per relay
  · Remove button (disabled for last relay)
· Available Pins:
  · Blue chips for available pins
  · Gray chips for used pins
  · Click to add relay
· Buttons: Add Relay, Save & Restart

System Page

· Info Dashboard (8 info boxes):
  · STA IP Address
  · AP IP Address
  · Free Heap Memory
  · Uptime (hours, minutes, seconds)
  · mDNS Hostname
  · WiFi RSSI
  · NTP Last Sync Time
  · NTP Server
· Hostname Configuration: Text input with save button
· Device Control:
  · Restart Device (orange button, confirmation dialog)
  · Factory Reset (red button, double confirmation)

---

📡 REST API SPECIFICATION

Relay Endpoints

GET /api/relays

Response: JSON array of all relays

[{
  "name": "Relay 1",
  "state": false,
  "manual": false,
  "schedules": [{
    "startHour": 8, "startMinute": 0, "startSecond": 0,
    "stopHour": 17, "stopMinute": 0, "stopSecond": 0,
    "enabled": true,
    "days": 127,
    "monthDays": 0
  }, ...]
}, ...]

POST /api/relay/manual

Request: {"relay": 0, "state": true}
Response: {"success": true}

POST /api/relay/reset

Request: {"relay": 0}
Response: {"success": true}
Effect: Clears manual override, returns to auto mode

POST /api/relay/save

Request: {"relay": 0, "schedules": [...]}
Response: {"success": true}
Effect: Saves all 8 schedules for specified relay

POST /api/relay/name

Request: {"relay": 0, "name": "Pump"}
Response: {"success": true}
Effect: Renames relay

Time Endpoints

GET /api/time

Response: {"time": "14:30:45", "wifi": true, "ntp": true}

POST /api/time/browser-sync

Request: {"epoch": 1700000000}
Response: {"success": true}

WiFi Endpoints

GET /api/wifi

Response:

{
  "ssid": "MyWiFi",
  "connected": true,
  "ip": "192.168.1.100",
  "rssi": -45
}

POST /api/wifi

Request: {"ssid": "MyWiFi", "password": "pass1234"}
Response: {"success": true}

POST /api/wifi/scan (Start scan)

Response: {"scanning": true} (HTTP 202)

GET /api/wifi/scan (Get results)

Response:

{
  "scanning": false,
  "networks": [
    {"ssid": "Network1", "rssi": -45, "enc": true},
    {"ssid": "Network2", "rssi": -80, "enc": false}
  ]
}

NTP Endpoints

GET /api/ntp

Response:

{
  "ntpServer": "pool.ntp.org",
  "gmtOffset": 28800,
  "daylightOffset": 0,
  "syncHours": 1
}

POST /api/ntp

Request: {"ntpServer": "...", "gmtOffset": 28800, "daylightOffset": 0, "syncHours": 1}
Response: {"success": true}

POST /api/ntp/sync

Response: {"success": true} or {"success": false}

AP Endpoints

GET /api/ap

Response:

{
  "ap_ssid": "ESP8266_Switch",
  "ap_password": "admin1234",
  "ap_channel": 6,
  "ap_hidden": false
}

POST /api/ap

Request: {"ap_ssid": "...", "ap_password": "...", "ap_channel": 6, "ap_hidden": false}
Response: {"success": true}
Side effect: Restarts AP with new settings

Pin Endpoints

GET /api/pins

Response:

{
  "numRelays": 9,
  "globalActiveLow": true,
  "relays": [
    {"name": "Relay 1", "pin": 16, "activeLow": true},
    ...
  ]
}

POST /api/pins

Request: Full pin configuration object
Response: {"success": true}
Side effect: Restarts device

System Endpoints

GET /api/system

Response:

{
  "hostname": "esp8266relay",
  "ip": "192.168.1.100",
  "ap_ip": "192.168.4.1",
  "uptime": 3600,
  "freeHeap": 45000,
  "ntpSynced": true,
  "ntpServer": "pool.ntp.org",
  "ntpSyncAge": 120,
  "wifiConnected": true,
  "wifiSSID": "MyWiFi",
  "rssi": -45,
  "mdnsHostname": "esp8266relay.local",
  "version": 7
}

POST /api/system

Request: {"hostname": "myrelay"}
Response: {"success": true}

POST /api/reset

Response: {"success": true}
Effect: Restarts device

POST /api/factory-reset

Response: {"success": true}
Effect: Deletes all config files and restarts

---

🎯 CAPTIVE PORTAL SUPPORT

Detection Endpoints

Endpoint Purpose Platform
/hotspot-detect.html Apple Captive Network Assistant iOS/macOS
/library/test/success.html Apple Alternative iOS/macOS
/generate_204 Android Captive Portal Detection Android
/success.txt Firefox Captive Detection Firefox
/canonical.html General Redirect Various
/connecttest.txt Microsoft NCSI Windows
/ncsi.txt Microsoft NCSI Alternative Windows
/redirect Generic Redirect Various

Behavior

· All DNS queries redirected to AP IP (192.168.4.1)
· Unrecognized HTTP requests redirected to home page
· Known captive portal endpoints return success or redirect

---

💡 STATUS LED INDICATION

LED Configuration

· Pin: GPIO2 (D4 on NodeMCU)
· Active Logic: LOW (LED ON when GPIO LOW)
· Patterns:

State Pattern Interval
WiFi Disconnected Fast blink 200ms ON, 200ms OFF
Time Not Synced Slow blink 1000ms ON, 1000ms OFF
All Systems Normal Solid ON Continuous

---

🔒 SECURITY & SAFETY FEATURES

File System Safety

· Atomic writes prevent corruption during power loss
· File size verification on read
· Magic number validation for all configs
· Automatic recovery with defaults if corrupt

WiFi Safety

· Rate-limited reconnection prevents ban from routers
· Cooldown periods reduce network congestion
· Maximum 20 reconnection attempts per hour

Memory Safety

· Heap checks before memory-intensive operations
· Deferred operations when memory is low
· Proper JSON document sizing (StaticJsonDocument for small, DynamicJsonDocument for large)

Data Integrity

· Version tracking for configuration migration
· Config dirty flag prevents unnecessary writes
· Save interval prevents flash wear
· Fallback defaults for all missing/corrupt values

---

🔄 AUTOMATIC BEHAVIORS

On Boot

1. Initialize LED
2. Mount LittleFS (format if needed)
3. Load system configuration
4. Load extended configuration
5. Load pin configuration
6. Load relay configurations
7. Restore RTC state
8. Initialize relay GPIO pins
9. Start WiFi (STA + AP)
10. Start DNS server
11. Configure web server
12. Begin NTP client

Periodic (Main Loop)

· DNS requests: Process continuously
· HTTP requests: Handle continuously
· mDNS updates: If connected
· AP restart: If pending
· LED updates: Based on system state
· WiFi check: Every 5 seconds
· NTP sync: As scheduled
· Schedule processing: Real-time
· Config flush: Every 30 seconds if dirty

On WiFi Connect

· Reset reconnection counters
· Start mDNS
· Initialize NTP client with configured server
· Perform immediate NTP sync

On WiFi Disconnect

· Stop mDNS
· Mark NTP as unsynced
· Begin reconnection attempts

---

📐 TECHNICAL SPECIFICATIONS

Pin Mapping (NodeMCU/D1 Mini)

Board Label GPIO Default Relay Usable
D0 16 Relay 1 ✅
D1 5 Relay 2 ✅
D2 4 Relay 3 ✅
D3 0 Relay 4 ✅
D4 2 Status LED ❌ Reserved
D5 14 Relay 5 ✅
D6 12 Relay 6 ✅
D7 13 Relay 7 ✅
D8 15 - ❌ Reserved (boot)
RX 3 Relay 8 ✅
TX 1 Relay 9 ✅
A0 ADC - Not available

Default Configurations

AP SSID: "ESP8266_9CH_Timer_Switch"
AP Password: "ESP8266-admin"
AP Channel: 6
Hostname: "esp8266relay"
NTP Server: "ph.pool.ntp.org"
GMT Offset: 28800 (UTC+8)
Sync Interval: 1 hour
Relay Logic: Active LOW

Performance Metrics

· Web UI Response: <100ms typical
· JSON API Response: <50ms typical
· WiFi Scan: 2-5 seconds async
· NTP Sync: 1-3 seconds
· Schedule Resolution: 1 second
· Config Save: <100ms atomic
· Boot Time: 2-4 seconds

---

🎨 USER EXPERIENCE FEATURES

Visual Feedback

· Status dots update every second
· Toast notifications for all actions
· Button state changes during operations
· Schedule badges update in real-time
· Network signal strength visualization
· Loading states during scans/syncs

Input Validation

· Relay names: Max 15 characters
· SSID: Max 31 characters
· Passwords: 8+ minimum for AP
· Time values: Bounded to valid ranges (0-23h, 0-59m, 0-59s)
· Pin selections: Conflict prevention
· Hostname: Lowercase, digits, hyphens only

Error Handling

· Friendly error messages in toasts
· Fallback values for all inputs
· Network scan timeout handling
· NTP sync failure fallback
· Connection failure retry with backoff

Confirmation Dialogs

· Restart confirmation
· Factory reset double confirmation (destructive action)
· Pin save confirmation (triggers restart)

---

🔧 EXTENSIBILITY

Adding More Relays

· Default: 9 relays
· Maximum: 16 relays
· Configured via Pin Configuration page
· GPIO pins must be available
· All 9 GPIO pins available for use

Adding Schedules

· Fixed: 8 schedules per relay
· Each with independent day/week/month settings
· Overnight scheduling supported natively

Future Expansion (Reserved)

· ExtConfig: 28 reserved bytes
· PinConfig: 29 reserved bytes
· SystemConfig: Version field for migration
· Modular design allows feature additions

---

Use cases 

· 🏠 Home automation (lighting, appliances)
· 🏢 Office/building management
· 🌾 Farm/greenhouse automation
· 🏭 Industrial timed control
· 🔬 Laboratory equipment scheduling
· 💧 Irrigation systems
· 🐔 Livestock feeding schedules
```

</details>
