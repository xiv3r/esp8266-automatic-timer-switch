## ESP32
👉 https://github.com/xiv3r/esp32-automatic-timer-switch

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

# GMT offset
> ⚠️ Set to your country time
- Search your country `gmt offsets in seconds` and paste it on the Time -> GMT Offset

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

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/scr1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/scr2.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/scr3.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic1.jpg">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/libraries/pic2.jpg">

<details><summary>

# Full Features
</summary>

---

## 1. Hardware Layer

### 1.1 Supported GPIO Pins

| GPIO | Arduino Pin | Common Name | Typical Use |
| :--- | :--- | :--- | :--- |
| 16 | D0 | WAKE | Relay 1 (default) |
| 5 | D1 | — | Relay 2 (default) |
| 4 | D2 | — | Relay 3 (default) |
| 14 | D5 | SCK | Relay 4 (default) |
| 12 | D6 | MISO | Relay 5 (default) |
| 13 | D7 | MOSI | Relay 6 (default) |
| 3 | RX | RXD0 | Relay 7 (default) |
| 1 | TX | TXD0 | Relay 8 (default) |
| 2 | D4 | LED_BUILTIN | Status LED (fixed) |
| 0 | D3 | FLASH | Factory Reset Button (fixed) |

### 1.2 Pin Configuration Flexibility

- Dynamic relay count: 1–16 relays (limited only by available GPIOs)
- Per-relay logic level: Each relay independently configurable as Active-LOW or Active-HIGH
- Global default: Sets default logic for new relays
- Pin conflict detection: UI prevents assigning same pin to multiple relays
- Runtime reconfiguration: Pin changes survive reboot via persistent storage
- Default pin preset: One-click reset to factory pin assignments while preserving schedules

### 1.3 Electrical Characteristics

| Parameter | Value |
| :--- | :--- |
| GPIO output current | 12mA max per pin |
| Relay driver requirement | External transistor/MOSFET or relay module with optocoupler |
| LED pin | GPIO 2 (D4), built-in pull-up on most boards |
| Reset button | GPIO 0, external pull-up required (built-in on NodeMCU) |
| Button debounce | 50ms software debounce |
| Factory reset hold time | 5,000ms continuous press |

### 1.4 Supported Relay Modules

- 5V/3.3V active-LOW relay boards (most common, optocoupled)
- 5V/3.3V active-HIGH relay boards
- Solid-state relays (SSR) with logic-level input
- Mixed logic boards (per-relay configuration handles this)

---

## 2. Relay & Schedule Engine

### 2.1 Relay Data Structure

```cpp
struct RelayConfig {
    TimerSchedule schedule;     // 8 independent schedules
    bool manualOverride;        // Manual mode active?
    bool manualState;           // If manual, ON or OFF?
    char name[16];             // User-assigned label
    uint8_t pin;               // GPIO pin number
    bool activeLow;            // Logic inversion
};
```

2.2 Schedule Data Structure (Per Schedule Slot)

```cpp
struct TimerSchedule {
    uint8_t  startHour[8];      // 0–23
    uint8_t  startMinute[8];    // 0–59
    uint8_t  startSecond[8];    // 0–59
    uint8_t  stopHour[8];       // 0–23
    uint8_t  stopMinute[8];     // 0–59
    uint8_t  stopSecond[8];     // 0–59
    bool     enabled[8];        // Schedule active?
    uint8_t  days[8];           // Day-of-week bitmask (bit 0=Sun)
    uint32_t monthDays[8];      // Day-of-month bitmask (bit 0=1st)
};
```

2.3 Schedule Evaluation Algorithm

```
For each relay:
  1. If manualOverride == true:
       → Apply manualState directly, skip schedules
  2. Else:
       For each of 8 schedule slots:
         a. If not enabled → skip
         b. Check day-of-week: (days & (1 << currentWeekday)) == 0? → skip
         c. Check day-of-month: monthDays != 0 AND (monthDays & (1 << (day-1))) == 0? → skip
         d. Calculate startSeconds, stopSeconds
         e. Evaluate time window:
              - start == stop → Always ON (24h)
              - start < stop  → Normal (e.g., 08:00–18:00)
              - start > stop  → Overnight (e.g., 22:00–06:00)
         f. If current time in window → relay = ON
         g. If any schedule says ON → break (first-match-wins)
       If no schedule matched → relay = OFF
```

2.4 Time Window Types

Type Example Behavior Use Case
Daytime 08:00:00 → 18:00:00 ON 8AM–6PM Office lights, irrigation
Overnight 22:00:00 → 06:00:00 ON 10PM–6AM next day Night lights, security
Always ON 08:00:00 → 08:00:00 ON continuously during allowed days Holiday lighting
Midnight cross 23:00:00 → 01:00:00 ON 11PM–1AM Late-night equipment

