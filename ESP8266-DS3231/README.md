# Requirements 
- ESP8266 v1. 12E 160MHZ NodeMCU
- 5v 1-9 Channel Relay
- Dupont Wire
- DS3231 RTC Module
- 5v 2-3a Power Supply

# Arduino Libraries
- ArduinoJson
- Preferences
- NTPClient
- [RTCLib](https://codeload.github.com/adafruit/RTClib/zip/refs/tags/1.14.1) 1.14.1

# Installation
- Download the firmware and flasher

- Flash offset address 
```
firmware: 0x0
```

# WiFi Key
- WiFi SSID: `ESP8266_9CH_Timer_Switch`
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
IN2 _____ D3
IN3 _____ D4
IN4 _____ D5
IN5 _____ D6
IN6 _____ D7
IN7 _____ D8
IN8 _____ RX
IN9 _____ TX
GND _____ GND
```
# DS3231 RTC Module 
```
RTC | ESP8266
SDA → D2
SCL → D1
VCC → 3.3V
GND → GND
```

# Full Features

## 🔌 Hardware Features

### Relay Configuration
- **9 Independent Channels** controlling 9 relays via dedicated GPIO pins
- **Active-Low Operation** support (configurable)
- **Pin Mapping**: D0 (GPIO16), D3 (GPIO0), D4 (GPIO2), D5 (GPIO14), D6 (GPIO12), D7 (GPIO13), D8 (GPIO15), GPIO3 (RX), GPIO1 (TX)
- **I2C Reserved Pins**: D1/GPIO5 (SCL) and D2/GPIO4 (SDA) dedicated to DS3231
- **Safe Boot State**: All relays default to OFF during initialization
- **WARNING**: D3 (GPIO0) and D4 (GPIO2) are boot-mode pins; external pull-up resistors required

### DS3231 Hardware RTC
- **High Precision**: Temperature-compensated crystal oscillator (±2ppm accuracy)
- **Battery Backup**: CR2032 battery maintains time during power outages
- **Automatic Power-Loss Detection**: Sets compile time if RTC lost power
- **I2C Interface**: Connected via D1/GPIO5 (SCL) and D2/GPIO4 (SDA)
- **Temperature Compensation**: ±0.432 sec/day typical accuracy
- **Periodic Sync**: Software RTC synced from DS3231 every 60 seconds

---

## 💾 Advanced Storage System

### LittleFS with Atomic Operations
- **File-Based Storage**: Three separate configuration files for modular management
  - `/system.cfg`: System and WiFi configuration
  - `/ext.cfg`: Extended configuration (AP, NTP intervals)
  - `/relays.cfg`: Relay configurations and schedules
- **Atomic Writes**: Temp-file + rename strategy prevents corruption on power loss
  - Write to `.tmp` file
  - Flush and verify file size
  - Atomic rename to final path
  - Automatic cleanup of failed writes
- **Deferred Writes**: Config changes batched and written every 10 seconds
  - Reduces flash wear from frequent writes
  - `markConfigDirty()` + `flushConfigIfNeeded()` pattern
  - Manual flush on restart/factory reset
- **Power-Loss Protection**: Incomplete writes detected and discarded
  - File size verification after write
  - Temp file cleanup on detection
  - No partial configurations survive

### Storage Reliability Features
- **Magic Number Validation**: 0x1234 for system config, 0xEC for extended config
- **Version Migration**: Automatic schema updates (currently v6)
- **Backward Compatibility**: 
  - Empty day masks default to all days (0x7F)
  - Empty month days default to not used (0)
  - Empty relay names default to "Relay N"
- **Automatic Formatting**: LittleFS format if mount fails

---

## ⏰ Time Synchronization & Scheduling

### Multi-Source Time Architecture
- **Priority 1**: DS3231 Hardware RTC (battery-backed, always available)
- **Priority 2**: Internal Software RTC (NTP-synced with drift compensation)
- **Priority 3**: Compile-time fallback for initial RTC setup
- **NTP → DS3231**: Hardware RTC updated with NTP time for maximum accuracy

### DS3231 Integration
- **60-Second Sync Cycle**: Software RTC regularly validated against DS3231
- **Power-Loss Recovery**: Automatic compile-time setting if RTC lost power
- **Bidirectional Sync**: NTP updates DS3231, DS3231 corrects software RTC
- **Seamless Operation**: No internet required for accurate timekeeping

### NTP Client
- **Primary Server**: `ph.pool.ntp.org` (default, configurable)
- **Fallback Chain**: 
  1. Custom/primary server
  2. `pool.ntp.org`
  3. `time.nist.gov`
  4. `time.google.com`
- **Automatic Failover**: Rotates through servers on failure
- **Configurable Sync Interval**: 1-24 hours
- **Manual Sync Trigger**: Force synchronization via web UI
- **GMT Offset**: Configurable timezone offset (default: UTC+8/28800s)
- **Daylight Saving**: Secondary offset for DST adjustments

### Internal RTC
- **Drift Compensation**: Adaptive algorithm with ±10% bounds
- **ESP8266 Optimized**: Floating-point calculations optimized for 80MHz CPU
- **Persistent Storage**: Epoch and drift saved to LittleFS
- **Dual-Source Validation**: Cross-reference between NTP and DS3231

---

## 📅 Per-Relay Scheduling

### Time-Based Control
- **8 Independent Schedules** per relay channel
- **Time-of-Day Precision**: Second-level granularity (HH:MM:SS)
- **Day-of-Week Selection**: 7-bit bitmask with individual day toggles
  - Bit 0: Sunday, Bit 1: Monday, ... Bit 6: Saturday
  - Default: 0x7F (all days)
- **Day-of-Month Selection**: 31-bit bitmask for monthly scheduling
  - Bit 0: 1st, Bit 1: 2nd, ... Bit 30: 31st
  - Default: 0 (not used/filter disabled)
- **Combined Filtering**: Both day-of-week AND day-of-month must match

### Schedule States
- **Standard Schedule**: Start time < Stop time (same-day operation)
- **Overnight Schedule**: Start time > Stop time (spans midnight)
- **24/7 Always ON**: Start time = Stop time with schedule enabled
- **Early Exit Optimization**: Loop breaks on first matching schedule

---

## 🌐 Network Features

### WiFi Connectivity (Station Mode)
- **Automatic Reconnection**: Non-blocking state machine
  - Maximum 10 reconnection attempts
  - 5-minute cooldown after failed attempts
  - `WifiConnState` enum: WCS_IDLE, WCS_PENDING
- **WiFi Network Scanner**: Async scanning with callback
  - Uses `WiFi.scanNetworksAsync()` for non-blocking operation
  - Maximum 20 networks returned
  - Signal strength bars (1-4) in web UI
  - Lock indicator for encrypted networks
- **Connection Status**: Real-time indicators (Green/Red dots)

### Access Point (AP)
- **Built-in Hotspot**: `ESP8266_9CH_Timer_Switch` (default)
- **Customizable**: Configurable SSID, password, channel (1-13)
- **Hidden SSID Support**: Option to hide network broadcast
- **Open Network Support**: Passwordless operation
- **Captive Portal**: Automatic redirection for easy configuration
  - DNS-based redirection (port 53)
  - Supports Android, iOS, Windows captive portal detection
  - Multiple detection endpoints

### mDNS (Bonjour/Avahi)
- **Local Network Discovery**: `esp8266relay.local` (default)
- **Custom Hostname**: Configurable via web UI or API
- **Automatic Hostname Sanitization**: Lowercase, alphanumeric + hyphens only
- **Service Advertisement**: HTTP service on port 80
- **WiFi-Dependent**: Only active when connected to station network

---

## 🎛️ Control Features

### Relay Control Modes
- **Automatic Mode**: DS3231-backed schedule-based operation
- **Manual Override**: Direct ON/OFF control via web interface
- **Named Relays**: Custom 15-character names (double-click to edit)
- **Visual Status**: Color-coded badges (Green=ON, Red=OFF, Orange=MANUAL)

### Web Interface Controls
- **Per-Relay Buttons**: ON/OFF/Auto for each channel
- **Bulk Schedule Editor**: Visual day/month toggles with real-time preview
- **Schedule Badges**: 
  - 🌙 Overnight indicator for midnight-spanning schedules
  - ● Always ON indicator for 24/7 schedules
  - Day-of-week and day-of-month summary
- **Inline Name Editing**: Double-click relay name to rename
- **Toast Notifications**: Operation feedback with color coding
- **ESP8266 Optimized**: String concatenation instead of ArduinoJson for memory efficiency

---

## 🌍 Web Interface

### Responsive Design (Memory Optimized)
- **Mobile-First Layout**: Optimized for phones and tablets
- **Adaptive Grid**: Auto-fill relay cards (340px minimum)
- **Sticky Header**: Navigation always accessible
- **PROGMEM Storage**: All HTML/CSS stored in flash memory
- **String Concatenation**: API responses built manually to save heap

### Pages & Sections

1. **Relays Dashboard** (`/`)
   - 9 relay cards with status badges
   - Schedule editor per relay (8 schedules each)
   - Manual control buttons
   - Inline relay renaming
   - Auto-refresh every 60 seconds
   - Real-time clock in header

2. **WiFi Settings** (`/wifi`)
   - Network scanner with RSSI visualization
   - Signal strength bars (1-4 bars)
   - Connection status with IP display
   - Credential management
   - Lock indicator for encrypted networks

3. **Time Settings** (`/ntp`)
   - NTP server configuration
   - Timezone/GMT offset adjustment
   - Daylight saving offset
   - Sync interval control (1-24 hours)
   - Manual sync trigger

4. **AP Settings** (`/ap`)
   - Hotspot SSID/password configuration
   - Channel selection (1-13)
   - Visibility toggle (hidden/broadcast)
   - Warning about client disconnection

5. **System Info** (`/system`)
   - Network diagnostics (STA IP, AP IP)
   - Resource monitoring (Free Heap, Uptime)
   - WiFi RSSI with quality description
   - NTP sync status with age
   - Hostname configuration
   - Device restart/factory reset

### ESP8266-Specific Optimizations
- **Minimal JSON Parsing**: Custom `extractJson*()` functions
  - `extractJsonInt()`: Integer extraction
  - `extractJsonBool()`: Boolean extraction
  - `extractJsonByte()`: Byte extraction
  - `extractJsonUInt32()`: 32-bit unsigned extraction
- **Avoids ArduinoJson**: Used only for WiFi/AP save handlers
- **String Pre-allocation**: `resp.reserve()` for known sizes
- **Yield Calls**: `yield()` throughout to prevent watchdog resets

---

## 🔧 API Endpoints

### Relay Management
| Endpoint | Method | Parser | Description |
|----------|--------|--------|-------------|
| `/api/relays` | GET | Manual | Get all 9 relay states and schedules |
| `/api/relay/manual` | POST | ArduinoJson | Set manual ON/OFF state |
| `/api/relay/reset` | POST | ArduinoJson | Return to automatic mode |
| `/api/relay/save` | POST | Manual Parser | Save schedule configuration |
| `/api/relay/name` | POST | ArduinoJson | Update relay name |

### Time & Network
| Endpoint | Method | Parser | Description |
|----------|--------|--------|-------------|
| `/api/time` | GET | Manual | Current time, WiFi/NTP status |
| `/api/wifi` | GET/POST | ArduinoJson | WiFi configuration & status |
| `/api/wifi/scan` | POST | - | Start async WiFi scan |
| `/api/wifi/scan` | GET | ArduinoJson | Poll scan results (20 max) |
| `/api/ntp` | GET/POST | ArduinoJson | NTP settings management |
| `/api/ntp/sync` | POST | - | Force NTP synchronization |
| `/api/ap` | GET/POST | ArduinoJson | Access point configuration |

### System Management
| Endpoint | Method | Parser | Description |
|----------|--------|--------|-------------|
| `/api/system` | GET | Manual | System diagnostics |
| `/api/system` | POST | ArduinoJson | Update hostname |
| `/api/reset` | POST | - | Restart device (flushes config) |
| `/api/factory-reset` | POST | - | Delete all files & restart |

### Captive Portal Endpoints
| Endpoint | Purpose |
|----------|---------|
| `/hotspot-detect.html` | Android captive portal detection |
| `/library/test/success.html` | Apple CNA detection |
| `/generate_204` | Android/Chrome detection |
| `/success.txt` | Firefox detection |
| `/canonical.html` | Apple detection |
| `/connecttest.txt` | Windows detection |
| `/ncsi.txt` | Windows NCSI detection |
| `/redirect` | Generic redirect |
| `/*` (catch-all) | Any unknown path → AP home |

---

## 🛡️ Safety & Reliability

### Hardware Protection
- **Safe Initialization**: All 9 relay pins set to OFF state before setup
- **Boot Pin Awareness**: D3 (GPIO0) and D4 (GPIO2) documented as boot-critical
- **Watchdog Friendly**: `yield()` called in loops and processing
- **Timeout Protection**: 
  - WiFi connection: 15 seconds
  - NTP retry: 30 seconds
  - Config save: 10 second batching

### Storage Reliability
- **Atomic Writes**: Never corrupts configuration on power loss
- **Three-File Architecture**: System, Ext, Relay configs separated
- **Backward Compatibility**: Loads v5 configs, migrates to v6
- **Automatic Recovery**: 
  - Corrupt files → initDefaults()
  - Missing files → initDefaults()
  - Wrong magic → initDefaults()
  - Failed LittleFS mount → format + retry

### Time Reliability
- **Triple Redundancy**: DS3231 HW RTC → Software RTC → NTP
- **Battery Backup**: DS3231 maintains time during power loss
- **Graceful Degradation**: 
  - WiFi down → DS3231 keeps time
  - DS3231 failure → Software RTC continues
  - Complete RTC failure → Schedule engine disabled
- **Drift Bounding**: ±10% compensation limits

### Software Resilience
- **Deferred Writes**: Reduces flash wear from frequent saves
- **Memory-Safe**: String concatenation avoids heap fragmentation
- **Non-Blocking Architecture**: 
  - Async WiFi scanning
  - State machine-based STA reconnection
  - Deferred config saves
- **Crash Prevention**: `yield()` in all loops

---

## 📊 Monitoring & Diagnostics

### System Metrics (System Page)
- **Free Heap Memory**: Real-time RAM usage (KB)
- **Uptime Counter**: Device runtime (hours:minutes:seconds)
- **WiFi RSSI**: Signal strength with quality descriptions:
  - ≥ -50 dBm: Excellent
  - -60 to -50 dBm: Good
  - -70 to -60 dBm: Fair
  - < -70 dBm: Weak
- **NTP Sync Age**: Time since last synchronization
- **mDNS Status**: Hostname with .local display

### Time Sources Status
- **DS3231**: Available/Unavailable
- **Software RTC**: Initialized/Uninitialized
- **NTP**: Synced/Not synced with server info
- **Drift Factor**: Current compensation value (0.90-1.10)

### Storage Status
- **Config File**: Size verification on load
- **Dirty Flag**: Pending writes indicator
- **Last Save**: 10-second batching timer
- **File System**: LittleFS with automatic formatting

---

## 🔄 Advanced Features

### Power Management (ESP8266 Optimized)
- **WiFi Power Save**: Implicit through non-blocking operations
- **Connection Backoff**: 10 attempts with 5-minute cooldown
- **Resource Optimization**: 
  - Config writes batched to 10 seconds
  - DS3231 sync every 60 seconds
  - WiFi check every 5 seconds
  - NTP sync configurable (1-24 hours)
- **Heap Conservation**: String concatenation over JSON libraries

### Storage Optimization
- **Three-File Separation**: 
  - System config changes → only system.cfg written
  - Schedule changes → only relays.cfg written
  - AP/interval changes → only ext.cfg written
- **Deferred Writing**: Multiple rapid changes batched into one write
- **Size Verification**: Prevents partial writes from being committed

### Security
- **Password Protection**: Configurable AP authentication (WPA2)
- **Hidden SSID**: Reduce network visibility
- **No Hardcoded Credentials**: All settings user-configurable
- **Captive Portal Isolation**: Limited attack surface

### Extensibility
- **JSON API**: Easy integration with home automation systems
- **mDNS Discovery**: Zero-configuration network finding
- **Version Tracking**: Forward-compatible configurations (v6+)
- **Modular Design**: Clear separation of concerns
- **Extended Config**: 28 reserved bytes for future features

### ESP8266-Specific Features
- **Watchdog Timer**: Automatic reset on hang (6-second timeout)
- **Yield Management**: Prevents software watchdog resets
- **PROGMEM Optimization**: HTML/CSS in flash, not RAM
- **WiFi Auto Connect**: SmartConfig compatible (not implemented)
- **Deep Sleep Ready**: Pin states preserved during sleep modes

---

## 📱 User Experience

### Initial Setup Flow
1. Power on ESP8266 → Default AP activates
2. Connect to `ESP8266_9CH_Timer_Switch` network
3. Captive portal automatically redirects to configuration
4. Configure WiFi station settings
5. Set timezone and NTP preferences
6. (Optional) Customize AP and hostname settings
7. Create relay schedules via web interface
8. Device connects to network and syncs time
9. DS3231 maintains time across reboots and power loss

### Daily Operation
- Schedules run automatically using DS3231 time
- Manual overrides via web/mobile browser
- Status monitoring at a glance
- Zero-touch after initial configuration
- Battery-backed RTC survives power outages (years on CR2032)

### Recovery Options
- **Factory Reset**: Web UI or API endpoint
  - Deletes all three config files
  - Reboots to default AP
- **WiFi Recovery**: 
  - Automatic reconnection with backoff
  - Fallback AP always available
  - Five-minute cooldown prevents rapid cycling
- **Time Recovery**:
  - DS3231 continues during network outages
  - NTP resync after extended offline periods
  - Compile-time fallback if RTC battery died

### Performance Characteristics
- **Typical Heap Free**: 20-30KB (varies with usage)
- **Relay Update Rate**: Every loop iteration (~1-10ms)
- **Web Server**: Single-threaded, handles one request at a time
- **WiFi Stack**: Shared with application, yield() critical
- **Flash Usage**: ~500KB firmware + LittleFS overhead
- **Config Size**: 
  - System.cfg: ~232 bytes
  - Ext.cfg: ~32 bytes
  - Relays.cfg: ~2,376 bytes (9 relays × 264 bytes)
