# About
The ESP8266 11-Channel Relay Smart Switch is an open-source, fully self-contained firmware that transforms a low-cost ESP8266 microcontroller into a professional WiFi-connected programmable timer. It can control up to 11 relays independently, with 8 customizable ON/OFF schedules per relay—giving you a total of 88 programmable time windows. Everything is managed through a modern, responsive web interface that runs entirely on the device, with no cloud dependency, no external server, and no mobile app required.

# Requirements 
- ESP8266 12E 160MHZ
- 5v 1-11 Channel Relay
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
- WiFi SSID: `ESP8266_11CH_Smart_Switch`
- Password: `ESP8266-admin`

# Activation
- Go to `wifi settings` and connect to your home wifi after the NTP is synchronized everything will work

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
RELAY     ESP8266
VCC _____ 5VIN 
IN1 _____ D0
IN2 _____ D1
IN3 _____ D2
IN4 _____ D3
IN5 _____ D4
IN6 _____ D5
IN7 _____ D6
IN8 _____ D7
GND _____ GND
```
- [ Extra Relay ]

⚠️ Undetermined problem from D8 so attach the relay connection to D8 10 seconds after the esp8266 boot (avoid power loss) otherwise don't use the D8.
```
VCC _____ 5VIN
IN1 _____ D8
IN2 _____ RX
IN3 _____ TX
GND _____ GND
```
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image2.png">

<details><summary>

# Full Features

</summary>

## 🌐 Network & Connectivity

### WiFi Station (STA) Mode
- Connects to existing WiFi networks (2.4GHz)
- Non-blocking automatic reconnection with state machine
- Configurable reconnection attempts (max 10) with 5-minute cooldown after failures
- WiFi status monitoring every 5 seconds
- Connection timeout handling (15 seconds)
- WiFi signal strength (RSSI) display with visual bars
- Network scanning with async API (up to 20 networks shown)
- Sorted by signal strength with encryption indicators

### Access Point (AP) Mode
- Simultaneous AP+STA mode support
- Configurable AP SSID (up to 31 characters)
- Optional AP password (WPA2, minimum 8 characters)
- Selectable WiFi channel (1-13)
- Hidden SSID option (disable broadcast)
- Captive portal support for easy configuration
- Fallback AP when STA connection fails

### DNS & Captive Portal
- Built-in DNS server (port 53)
- Redirects all DNS requests to AP IP
- Captive portal detection for Android, iOS, Windows
- Handles multiple portal detection endpoints (hotspot-detect.html, success.txt, etc.)
- Automatic redirect to configuration page

### mDNS (Bonjour/Avahi)
- Accessible via `[hostname].local` on local network
- Configurable hostname (lowercase, digits, hyphens)
- Auto-restart on hostname change
- HTTP service advertised on port 80

---

## ⏰ Time & Scheduling

### NTP Synchronization
- Primary NTP server configuration (default: ph.pool.ntp.org)
- Automatic fallback chain (4 servers):
  - ph.pool.ntp.org → pool.ntp.org → time.nist.gov → time.google.com
- Configurable GMT offset (seconds, e.g., 28800 for UTC+8)
- Configurable daylight saving offset (seconds)
- Adjustable sync interval (1-24 hours)
- Manual sync trigger via web UI
- Retry on failure every 30 seconds
- Connection status indicators

### Internal RTC (Real-Time Clock)
- Millis-based timekeeping with NTP calibration
- Drift compensation algorithm (moving average)
- Persists last known time to LittleFS
- Survives reboots without WiFi
- Drift bounds: 0.90x to 1.10x of nominal rate

### 8-Slot Timer Schedules (per relay)
- Start/stop time with second precision
- **Day of Week** selection (Sun-Sat) with individual toggle
- **Day of Month** selection (1-31) with individual toggle
- Combined Day-of-Week + Day-of-Month filtering

### Schedule Behaviors
- **Normal schedule**: ON between start and stop (same day)
- **Overnight schedule**: ON when stop time is earlier than start (spans midnight)
- **Always ON mode**: When start equals stop time, channel stays permanently ON during active days
- Empty day mask: Schedule disabled for that slot
- Empty month mask: All month days enabled
- Visual badges on web UI showing schedule type

### Time Display
- Live clock on every page (updates every second)
- Connection status dots (green/yellow/red)
- Time fetch API endpoint

---

## 🔌 Relay Control

### 11 Relay Channels
- GPIO pins: D0, D1, D2, D3, D4, D5, D6, D7, D8, GPIO3, GPIO1
- Configurable active-low logic (relay ON = LOW)
- Individual naming (up to 15 characters, double-click to edit inline)
- Manual override with ON/OFF buttons
- Auto mode (schedule-driven)

### Web Control Interface
- Real-time relay state feedback
- Grid layout (responsive, 1-3 columns)
- Color-coded badges (Green=ON, Red=OFF, Orange=MANUAL)
- Inline schedule editor with instant preview
- Individual save per relay
- Auto-refresh every 60 seconds

### Safety Features
- All relays initialized OFF on boot
- Manual override persists until explicitly reset
- Schedule processing yields to prevent watchdog triggers

---

## 💾 Configuration Management

### Persistent Storage (LittleFS)
Three configuration files with atomic writes:

1. **`/system.cfg`** - Main configuration (magic number: 0x1234, version 6)
   - WiFi STA credentials (SSID + password)
   - AP credentials
   - NTP server settings
   - Hostname
   - RTC drift/epoch data

2. **`/ext.cfg`** - Extended configuration (magic number: 0xEC)
   - AP channel
   - NTP sync interval
   - AP hidden flag

3. **`/relays.cfg`** - Relay schedules and names
   - 11 relays × 8 schedules each
   - Manual override states
   - Custom names

### Atomic File Operations
- Write to `.tmp` file first
- Verify write integrity (size check)
- Atomic rename to target path
- Power-loss protection for all config saves

### Configuration Optimization
- Delayed write strategy (10-second buffer)
- Dirty flag tracking
- Automatic flush in main loop
- Manual flush before restart/reset

### Factory Reset
- Removes all configuration files
- Restarts with default settings
- Default credentials: `ESP8266_11CH_Timer_Switch` / `ESP8266-admin`

---

## 🖥️ Web User Interface

### Responsive Design
- Mobile-friendly (max-width 500px breakpoint)
- Grid auto-fill layout
- Sticky header with navigation
- Toast notifications for actions

### Pages
1. **Relays** (`/`) - Main control panel with schedules
2. **WiFi** (`/wifi`) - Network scanning & STA configuration
3. **Time** (`/ntp`) - NTP settings & manual sync
4. **AP** (`/ap`) - Access point configuration
5. **System** (`/system`) - Device info, hostname, restart, factory reset

### Interactive Elements
- Inline relay renaming (double-click, Enter to save, Escape to cancel)
- Day/month toggles with visual feedback
- Time picker with seconds precision
- Network scanner with progress indication
- WiFi signal strength bars
- Confirmation dialogs for destructive actions

### Visual Design
- Material-inspired color scheme (#1565C0 primary blue)
- Smooth hover transitions
- Card-based layout with shadows
- Color-coded status indicators
- Custom scrollbar styling
- Monospace time inputs

---

## 🔧 API Endpoints

### Relay APIs
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/relays` | Get all relay states & schedules |
| POST | `/api/relay/manual` | Set manual ON/OFF |
| POST | `/api/relay/reset` | Return to auto mode |
| POST | `/api/relay/save` | Save schedules for one relay |
| POST | `/api/relay/name` | Rename a relay |