2.5 Day Filtering Details

Day-of-Week Bitmask

Bit: 6 5 4 3 2 1 0
Day: Sat Fri Thu Wed Tue Mon Sun
Value: 64 32 16 8 4 2 1

· 0x7F (127) = All 7 days ("Everyday")
· 0x00 = No days (schedule ignored)
· 0x3E (62) = Mon–Fri only
· 0x41 (65) = Sun + Sat only

Day-of-Month Bitmask

· 0xFFFFFFFF = All 31 days ("All month days")
· 0x00000000 = No filter (schedule only uses day-of-week)
· 0x80000001 = 1st and 31st only

2.6 Manual Override System

State Behavior Persistence
Manual ON Relay forced ON, all schedules ignored Survives reboot (saved to /state.bak)
Manual OFF Relay forced OFF, all schedules ignored Survives reboot
Auto Schedule engine controls relay Normal operation

---

3. Timekeeping Architecture

3.1 Software RTC Design

```

│                  Time Sources                                 
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐     
│  │   NTP      │  │  Browser   │  │  Saved RTC State    │     
│  │ (Primary)  │  │ (Fallback) │  │  (Cold Boot)        │    
│  └────┬─────┘  └────┬─────┘  └────────┬─────────┘     
│        │               │                    │              
│     ────────────────|───────────────────            
│                       ▼                              
│          ┌─────────────────────┐                    
│          │  internalEpoch          │                    
│          │  (Unix timestamp)       │                    
│          └──────────┬──────────┘                    
│                       │                               
│          ┌──────────▼──────────┐                    
│          │  Drift Compensation     │                    
│          │  (weighted moving       │                    
│          │   average filter)       │                    
│          └──────────┬──────────┘                    
│                       │                               
│          ┌──────────▼──────────┐                    
│          │  getCurrentEpoch()      │                    
│          │  = internalEpoch        │                    
│          │  + (elapsed_ms          │                    
│          │     × drift_factor)     │                    
│          └──────────┬──────────┘                    
│                       │                               
│          ┌──────────▼──────────┐                    
│          │  Local Time             │                    
│          │  + GMT offset           │                    
│          │  + DST offset           │                    
│          └─────────────────────┘                    

```

3.2 Drift Calibration Algorithm

```cpp
// During first 10 syncs: aggressive learning (50% new, 50% old)
driftCompensation = driftCompensation * 0.5 + measuredRate * 0.5;

// After 10 syncs: gradual refinement (95% old, 5% new)
driftCompensation = driftCompensation * 0.95 + measuredRate * 0.05;

// Safety clamping
if (driftCompensation < 0.9) driftCompensation = 0.9;
if (driftCompensation > 1.1) driftCompensation = 1.1;
```

3.3 NTP Client Configuration

Parameter Value
Update interval Configurable 1–24 hours (default: 1h)
Update method forceUpdate() — blocking with timeout
Timeout Library default (typically 1–5s)
Retry strategy Rotate through 4 servers on failure
Min heap for sync 8,192 bytes
Retry interval 30,000ms after failed attempt
Epoch validity Must be between 1,000,000,000 and 5,000,000,000

3.4 NTP Server Fallback Chain

1. nts.netnod.se (Sweden, Netnod)
2. pool.ntp.org (Global pool)
3. time.nist.gov (USA, NIST)
4. ntp1.time.nl (Netherlands)
5. Cycle back to Priority 1

3.5 Browser Time Sync

· Trigger: User clicks "Sync Browser" in Time page
· Source: Date.now() / 1000 from client JavaScript
· Validation: Epoch must be in valid range (1B–5B)
· Drift calibration: Same algorithm as NTP sync
· No NTP flag: Does NOT set lastNTPSync, only updates internal clock
· Use case: Devices without internet access, local network deployments

3.6 Epoch Sanity Checks

Check Interval Action on Failure
Time verification (WiFi) 60 min Trigger RTC recovery
Internal time check 60 sec Trigger RTC recovery
NTP drift check 24 hours Force re-sync
Epoch range validation Every read (1B–5B) only

3.7 RTC State Persistence

```cpp
struct SystemConfig {
    time_t  last_rtc_epoch;    // Saved epoch for cold boot
    float   rtc_drift;         // Saved drift factor
};
```

· Saved on every NTP/browser sync
· Saved on every config flush (10s intervals)
· Loaded on boot to restore time without network
· Drift factor validated on load (must be 0.9–1.1)

---

4. WiFi & Network Stack

