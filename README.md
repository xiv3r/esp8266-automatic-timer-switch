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
IN8 _____ RX
IN9 _____ TX
GND _____ GND
```

<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image1.png">
<img src="https://github.com/xiv3r/esp8266-automatic-timer-switch/blob/main/images/image2.png">

<details><summary>

# Full Features
</summary>

---

## 🚀 Core Features

### 1. Relay Management
- **9-Channel Control**: Independently control up to 9 relays (expandable to 16)
- **Manual Override**: Direct ON/OFF control via web interface
- **Auto Mode**: Return to schedule-based control
- **Custom Naming**: Each relay can be renamed via double-click
- **Active LOW/HIGH**: Configurable logic per relay or globally
- **State Persistence**: All settings survive power cycles

### 2. Advanced Scheduling System
- **8 Schedules Per Relay**: Each relay supports up to 8 independent schedules
- **24-Hour Time Format**: Precise scheduling with hours, minutes, and seconds
- **Day of Week Selection**: Individual day selection (Sun-Sat)
- **Day of Month Selection**: Specific days (1-31) for monthly schedules
- **Overnight Support**: Schedules spanning midnight (e.g., 22:00 to 06:00)
- **Always ON Mode**: Set same start/stop time for continuous operation
- **Schedule Overlap**: Multiple schedules can control the same relay

### 3. Time Management
- **NTP Synchronization**: Automatic time sync from multiple servers
- **Fallback NTP Servers**: 
  - ph.pool.ntp.org (primary)
  - pool.ntp.org
  - time.nist.gov
  - time.google.com
- **Browser Time Sync**: Manual sync from browser's clock
- **GMT Offset**: Configurable timezone offset
- **Daylight Saving**: Separate DST offset support
- **RTC with Drift Compensation**: Internal clock with automatic calibration
- **Configurable Sync Interval**: 1-24 hours auto-sync

### 4. WiFi Connectivity
- **Station Mode**: Connect to existing WiFi network
- **Access Point**: Built-in AP for direct connection
- **WiFi Scan**: Scan and display available networks
- **Auto Reconnect**: Automatic reconnection with cooldown
- **Connection Rate Limiting**: Prevents excessive reconnection attempts
- **Signal Strength Display**: RSSI with visual bars

### 5. Web Interface
- **Responsive Design**: Mobile-friendly interface
- **6 Main Pages**:
  - **Relays**: Main control and scheduling
  - **WiFi**: Network settings and scanning
  - **Time**: NTP configuration and sync
  - **AP**: Access point settings
  - **Pins**: GPIO pin configuration
  - **System**: Device information and controls
- **Real-time Clock**: Live time display in header
- **Status Indicators**: WiFi and NTP sync status
- **Toast Notifications**: Success/error feedback
- **Captive Portal**: Automatic redirect for new connections

### 6. Dynamic GPIO Management
- **9 Valid GPIO Pins**: D0(16), D1(5), D2(4), D3(0), D5(14), D6(12), D7(13), RX(3), TX(1)
- **Per-Relay Pin Assignment**: Each relay can use any available pin
- **Global Logic Setting**: Active LOW/HIGH for all relays
- **Per-Relay Logic**: Individual logic setting per relay
- **Visual Pin Map**: Shows available and used pins
- **Add/Remove Relays**: Dynamically add up to 16 relays
- **Reserved Pins Protection**: D4(GPIO2) and D8(GPIO15) are reserved

### 7. System Features
- **mDNS Support**: Accessible via hostname.local
- **Configurable Hostname**: Custom hostname for network identification
- **Status LED**: Built-in LED for status indication
  - Fast blink: WiFi disconnected
  - Slow blink: No NTP sync
  - Solid: All systems normal
- **System Information**:
  - Free heap memory
  - Uptime counter
  - WiFi signal strength
  - NTP sync status
  - Last sync time

### 8. Data Persistence
- **LittleFS Storage**: All settings stored in flash memory
- **Atomic Writes**: Safe file operations with .tmp files
- **4 Configuration Files**:
  - `/system.cfg`: WiFi, NTP, hostname settings
  - `/ext.cfg`: AP channel, sync interval, visibility
  - `/relays.cfg`: All relay schedules and names
  - `/pins.cfg`: GPIO pin assignments
- **Auto-Save**: Periodic config save with dirty flag
- **Low Memory Protection**: Prevents save when heap is low

### 9. Security & Stability
- **Production Constants**:
  - Minimum heap for save: 5120 bytes
  - Minimum heap for NTP: 8192 bytes
  - Max reconnect attempts: 20 per hour
  - Reconnect cooldown: 1 hour
- **Watchdog Prevention**: Strategic yield() calls
- **Error Handling**: Comprehensive edge case management
- **Factory Reset**: Complete settings wipe option
- **Graceful Degradation**: Continues operating without WiFi

### 10. API Endpoints
- **GET /api/relays**: List all relays and schedules
- **POST /api/relay/manual**: Manual relay control
- **POST /api/relay/reset**: Reset to auto mode
- **POST /api/relay/save**: Save schedule config
- **POST /api/relay/name**: Rename relay
- **GET /api/time**: Current time and status
- **POST /api/time/browser-sync**: Browser time sync
- **GET /api/wifi**: WiFi status
- **POST /api/wifi**: Save WiFi settings
- **POST /api/wifi/scan**: Start WiFi scan
- **GET /api/wifi/scan**: Get scan results
- **GET /api/ntp**: NTP settings
- **POST /api/ntp**: Save NTP settings
- **POST /api/ntp/sync**: Force NTP sync
- **GET /api/ap**: AP settings
- **POST /api/ap**: Save AP settings
- **GET /api/pins**: Pin configuration
- **POST /api/pins**: Save pin config
- **GET /api/system**: System info
- **POST /api/system**: Save hostname
- **POST /api/reset**: Restart device
- **POST /api/factory-reset**: Factory reset

---

## 🔧 Technical Specifications

### Hardware Requirements
- ESP8266 (NodeMCU, Wemos D1, etc.)
- 9-channel relay module (active LOW)
- GPIO pins: D0, D1, D2, D3, D5, D6, D7, RX, TX
- Status LED on D4 (GPIO2)

### Software Stack
- ESP8266 Arduino Core
- Libraries:
  - ESP8266WiFi
  - ESP8266mDNS
  - NTPClient
  - ESP8266WebServer
  - DNSServer
  - LittleFS
  - ArduinoJson

### Memory Usage
- Configuration size: ~2KB
- Web interface: ~15KB (PROGMEM)
- Free heap monitoring with auto-protection

---

## 📱 Default Settings

| Setting | Default Value |
|---------|---------------|
| AP SSID | ESP8266_9CH_Timer_Switch |
| AP Password | ESP8266-admin |
| AP Channel | 6 |
| NTP Server | ph.pool.ntp.org |
| GMT Offset | +28800 (UTC+8) |
| DST Offset | 0 |
| Sync Interval | 1 hour |
| Hostname | esp8266relay |
| Relay Logic | Active LOW |
| Number of Relays | 9 |

---

## 🎯 Special Features

### Schedule Engine
- Handles normal schedules (08:00-17:00)
- Handles overnight schedules (22:00-06:00)
- Handles always-on schedules (same start/stop)
- Day of week masking (128 combinations)
- Day of month masking (31 days)
- Timezone-aware comparison

### WiFi Manager
- Non-blocking reconnection
- Rate-limited attempts
- Async network scanning
- Captive portal support
- Auto-reconnect with backoff

### Time Management
- RTC with drift compensation (0.95-1.05 range)
- Persists time across reboots
- Supports multiple NTP sources
- Browser-based time sync as fallback
- Applies timezone offset for local time

---

## 🔄 Operation Modes

1. **Normal Operation**: Connected to WiFi with NTP sync
2. **AP Mode**: Own access point when no WiFi configured
3. **Hybrid Mode**: Both STA and AP active simultaneously
4. **Offline Mode**: Continues with internal RTC if WiFi lost

---

## 🛡️ Error Recovery

- Memory protection prevents corruption
- Atomic file writes prevent partial saves
- NTP server rotation on failure
- WiFi reconnection with exponential backoff
- Watchdog-friendly yield() placement
- Factory reset option for recovery

---

## 📊 Performance

- Non-blocking operations throughout
- Efficient JSON parsing
- Optimized file I/O
- Minimal heap fragmentation
- Quick web interface response
- Schedule processing every 100ms

---

</details>
