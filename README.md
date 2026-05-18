# Requirements 
- ESP8266 v.1 12E 160MHZ / (Wemos D1 Mini)
- 5v 1-8 Channel Relay
- Dupont Wire
- Home Wifi for NTP/RTC sync
- 5v 2-5a Power Supply

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
- WiFi SSID: `ESP8266_8CH_Timer_Switch`
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
IN4 _____ D5
IN5 _____ D6
IN6 _____ D7
IN7 _____ RX
IN8 _____ TX
GND _____ GND
```

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic-1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic-2.png">

<details><summary>

# Full Features
</summary>

 
```
Overview

The ESP8266 8-Channel Relay Smart Switch is a comprehensive home, business, and farm automation system featuring:

· 9 configurable relay channels (expandable up to 16)
· 8 programmable schedules per relay with day-of-week and day-of-month selection
· Manual override capability for each relay
· WiFi configuration via captive portal
· NTP time synchronization with automatic drift compensation
· Persistent storage using LittleFS
· Self-healing recovery mechanisms

---

Hardware Features

Relay Configuration

Feature Specification
Maximum Relays 16 channels
Default Relays 8 channels
Supported GPIO Pins D0(16), D1(5), D2(4), D5(14), D6(12), D7(13), RX(3), TX(1)
Relay Logic Configurable Active LOW/HIGH
Pin Assignment Fully customizable via web interface

Default Pin Mapping

Relay # GPIO Pin Name
1 16 D0
2 5 D1
3 4 D2
4 14 D5
5 12 D6
6 13 D7
7 3 RX
8 1 TX

Status LED

· Pin: D4 (GPIO2)
· Active LOW logic: true
· Blink Patterns:
  · Fast blink (200ms): WiFi not connected
  · Slow blink (1000ms): No time sync
  · Solid: Normal operation

---

Network & Connectivity

WiFi Station (Client) Mode

Feature Description
SSID Configurable (max 31 chars)
Password Configurable (max 63 chars)
Auto-reconnect Yes, with exponential backoff
Connection timeout 15 seconds
Max reconnect attempts 10 per hour
RSSI monitoring Self-healing on weak signal (< -85dBm)
Network scanning Async scanning with JSON results

Access Point Mode

Feature Description
Default SSID ESP8266_9CH_Timer_Switch
Default Password ESP8266-admin
Channel 1-13 (default: 6)
Hidden SSID Optional
Captive Portal Automatic redirect to configuration
DNS Server Built-in on port 53

mDNS Support

· Default hostname: esp8266relay.local
· Service: HTTP on port 80
· Configurable: Yes, via web interface

NTP Configuration

Feature Description
Primary Server Configurable (default: ph.pool.ntp.org)
Fallback Servers pool.ntp.org, time.nist.gov, time.google.com
Sync Interval 1-24 hours (default: 1 hour)
Auto-retry Yes, with server rotation on failure
GMT Offset Configurable (default: +8 hours / 28800 sec)
Daylight Saving Configurable offset

---

Relay Control System

Manual Control

· Per-relay manual override - Toggle ON/OFF independently
· Auto mode - Return to schedule-based control
· State persistence - Manual states survive reboots
· Real-time response - Immediate digitalWrite execution

Control Methods

Method Interface
Web UI buttons ON/OFF/Auto per relay
REST API /api/relay/manual, /api/relay/reset
Schedule override Manual takes precedence

Relay State Management

· Current state tracking - Read from actual GPIO
· Schedule evaluation - Every loop iteration
· Automatic fallback - OFF when time invalid
· State backup - Preserved during recovery

---

Scheduling System

Schedule Structure (8 per relay)

{
  "startHour": 0-23,
  "startMinute": 0-59,
  "startSecond": 0-59,
  "stopHour": 0-23,
  "stopMinute": 0-59,
  "stopSecond": 0-59,
  "enabled": true/false,
  "days": 0-127 (bitmask, Sunday=bit0),
  "monthDays": 0-4294967295 (bitmask 1-31)
}

Day-of-Week Selection

Bit Day Bitmask Value
0 Sunday 1
1 Monday 2
2 Tuesday 4
3 Wednesday 8
4 Thursday 16
5 Friday 32
6 Saturday 64
7 Everyday 127 (0x7F)

Day-of-Month Selection