4.1 WiFi Modes

Mode Description When Active
WIFI_AP Access Point only No STA credentials configured
WIFI_AP_STA AP + Station simultaneously STA credentials configured
WIFI_STA Station only (temporary) During recovery operations

4.2 Connection State

4.3 Reconnection Rate Limiting

Parameter Value Purpose
MAX_RECONNECT_ATTEMPTS 20/hour Prevent thrashing
RECONNECT_COOLDOWN 60 min Pause after hitting limit
Persistent disconnect 2 hours Triggers recovery if still disconnected
Gentle healing interval 30s Background soft reconnection

4.4 Network Scanning

· Type: Asynchronous (WiFi.scanNetworksAsync)
· Max results: 20 displayed
· Scan duration: ~2–3 seconds typical
· Poll interval: 2,500ms (client-side)
· Display: SSID, RSSI (dBm), signal bars, encryption lock icon
· Click-to-select: Populates SSID field and focuses password
· Sorting: Descending by RSSI (strongest first)
· Concurrency protection: Won't start scan during connection attempt

4.5 Access Point Details

Parameter Default Range Notes
SSID ESP8266_8CH_Timer_Switch 1–31 chars Configurable
Password ESP8266-admin 0 or 8–31 chars Blank = open network
Channel 6 1–13 2.4GHz only
Hidden No Yes/No Doesn't broadcast SSID
Max clients 4 (ESP8266 limit) — Hardware limitation
IP 192.168.4.1 — Fixed

4.6 AP Restart Protocol

```
User saves AP settings
        │
        ▼
┌──────────────────┐
│  APR_PENDING        │ → WiFi.softAPdisconnect(false)
└────────┬─────────┘
         │
    AP_RESTART_DELAY (50ms)
         │
         ▼
┌──────────────────┐
│ APR_RESTARTING      │ → WiFi.softAP(new settings)
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│   APR_IDLE          │
└──────────────────┘
```

4.7 Captive Portal Implementation

The device intercepts common connectivity-check URLs:

Endpoint Origin Response
/generate_204 Android Redirect to home
/hotspot-detect.html Apple Success page
/library/test/success.html Apple Success page
/success.txt Firefox "success\n"
/canonical.html Generic Redirect to home
/redirect Generic Redirect to home
/connecttest.txt Microsoft "Microsoft Connect Test"
/ncsi.txt Microsoft NCSI "Microsoft NCSI"

DNS Interception: All DNS queries resolved to AP IP (192.168.4.1)

4.8 mDNS (Bonjour/Avahi)

Feature Detail
Hostname Configurable (default: esp8266relay)
Format [hostname].local
Service _http._tcp on port 80
Validation Lowercase letters, digits, hyphens only
Restart Auto-restarts on hostname change or WiFi reconnect

4.9 DNS Server

· Type: Captive portal DNS (DNSServer)
· Port: 53 (standard DNS)
· Behavior: All queries resolve to AP IP
· Restart: Auto-restarted hourly for memory management

---

5. Web Server & API

5.1 Server Architecture

```
ESP8266WebServer (port 80)
│
├── Static Pages (PROGMEM)
│   ├── /            → index_html     (Relay Dashboard)
│   ├── /wifi        → wifi_html      (WiFi Settings)
│   ├── /ntp         → ntp_html       (Time Settings)
│   ├── /ap          → ap_html        (AP Settings)
│   ├── /pins        → pins_html      (GPIO Configuration)
│   ├── /system      → system_html    (System Info)
│   └── /style.css   → style_css      (Shared Stylesheet)
│
├── API Endpoints (Dynamic JSON)
│   ├── GET  /api/relays
│   ├── POST /api/relay/manual
│   ├── POST /api/relay/reset
│   ├── POST /api/relay/save
│   ├── POST /api/relay/name
│   ├── GET  /api/time
│   ├── POST /api/time/browser-sync
│   ├── GET  /api/wifi
│   ├── POST /api/wifi
│   ├── POST /api/wifi/scan
│   ├── GET  /api/wifi/scan
│   ├── GET  /api/ntp
│   ├── POST /api/ntp
│   ├── POST /api/ntp/sync
│   ├── GET  /api/ap
│   ├── POST /api/ap
│   ├── GET  /api/pins
│   ├── POST /api/pins
│   ├── POST /api/pins/reset
│   ├── GET  /api/system
│   ├── POST /api/system
│   ├── POST /api/reset
│   └── POST /api/factory-reset
│
└── Captive Portal Handlers
    └── (8 endpoints, see section 4.7)
```

5.2 Request Handling Flow