### WiFi APIs
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/wifi` | Get STA status & SSID |
| POST | `/api/wifi` | Save WiFi credentials & restart |
| POST | `/api/wifi/scan` | Start async network scan |
| GET | `/api/wifi/scan` | Get scan results |

### Time APIs
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/time` | Get current time & status dots |
| GET | `/api/ntp` | Get NTP configuration |
| POST | `/api/ntp` | Save NTP settings |
| POST | `/api/ntp/sync` | Trigger manual NTP sync |

### System APIs
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/system` | System info (IP, heap, uptime, etc.) |
| POST | `/api/system` | Save hostname |
| GET | `/api/ap` | Get AP configuration |
| POST | `/api/ap` | Save AP config & restart AP |
| POST | `/api/reset` | Restart device |
| POST | `/api/factory-reset` | Factory reset & restart |

### Captive Portal Endpoints
- `/hotspot-detect.html`, `/library/test/success.html`, `/generate_204`
- `/success.txt`, `/canonical.html`, `/connecttest.txt`, `/ncsi.txt`
- `/redirect`
- Catch-all: Redirects to AP IP

---

## 🛡️ Reliability Features

### Watchdog Protection
- Proper `yield()` calls in loops
- Non-blocking WiFi state machine
- Async network scanning

### Memory Optimization
- PROGMEM storage for HTML pages
- String concatenation with reserve()
- StaticJsonDocument for fixed-size parsing
- DynamicJsonDocument for scan results only

### Power-Loss Safety
- Atomic file writes with verification
- Delayed config saves
- Config flush before restart/reset

### Error Handling
- JSON validation on all POST endpoints
- Bounds checking on all array accesses
- NTP epoch validation (rejects invalid timestamps)
- File size verification on reads

---

## 📊 System Monitoring

### Dashboard Stats (Web UI)
- STA IP address & connection status
- AP IP address
- Free heap memory (KB)
- System uptime
- mDNS hostname
- WiFi RSSI with quality description (Excellent/Good/Fair/Weak)
- Last NTP sync time ("Just now" / "X min ago" / "Never")
- Active NTP server

### Live Updates
- Clock ticks every second
- System stats refresh every 5 seconds
- Relay grid auto-refresh every 60 seconds (when idle)
- Toast notifications with 3-second timeout

---

## 🔄 Boot Sequence

1. Initialize all relays OFF
2. Mount LittleFS (auto-format if first boot)
3. Load system configuration
4. Load extended configuration
5. Load relay configurations (with backward compatibility)
6. Restore RTC state
7. Attempt WiFi STA connection (15-second timeout)
8. Start NTP client (if WiFi connected)
9. Start mDNS responder (if WiFi connected)
10. Start AP (always)
11. Start DNS server
12. Start web server
13. Enter main loop

---

## 🎨 UI Color Scheme
- **Green** (#69F0AE): WiFi connected / Relay ON
- **Red** (#FF5252): WiFi disconnected / Relay OFF
- **Yellow** (#FFD740): NTP unsynchronized
- **Blue** (#1565C0): Primary actions / Active elements
- **Gray** (#546E7A): Neutral / Manual mode
- **Purple** (#7B1FA2): Month day selections
- **Amber** (#F9A825): Warnings
- **Dark Red** (#B71C1C): Danger actions

---

## 🔒 Security Considerations
- AP password minimum 8 characters (when set)
- STA password stored in LittleFS (not encrypted)
- No authentication required for web interface (local network only)
- Captive portal redirects to configuration
- Factory reset accessible without authentication

---

## 📝 Default Configuration
| Setting | Default Value |
|---------|---------------|
| AP SSID | `ESP8266_11CH_Timer_Switch` |
| AP Password | `ESP8266-admin` |
| AP Channel | 6 |
| AP Hidden | No |
| NTP Server | `ph.pool.ntp.org` |
| GMT Offset | 28800 (UTC+8) |
| DST Offset | 0 |
| NTP Sync Interval | 1 hour |
| mDNS Hostname | `esp8266relay` |
| Relay Names | "Relay 1" through "Relay 11" |
| All Schedules | Disabled, All days selected |

---

## 🚀 Advanced Features
- Overnight schedule detection with moon icon (🌙)
- Always-ON schedule with special badge
- Day-of-week + Day-of-month AND logic for complex schedules
- RTC drift learning and compensation
- Progressive NTP fallback on failures
- Non-blocking operations throughout (no `delay()` in production code)
- JSON API for potential third-party integration
- Atomic file system operations prevent corruption on power loss
</details>

# Other ESP8266 Board
- changes for sketch.ino
```
#define NUM_RELAYS 11 // Change 11 to number of relays supported by your board 

const int  relayPins[NUM_RELAYS] = { D0, D1, D2, D3, D4, D5, D6, D7, D8, 3, 1 }; // Reduce or Add GPIO based on your esp8266 board
```