· Bitmask of 31 days - Select specific calendar days
· All days - Set mask to 0xFFFFFFFF
· Examples:
  · 1st and 15th: bits 0 and 14 set
  · Weekdays only: Use day-of-week instead

Schedule Logic Types

Type Condition Behavior
Always ON start == stop Relay ON during matching days
Normal start < stop ON between start and stop times
Overnight start > stop ON from start through midnight to stop
Disabled enabled = false Schedule ignored

Time Resolution

· Hour: 0-23
· Minute: 0-59
· Second: 0-59
· Evaluation: Every loop iteration (~milliseconds)

---

Time Management

Internal RTC (Real-Time Clock)

Feature Description
Synchronization source NTP or Browser
Drift compensation Adaptive algorithm (0.9-1.1 range)
Persistence Saved to LittleFS
Initialization From saved state or first NTP sync
Accuracy monitoring Hourly verification

Drift Compensation Algorithm

measuredRate = actualSeconds / nominalSeconds
driftCompensation = (driftCompensation * 0.95) + (measuredRate * 0.05)

· Early phase (<10 syncs): 50/50 weighting
· Stable phase (>10 syncs): 95/5 weighting
· Bounds: 0.9 - 1.1

Time Sources

NTP Synchronization

· Automatic at configured interval
· Server rotation on failure (4 servers total)
· Force sync via web button
· Memory check before sync (min 8KB heap)

Browser Synchronization

· Manual sync from client browser
· Useful when NTP unavailable
· Uses client's system time
· Same drift compensation applied

Time Functions

time_t getCurrentEpoch()     // Returns compensated time
void syncInternalRTC()        // Sync from NTP
void syncInternalRTCFromBrowser() // Sync from browser

---

Configuration Management

File Structure

File Content Size Magic Number
/system.cfg System configuration 136 bytes 0x1234
/ext.cfg Extended settings 32 bytes 0xEC
/pins.cfg Pin assignments 32 bytes 0x50
/relays.cfg Relay schedules ~2KB -
/state.bak State backup ~48 bytes 0xAB

System Configuration Structure

struct SystemConfig {
    uint16_t magic;           // 0x1234
    uint8_t version;          // 7
    char sta_ssid[32];        // WiFi SSID
    char sta_password[64];    // WiFi password
    char ap_ssid[32];         // AP SSID
    char ap_password[32];     // AP password
    char ntp_server[48];      // NTP server
    long gmt_offset;          // Timezone offset
    int daylight_offset;      // DST offset
    time_t last_rtc_epoch;    // Last RTC value
    float rtc_drift;          // Drift compensation
    char hostname[32];        // mDNS hostname
};

Extended Configuration

struct ExtConfig {
    uint8_t magic;        // 0xEC
    uint8_t ap_channel;   // 1-13
    uint8_t ntp_sync_hours; // 1-24
    uint8_t ap_hidden;    // 0/1
    uint8_t reserved[28];
};

Pin Configuration

struct PinConfig {
    uint8_t magic;           // 0x50
    uint8_t numRelays;       // 1-16
    bool globalActiveLow;    // Global logic
    uint8_t reserved[29];
};

Relay Configuration

struct RelayConfig {
    TimerSchedule schedule;   // 8 schedules
    bool manualOverride;      // Manual mode active
    bool manualState;         // Manual ON/OFF
    char name[16];           // Relay name
    uint8_t pin;             // GPIO pin
    bool activeLow;          // Per-relay logic
};

Configuration Features

· Atomic saves - Temporary file + rename
· Dirty tracking - Batch saves every 10 seconds
· Memory check - Requires 5KB free heap to save
· Version migration - Auto-updates from older versions
· Factory reset - Deletes all configuration files

---

Web Interface

Pages Overview

Page URL Features
Relays / Schedule editor, manual controls, rename relays
WiFi /wifi SSID/password, network scan, connection status
Time /ntp NTP settings, manual sync, browser sync
AP /ap Access Point configuration
Pins /pins GPIO assignment, relay logic
System /system Status, hostname, restart, factory reset

Relay Page Features

· Real-time relay status - ON/OFF/Auto badges
· Double-click to rename - Inline editing
· 8 schedule tabs per relay - Collapsible sections
· Day-of-week toggles - Visual selection
· Day-of-month toggles - Calendar-style selection
· Time pickers - HTML5 time inputs with seconds
· Night badge - Shows overnight schedule indicator
· Save per relay - Individual schedule saving