```
Client Request
    │
    ▼
server.handleClient()
    │
    ├── Static route match? → Serve PROGMEM page
    │
    ├── API route match? → Parse JSON body (if POST)
    │                      → Execute handler
    │                      → Return JSON response
    │
    └── No match? → handleNotFound() → Redirect to home
```

5.3 JSON Body Parsing (Custom Lightweight Parser)

Instead of parsing full JSON for every request, the system uses targeted extraction:

```cpp
int extractJsonInt(const String& json, const char* key);
bool extractJsonBool(const String& json, const char* key);
uint8_t extractJsonByte(const String& json, const char* key);
uint32_t extractJsonUInt32(const String& json, const char* key);
```

· Max body size: 2,048 bytes (safety check)
· Max integer digits: 10 (prevents overflow)
· No dynamic allocation: Works within stack memory
· Fallback: ArduinoJson used for simple objects only

5.4 Response Headers

Header Value
Content-Type text/html, text/css, application/json, text/plain
Location Used for redirects (302)
Cache-Control Not set (dynamic content)

---

6. Self-Healing & Recovery System

6.1 Recovery Tier Architecture

```
Tier 1: MEMORY_CLEANUP
├── Trigger: Heap < 8KB
├── Action: Reset free stack, light sleep cycle, clear server args
└── Escalation: If heap still < 4KB → Tier 5

Tier 2: WIFI_RESET
├── Trigger: WiFi disconnected, reconnect failures
├── Action: STA disable/enable, re-begin connection, reload configs
└── Escalation: If still disconnected → Tier 2 retry (max 5x) → Tier 6

Tier 3: WEB_SERVER_RESET
├── Trigger: Server unresponsive
├── Action: Stop/close server, restart DNS, restart server
└── Escalation: If fails → Tier 6

Tier 4: RTC_RESET
├── Trigger: Invalid epoch, drift corruption
├── Action: Clear all time state, re-sync NTP if WiFi available
└── Escalation: N/A (self-contained)

Tier 5: TIMER_RESET
├── Trigger: Pin configuration changes
├── Action: Reload all configs from flash, reapply pin modes
└── Escalation: N/A (reload cycle)

Tier 6: FULL_SOFT_RESET
├── Trigger: Multiple lower-tier failures, watchdog timeout
├── Action: Complete WiFi disconnect, format FS if corrupt, reload all,
│           restart AP, restart DNS, restart server
└── Escalation: N/A (maximum recovery level)
```

6.2 Recovery Safeguards

Safeguard Value Purpose
Max recovery attempts 5 Prevents infinite recovery loops
Recovery cooldown 2 seconds Allows system to stabilize
Hourly reset tracking — Resets counter if stable for 1 hour
State preservation /state.bak Relay states saved before recovery
Config flush Before recovery Ensures latest settings persisted
RTC state save Before recovery Preserves time calibration

6.3 Automatic Health Checks (Complete List)

Check Interval Monitors Action
Auto-heal 60s WiFi status, heap, time validity Targeted recovery
Proactive maintenance 60s Heap, WiFi RSSI, NTP sync age, FS health, DNS Gentle fixes
Gentle network healing 30s WiFi connection Background reconnect
Heap check 60 min Free heap Memory cleanup
Self-check 24 hours FS integrity, config magic, pin modes Config recovery
Time verification 60 min Epoch range (1B–5B) RTC recovery
Internal time check 60 sec Same as above RTC recovery
Loop watchdog 5 min Main loop heartbeat Full soft reset
DNS restart 60 min DNS server Stop/start cycle
FS health check 24 hours Config file existence Reload configs

6.4 Memory Cleanup Details

```
void performMemoryCleanup() {
    ESP.resetFreeContStack();        // Defragment heap
    WiFi.setSleepMode(WIFI_LIGHT_SLEEP);  // Release WiFi buffers
    delay(10);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // Restore normal mode
}
```

6.5 Watchdog Implementation

```
Main Loop Heartbeat
        │
        ▼
lastSuccessfulLoop = now
        │
   (loop runs)
        │
        ▼
Check: elapsedSince(lastSuccessfulLoop) > LOOP_WATCHDOG_TIMEOUT (5 min)?
        │
   YES  │  NO
        │   │
        ▼   └── Continue
FULL_SOFT_RESET
```

6.6 Proactive Maintenance Details

```
void proactiveMaintenance() {
    // 1. Heap management
    if (heap < 10KB) → performMemoryCleanup()
    
    // 2. Weak signal recovery
    if (RSSI < -85 dBm && last recovery > 5 min ago) → WiFi.reconnect()
    
    // 3. NTP catch-up
    if (time since last sync > 1 hour) → tryNTPSync()
    
    // 4. Filesystem health
    if (config file missing after 24h) → reload configs
    
    // 5. DNS server stability
    if (DNS running > 1 hour) → restart DNS
}
```

---

7. Configuration & Storage

7.1 LittleFS Layout

```
LittleFS Root
├── /system.cfg       (SystemConfig: ~200 bytes)
├── /ext.cfg          (ExtConfig: ~32 bytes)
├── /relays.cfg       (RelayConfig[16]: ~4KB)
├── /pins.cfg         (PinConfig: ~32 bytes)
└── /state.bak        (RelayStateBackup: ~50 bytes) [transient]
```

7.2 Data Structures with Validation

SystemConfig (/system.cfg)

```
struct SystemConfig {
    uint16_t magic;           // 0x1234
    uint8_t  version;         // 7
    char     sta_ssid[32];
    char     sta_password[64];
    char     ap_ssid[32];
    char     ap_password[32];
    char     ntp_server[48];
    long     gmt_offset;
    int      daylight_offset;
    time_t   last_rtc_epoch;
    float    rtc_drift;
    char     hostname[32];
};
```

ExtConfig (/ext.cfg)

```
struct ExtConfig {
    uint8_t magic;            // 0xEC
    uint8_t ap_channel;       // 1–13
    uint8_t ntp_sync_hours;   // 1–24
    uint8_t ap_hidden;        // 0 or 1
    uint8_t reserved[28];
};
```

PinConfig (/pins.cfg)

```
struct PinConfig {
    uint8_t magic;            // 0x50
    uint8_t numRelays;        // 1–16
    bool    globalActiveLow;
    uint8_t reserved[29];
};
```

RelayStateBackup (/state.bak)

```
struct RelayStateBackup {
    uint8_t magic;            // 0xAB
    bool    states[16];
    bool    manual[16];
    uint8_t reserved[13];
};
```

7.3 Atomic Write Protocol

1. Open temp file: /system.cfg.tmp
2. Write data to temp file
3. Flush and close temp file
4. Verify bytes written == expected size
5. Delete original file: /system.cfg
6. Rename: /system.cfg.tmp → /system.cfg
7. On failure at any step: delete temp file

7.4 Save Trigger Conditions

Trigger Debounce
Relay name change Immediate mark, save within 10s
Schedule change Immediate mark, save within 10s
Manual override Immediate mark, save within 10s
WiFi settings change Immediate save
NTP settings change Immediate save
AP settings change Immediate save
Pin configuration change Immediate save
System hostname change Immediate save
Periodic flush Every 10s if dirty

7.5 Save Safety Checks

Check Threshold
Minimum heap 5,120 bytes
Filesystem usage < 90% capacity
Heap for NTP sync 8,192 bytes
Low-memory pending flag Retry on next interval

7.6 Configuration Recovery

```
On boot:
  1. Load /system.cfg
  2. Check magic (0x1234) and version (7)
  3. If invalid → initDefaults()
  4. Load /relays.cfg
  5. If invalid → create default relay configs
  6. Load /ext.cfg
  7. Check magic (0xEC)
  8. If invalid → create default ext config
  9. Load /pins.cfg
  10. Check magic (0x50)
  11. If invalid → create default pin config
  12. Load /state.bak (if exists)
  13. Restore relay states if valid
```

---

8. User Interface (Web App)

8.1 Design System