WiFi Page Features

· SSID input with paste support
· Network scanner - Async scan with progress
· Signal strength bars - Visual RSSI indicator
· Encryption indicator - Lock icon for secured networks
· Click to select - Auto-fill SSID from scan results
· Connection status - Shows current network and IP

Time Page Features

· NTP server configuration - Primary server only
· GMT offset - Seconds from UTC
· Sync interval - 1-24 hours
· Sync Now button - Force NTP sync
· Browser sync button - Use client time
· Fallback servers - Automatic on failure

AP Page Features

· SSID configuration - 31 character max
· Password - 8+ chars or blank for open
· Channel selection - 1-13
· Hidden SSID option
· Warning - Clients will disconnect

Pins Page Features

· Global logic selector - Active LOW/HIGH
· Per-relay pin assignment - Dropdown with used pin detection
· Per-relay logic override - Individual active LOW/HIGH
· Relay name editing - Inline
· Add relay button - Auto-assigns next available pin
· Remove relay button - Minimum 1 relay
· Available pins display - Click to add relay
· Restart required - Pin changes need reboot

System Page Features

· Real-time status cards:
  · STA IP address
  · AP IP address
  · Free heap (KB)
  · Device uptime
  · mDNS hostname
  · WiFi RSSI with quality description
  · Last NTP sync time
  · Current NTP server
· Hostname configuration - mDNS .local address
· Restart button - Soft reset
· Factory reset button - Complete wipe

CSS Features

· Responsive design - Mobile-friendly grid layout
· Dark/Light neutral - High contrast, accessible
· Toast notifications - Non-intrusive feedback
· Loading states - Disabled buttons during operations
· Status dots - WiFi and NTP connection indicators
· Header clock - Real-time display

---

Recovery & Stability Systems

Recovery Actions

Action Trigger Behavior
RECOVERY_NONE Normal operation No action
RECOVERY_WIFI_RESET WiFi failure Reset WiFi stack, reconnect
RECOVERY_WEB_SERVER_RESET Web server hang Restart HTTP and DNS servers
RECOVERY_FULL_SOFT_RESET Severe instability Full reset with state preservation
RECOVERY_RTC_RESET Time corruption Reset time tracking
RECOVERY_TIMER_RESET Schedule corruption Reload all configurations
RECOVERY_MEMORY_CLEANUP Low memory Garbage collection, stack reset

Self-Healing Mechanisms

WiFi Healing

· Rate limiting - Max 10 reconnects per hour
· Cooldown period - 1 hour after max attempts
· Gentle healing - Background reconnection attempt
· RSSI monitoring - Reconnect on < -85dBm
· Persistent failure detection - 2 hours triggers recovery

Memory Management

· Heap check interval - Every hour
· Minimum heap for save - 5KB
· Minimum heap for NTP - 8KB
· Memory cleanup - Reset stack, free fragments
· Emergency cleanup - Auto-triggered at <8KB heap

Time Validation

· Sanity check - Epoch between 1B and 5B seconds
· Hourly verification - Compare against last sync
· Auto-correction - Reset RTC on detection of corruption
· Drift bounds - Kept between 0.9 and 1.1

Watchdog System

· Loop timeout - 5 minutes without successful loop
· Heartbeat tracking - lastLoopHeartbeat timestamp
· Auto recovery - Full soft reset on timeout
· Last successful loop - Recovery reference point

Proactive Maintenance

Interval Action
60 seconds Memory cleanup if <10KB free
60 seconds Weak signal recovery
60 seconds NTP retry if needed
24 hours Filesystem integrity check
1 hour DNS server restart

State Preservation

· Relay states backed up before any recovery
· Restored after recovery to minimize disruption
· Automatic cleanup of backup after successful restore

Recovery Limits

· Max attempts per incident: 5
· Attempt counter reset: After 1 hour of stability
· Cooldown: 2 seconds between recovery steps

---

LED Status Indicators

Status LED Patterns

Condition Pattern Description
Normal Solid ON WiFi connected, time synced
No WiFi Fast blink (200ms) Scanning/connecting to network
No Time Slow blink (1000ms) Waiting for NTP sync
Recovery Ultra-fast blink (100ms) Recovery in progress

LED Logic

STATUS_LED_PIN = D4 (GPIO2)
STATUS_LED_ACTIVE_LOW = true  // LED on when pin LOW

Web Interface Indicators

· WiFi dot - Green when connected, Red when disconnected
· NTP dot - Green when synced, Yellow when pending
· Real-time clock - Updates every second

---

API Endpoints

Relay Management

Endpoint Method Body Response Description
/api/relays GET - Relay array Get all relay states & schedules
/api/relay/manual POST {relay, state} {success} Set manual ON/OFF
/api/relay/reset POST {relay} {success} Return to auto mode
/api/relay/save POST {relay, schedules} {success} Save schedule configuration
/api/relay/name POST {relay, name} {success} Rename relay

Time Management

Endpoint Method Body Response Description
/api/time GET - {time, wifi, ntp} Current time & status
/api/time/browser-sync POST {epoch} {success} Sync from browser
/api/ntp GET - NTP settings Get NTP configuration
/api/ntp POST NTP settings {success} Save NTP configuration
/api/ntp/sync POST - {success} Force NTP sync

WiFi Management

Endpoint Method Body Response Description
/api/wifi GET - WiFi status Get current WiFi configuration
/api/wifi POST {ssid, password} {success} Save WiFi credentials
/api/wifi/scan POST - {scanning:true} Start network scan
/api/wifi/scan GET - Networks array Get scan results

AP Management

Endpoint Method Body Response Description
/api/ap GET - AP settings Get AP configuration
/api/ap POST AP settings {success} Save AP configuration

Pin Management

Endpoint Method Body Response Description
/api/pins GET - Pin configuration Get pin assignments
/api/pins POST Pin config {success, softReset} Save pin configuration

System Management

Endpoint Method Body Response Description
/api/system GET - System status Get system information
/api/system POST {hostname} {success} Save hostname
/api/reset POST - {success, recovery} Soft reset
/api/factory-reset POST - {success} Factory reset

API Response Examples

GET /api/relays (simplified)

[{
  "name": "Living Room",
  "state": true,
  "manual": false,
  "schedules": [{
    "startHour": 6, "startMinute": 0, "startSecond": 0,
    "stopHour": 22, "stopMinute": 0, "stopSecond": 0,
    "enabled": true, "days": 127, "monthDays": 0
  }]
}]

GET /api/system

{
  "hostname": "esp8266relay",
  "ip": "192.168.1.100",
  "ap_ip": "192.168.4.1",
  "uptime": 86400,
  "freeHeap": 28000,
  "ntpSynced": true,
  "ntpServer": "ph.pool.ntp.org",
  "ntpSyncAge": 3600,
  "wifiConnected": true,
  "rssi": -55,
  "mdnsHostname": "esp8266relay.local",
  "version": 7
}

---

File System Structure

LittleFS Layout

/
├── system.cfg      # Main configuration (magic: 0x1234)
├── ext.cfg         # Extended settings (magic: 0xEC)
├── pins.cfg        # Pin assignments (magic: 0x50)
├── relays.cfg      # Relay schedules (no magic)
└── state.bak       # Runtime state backup (magic: 0xAB)

File Operations

Operation Method Safety
Read loadFromFile() Size validation
Write saveToFileAtomic() Temp file + rename
Delete LittleFS.remove() Direct
Format LittleFS.format() On init failure

Atomic Save Process

1. Create .tmp file
2. Write data to .tmp
3. Flush and close
4. Delete original if exists
5. Rename .tmp to target
6. Delete .tmp if rename fails

---

Timing Constants Reference

Constant Value Description
NTP_RETRY_INTERVAL 30 sec NTP retry on failure
WIFI_CHECK_INTERVAL 5 sec WiFi status polling
WIFI_CONNECT_TIMEOUT 15 sec Max connection wait
RTC_UPDATE_INTERVAL 100 ms Internal time update
CONFIG_SAVE_INTERVAL 10 sec Batch save delay
MAX_DRIFT_CALIBRATION_INTERVAL 30 days Max drift measurement
PROACTIVE_MAINTENANCE_INTERVAL 60 sec Maintenance tasks
GENTLE_NETWORK_HEAL_INTERVAL 30 sec Background healing
LED_BLINK_FAST 200 ms No WiFi indication
LED_BLINK_SLOW 1000 ms No time indication
LOOP_WATCHDOG_TIMEOUT 5 min Loop dead detection
AUTO_HEAL_INTERVAL 60 sec Health checks
RECOVERY_DELAY 2 sec Recovery cooldown
HEAP_CHECK_INTERVAL 1 hour Memory monitoring
SELF_CHECK_INTERVAL 24 hours Integrity verification
TIME_CHECK_INTERVAL 60 sec Time sanity check