Element Specification
Font System UI (-apple-system, BlinkMacSystemFont, Segoe UI, Roboto)
Base size 14px
Color scheme Blue primary (#1565C0), white cards, gray backgrounds
Border radius 5–10px (buttons, cards)
Shadows Subtle elevation (0 2px 8px rgba(0,0,0,.08))
Responsive Mobile-first, breakpoint at 500px
Animations Hover transitions (150ms), toast slide-up (280ms)

8.2 Shared UI Components

Component Description
Header Sticky, gradient blue, navigation tabs, status dots, live clock
Status Dots Green (WiFi ok), Red (WiFi down), Yellow (NTP syncing), Gray (NTP never)
Cards White, rounded, hover shadow elevation
Badges ON (green), OFF (red), MANUAL (orange)
Buttons ON (green), OFF (red), Auto (gray), Save (blue), Danger (dark red)
Toast Bottom-center, slide-up animation, auto-dismiss 3s
Inputs Bordered, focus ring blue, monospace for time fields
Day Toggles 3-character abbreviations, toggleable, blue when selected
Month Day Toggles Numeric 1–31, purple theme, toggleable

8.3 Page-Specific Features

Page Features
Relay Dashboard (/) Relay cards with double-click name edit, 8 collapsible schedules, HH:MM:SS pickers, day/month toggles, night badge
WiFi Page (/wifi) Connection status, network scanner with signal bars, click-to-connect
Time Page (/ntp) NTP server config, GMT/DST offsets, sync interval, Sync Now & Browser Sync buttons
AP Page (/ap) Warning banner, SSID/Password, channel selector, hidden SSID toggle
Pins Page (/pins) Global logic selector, relay table with GPIO dropdowns, conflict detection, add relay button
System Page (/system) Info grid (IP, heap, uptime, mDNS, RSSI), auto-refresh, restart & factory reset buttons

8.4 JavaScript Architecture

Feature Implementation
Real-time clock setInterval(tick, 1000) fetching /api/time
Toast system toast(message, isSuccess) with CSS animation
Relay control AJAX POST with JSON, auto-reload on success
Schedule editing Client-side model updates, explicit save
Day toggles Bitmask manipulation (^= 1<<dayIndex)
Network scan Polling loop (2.5s intervals) until complete
Inline editing DOM replacement of span with input on double-click

---

9. Security & Access

9.1 Authentication

· No authentication: Device is intended for trusted local networks
· AP password: Optional, minimum 8 characters if set
· Open AP mode: If password is blank, AP is open (no encryption)

9.2 Network Isolation

· AP isolation: Not enabled (clients can communicate)
· STA firewall: Not implemented (ESP8266 limitation)
· Recommended deployment: Behind a firewall/router, not directly internet-exposed

9.3 Data at Rest

· WiFi passwords: Stored as plaintext in LittleFS
· No encryption: ESP8266 lacks hardware encryption acceleration
· Physical access: Anyone with physical access can read flash via serial

9.4 Captive Portal Security

· DNS hijacking: All queries redirected to device IP
· HTTP only: No HTTPS support (ESP8266 limitation)
· Portal detection: Standard endpoints for major OS platforms

---

10. Factory Reset System

10.1 Reset Triggers

Method Action Confirmation
Web UI button POST /api/factory-reset Double browser confirmation
API call POST /api/factory-reset None
Hardware button Hold GPIO 0 for 5 seconds Visual LED feedback

10.2 Hardware Button Sequence

```
Button Press Timeline:
0ms     ─── Press detected (debounced)
│
0–5s    ─── LED blinks with increasing speed
│           500ms → 250ms → 125ms → 62ms → 50ms
│
5s      ─── LED solid ON (reset threshold reached)
│           ─── Release button
│
Release ─── 10 rapid LED blinks (confirmation)
│           ─── Files deleted
│           ─── Configs cleared
│           ─── ESP.restart()
```

10.3 Reset Actions (Complete)

```
1. Delete /system.cfg
2. Delete /ext.cfg
3. Delete /relays.cfg
4. Delete /pins.cfg
5. Delete /state.bak
6. memset(&sysConfig, 0, sizeof(SystemConfig))
7. memset(&extConfig, 0, sizeof(ExtConfig))
8. memset(&pinConfig, 0, sizeof(PinConfig))
9. Delay 500ms
10. ESP.restart()
```

Note: Does NOT format LittleFS — only removes config files.

---

11. LED Status Indication

11.1 LED Patterns

System State LED Pattern Frequency
All normal OFF (or ON if active-low) Steady
WiFi disconnected Fast blink 200ms period
Time not synced Slow blink 1,000ms period
Recovery in progress Very fast blink 100ms period
Factory reset countdown Progressive acceleration 500ms → 50ms
Factory reset triggered 10 rapid blinks 100ms each
Button held < 5s See countdown pattern Variable

11.2 LED Configuration

```
#define STATUS_LED_PIN       D4     
#define STATUS_LED_ACTIVE_LOW true
```

11.3 Priority System

```
Factory reset feedback (highest priority)
    ↓
Recovery mode indicator
    ↓
WiFi disconnected indicator
    ↓
NTP/time not synced indicator
    ↓
Normal state (LED off)
```

---

12. Memory & Performance

12.1 Memory Budget

Resource Typical Usage Max Limit
Heap (free) 15–25 KB 40–50 KB total
Stack 4 KB —
Program flash ~450 KB 1 MB (ESP-12E)
LittleFS ~200 KB used 1–3 MB available
PROGMEM strings ~25 KB —
JSON responses 2–4 KB peak 2 KB safety limit

12.2 Performance Metrics

Operation Time Notes
Boot to AP ready ~1.5 seconds Without STA connection
Boot to STA connected 3–8 seconds Depends on network
NTP sync 1–5 seconds Blocking, server-dependent
Network scan 2–3 seconds Async, non-blocking
Schedule evaluation < 1ms Per loop iteration
Config save (atomic) ~50ms Flash write time
Web page serve < 50ms From PROGMEM
JSON API response 10–50ms String building

12.3 Optimization Techniques

Technique Applied To
PROGMEM storage All HTML/CSS pages
String::reserve() JSON response building
Stack defragmentation Hourly heap cleanup
Light sleep WiFi buffer release during low memory
Targeted JSON parsing Schedule save endpoint
Atomic writes Config persistence
Async scanning WiFi network discovery
StaticJsonDocument Simple API endpoints
Pointer-based config Relay configuration arrays

12.4 Loop Timing

```
Main loop cycle:
├── Factory reset button check     (~50μs)
├── Recovery execution (if pending) (~variable)
├── DNS server processing          (~100μs)
├── Web server client handling     (~1-5ms if request)
├── mDNS update (if active)        (~500μs)
├── AP restart processing          (~10μs)
├── LED update                     (~10μs)
├── Health checks (periodic)       (~variable)
├── WiFi connection management     (~100μs)
├── NTP sync (periodic)            (~1-5s, blocking)
├── Schedule processing            (~100μs per relay)
└── Config flush (periodic)        (~50ms)
```

· Average idle loop: < 5ms
· Peak loop (with NTP sync): 1–5 seconds (infrequent)

---

13. API Reference

13.1 Relay API

GET /api/relays

Response: Array of relay objects

```json
[{
  "name": "Relay 1",
  "state": true,
  "manual": false,
  "schedules": [{
    "startHour": 8, "startMinute": 0, "startSecond": 0,
    "stopHour": 18, "stopMinute": 0, "stopSecond": 0,
    "enabled": true, "days": 127, "monthDays": 0
  }]
}]
```

POST /api/relay/manual

Body: {"relay": 0, "state": true}

Response: {"success": true}

POST /api/relay/reset

Body: {"relay": 0}

Response: {"success": true}

POST /api/relay/save

Body: {"relay": 0, "schedules": [...]}

Response: {"success": true}

POST /api/relay/name

Body: {"relay": 0, "name": "Pump"}

Response: {"success": true}

13.2 WiFi API

GET /api/wifi

Response:

```json
{
  "ssid": "MyNetwork",
  "connected": true,
  "ip": "192.168.1.100",
  "rssi": -55
}
```

POST /api/wifi

Body: {"ssid": "MyNetwork", "password": "pass1234"}

Response: {"success": true}

POST /api/wifi/scan (Start)

Response: {"scanning": true}

GET /api/wifi/scan (Results)

Response:

```
{
  "scanning": false,
  "networks": [
    {"ssid": "MyNetwork", "rssi": -55, "enc": true}
  ]
}
```

13.3 Time API

GET /api/time

Response:

```
{
  "time": "14:30:45",
  "wifi": true,
  "ntp": true
}
```
```

POST /api/time/browser-sync

Body: {"epoch": 1700000000}

Response: {"success": true}
```
GET /api/ntp

Response:

```json
{
  "ntpServer": "nts.netnod.se",
  "gmtOffset": 28800,
  "daylightOffset": 0,
  "syncHours": 1
}
```
```

POST /api/ntp

Body: {"ntpServer": "pool.ntp.org", "gmtOffset": 0, "daylightOffset": 3600, "syncHours": 6}

Response: {"success": true}

POST /api/ntp/sync

Response: {"success": true} or {"success": false}
```

13.4 AP API

GET /api/ap

Response:

```
{
  "ap_ssid": "ESP8266_8CH_Timer_Switch",
  "ap_password": "ESP8266-admin",
  "ap_channel": 6,
  "ap_hidden": false
}
```
```

POST /api/ap

Body: {"ap_ssid": "MyAP", "ap_password": "pass1234", "ap_channel": 11, "ap_hidden": true}

Response: {"success": true}
```

13.5 Pins API

GET /api/pins

Response:

```
{
  "numRelays": 8,
  "globalActiveLow": true,
  "relays": [
    {"name": "Relay 1", "pin": 16, "activeLow": true}
  ]
}
```
```

POST /api/pins

Body: Full pin configuration object

Response: {"success": true, "softReset": true}

POST /api/pins/reset

Response: {"success": true, "message": "GPIO pins reset to default"}
```

13.6 System API

GET /api/system

Response:

```
{
  "hostname": "esp8266relay",
  "ip": "192.168.1.100",
  "ap_ip": "192.168.4.1",
  "uptime": 86400,
  "freeHeap": 20480,
  "ntpSynced": true,
  "ntpServer": "nts.netnod.se",
  "ntpSyncAge": 300,
  "wifiConnected": true,
  "wifiSSID": "MyNetwork",
  "rssi": -55,
  "mdnsHostname": "esp8266relay.local",
  "version": 7
}
```
```

POST /api/system

Body: {"hostname": "myrelay"}

Response: {"success": true}

POST /api/reset

Response: {"success": true, "recovery": "wifi_reset"}

POST /api/factory-reset

Response: {"success": true} (Device restarts after 500ms)
```
---

14. Deployment Scenarios

14.1 Home Automation

· Lighting control: Schedule indoor/outdoor lights by time and day
· Garden irrigation: Water pumps with day-of-week schedules
· Aquarium management: Light/heater cycles with overnight support
· Holiday lighting: Month-day schedules for seasonal decorations
· Appliance timers: Coffee makers, fans, humidifiers

14.2 Business Automation

· Office lighting: Mon–Fri business hours with weekend override
· Signage control: Store hours illumination
· HVAC auxiliary: Ventilation fan scheduling
· Security lighting: Overnight schedules with randomization potential
· Water feature control: Fountain pumps during business hours

14.3 Farm/Agricultural

· Irrigation zones: Multiple pumps on staggered schedules
· Greenhouse lighting: Photoperiod control with day-of-month seasonal adjustment
· Livestock: Automatic feeder/waterer timing
· Pond aeration: Nighttime aeration cycles
· Frost protection: Heater activation based on time windows

14.4 Network Topology Options

```
Option A: Standalone AP
┌──────────┐
│  ESP8266   │◄── WiFi ──► Mobile/PC
│  (AP)      │
└──────────┘

Option B: LAN Connected
┌──────────┐     ┌──────────┐
│  Router    │◄───►│  ESP8266  │◄── WiFi ──► Mobile/PC
│            │     │ (AP+STA)   │
└──────────┘     └──────────┘

Option C: Mesh-Ready
┌──────────┐     ┌──────────┐     ┌──────────┐
│  Router    │◄───►│ ESP8266   │     │ ESP8266   │
│            │     │ #1 (STA)   │     │ #2 (AP)   │
└──────────┘     └──────────┘     └──────────┘
                      │                  │
                 Relay Bank #1      Relay Bank #2
```

14.5 Long-Term Reliability Features

Feature Benefit
Atomic config writes No corruption on power loss
Watchdog timer Recovers from hangs
Drift-compensated RTC Accurate scheduling without constant NTP
Rate-limited reconnection Prevents network flooding
Self-healing stack 6-tier automatic recovery
State backup Relay states survive crashes
Memory cleanup Prevents heap fragmentation
FS health checks Detects storage corruption early

---

Appendix A: Default Configuration

```
WiFi Station:
  SSID: ""
  Password: ""

Access Point:
  SSID: "ESP8266_8CH_Timer_Switch"
  Password: "ESP8266-admin"
  Channel: 6
  Hidden: No

NTP:
  Server: "nts.netnod.se"
  GMT Offset: +28800 (UTC+8)
  DST Offset: 0
  Sync Interval: 1 hour

Relays:
  Count: 8
  Logic: Active-LOW (global)
  Pins: D0, D1, D2, D5, D6, D7, RX, TX
  Names: "Relay 1" through "Relay 8"
  Schedules: All disabled, all days selected

System:
  Hostname: "esp8266relay"
  mDNS: enabled
  LED: GPIO 2, Active-LOW
  Factory Reset Button: GPIO 0, 5-second hold
```

Appendix B: GPIO Pin Reference Card

```
     ESP8266 Pinout (NodeMCU)
    ┌─────────────────────┐
    │        ANTENNA          │
    ├────┬──┬──┬──┬──┬──┬─┤
    │    │  │  │  │  │  │    │
    │ D0 │  │  │  │  │  │    │  ← Relay 1 (default)
    │ D1 │  │  │  │  │  │    │  ← Relay 2
    │ D2 │  │  │  │  │  │    │  ← Relay 3
    │ D3 │  │  │  │  │  │    │  ← Factory Reset Button
    │ D4 │  │  │  │  │  │    │  ← Status LED (built-in)
    │ 3V │  │  │  │  │  │    │
    │ G  │  │  │  │  │  │    │
    │ D5 │  │  │  │  │  │    │  ← Relay 4
    │ D6 │  │  │  │  │  │    │  ← Relay 5
    │ D7 │  │  │  │  │  │    │  ← Relay 6
    │ RX │  │  │  │  │  │    │  ← Relay 7
    │ TX │  │  │  │  │  │    │  ← Relay 8
    │ G  │  │  │  │  │  │    │
    │ 5V │  │  │  │  │  │    │
    └────┴──┴──┴──┴──┴──┴─┘
```


</details>