---

Memory & Performance

Heap Requirements

Operation Minimum Heap
Save configuration 5 KB
NTP synchronization 8 KB
Web server operation 10 KB
Emergency cleanup 4 KB

Performance Characteristics

· Schedule evaluation: O(relays × schedules)
· Web server response: <100ms typical
· NTP sync time: 1-3 seconds
· WiFi scan duration: 2-5 seconds
· Configuration save: <50ms

Optimization Features

· JSON streaming - No large allocations
· String reservation - Pre-allocated buffers
· Stack reset - Manual free stack cleanup
· Sleep management - Light sleep on low memory

---

Security Features

Access Control

· AP password required (optional)
· Captive portal - No direct IP access needed
· No remote access - Local network only
· Factory reset - Full settings wipe

Configuration Protection

· Atomic writes - No partial saves
· Magic numbers - Corruption detection
· Size validation - File integrity checks
· Backup system - State preservation

Network Security

· WPA2-PSK - AP encryption when password set
· Hidden SSID - Optional stealth mode
· No telnet/SSH - Web only
· Input validation - All API parameters sanitized

---

Example Use Cases

Home Automation

· Morning routine - Turn on lights, coffee maker at 6 AM
· Evening mode - Dim lights at sunset, turn off at 11 PM
· Security lighting - Motion-activated schedules
· HVAC control - Temperature-based scheduling

Business Applications

· Store lighting - ON at 8 AM, OFF at 9 PM (except Sunday)
· Signage control - Specific day-of-month for billboards
· Inventory systems - Scheduled equipment activation
· Access control - Time-based door/gate operation

Farm Automation

· Irrigation - Early morning water cycles (2 AM - 5 AM)
· Feeders - Multiple daily schedules
· Ventilation - Temperature-responsive schedules
· Lighting - Seasonal day-length adjustment

Custom Scenarios

· Overnight schedules - Run from 11 PM to 5 AM
· Weekend-only - Day-of-week filtering
· Monthly maintenance - 1st and 15th only
· Holiday mode - Manual override for special days

---

Version History

Version Changes
7 Current - Full schedule system, recovery mechanisms
<7 Legacy - Basic timer functionality

---

Troubleshooting Guide

Common Issues

Symptom Possible Cause Solution
LED fast blink No WiFi Check credentials, AP availability
LED slow blink No time sync Check NTP server, internet connection
Relays not responding Schedule misconfigured Check enabled flag, time settings
Web UI slow Low memory Restart device, reduce schedules
Time jumps backward RTC drift Browser sync, NTP interval adjustment
AP not visible Hidden SSID Enable visibility or connect manually

Recovery Indicators

· Persistent WiFi failure → Check if IP is 0.0.0.0
· Time corruption → Last NTP sync > 24 hours ago
· Low memory → Free heap < 10KB
· Loop timeout → Web server unresponsive

---

Technical Specifications

Microcontroller

· Model: ESP8266 (ESP-12E/F)
· CPU: 80 MHz (configurable to 160 MHz)
· Flash: 4MB (typical)
· RAM: 80KB user available

Power Requirements

· Voltage: 3.3V (logic), 5-12V (relay board)
· Current (idle): ~70mA
· Current (WiFi active): ~170mA
· Current (peak): ~350mA

Relay Module Compatibility

· Voltage: 5V or 12V (depending on module)
· Current per relay: 10A @ 250VAC / 30VDC (typical)
· Optoisolation: Recommended for inductive loads

Communication

· WiFi: 802.11 b/g/n (2.4 GHz)
· Web: HTTP/1.1
· mDNS: RFC 6762 compliant
· NTP: RFC 5905 compliant

---

Project: ESP8266 9-Channel Relay Smart Switch
Author: Raff Alds
Repository: https://github.com/xiv3r
```

</details>
