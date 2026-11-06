/*
===============================================================================
 *  ESP8266 6-Channel Relay Smart Switch w/ LED Status Indicator
 *  Author: Raff Alds
 *  Github: https://www.github.com/xiv3r
 *  License: GPLv3
===============================================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include <ctype.h>
#include <PubSubClient.h>
#include <WiFiClientSecureBearSSL.h>

// =============================================================================
//  64-bit epoch helpers
// =============================================================================
typedef int64_t epoch64_t;
static const epoch64_t EPOCH64_MIN_VALID = 1000000000LL;
static const epoch64_t EPOCH64_APP_MIN   = 1577836800LL;
static const epoch64_t EPOCH64_APP_MAX   = 4102444799LL;
static const epoch64_t EPOCH64_RTC_MIN   = 1700000000LL;
static const epoch64_t EPOCH64_NTP_MAX   = 4102444800LL;
static inline bool epoch64IsValid(epoch64_t v) {
    return (v >= EPOCH64_MIN_VALID && v <= EPOCH64_APP_MAX);
}
struct tm64 {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
};
static inline int64_t epoch64FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}
static inline int epoch64FloorMod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return (int)r;
}
static inline bool epoch64ToTmUtc(epoch64_t epoch, struct tm64* out) {
    if (!out) return false;
    int64_t secs = (int64_t)epoch;
    int64_t days = epoch64FloorDiv(secs, 86400LL);
    int64_t rem  = secs - days * 86400LL;
    out->tm_hour = (int)(rem / 3600); rem %= 3600;
    out->tm_min  = (int)(rem / 60);
    out->tm_sec  = (int)(rem % 60);
    int64_t z   = days + 719468LL;
    int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    int64_t doe = z - era * 146097LL;
    int64_t yoe = (doe - doe / 1460LL + doe / 36524LL - doe / 146096LL) / 365LL;
    int64_t y   = yoe + era * 400LL;
    int64_t doy = doe - (365LL * yoe + yoe / 4LL - yoe / 100LL);
    int64_t mp  = (5LL * doy + 2LL) / 153LL;
    int64_t d   = doy - (153LL * mp + 2LL) / 5LL + 1LL;
    int64_t m   = mp + (mp < 10LL ? 3LL : -9LL);
    if (m <= 2LL) y++;
    out->tm_year = (int)(y - 1900LL);
    out->tm_mon  = (int)(m - 1LL);
    out->tm_mday = (int)d;
    out->tm_wday = epoch64FloorMod(days + 4LL, 7LL);
    return true;
}

// =============================================================================
//  LittleFS
// =============================================================================
#define CONFIG_FILE    "/system.cfg"
#define EXTCONFIG_FILE "/ext.cfg"
#define RELAY_FILE     "/relays.cfg"
#define PINS_FILE      "/pins.cfg"

// =============================================================================
//  Magic numbers
// =============================================================================
#define EEPROM_MAGIC   0x1234
#define EEPROM_VERSION 9  
#define EXT_CFG_MAGIC  0xEC
#define PINS_MAGIC     0x50

// =============================================================================
//  Timing
// =============================================================================
static const unsigned long NTP_RETRY_INTERVAL   =   30000UL;
static const unsigned long WIFI_CHECK_INTERVAL  =    5000UL;
static const unsigned long WIFI_CONNECT_TIMEOUT =   15000UL;
static const unsigned long CONFIG_SAVE_MIN_INTERVAL = 30000UL;
static const unsigned long CONFIG_SAVE_INTERVAL =  300000UL;
static const unsigned long MAX_DRIFT_CALIBRATION_INTERVAL = 2592000000UL;
static const unsigned long PROACTIVE_MAINTENANCE_INTERVAL = 60000UL;
static const unsigned long GENTLE_NETWORK_HEAL_INTERVAL = 30000UL;
static const unsigned long HEAP_DEFRAG_INTERVAL = 1800000UL;
static const unsigned long CONNECTION_CLEANUP_INTERVAL = 300000UL; 
static const unsigned long DS3231_SYNC_INTERVAL = 3600000UL; 
static const unsigned long DEEP_HEAL_INTERVAL = 86400000UL;
static const unsigned long CRYSTAL_CALIBRATION_MIN_INTERVAL = 3600000UL;

// =============================================================================
//  Stability
// =============================================================================
static const unsigned long MIN_HEAP_FOR_SAVE    =   5120UL; 
static const unsigned long MIN_HEAP_FOR_NTP     =   8192UL;  
static const uint8_t       MAX_RECONNECT_ATTEMPTS = 20;     
static const unsigned long HEAP_CHECK_INTERVAL  = 1800000UL;
static const unsigned long SELF_CHECK_INTERVAL  = 86400000UL;
static const unsigned long TIME_CHECK_INTERVAL  = 60000UL;
static const uint8_t       API_RATE_LIMIT = 10;           
static const unsigned long RATE_LIMIT_WINDOW = 1000UL;    
static const unsigned long MIN_HEAP_FOR_FULL_RESPONSE = 8192UL;
static const unsigned long WEB_SERVER_RECOVERY_INTERVAL = 30000UL;
static const unsigned long CRITICAL_HEAP_THRESHOLD = 8192UL;
static const unsigned long EMERGENCY_HEAP_THRESHOLD = 5120UL;

// =============================================================================
//  LED
// =============================================================================
#define STATUS_LED_PIN       D4
#define STATUS_LED_ACTIVE_LOW true
static const unsigned long LED_BLINK_FAST = 200UL;
static const unsigned long LED_BLINK_SLOW = 1000UL;
static const unsigned long LED_BLINK_HEAL = 500UL;

// =============================================================================
//  Factory Reset Button
// =============================================================================
#define FACTORY_RESET_PIN    0  
static const unsigned long FACTORY_RESET_HOLD_MS = 5000UL;  
unsigned long factoryResetPressStart = 0;
bool factoryResetInProgress = false;
uint8_t factoryResetLEDStep = 0;
unsigned long lastFactoryResetBlink = 0;
bool          buttonHeld = false;

// =============================================================================
//  Non-blocking state machine enums
// =============================================================================
enum FactoryResetState { FR_IDLE, FR_INIT_LED, FR_DELETE_FILES, FR_CLEAR_CONFIG, 
                         FR_RESET_WIFI, FR_SETUP_AP, FR_DNS_RESTART, FR_WEB_RESTART, FR_COMPLETE };
FactoryResetState frState = FR_IDLE;
unsigned long frStepTimer = 0;
enum NTPState { NTP_IDLE, NTP_REQUESTING, NTP_PROCESSING };
NTPState ntpState = NTP_IDLE;
unsigned long ntpTimer = 0;
enum WiFiHealState { WH_IDLE, WH_DISCONNECT, WH_CONNECTING, WH_WAITING };
WiFiHealState whState = WH_IDLE;
unsigned long whTimer = 0;

// =============================================================================
//  Self-recovery states
// =============================================================================
enum SelfRecoveryState {
    SR_IDLE,
    SR_MEMORY_CLEANUP,
    SR_WIFI_RECONNECT,
    SR_WEB_SERVER_RECOVER,
    SR_DNS_RECOVER,
    SR_RTC_RECOVER,
    SR_NETWORK_HEAL,
    SR_DEEP_HEAL
};
SelfRecoveryState selfRecoveryState = SR_IDLE;

// =============================================================================
//  Watchdog
// =============================================================================
static const unsigned long WATCHDOG_FEED_INTERVAL = 100UL;
unsigned long lastWatchdogFeed = 0;

// =============================================================================
//  NTP Fallback
// =============================================================================
static const char* NTP_SERVERS[] = {
    "time.google.com",
    "time.windows.com",
    "time.cloudflare.com",
    "time.facebook.com"
};
static const uint8_t NUM_NTP_SERVERS = 4;

// =============================================================================
//  DNS + Web server
// =============================================================================
DNSServer        dnsServer;
ESP8266WebServer server(80);
const byte       DNS_PORT = 53;

// =============================================================================
//  NTP Client
// =============================================================================
WiFiUDP   ntpUDP;
bool      webServerHandlersRegistered = false;
NTPClient timeClient(ntpUDP, NTP_SERVERS[0], 28800, 3600000UL);

// =============================================================================
//  DS3231 RTC
// =============================================================================
RTC_DS3231 rtc;
bool ds3231Present = false;
unsigned long lastDS3231Sync = 0;
epoch64_t lastDS3231SyncEpoch = 0;

// =============================================================================
//  Dynamic Relay
// =============================================================================
#define MAX_RELAYS 16
uint8_t       numRelays = 6; 
bool          relayActiveLow = true;

// =============================================================================
//  Connection Management
// =============================================================================
unsigned long lastConnectionCleanup = 0;

// =============================================================================
//  Rate Limiting
// =============================================================================
struct RateLimiter {
    unsigned long windowStart = 0;
    uint8_t requestCount = 0;
};
RateLimiter apiLimiter;
RateLimiter pageLimiter;

// =============================================================================
//  Web Server Health
// =============================================================================
unsigned long lastWebServerCheck = 0;
unsigned long lastDNSCheck = 0;

// =============================================================================
//  Data structures
// =============================================================================
struct TimerSchedule {
    uint8_t  startHour[8], startMinute[8], startSecond[8];
    uint8_t  stopHour[8],  stopMinute[8],  stopSecond[8];
    bool     enabled[8];
    uint8_t  days[8];       
    uint32_t monthDays[8];  
    uint16_t monthMask[8];
};
struct RelayConfig {
    TimerSchedule schedule;
    bool          manualOverride;
    bool          manualState;
    char          name[16];
    uint8_t       pin;
    bool          activeLow;
};
struct SystemConfig {
    uint16_t magic;
    uint8_t  version;
    char     sta_ssid[32];
    char     sta_password[64];
    char     ap_ssid[32];
    char     ap_password[32];
    char     ntp_server[48];
    long     gmt_offset;
    int      daylight_offset;
    uint32_t last_rtc_epoch_lo;
    float    rtc_drift;
    char     hostname[32];
    float    crystal_ppm;
    bool     crystal_calibrated;
    bool     wifi_station_enabled;
    uint32_t last_rtc_epoch_hi;
    uint8_t  reserved[22];
};
struct ExtConfig {
    uint8_t magic;
    uint8_t ap_channel;
    uint8_t ntp_sync_hours;
    uint8_t ap_hidden;
    uint8_t reserved[28];
};
struct PinConfig {
    uint8_t magic;
    uint8_t numRelays;
    bool    globalActiveLow;
    uint8_t reserved[29];
};

// =============================================================================
//  Globals
// =============================================================================
SystemConfig sysConfig;
ExtConfig    extConfig;
PinConfig    pinConfig;
RelayConfig  relayConfigs[MAX_RELAYS];
epoch64_t     internalEpoch            = 0;
unsigned long internalMillisAtLastSync = 0;
float         driftCompensation        = 1.0f;
bool          rtcInitialized           = false;
uint8_t       ntpServerIndex  = 0;
uint8_t       ntpFailCount    = 0;
unsigned long lastNTPSync     = 0;
unsigned long lastNTPAttempt  = 0;
epoch64_t     lastNTPSyncEpoch = 0;    
bool          wifiConnected         = false;
bool          mdnsStarted           = false;
unsigned long lastWiFiCheck         = 0;
uint8_t       wifiReconnectAttempts = 0;
unsigned long wifiGiveUpUntil       = 0;
static const uint8_t MAX_RECONNECT  = 10;
uint8_t       reconnectCountThisHour = 0;
unsigned long reconnectHourStart     = 0;
enum WifiConnState { WCS_IDLE, WCS_PENDING };
WifiConnState wcsState = WCS_IDLE;
unsigned long wcsStart = 0;
enum APRestartState { APR_IDLE, APR_PENDING, APR_RESTARTING };
APRestartState aprState = APR_IDLE;
unsigned long aprStartTime = 0;
static const unsigned long AP_RESTART_DELAY = 300UL;
volatile bool scanInProgress  = false;
volatile int  scanResultCount = -1;
unsigned long scanStartTime   = 0;
static const unsigned long SCAN_TIMEOUT_MS = 30000UL;
bool          configDirty    = false;
unsigned long configDirtySince = 0;
unsigned long lastConfigSave = 0;
unsigned long lastLedToggle = 0;
bool          ledState      = false;
unsigned long lastTimeVerification = 0;
unsigned long lastHeapCheck        = 0;
unsigned long lastSelfCheck        = 0;
unsigned long lastInternalTimeCheck = 0;
int           ntpSyncCount = 0;
unsigned long persistentDisconnectTime = 0;
unsigned long lastProactiveMaintenance = 0;
unsigned long lastGentleHealing = 0;
bool networkHealingInProgress = false;
unsigned long lastDNSRestart = 0;
unsigned long lastHeapDefrag = 0;
unsigned long lastWebServerRecovery = 0;
unsigned long lastDeepHeal = 0;
bool deepHealInProgress = false;
uint8_t flashWriteCounter = 0;
unsigned long flashWriteHourStart = 0;
bool pendingWiFiReconnect = false;
unsigned long pendingWiFiReconnectAt = 0;
bool pendingAPRestart = false;
unsigned long pendingAPRestartAt = 0;
bool pendingStationDisable = false;
unsigned long pendingStationDisableAt = 0;
bool wifiStationEnabled = true;

// =============================================================================
//  sysConfig epoch helpers
// =============================================================================
static inline epoch64_t sysConfigGetLastRtcEpoch() {
    return (epoch64_t)(((uint64_t)sysConfig.last_rtc_epoch_hi << 32) |
                       (uint64_t)sysConfig.last_rtc_epoch_lo);
}
static inline void sysConfigSetLastRtcEpoch(epoch64_t v) {
    uint64_t u = (uint64_t)v;
    sysConfig.last_rtc_epoch_lo = (uint32_t)(u & 0xFFFFFFFFu);
    sysConfig.last_rtc_epoch_hi = (uint32_t)(u >> 32);
}

// =============================================================================
//  Crystal calibration globals
// =============================================================================
float crystalDriftPPM = 0;
bool crystalCalibrated = false;
epoch64_t lastBrowserSyncEpoch = 0;
unsigned long lastBrowserSyncMs = 0;

// =============================================================================
//  Multi-year reliability helpers
// =============================================================================
static unsigned long uptimeLastMillis = 0;
static uint64_t      uptimeWraps      = 0;
inline uint64_t getUptimeMs64() {
    unsigned long now = millis();
    if (now < uptimeLastMillis) {
        uptimeWraps++;
    }
    uptimeLastMillis = now;
    return ((uint64_t)uptimeWraps << 32) | (uint64_t)now;
}
inline unsigned long elapsedSince(unsigned long since) {
    unsigned long now = millis();
    if (now >= since) {
        return now - since;
    }
    return (0xFFFFFFFFUL - since) + now + 1UL;
}
inline void appendJsonEscaped(String& out, const char* s) {
    if (!s) return;
    static const char hexDigits[] = "0123456789ABCDEF";
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '"')       { out += '\\'; out += '"'; }
        else if (c == '\\') { out += '\\'; out += '\\'; }
        else if (c == '\b') { out += '\\'; out += 'b'; }
        else if (c == '\f') { out += '\\'; out += 'f'; }
        else if (c == '\n') { out += '\\'; out += 'n'; }
        else if (c == '\r') { out += '\\'; out += 'r'; }
        else if (c == '\t') { out += '\\'; out += 't'; }
        else if (c < 0x20)  {
            out += '\\'; out += 'u'; out += '0'; out += '0';
            out += hexDigits[(c >> 4) & 0x0F];
            out += hexDigits[c & 0x0F];
        }
        else { out += (char)c; }
    }
}
inline bool checkApiRateLimit() {
    unsigned long now = millis();
    if (now - apiLimiter.windowStart > RATE_LIMIT_WINDOW) {
        apiLimiter.windowStart = now;
        apiLimiter.requestCount = 1;
        return true;
    }    
    apiLimiter.requestCount++;
    if (apiLimiter.requestCount > API_RATE_LIMIT) {
        return false;
    }
    return true;
}
inline bool checkPageRateLimit() {
    unsigned long now = millis();
    if (now - pageLimiter.windowStart > RATE_LIMIT_WINDOW) {
        pageLimiter.windowStart = now;
        pageLimiter.requestCount = 1;
        return true;
    }    
    pageLimiter.requestCount++;
    if (pageLimiter.requestCount > API_RATE_LIMIT * 2) {
        return false;
    }
    return true;
}
inline bool checkFlashWriteRate() {
    unsigned long now = millis();
    if (now - flashWriteHourStart >= 3600000UL) {
        flashWriteCounter = 0;
        flashWriteHourStart = now;
    }
    if (flashWriteCounter >= 100) return false;
    flashWriteCounter++;
    return true;
}

// =============================================================================
//  Prototypes
// =============================================================================
epoch64_t getCurrentEpoch();
void updateCrystalCalibration(epoch64_t measuredEpoch, unsigned long measuredMs, epoch64_t previousEpoch, unsigned long previousMs);
void syncInternalRTC();
void syncInternalRTCFromBrowser(epoch64_t browserEpoch);
void loadRTCState();
void saveRTCState();
void loadConfiguration();
void saveConfiguration();
void loadExtConfig();
void saveExtConfig();
void loadPinConfig();
void savePinConfig();
void initDefaults();
void updateRelayPins();
void processRelaySchedules();
void setupWebServer();
void restartAP();
void processAPRestart();
void tryNTPSync();
void processNTPState();
void beginWiFiConnect();
void startMDNS();
void updateStatusLED();
void loadFromFile(const char* path, void* data, size_t size);
bool saveToFileAtomic(const char* path, const void* data, size_t size);
void markConfigDirty();
void flushConfigIfNeeded();
void checkConnectionRateLimit();
void performHeapCleanup();
void performDeepHeal();
void performSelfCheck();
void proactiveMaintenance();
void gentleNetworkHealing();
void processNetworkHealing();
void checkFactoryResetButton();
void processFactoryReset();
void performFactoryReset();
void defragmentHeap();
void manageConnections();
void cleanupIdleConnections();
void selfRecoveryWifiReconnect();
void selfRecoveryWebServer();
void selfRecoveryDNS();
void selfRecoveryRTC();
void selfRecoveryMemoryCleanup();
void selfRecoveryNetworkHeal();
void selfRecoveryDeepHeal();
void checkWebServerHealth();
void checkDNSHealth();
void initDS3231();
void syncFromDS3231();
void syncToDS3231(epoch64_t epoch);
void monitorHeapHealth();
void checkWiFiStability();
void emergencyHeapRecovery();
void enableWiFiStation();
void disableWiFiStation();
void handleRoot();
void handleWiFiPage();
void handleNtpPage();
void handleApPage();
void handleSystemPage();
void handlePinsPage();
void handleStyleCss();
void handleCaptivePortal();
void handleRedirect();
void handleSuccess();
void handleConnectTest();
void handleNcsi();
void handleNotFound();
void handleGetRelays();
void handleSaveRelayName();
void handleManualControl();
void handleResetManual();
void handleSaveRelay();
void handleGetTime();
void handleGetWiFi();
void handleSaveWiFi();
void handleWiFiScanStart();
void handleWiFiScanResults();
void handleGetNTP();
void handleSaveNTP();
void handleSyncNTP();
void handleBrowserTimeSync();
void handleGetAP();
void handleSaveAP();
void handleGetSystem();
void handleSaveSystem();
void handleGetPins();
void handleSavePins();
void handleResetPins(); 
void handleReset();
void handleFactoryReset();
void handleWiFiToggle();
void handleWiFiStationStatus();

// =============================================================================
//  SHARED CSS
// =============================================================================
const char style_css[] PROGMEM = R"css(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;background:#EEF2F7;color:#1A1A2E;font-size:14px;line-height:1.5}
header{background:linear-gradient(135deg,#1565C0 0%,#0D47A1 100%);color:#fff;padding:10px 16px;display:flex;align-items:center;gap:10px;position:sticky;top:0;z-index:50;box-shadow:0 2px 10px rgba(0,0,0,.3);flex-wrap:wrap}
.logo{font-size:13px;font-weight:700;white-space:nowrap}
nav{display:flex;gap:3px;flex-wrap:wrap;flex:1}
nav a{color:rgba(255,255,255,.8);text-decoration:none;padding:5px 8px;border-radius:5px;font-size:12px;transition:.15s}
nav a:hover,nav a.cur{background:rgba(255,255,255,.2);color:#fff}
.hdr-r{display:flex;align-items:center;gap:6px;margin-left:auto;font-size:12px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;background:#546E7A;flex-shrink:0}
.g{background:#69F0AE}.r{background:#FF5252}.y{background:#FFD740}.b{background:#42A5F5}
main{max-width:1200px;margin:0 auto;padding:16px}
.ptitle{font-size:17px;font-weight:700;color:#1565C0;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:14px}
.card{background:#fff;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,.08);padding:16px;transition:box-shadow .2s}
.card:hover{box-shadow:0 4px 18px rgba(0,0,0,.13)}
.card-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}
.ctitle{font-weight:700;font-size:15px;cursor:pointer;transition:background .15s;padding:2px 4px;border-radius:4px}
.ctitle:hover{background:#E3F2FD;color:#1565C0}
.badge{padding:3px 9px;border-radius:20px;font-size:11px;font-weight:700}
.bon{background:#E8F5E9;color:#2E7D32}.boff{background:#FFEBEE;color:#C62828}
.bman{background:#FFF3E0;color:#E65100}
.bman-on{background:#FFF3E0;color:#E65100;border:1px solid #FFB74D}
.bman-off{background:#FBE9E7;color:#BF360C;border:1px solid #FF8A65}
.brow{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px}
.btn{border:none;padding:7px 12px;border-radius:6px;cursor:pointer;font-size:12px;font-weight:600;transition:.15s}
.btn:hover{filter:brightness(1.1)}.btn:disabled{opacity:.5;cursor:default}
.bon-b{background:#43A047;color:#fff}.boff-b{background:#E53935;color:#fff}.baut{background:#546E7A;color:#fff}
.bsave{background:#1565C0;color:#fff;width:100%;padding:9px;font-size:13px;border-radius:6px;margin-top:8px}
.bntp-action{flex:1 1 0;min-width:140px;margin-top:0;padding:9px 12px;border-radius:6px;font-size:13px;font-weight:600;white-space:nowrap;background:#1565C0;color:#fff}
.bsync{background:#FB8C00;color:#fff}.bdanger{background:#B71C1C;color:#fff}.bwarn{background:#F9A825;color:#212121}
.bscan{background:#0288D1;color:#fff}.bbrowser{background:#7B1FA2;color:#fff}
.sys-action{flex:1 1 0;min-width:150px;padding:9px 18px;border-radius:6px;font-size:13px;font-weight:600;white-space:nowrap;color:#fff}
.badd{background:#43A047;color:#fff}.bremove{background:#E53935;color:#fff}
.btoggle{background:#00897B;color:#fff;width:100%;padding:9px;font-size:13px;border-radius:6px;margin-top:8px}
.slist{display:flex;flex-direction:column;gap:6px;margin-bottom:8px;max-height:500px;overflow-y:auto;padding-right:2px}
.si{border:1px solid #E3E8EF;border-radius:7px;padding:9px;min-height:auto;height:auto;overflow:visible}
.si.act{border-color:#90CAF9;background:#F0F7FF}
.shdr{display:flex;align-items:flex-start;gap:7px;margin-bottom:7px;font-size:11px;font-weight:700;color:#607D8B;text-transform:uppercase;flex-wrap:wrap}
.shdr label{display:flex;align-items:center;gap:4px;cursor:pointer;font-size:12px;font-weight:700;color:#1A1A2E;text-transform:none;white-space:nowrap}
.trow{display:flex;align-items:center;gap:8px;font-size:12px;margin-top:5px}
.trow .l{color:#90A4AE;font-weight:600;width:32px;flex-shrink:0}
.days{display:flex;gap:3px;margin-top:5px;flex-wrap:wrap}
.day{width:36px;height:36px;border-radius:4px;border:1px solid #CFD8DC;display:flex;align-items:center;justify-content:center;font-size:11px;font-weight:600;cursor:pointer;background:#FAFAFA;transition:.15s;user-select:none;padding:0;font-family:inherit;color:#1A1A2E;-webkit-appearance:none;appearance:none}
.day:hover{border-color:#90CAF9;background:#E3F2FD}
.day.on{background:#1565C0;color:#fff;border-color:#1565C0}
.day:focus-visible{outline:2px solid #1565C0;outline-offset:2px}
.mdays{display:flex;gap:2px;margin-top:5px;flex-wrap:wrap}
.mday{width:32px;height:32px;border-radius:3px;border:1px solid #CFD8DC;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:600;cursor:pointer;background:#FAFAFA;transition:.15s;user-select:none;padding:0;font-family:inherit;color:#1A1A2E;-webkit-appearance:none;appearance:none}
.mday:hover{border-color:#90CAF9;background:#E3F2FD}
.mday.on{background:#1565C0;color:#fff;border-color:#1565C0}
.mday:focus-visible{outline:2px solid #1565C0;outline-offset:2px}
.months{display:flex;gap:3px;margin-top:5px;flex-wrap:wrap}
.month{width:44px;height:36px;border-radius:4px;border:1px solid #CFD8DC;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:600;cursor:pointer;background:#FAFAFA;transition:.15s;user-select:none;padding:0;font-family:inherit;color:#1A1A2E;-webkit-appearance:none;appearance:none}
.month:hover{border-color:#90CAF9;background:#E3F2FD}
.month.on{background:#1565C0;color:#fff;border-color:#1565C0}
.month:focus-visible{outline:2px solid #1565C0;outline-offset:2px}
.sched-section{margin-top:4px;font-size:10px;font-weight:600;color:#90A4AE;text-transform:uppercase;margin-bottom:2px}
.night{font-size:10px;color:#0D47A1;background:#E3F2FD;padding:2px 6px;border-radius:4px;margin-left:auto;white-space:normal;word-break:break-word;max-width:100%;line-height:1.4;flex:1 1 auto;text-align:right}
.night.always{background:#E8F5E9;color:#2E7D32}
input[type=time]{flex:1;padding:5px 8px;border:1px solid #CFD8DC;border-radius:5px;font-size:13px;font-family:monospace;background:#FAFAFA;cursor:pointer;min-width:0}
input[type=time]:focus{outline:none;border-color:#1565C0;box-shadow:0 0 0 3px rgba(21,101,192,.15);background:#fff}
.ibar{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:8px;margin-bottom:16px}
.ibox{background:#fff;border-radius:8px;padding:12px;box-shadow:0 1px 4px rgba(0,0,0,.07)}
.ibox .l{font-size:11px;color:#90A4AE;text-transform:uppercase;font-weight:600}
.ibox .v{font-size:15px;font-weight:700;color:#1A1A2E;margin-top:2px}
.fcrd{max-width:600px}
.fg{margin-bottom:14px}
.fg label{display:block;font-size:11px;font-weight:700;color:#607D8B;margin-bottom:5px;text-transform:uppercase;letter-spacing:.4px}
.fg input,.fg select{width:100%;padding:9px 12px;border:1px solid #CFD8DC;border-radius:7px;font-size:14px;background:#FAFAFA}
.fg input:focus,.fg select:focus{outline:none;border-color:#1565C0;box-shadow:0 0 0 3px rgba(21,101,192,.15);background:#fff}
.fg small{display:block;margin-top:4px;font-size:11px;color:#90A4AE}
.input-row{display:flex;gap:8px}
.input-row input{flex:1;min-width:0}
.alert{border-radius:7px;padding:11px 14px;font-size:13px;margin-bottom:14px}
.aw{background:#FFF8E1;border-left:4px solid #F9A825;color:#5D4037}
.ai{background:#E3F2FD;border-left:4px solid #1565C0;color:#0D47A1}
.as{background:#E8F5E9;border-left:4px solid #43A047;color:#2E7D32}
hr{border:none;border-top:1px solid #ECEFF1;margin:14px 0}
.netlist{margin-top:10px;display:none}
.net-hdr{font-size:11px;font-weight:700;color:#607D8B;text-transform:uppercase;margin-bottom:6px}
.netitem{display:flex;align-items:center;gap:8px;padding:8px 10px;border:1px solid #E3E8EF;border-radius:7px;cursor:pointer;margin-bottom:5px;background:#FAFAFA;transition:.15s}
.netitem:hover{background:#EEF2F7;border-color:#90CAF9}
.netitem .ns{flex:1;font-size:13px;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.netitem .nr{font-size:11px;color:#90A4AE;white-space:nowrap}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}
.bar{width:4px;border-radius:1px;background:#CFD8DC}
.bar.on{background:#43A047}
.pin-table{width:100%;border-collapse:collapse;margin:10px 0}
.pin-table th{background:#F5F7FA;padding:10px;text-align:left;font-size:11px;font-weight:700;color:#607D8B;text-transform:uppercase;border-bottom:2px solid #E3E8EF}
.pin-table td{padding:12px 10px;border-bottom:1px solid #ECEFF1;font-size:13px}
.pin-row:hover{background:#F8FAFC}
.pin-chip{display:inline-block;padding:6px 10px;margin:3px;border-radius:6px;font-size:11px;font-weight:600;cursor:pointer;transition:all .15s;border:2px solid transparent}
.pin-chip.available{background:#E3F2FD;color:#1565C0;border-color:#90CAF9}
.pin-chip.available:hover{background:#BBDEFB}
.pin-chip.used{background:#E8E8E8;color:#90A4AE;border-color:#CFD8DC;cursor:default}
#toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%) translateY(80px);background:#323232;color:#fff;padding:10px 20px;border-radius:8px;font-size:13px;transition:transform .28s;z-index:999;pointer-events:none;box-shadow:0 4px 16px rgba(0,0,0,.3);min-width:180px;max-width:calc(100vw - 32px);word-break:break-word;text-align:center}
#toast.show{transform:translateX(-50%) translateY(0)}
#toast.ok{background:#2E7D32}#toast.er{background:#C62828}
.rtc-status{display:inline-block;padding:3px 9px;border-radius:20px;font-size:11px;font-weight:700}
.rtc-ok{background:#E8F5E9;color:#2E7D32}
.rtc-na{background:#FFEBEE;color:#C62828}
.rtc-temp{font-size:12px;color:#607D8B}
@media(max-width:500px){.grid{grid-template-columns:1fr}.ibar{grid-template-columns:1fr}.input-row{flex-direction:column}.day{width:34px;height:34px;font-size:10px}.mday{width:30px;height:30px;font-size:9px}.month{width:40px;height:34px;font-size:9px}.sys-action,.bntp-action{min-width:0;flex:1 1 0;padding:9px 6px;font-size:12px}.hdr-r{font-size:10px;gap:3px}}
)css";

// =============================================================================
//  HTML PAGES
// =============================================================================

const char index_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relays</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/" class="cur">Relays</a>
<a href="/wifi">WiFi</a>
<a href="/ntp">Time</a>
<a href="/ap">AP</a>
<a href="/pins">Pins</a>
<a href="/system">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">Relay Controls &amp; Schedules</p>
<div class="grid" id="grid"></div>
</main>
<div id="toast"></div>
<script>
const D=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
const M=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];

function toast(m,ok=true){
  const t=document.getElementById('toast');
  if(!t) return;
  t.textContent=m;
  t.className='show '+(ok?'ok':'er');
  clearTimeout(t._t);
  t._t=setTimeout(()=>t.className='',3000);
}

function fetchWithTimeout(url, options, timeout){
  options = options || {};
  timeout = timeout || 10000;
  try {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    const opts = Object.assign({}, options, {signal: controller.signal});
    return fetch(url, opts).then(
      r => { clearTimeout(timer); return r; },
      e => { clearTimeout(timer); throw e; }
    );
  } catch(e) {
    return fetch(url, options);
  }
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = String(text == null ? '' : text);
  return div.innerHTML;
}

function utf8ByteLen(str){
  if(typeof str !== 'string') return 0;
  try {
    if(typeof TextEncoder !== 'undefined') return new TextEncoder().encode(str).length;
  } catch(e) {}
  let n = 0;
  for(let i = 0; i < str.length; i++){
    const c = str.charCodeAt(i);
    if(c < 0x80) n += 1;
    else if(c < 0x800) n += 2;
    else if(c >= 0xD800 && c <= 0xDBFF){ n += 4; i++; }
    else n += 3;
  }
  return n;
}

function clampUtf8Bytes(str, maxBytes){
  if(typeof str !== 'string') return '';
  if(maxBytes <= 0) return '';
  let bytes = 0;
  let out = '';
  for(let i = 0; i < str.length; i++){
    const c = str.charCodeAt(i);
    let step;
    if(c < 0x80){ step = 1; }
    else if(c < 0x800){ step = 2; }
    else if(c >= 0xD800 && c <= 0xDBFF){
      if(i + 1 < str.length){
        const c2 = str.charCodeAt(i + 1);
        if(c2 >= 0xDC00 && c2 <= 0xDFFF){ step = 4; }
        else { step = 3; }
      } else { step = 3; }
    } else { step = 3; }
    if(bytes + step > maxBytes) break;
    bytes += step;
    if(step === 4){ out += str[i] + str[i+1]; i++; }
    else out += str[i];
  }
  return out;
}

function toTS(h,m,s){
  h = Math.min(23, Math.max(0, parseInt(h) || 0));
  m = Math.min(59, Math.max(0, parseInt(m) || 0));
  s = Math.min(59, Math.max(0, parseInt(s) || 0));
  return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
}
function fromTS(v){
  const p=(v||'00:00:00').split(':');
  return {
    h: Math.min(23, Math.max(0, parseInt(p[0]) || 0)),
    m: Math.min(59, Math.max(0, parseInt(p[1]) || 0)),
    s: Math.min(59, Math.max(0, parseInt(p[2]) || 0))
  };
}

function dayMaskToStr(d){
  if(d === undefined || d === null) d = 0x7F;
  d = d & 0x7F;
  if(d === 0x7F) return 'Everyday';
  if(d === 0) return 'None';
  let s='';
  for(let i=0;i<7;i++) if(d&(1<<i)) s+=D[i]+' ';
  return s.trim()||'None';
}

function monthDayMaskToStr(md){
  if (md === undefined || md === null || md === 0) return 'All month days';
  md = md & 0x7FFFFFFF;
  if (md === 0x7FFFFFFF) return 'All month days';
  if (md === 0) return 'None';
  let s = '';
  for (let i = 0; i < 31; i++) if (md & (1 << i)) s += (i+1) + ',';
  return s.replace(/,$/, '') || 'None';
}

function monthMaskToStr(mm){
  if (mm === undefined || mm === null || mm === 0) return 'All months';
  mm = mm & 0x0FFF;
  if (mm === 0x0FFF) return 'All months';
  if (mm === 0) return 'None';
  let s = '';
  for (let i = 0; i < 12; i++) if (mm & (1 << i)) s += M[i] + ' ';
  return s.trim() || 'None';
}

function nightBadge(sc){
  if(!sc || !sc.enabled) return '';
  const sh = (typeof sc.startHour === 'number') ? sc.startHour : 0;
  const sm = (typeof sc.startMinute === 'number') ? sc.startMinute : 0;
  const ss = (typeof sc.startSecond === 'number') ? sc.startSecond : 0;
  const eh = (typeof sc.stopHour === 'number') ? sc.stopHour : 0;
  const em = (typeof sc.stopMinute === 'number') ? sc.stopMinute : 0;
  const es = (typeof sc.stopSecond === 'number') ? sc.stopSecond : 0;
  const a = sh*3600 + sm*60 + ss;
  const b = eh*3600 + em*60 + es;
  const dmask = (sc.days === undefined || sc.days === null) ? 0x7F : (sc.days & 0x7F);
  if(dmask === 0){
    return '<span class="night" style="background:#FFEBEE;color:#C62828">&#x26A0; No days selected &mdash; never runs</span>';
  }
  const ds = dayMaskToStr(dmask);
  const ms = monthDayMaskToStr(sc.monthDays);
  const mm = monthMaskToStr(sc.monthMask);
  let parts = [];
  if(ds) parts.push(ds);
  if(ms && ms !== 'All month days') parts.push('Days: ' + ms);
  if(mm && mm !== 'All months') parts.push('Months: ' + mm);
  let info = parts.join(' | ');
  if(!info) info = 'Always';
  if(a === b) return '<span class="night always">&#x25CF; Always ON (' + info + ')</span>';
  if(a > b) return '<span class="night">&#x1F319; Overnight (' + info + ')</span>';
  return '<span class="night">&#x1F319; ' + info + '</span>';
}

const NS = 8;
let relays = [];
let loadInFlight = false;
let pendingLoad = false;
let busyCount = 0;
let dirtyRelays = new Set();
let savingRelays = {};
let editingRelay = -1;
let editingInput = null;
let editSuppressBlur = false;
let nameEditSeq = {};

function requestLoad(force){
  if(busyCount > 0){ pendingLoad = true; return; }
  load(force);
}

function load(force){
  if(busyCount > 0) return;
  if(loadInFlight) return;
  if(!force && dirtyRelays.size > 0) return;
  loadInFlight = true;
  const g = document.getElementById('grid');
  fetchWithTimeout('/api/relays', {}, 15000).then(r => {
    if(!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }).then(d => {
    loadInFlight = false;
    if(!Array.isArray(d)){
      toast('Load error', false);
      if(g && (!Array.isArray(relays) || relays.length === 0)) g.innerHTML = '';
      return;
    }
    d = d.filter(r => r && typeof r === 'object');
    d.forEach(r => {
      if(!Array.isArray(r.schedules)) r.schedules = [];
      r.schedules = r.schedules.filter(s => s && typeof s === 'object');
      if(r.schedules.length > NS) r.schedules = r.schedules.slice(0, NS);
      while(r.schedules.length < NS) r.schedules.push({
        startHour:0,startMinute:0,startSecond:0,
        stopHour:0,stopMinute:0,stopSecond:0,
        enabled:false,days:0x7F,monthDays:0,monthMask:0
      });
    });
    relays = d;
    dirtyRelays.clear();
    render();
  }).catch(() => {
    loadInFlight = false;
    toast('Load error', false);
    if(g && (!Array.isArray(relays) || relays.length === 0)) g.innerHTML = '';
  });
}

function startEditName(relayIdx){
  if(!relays[relayIdx] || typeof relays[relayIdx] !== 'object') return;
  if(editingRelay === relayIdx){
    if(editingInput){ editingInput.focus(); editingInput.select(); }
    return;
  }
  if(editingRelay !== -1) cancelEdit();
  const nameSpan = document.getElementById('name_' + relayIdx);
  if(!nameSpan) return;
  const currentName = relays[relayIdx].name || ('Relay ' + (relayIdx + 1));
  const input = document.createElement('input');
  input.type = 'text';
  input.value = currentName;
  input.maxLength = 15;
  input.style.cssText = 'font-size:15px;font-weight:700;padding:2px 6px;border:1px solid #1565C0;border-radius:5px;width:120px;background:#fff;color:#1A1A2E;';
  input.onblur = () => {
    if(editSuppressBlur) return;
    saveNameEdit(relayIdx, input.value);
  };
  input.addEventListener('input', () => {
    if(utf8ByteLen(input.value) > 15){
      input.value = clampUtf8Bytes(input.value, 15);
    }
  });
  input.onkeydown = (e) => {
    if(e.key === 'Enter'){
      e.preventDefault();
      editSuppressBlur = true;
      saveNameEdit(relayIdx, input.value);
      editSuppressBlur = false;
    } else if(e.key === 'Escape'){
      e.preventDefault();
      editSuppressBlur = true;
      cancelEdit();
      editSuppressBlur = false;
    }
  };
  nameSpan.style.display = 'none';
  nameSpan.parentNode.insertBefore(input, nameSpan.nextSibling);
  editingRelay = relayIdx;
  editingInput = input;
  input.focus();
  input.select();
}

function cancelEdit(){
  if(editingRelay !== -1){
    const nameSpan = document.getElementById('name_' + editingRelay);
    if(nameSpan) nameSpan.style.display = '';
    editSuppressBlur = true;
    if(editingInput) editingInput.remove();
    editSuppressBlur = false;
    editingRelay = -1;
    editingInput = null;
  }
}

function saveNameEdit(relayIdx, newName){
  if(!relays[relayIdx] || typeof relays[relayIdx] !== 'object'){
    if(editingRelay === relayIdx){ editingRelay = -1; editingInput = null; }
    return;
  }
  if(typeof newName !== 'string') newName = '';
  newName = newName.trim();
  if(utf8ByteLen(newName) > 15) newName = clampUtf8Bytes(newName, 15).trim();
  while(newName.length > 0 && utf8ByteLen(newName) > 15) newName = newName.slice(0, -1).trim();
  if(newName.length === 0) newName = 'Relay ' + (relayIdx + 1);
  const nameSpan = document.getElementById('name_' + relayIdx);
  const oldName = relays[relayIdx].name || ('Relay ' + (relayIdx + 1));
  relays[relayIdx].name = newName;
  if(nameSpan){ nameSpan.textContent = newName; nameSpan.style.display = ''; }
  editSuppressBlur = true;
  if(editingInput) editingInput.remove();
  editSuppressBlur = false;
  editingRelay = -1;
  editingInput = null;
  const mySeq = (nameEditSeq[relayIdx] || 0) + 1;
  nameEditSeq[relayIdx] = mySeq;
  fetchWithTimeout('/api/relay/name', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({relay: relayIdx, name: newName})
  }).then(r => r.json()).then(d => {
    if(nameEditSeq[relayIdx] !== mySeq) return;
    if(d && d.success) toast('Name saved!');
    else {
      relays[relayIdx].name = oldName;
      const span = document.getElementById('name_' + relayIdx);
      if(span) span.textContent = oldName;
      toast('Failed to save name', false);
    }
  }).catch(() => {
    if(nameEditSeq[relayIdx] !== mySeq) return;
    relays[relayIdx].name = oldName;
    const span = document.getElementById('name_' + relayIdx);
    if(span) span.textContent = oldName;
    toast('Error saving name', false);
  });
}

function render(){
  const g = document.getElementById('grid');
  if(!g) return;
  g.innerHTML = '';
  if(!Array.isArray(relays) || relays.length === 0){
    g.innerHTML = '<div class="card" style="text-align:center;padding:40px"><p style="color:#90A4AE">No relays configured. <a href="/pins" style="color:#1565C0">Configure GPIO pins</a></p></div>';
    return;
  }
  relays.forEach((r, i) => {
    if(!r || typeof r !== 'object'){
      r = {state:false, manual:false, name:'Relay ' + (i+1), schedules:[]};
      relays[i] = r;
    }
    const sl = r.manual ? (r.state ? 'MANUAL ON' : 'MANUAL OFF') : (r.state ? 'ON' : 'OFF');
    const sc = r.manual ? (r.state ? 'bman-on' : 'bman-off') : (r.state ? 'bon' : 'boff');
    const displayName = r.name || ('Relay ' + (i+1));
    let html = `<div class="card">
<div class="card-hdr">
<span class="ctitle" id="name_${i}" ondblclick="startEditName(${i})" title="Double-click to rename">${escapeHtml(displayName)}</span>
<span class="badge ${sc}">${sl}</span>
</div>
<div class="brow">
<button type="button" class="btn bon-b" onclick="mc(${i},true)">ON</button>
<button type="button" class="btn boff-b" onclick="mc(${i},false)">OFF</button>
<button type="button" class="btn baut" onclick="ra(${i})">Auto</button>
</div>
<div class="slist">`;
    for(let s = 0; s < NS; s++){
      const sc2 = r.schedules[s] || {
        startHour:0,startMinute:0,startSecond:0,
        stopHour:0,stopMinute:0,stopSecond:0,
        enabled:false,days:0x7F,monthDays:0,monthMask:0
      };
      const dayBits = (sc2.days === undefined || sc2.days === null) ? 0x7F : (sc2.days & 0x7F);
      const rawMD = sc2.monthDays;
      const monthDayBits = (rawMD === undefined || rawMD === null || rawMD === 0)
        ? 0x7FFFFFFF
        : (rawMD & 0x7FFFFFFF);
      const rawMM = sc2.monthMask;
      const monthMask = (rawMM === undefined || rawMM === null || rawMM === 0)
        ? 0x0FFF
        : (rawMM & 0x0FFF);
      html += `<div class="si${sc2.enabled?' act':''}" id="si_${i}_${s}">
<div class="shdr">
<label><input type="checkbox" id="en_${i}_${s}" ${sc2.enabled?'checked':''} onchange="uf(${i},${s},'en',this.checked)"> Sched ${s+1}</label>
<span id="nb_${i}_${s}">${nightBadge(sc2)}</span>
</div>
<div class="trow"><span class="l">Start</span>
<input type="time" step="1" id="st_${i}_${s}" value="${toTS(sc2.startHour,sc2.startMinute,sc2.startSecond)}" onchange="uf(${i},${s},'start',this.value)" min="00:00:00" max="23:59:59">
</div>
<div class="trow"><span class="l">Stop</span>
<input type="time" step="1" id="et_${i}_${s}" value="${toTS(sc2.stopHour,sc2.stopMinute,sc2.stopSecond)}" onchange="uf(${i},${s},'stop',this.value)" min="00:00:00" max="23:59:59">
</div>
<div class="sched-section">Days of Week</div>
<div class="days" id="day_${i}_${s}">`;
      for(let d = 0; d < 7; d++){
        const mask = 1 << d;
        html += `<button type="button" class="day${(dayBits&mask)?' on':''}" onclick="toggleDay(${i},${s},${d})" aria-pressed="${(dayBits&mask)?'true':'false'}" aria-label="${D[d]}">${D[d]}</button>`;
      }
      html += `</div>
<div class="sched-section">Days of Month</div>
<div class="mdays" id="mday_${i}_${s}">`;
      for(let d = 0; d < 31; d++){
        const mask = 1 << d;
        html += `<button type="button" class="mday${(monthDayBits&mask)?' on':''}" onclick="toggleMonthDay(${i},${s},${d})" title="Day ${d+1}" aria-pressed="${(monthDayBits&mask)?'true':'false'}" aria-label="Day ${d+1}">${d+1}</button>`;
      }
      html += `</div>
<div class="sched-section">Months</div>
<div class="months" id="month_${i}_${s}">`;
      for(let m = 0; m < 12; m++){
        const mask = 1 << m;
        html += `<button type="button" class="month${(monthMask&mask)?' on':''}" onclick="toggleMonth(${i},${s},${m})" aria-pressed="${(monthMask&mask)?'true':'false'}" aria-label="${M[m]}">${M[m]}</button>`;
      }
      html += `</div>
</div>`;
    }
    html += `</div><button type="button" class="btn bsave" onclick="save(${i})">&#x1F4BE; Save ${escapeHtml(displayName)}</button></div>`;
    const el = document.createElement('div');
    el.innerHTML = html;
    while(el.firstChild) g.appendChild(el.firstChild);
  });
}

function toggleDay(ri, si, dayIdx){
  if(!relays[ri] || !Array.isArray(relays[ri].schedules)) return;
  let sc = relays[ri].schedules[si];
  if(!sc || typeof sc !== 'object') return;
  dirtyRelays.add(ri);
  const mask = 1 << dayIdx;
  let cur = (sc.days === undefined || sc.days === null) ? 0x7F : (sc.days & 0x7F);
  cur ^= mask;
  sc.days = cur;
  const el = document.getElementById('day_' + ri + '_' + si);
  if(el && el.children[dayIdx]){
    const c = el.children[dayIdx];
    c.className = 'day' + ((cur & mask) ? ' on' : '');
    c.setAttribute('aria-pressed', (cur & mask) ? 'true' : 'false');
  }
  const nb = document.getElementById('nb_' + ri + '_' + si);
  if(nb) nb.innerHTML = nightBadge(sc);
}

function toggleMonthDay(ri, si, dayIdx){
  if(!relays[ri] || !Array.isArray(relays[ri].schedules)) return;
  let sc = relays[ri].schedules[si];
  if(!sc || typeof sc !== 'object') return;
  const mask = 1 << dayIdx;
  let md = sc.monthDays;
  if(md === undefined || md === null || md === 0) md = 0x7FFFFFFF;
  md = md & 0x7FFFFFFF;
  const nextMd = (md ^ mask) & 0x7FFFFFFF;
  if(nextMd === 0){
    toast('At least one month-day must stay selected', false);
    return;
  }
  dirtyRelays.add(ri);
  sc.monthDays = (nextMd === 0x7FFFFFFF) ? 0 : nextMd;
  const el = document.getElementById('mday_' + ri + '_' + si);
  if(el && el.children[dayIdx]){
    const c = el.children[dayIdx];
    const on = (nextMd & mask) !== 0;
    c.className = 'mday' + (on ? ' on' : '');
    c.setAttribute('aria-pressed', on ? 'true' : 'false');
  }
  const nb = document.getElementById('nb_' + ri + '_' + si);
  if(nb) nb.innerHTML = nightBadge(sc);
}

function toggleMonth(ri, si, mIdx){
  if(!relays[ri] || !Array.isArray(relays[ri].schedules)) return;
  let sc = relays[ri].schedules[si];
  if(!sc || typeof sc !== 'object') return;
  const mask = 1 << mIdx;
  let mm = sc.monthMask;
  if(mm === undefined || mm === null || mm === 0) mm = 0x0FFF;
  mm = mm & 0x0FFF;
  const nextMm = (mm ^ mask) & 0x0FFF;
  if(nextMm === 0){
    toast('At least one month must stay selected', false);
    return;
  }
  dirtyRelays.add(ri);
  sc.monthMask = (nextMm === 0x0FFF) ? 0 : nextMm;
  const el = document.getElementById('month_' + ri + '_' + si);
  if(el && el.children[mIdx]){
    const c = el.children[mIdx];
    const on = (nextMm & mask) !== 0;
    c.className = 'month' + (on ? ' on' : '');
    c.setAttribute('aria-pressed', on ? 'true' : 'false');
  }
  const nb = document.getElementById('nb_' + ri + '_' + si);
  if(nb) nb.innerHTML = nightBadge(sc);
}

function uf(ri, si, field, val){
  if(!relays[ri] || !Array.isArray(relays[ri].schedules)) return;
  let sc = relays[ri].schedules[si];
  if(!sc || typeof sc !== 'object') return;
  dirtyRelays.add(ri);
  if(field === 'en'){
    sc.enabled = !!val;
    const el = document.getElementById('si_' + ri + '_' + si);
    if(el) el.className = 'si' + (val ? ' act' : '');
  } else if(field === 'start'){
    const t = fromTS(val);
    sc.startHour = t.h; sc.startMinute = t.m; sc.startSecond = t.s;
  } else if(field === 'stop'){
    const t = fromTS(val);
    sc.stopHour = t.h; sc.stopMinute = t.m; sc.stopSecond = t.s;
  }
  const nb = document.getElementById('nb_' + ri + '_' + si);
  if(nb) nb.innerHTML = nightBadge(sc);
}

function patchRelayBadge(ri){
  if(!relays[ri]) return;
  const nameEl = document.getElementById('name_' + ri);
  if(!nameEl || !nameEl.parentNode) return;
  const r = relays[ri];
  const sl = r.manual ? (r.state ? 'MANUAL ON' : 'MANUAL OFF') : (r.state ? 'ON' : 'OFF');
  const sc = r.manual ? (r.state ? 'bman-on' : 'bman-off') : (r.state ? 'bon' : 'boff');
  const badge = nameEl.parentNode.querySelector('.badge');
  if(badge){ badge.textContent = sl; badge.className = 'badge ' + sc; }
}

function refreshOneRelayState(ri){
  if(busyCount > 0) return;
  if(loadInFlight) return;
  loadInFlight = true;
  fetchWithTimeout('/api/relays', {}, 10000).then(r => r.ok ? r.json() : null).then(d => {
    loadInFlight = false;
    if(Array.isArray(d) && d[ri]){
      relays[ri].state  = !!d[ri].state;
      relays[ri].manual = !!d[ri].manual;
      patchRelayBadge(ri);
    }
    if(busyCount === 0 && pendingLoad){ pendingLoad = false; load(); }
  }).catch(() => {
    loadInFlight = false;
    if(busyCount === 0 && pendingLoad){ pendingLoad = false; load(); }
  });
}

function mc(ri, state){
  if(!relays[ri]) return;
  fetchWithTimeout('/api/relay/manual', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({relay: ri, state: !!state})
  }).then(r => r.json()).then(d => {
    if(d && d.success){
      relays[ri].manual = true;
      relays[ri].state  = !!state;
      patchRelayBadge(ri);
      toast((relays[ri].name || ('Relay ' + (ri+1))) + ' ' + (state ? 'ON' : 'OFF'));
    } else toast('Failed', false);
  }).catch(() => toast('Error', false));
}

function ra(ri){
  if(!relays[ri]) return;
  fetchWithTimeout('/api/relay/reset', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({relay: ri})
  }).then(r => r.json()).then(d => {
    if(d && d.success){
      relays[ri].manual = false;
      patchRelayBadge(ri);
      refreshOneRelayState(ri);
      toast((relays[ri].name || ('Relay ' + (ri+1))) + ' \u2192 Auto');
    } else toast('Failed', false);
  }).catch(() => toast('Error', false));
}

function save(ri){
  if(!relays[ri]) return;
  if(savingRelays[ri]) return;
  savingRelays[ri] = true;
  busyCount++;
  const btn = document.querySelector(`button[onclick="save(${ri})"]`);
  const safeName = escapeHtml((relays[ri] && relays[ri].name) || ('Relay ' + (ri+1)));
  if(btn){ btn.disabled = true; btn.textContent = 'Saving...'; }
  const srcSchedules = (Array.isArray(relays[ri].schedules) ? relays[ri].schedules : []).slice(0, NS);
  const clampInt = (v, lo, hi, dflt) => {
    const n = parseInt(v, 10);
    if(isNaN(n)) return dflt;
    return Math.max(lo, Math.min(hi, n));
  };
  const payloadSchedules = srcSchedules.map(function(s){
    s = s || {};
    return {
      startHour:   clampInt(s.startHour,   0, 23, 0),
      startMinute: clampInt(s.startMinute, 0, 59, 0),
      startSecond: clampInt(s.startSecond, 0, 59, 0),
      stopHour:    clampInt(s.stopHour,    0, 23, 0),
      stopMinute:  clampInt(s.stopMinute,  0, 59, 0),
      stopSecond:  clampInt(s.stopSecond,  0, 59, 0),
      enabled:     !!s.enabled,
      days:        ((typeof s.days === 'number' ? s.days : 0x7F) & 0x7F),
      monthDays:   (typeof s.monthDays === 'number' ? (s.monthDays & 0x7FFFFFFF) : 0),
      monthMask:   (typeof s.monthMask === 'number' ? (s.monthMask & 0x0FFF) : 0)
    };
  });
  while(payloadSchedules.length < NS){
    payloadSchedules.push({
      startHour:0, startMinute:0, startSecond:0,
      stopHour:0, stopMinute:0, stopSecond:0,
      enabled:false, days:0x7F, monthDays:0, monthMask:0
    });
  }
  fetchWithTimeout('/api/relay/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({relay: ri, schedules: payloadSchedules})
  }, 15000).then(r => r.json()).then(d => {
    savingRelays[ri] = false;
    if(busyCount > 0) busyCount--;
    if(btn){ btn.disabled = false; btn.innerHTML = '&#x1F4BE; Save ' + safeName; }
    if(d && d.success){
      dirtyRelays.delete(ri);
      toast((relays[ri].name || ('Relay ' + (ri+1))) + ' saved!');
    } else {
      toast('Save failed', false);
    }
    if(busyCount === 0 && pendingLoad){ pendingLoad = false; load(); }
  }).catch(() => {
    savingRelays[ri] = false;
    if(busyCount > 0) busyCount--;
    if(btn){ btn.disabled = false; btn.innerHTML = '&#x1F4BE; Save ' + safeName; }
    if(busyCount === 0 && pendingLoad){ pendingLoad = false; load(); }
    toast('Error', false);
  });
}

function tick(){
  if(tick._inflight) return;
  tick._inflight = true;
  fetchWithTimeout('/api/time', {}, 8000).then(r => {
    if(!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  }).then(d => {
    tick._inflight = false;
    if(!d || typeof d !== 'object' || Array.isArray(d)) return;
    const clkEl = document.getElementById('clk');
    if(clkEl) clkEl.textContent = d.time || '--:--:--';
    const dayEl = document.getElementById('day');
    if(dayEl) dayEl.textContent = d.day || '---';
    const dateEl = document.getElementById('date');
    if(dateEl) dateEl.textContent = d.date || '--/--/----';
    const w = document.querySelector('.wd');
    const n = document.querySelector('.nd');
    if(w) w.className = 'dot wd ' + (d.wifi ? 'g' : 'r');
    if(n) n.className = 'dot nd ' + (d.ntp ? 'g' : 'y');
  }).catch(() => { tick._inflight = false; });
}
setInterval(tick, 1000);
tick();

window.addEventListener('beforeunload', function(e){
  if(dirtyRelays.size > 0){ e.preventDefault(); e.returnValue = ''; return ''; }
});

load();
</script></body></html>)raw";

const char wifi_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/">Relays</a>
<a href="/wifi" class="cur">WiFi</a>
<a href="/ntp">Time</a>
<a href="/ap">AP</a>
<a href="/pins">Pins</a>
<a href="/system">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">WiFi Station Settings</p>
<div class="card fcrd">
<div id="status" class="alert ai" style="display:none"></div>
<div class="fg">
<label>Network SSID</label>
<div class="input-row">
<input type="text" id="ssid" placeholder="Enter wifi name or scan" required>
<button class="btn bscan" id="scanBtn" onclick="startScan()" style="white-space:nowrap">&#x1F4F6; Scan</button>
</div>
</div>
<div class="netlist" id="netlist"></div>
<div class="fg"><label>Password</label>
<div class="input-row">
<input type="password" id="pw" placeholder="Leave blank to keep current" autocomplete="new-password">
<button class="btn bscan" type="button" onclick="togglePw('pw',this)" style="white-space:nowrap">&#x1F441;</button>
</div>
</div>
<div class="fg"><label><input type="checkbox" id="clearPw"> Clear saved password</label></div>
<button class="btn bsave" onclick="save()">&#x1F4BE; Save &amp; Connect</button>
</div>

<div class="card fcrd" style="margin-top:14px">
<div class="card-hdr">
<span class="ctitle">WiFi Station Control</span>
<span class="badge" id="staBadge">ON</span>
</div>
<p style="font-size:12px;color:#90A4AE;margin-bottom:12px">
Turn off to use Access Point mode only. Relay schedules and AP remain active.
</p>
<button class="btn btoggle" id="toggleBtn" onclick="toggleStation()">Disable WiFi Station</button>
</div>
</main>
<div id="toast"></div>
<script>
function togglePw(id,btn){const el=document.getElementById(id);if(!el)return;el.type=el.type==='password'?'text':'password';btn.textContent=el.type==='password'?'\uD83D\uDC41':'\uD83D\uDE48';}
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function rssiBar(rssi){
  const b=rssi>=-50?4:rssi>=-60?3:rssi>=-70?2:1;
  let s='<span class="bars">';
  for(let i=1;i<=4;i++)s+='<span class="bar'+(i<=b?' on':'')+('" style="height:'+(i*3+2)+'px"></span>');
  return s+'</span>';
}

function getWiFiStatusText(statusCode) {
  switch(statusCode) {
    case 0: return 'Idle';
    case 1: return 'SSID not available';
    case 2: return 'Scan completed';
    case 3: return 'Connected';
    case 4: return 'Connection failed';
    case 5: return 'Connection lost';
    case 6: return 'Wrong password';
    default: return 'Unknown status';
  }
}

function toggleStation(){
  const btn=document.getElementById('toggleBtn');
  btn.disabled=true;
  btn.textContent='Toggling...';
  fetch('/api/wifi/toggle',{method:'POST'})
  .then(r=>r.json()).then(d=>{
    btn.disabled=false;
    if(d.success){
      const badge=document.getElementById('staBadge');
      const enabled=d.enabled;
      badge.textContent=enabled?'ON':'OFF';
      badge.className='badge '+(enabled?'bon':'boff');
      btn.textContent=enabled?'Disable WiFi Station':'Enable WiFi Station';
      toast(enabled?'WiFi station enabled':'WiFi station disabled');
      setTimeout(loadWiFiInfo,2000);
    }else{
      btn.disabled=false;
      btn.textContent = document.getElementById('staBadge').textContent === 'OFF'
          ? 'Enable WiFi Station'
          : 'Disable WiFi Station';
      toast('Toggle failed',false);
    }
  }).catch(()=>{
    btn.disabled=false;
    btn.textContent = document.getElementById('staBadge').textContent === 'OFF'
        ? 'Enable WiFi Station'
        : 'Disable WiFi Station';
    toast('Error',false);
  });
}

function loadWiFiInfo(){
  fetch('/api/wifi').then(r=>r.json()).then(d=>{
    document.getElementById('ssid').value=d.ssid||'';
    const s=document.getElementById('status');
    s.style.display='';
    
    const btn=document.getElementById('toggleBtn');
    const badge=document.getElementById('staBadge');
    if(d.stationEnabled!==undefined){
      badge.textContent=d.stationEnabled?'ON':'OFF';
      badge.className='badge '+(d.stationEnabled?'bon':'boff');
      btn.textContent=d.stationEnabled?'Disable WiFi Station':'Enable WiFi Station';
    }
    
    if(d.connected){
      const bars=rssiBar(d.rssi||0);
      s.innerHTML='&#x2705; Connected to: <strong>'+escapeHtml(d.ssid)+'</strong> ('+escapeHtml(d.ip)+') &nbsp;'+bars+' '+d.rssi+'dBm';
      s.className='alert as';
    }else{
      s.className='alert aw';
      if(d.stationEnabled === false) {
        s.textContent='WiFi station is disabled';
      } else if(d.lastStatus !== undefined && d.ssid) {
        s.innerHTML='&#x26A0;&#xFE0F; '+getWiFiStatusText(d.lastStatus)+' - Check SSID and password';
      } else if(d.ssid) {
        s.textContent='Not connected to '+d.ssid;
      } else {
        s.textContent='Not connected to any network';
      }
    }
  }).catch(()=>{});
}

loadWiFiInfo();

let scanTimer=null,scanning=false;
function startScan(){
  if(scanning)return;
  scanning=true;
  document.getElementById('scanBtn').textContent='\uD83D\uDD04 Scanning\u2026';
  document.getElementById('scanBtn').disabled=true;
  const nl=document.getElementById('netlist');
  nl.style.display='block';
  nl.innerHTML='<div style="text-align:center;color:#90A4AE;padding:12px;font-size:13px">Scanning for networks\u2026</div>';
  fetch('/api/wifi/scan',{method:'POST'})
  .then(r => {
    if(!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  })
  .then(()=>{ scanTimer=setInterval(pollScan,2500); })
  .catch(()=>{ endScan(); toast('Cannot scan right now \u2014 try again shortly', false); });
}
function pollScan(){
  fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{
    if(!d.scanning){clearInterval(scanTimer);endScan();renderNets(d.networks||[]);}
  }).catch(()=>{clearInterval(scanTimer);endScan();});
}
function endScan(){
  scanning=false;
  document.getElementById('scanBtn').textContent='\uD83D\uDCF6 Scan';
  document.getElementById('scanBtn').disabled=false;
}
function renderNets(nets){
  const nl=document.getElementById('netlist');
  if(!nets.length){nl.innerHTML='<div style="color:#90A4AE;text-align:center;padding:8px;font-size:13px">No networks found.</div>';return;}
  const frag=document.createDocumentFragment();
  const hdr=document.createElement('div');hdr.className='net-hdr';hdr.textContent='Available Networks (click to select)';frag.appendChild(hdr);
  nets.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    const d=document.createElement('div');d.className='netitem';
    const ns=document.createElement('span');ns.className='ns';ns.textContent=n.ssid;
    const nr=document.createElement('span');nr.className='nr';nr.textContent=n.rssi+'dBm';
    const bar=document.createElement('span');bar.innerHTML=rssiBar(n.rssi);
    const lock=document.createElement('span');lock.style.fontSize='13px';lock.textContent=n.enc?'\uD83D\uDD12':'\uD83D\uDD13';
    d.appendChild(ns);d.appendChild(nr);d.appendChild(bar);d.appendChild(lock);
    d.addEventListener('click',()=>{
      document.getElementById('ssid').value=n.ssid;
      document.getElementById('pw').focus();
      toast('Selected: '+n.ssid+(n.enc?' (password required)':' (open network)'));
    });
    frag.appendChild(d);
  });
  nl.innerHTML='';nl.appendChild(frag);
}
function save(){
  const ssid=document.getElementById('ssid').value.trim();
  if(!ssid){toast('SSID required',false);return;}
  
  const s=document.getElementById('status');
  s.style.display='';
  s.innerHTML='&#x23F3; Connecting to <strong>'+escapeHtml(ssid)+'</strong>...';
  s.className='alert ai';

  const saveBtn = document.querySelector('button.bsave[onclick="save()"]');
  if(saveBtn){ saveBtn.disabled = true; saveBtn.textContent = 'Saving\u2026'; }

  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password:document.getElementById('pw').value,clearPassword:document.getElementById('clearPw').checked})})
  .then(r=>r.json()).then(d=>{
    if(saveBtn){ saveBtn.disabled = false; saveBtn.innerHTML = '&#x1F4BE; Save &amp; Connect'; }
    if(d.success){
      toast('Saved! Connecting to '+ssid+'...');
      setTimeout(loadWiFiInfo, 3000);
      setTimeout(loadWiFiInfo, 8000);
      setTimeout(loadWiFiInfo, 15000);
    } else {
      toast('Failed: '+(d.error||'unknown'),false);
      s.innerHTML='&#x26A0;&#xFE0F; Save failed';
      s.className='alert aw';
    }
  }).catch(()=>{
    if(saveBtn){ saveBtn.disabled = false; saveBtn.innerHTML = '&#x1F4BE; Save &amp; Connect'; }
    s.innerHTML='&#x26A0;&#xFE0F; Connection error \u2014 check network';
    s.className='alert aw';
    toast('Error',false);
  });
}
</script></body></html>)raw";

const char ntp_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Time</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/">Relays</a>
<a href="/wifi">WiFi</a>
<a href="/ntp" class="cur">Time</a>
<a href="/ap">AP</a>
<a href="/pins">Pins</a>
<a href="/system">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">Time &amp; NTP Settings</p>
<div class="card fcrd">
<div class="fg">
<label>NTP Server</label>
<input type="text" id="srv" placeholder="time.google.com" required>
<small>Fallbacks: time.google.com &rarr; time.windows.com &rarr; time.cloudflare.com &rarr; time.facebook.com</small>
</div>
<div class="fg"><label>GMT Offset</label><input type="number" id="gmt" required></div>
<div class="fg"><label>Daylight Saving</label><input type="number" id="dst" value="0"></div>
<div class="fg"><label>Auto Sync (hours, 1&ndash;24)</label><input type="number" id="shi" min="1" max="24" value="1"></div>
<div style="display:flex;gap:8px;flex-wrap:wrap">
<button class="btn bntp-action" onclick="save()">&#x1F4BE; Save Settings</button>
<button class="btn bntp-action" id="sbtn" onclick="sync()">&#x1F504; Sync NTP</button>
<button class="btn bntp-action" id="bbtn" onclick="syncFromBrowser()">&#x1F310; Sync Browser</button>
</div>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
fetch('/api/ntp').then(r=>r.json()).then(d=>{
  document.getElementById('srv').value=d.ntpServer||'time.google.com';
  document.getElementById('gmt').value=(d.gmtOffset!==undefined&&d.gmtOffset!==null)?d.gmtOffset:28800;
  document.getElementById('dst').value=(d.daylightOffset!==undefined&&d.daylightOffset!==null)?d.daylightOffset:0;
  document.getElementById('shi').value=(d.syncHours!==undefined&&d.syncHours!==null)?d.syncHours:1;
}).catch(()=>{});
function save(){
  const h=parseInt(document.getElementById('shi').value);
  if(isNaN(h)||h<1||h>24){toast('Sync interval must be 1\u201324 h',false);return;}
  const srv=document.getElementById('srv').value.trim();
  if(!srv){toast('NTP server required',false);return;}
  fetch('/api/ntp',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    ntpServer:srv,
    gmtOffset:parseInt(document.getElementById('gmt').value),
    daylightOffset:parseInt(document.getElementById('dst').value),
    syncHours:h
  })}).then(r=>r.json()).then(d=>{if(d.success)toast('NTP settings saved!');else toast('Failed: '+(d&&d.error?d.error:'unknown'),false);})
  .catch(()=>toast('Error',false));
}
function sync(){
  const b=document.getElementById('sbtn');b.disabled=true;b.textContent='Syncing\u2026';
  fetch('/api/ntp/sync',{method:'POST'}).then(r=>r.json()).then(d=>{
    b.disabled=false;b.innerHTML='&#x1F504; Sync NTP';
    if(d.success)toast('Sync requested \u2014 check clock in a few seconds');else toast('Sync failed \u2014 check WiFi',false);
    setTimeout(tick, 2500);
  }).catch(()=>{b.disabled=false;b.innerHTML='&#x1F504; Sync NTP';toast('Error',false);});
}
function syncFromBrowser(){
  const b=document.getElementById('bbtn');b.disabled=true;b.textContent='Syncing\u2026';
  const epoch=Math.floor(Date.now()/1000);
  fetch('/api/time/browser-sync',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:epoch})})
  .then(r=>r.json()).then(d=>{
    b.disabled=false;b.innerHTML='&#x1F310; Sync Browser';
    if(d.success){
      toast('Time synced from browser successfully!');
      tick();
      setTimeout(() => {
        fetch('/api/time').then(r=>r.json()).then(d=>{
          document.getElementById('clk').textContent=d.time||'--:--:--';
          var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';
          var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';
        });
      }, 500);
    }else toast('Browser sync failed: '+(d.error||'unknown'),false);
  }).catch(()=>{b.disabled=false;b.innerHTML='&#x1F310; Sync from Browser';toast('Error',false);});
}
</script></body></html>)raw";

const char ap_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AP</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/">Relays</a>
<a href="/wifi">WiFi</a>
<a href="/ntp">Time</a>
<a href="/ap" class="cur">AP</a>
<a href="/pins">Pins</a>
<a href="/system">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">Access Point Settings</p>
<div class="card fcrd">
<div class="alert aw">&#x26A0;&#xFE0F; Changes will apply immediately without restarting the device.</div>
<div class="fg"><label>AP SSID</label><input type="text" id="ssid" maxlength="31" required></div>
<div class="fg"><label>AP Password</label>
<div class="input-row">
<input type="password" id="pw" minlength="8" placeholder="Leave blank to keep current">
<button class="btn bscan" type="button" onclick="togglePw('pw',this)" style="white-space:nowrap">&#x1F441;</button>
</div>
<small id="pwWarn" style="display:none;color:#C62828">&#x26A0; Open network &mdash; anyone can connect.</small>
</div>
<div class="fg">
<label>Channel (1&ndash;13)</label>
<select id="ch">
<option value="1">1</option><option value="2">2</option><option value="3">3</option>
<option value="4">4</option><option value="5">5</option><option value="6">6 (default)</option>
<option value="7">7</option><option value="8">8</option><option value="9">9</option>
<option value="10">10</option><option value="11">11</option><option value="12">12</option>
<option value="13">13</option>
</select>
<small>Lower interference: pick a channel not used by nearby networks.</small>
</div>
<div class="fg">
<label>SSID Visibility</label>
<select id="hidden">
<option value="0">Visible (broadcast SSID)</option>
<option value="1">Hidden (do not broadcast)</option>
</select>
</div>
<div class="fg"><label><input type="checkbox" id="clearPw"> Clear saved password (open network)</label></div>
<button type="button" class="btn bsave" onclick="save()">&#x1F4BE; Save &amp; Apply</button>
</div>
</main>
<div id="toast"></div>
<script>
function togglePw(id,btn){const el=document.getElementById(id);if(!el)return;el.type=el.type==='password'?'text':'password';btn.textContent=el.type==='password'?'\uD83D\uDC41':'\uD83D\uDE48';}
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
function refreshPwWarn(){
  const pwEl = document.getElementById('pw');
  const warnEl = document.getElementById('pwWarn');
  if(!pwEl || !warnEl) return;
  warnEl.style.display = (pwEl.value.length === 0) ? '' : 'none';
}
fetch('/api/ap').then(r=>r.json()).then(d=>{
  document.getElementById('ssid').value=d.ap_ssid||'';
  document.getElementById('ch').value=d.ap_channel||6;
  document.getElementById('hidden').value=d.ap_hidden?'1':'0';
  var hasPw = (d.hasPassword === true) || (typeof d.ap_password === 'string' && d.ap_password.length > 0);
  document.getElementById('pwWarn').style.display = hasPw ? 'none' : '';
}).catch(()=>{
  refreshPwWarn();
});
document.getElementById('pw').addEventListener('input',function(){
  document.getElementById('pwWarn').style.display = (this.value.length === 0) ? '' : 'none';
});
function save(){
  const ssid=document.getElementById('ssid').value;
  const pw=document.getElementById('pw').value;
  if(ssid.length<1||ssid.length>31){toast('AP SSID must be 1-31 characters',false);return;}
  if(pw.length>0&&pw.length<8){toast('Password must be 8+ chars or blank',false);return;}
  const btn=document.querySelector('button.bsave[onclick="save()"]');
  if(btn){btn.disabled=true;btn.textContent='Applying\u2026';}
  fetch('/api/ap',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    ap_ssid:ssid,
    ap_password:pw,
    clearPassword:document.getElementById('clearPw').checked,
    ap_channel:parseInt(document.getElementById('ch').value),
    ap_hidden:document.getElementById('hidden').value==='1'
  })}).then(r=>r.json()).then(d=>{
    if(btn){btn.disabled=false;btn.innerHTML='&#x1F4BE; Save &amp; Apply';}
    if(d&&d.success){toast('AP settings applied!');}
    else{toast('Failed: '+((d&&d.error)?d.error:'unknown'),false);}
  }).catch(()=>{
    if(btn){btn.disabled=false;btn.innerHTML='&#x1F4BE; Save &amp; Apply';}
    toast('Error',false);
  });
}
</script></body></html>)raw";

const char pins_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pins</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/">Relays</a>
<a href="/wifi">WiFi</a>
<a href="/ntp">Time</a>
<a href="/ap">AP</a>
<a href="/pins" class="cur">Pins</a>
<a href="/system">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">GPIO Pin Configuration</p>
<div class="card fcrd">
<div class="alert ai">&#x26A0;&#xFE0F; Changes apply immediately. No restart required.</div>

<div class="fg">
<label>Global Relay Logic</label>
<div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
<select id="globalActiveLow" style="flex:1">
<option value="1">Active LOW</option>
<option value="0">Active HIGH</option>
</select>
<button class="btn baut" onclick="applyGlobalLogicToAll()" style="white-space:nowrap;padding:7px 12px">Apply to All Relays</button>
</div>
</div>

<h3 style="margin:16px 0 12px;color:#1565C0">Relay Assignments</h3>
<div style="overflow-x:auto">
<table class="pin-table" id="pinTable">
<thead><tr><th>Relay</th><th>Name</th><th>GPIO Pin</th><th>Logic</th><th></th></tr></thead>
<tbody id="pinBody"></tbody>
</table>
</div>

<div style="display:flex;gap:8px;margin-top:12px;flex-wrap:wrap">
<button class="btn sys-action" onclick="addRelay()" style="background:#1565C0;color:#fff">&#x2795; Add Relay</button>
<button class="btn sys-action" onclick="savePins()" style="background:#1565C0;color:#fff">&#x1F4BE; Save &amp; Apply</button>
<button class="btn sys-action" id="resetPinsBtn" onclick="resetPinsToDefault()" style="background:#1565C0;color:#fff">&#x1F504; Reset All</button>
</div>
</div>

<div class="card fcrd" style="margin-top:14px">
<h3 style="margin-bottom:12px;color:#1565C0">Available GPIO Pins</h3>
<p style="font-size:12px;color:#90A4AE;margin-bottom:10px">Click an available pin to add a relay on that pin</p>
<div id="availPins" style="display:flex;gap:6px;flex-wrap:wrap"></div>
</div>
</main>
<div id="toast"></div>
<script>
let pinCfg={numRelays:6,globalActiveLow:true,relays:[]};
const allValidPins = [16, 14, 12, 13, 3, 1];
const pinNames={16:'D0',14:'D5',12:'D6',13:'D7',3:'RX',1:'TX'};

function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();

function escapeHtml(s){
  return String(s == null ? '' : s)
    .replace(/&/g,'&amp;')
    .replace(/</g,'&lt;')
    .replace(/>/g,'&gt;')
    .replace(/"/g,'&quot;')
    .replace(/'/g,'&#39;');
}

function utf8Clamp(str, maxBytes){
  if(typeof str !== 'string') return '';
  let bytes = 0, out = '';
  for(let i = 0; i < str.length; i++){
    const c = str.charCodeAt(i);
    let step;
    if(c < 0x80) step = 1;
    else if(c < 0x800) step = 2;
    else if(c >= 0xD800 && c <= 0xDBFF){
      const c2 = str.charCodeAt(i+1);
      step = (c2 >= 0xDC00 && c2 <= 0xDFFF) ? 4 : 3;
    } else step = 3;
    if(bytes + step > maxBytes) break;
    bytes += step;
    if(step === 4){ out += str[i] + str[i+1]; i++; }
    else out += str[i];
  }
  return out;
}

function pn(p){return pinNames[p]||('GPIO'+p);}

function getAvailablePins() {
    const usedPins = pinCfg.relays.map(r => r.pin);
    return allValidPins.filter(p => !usedPins.includes(p));
}

function load(){
  fetch('/api/pins').then(r=>r.json()).then(d=>{
    if(!d || typeof d !== 'object' || d.error || !Array.isArray(d.relays)){
      toast('Load failed: ' + ((d && d.error) ? d.error : 'bad response'), false);
      return;
    }
    pinCfg=d;
    for (let i = 0; i < pinCfg.relays.length; i++) {
        if (pinCfg.relays[i].activeLow === undefined) pinCfg.relays[i].activeLow = pinCfg.globalActiveLow;
    }
    document.getElementById('globalActiveLow').value=pinCfg.globalActiveLow?'1':'0';
    markPinsClean();
    render();
  }).catch(()=>toast('Failed to load',false));
}

function applyGlobalLogicToAll() {
    const newGlobalValue = document.getElementById('globalActiveLow').value === '1';
    if (confirm('Apply "' + (newGlobalValue ? 'Active LOW' : 'Active HIGH') + '" to ALL relays? Individual relay logic will be overwritten.')) {
        commitPendingNames();
        for (let i = 0; i < pinCfg.relays.length; i++) {
            pinCfg.relays[i].activeLow = newGlobalValue;
        }
        pinCfg.globalActiveLow = newGlobalValue;
        markPinsDirty();
        render();
        toast('Applied in form \u2014 click Save & Apply to persist');
    } else {
        document.getElementById('globalActiveLow').value = pinCfg.globalActiveLow ? '1' : '0';
    }
}

function getPinOpts(cur){
  const used=pinCfg.relays.map(r=>r.pin);
  let opts = '';
  const curValid = allValidPins.includes(cur);
  if(!curValid){
    opts += '<option value="" selected disabled>(unassigned)</option>';
  }
  opts += allValidPins.map(p=>{
    const usedByOther=used.includes(p) && p!==cur;
    return '<option value="'+p+'" '+(p===cur?'selected':'')+' '+(usedByOther?'disabled':'')+'>'+pn(p)+' (GPIO'+p+')'+(usedByOther?' [USED]':'')+'</option>';
  }).join('');
  return opts;
}

function commitPendingNames(){
  const tb = document.getElementById('pinBody');
  if(!tb || !pinCfg || !Array.isArray(pinCfg.relays)) return;
  const inputs = tb.querySelectorAll('input[type="text"]');
  for(let i = 0; i < inputs.length && i < pinCfg.relays.length; i++){
    const el = inputs[i];
    if(el && typeof el.value === 'string'){
      pinCfg.relays[i].name = utf8Clamp(el.value, 15);
    }
  }
}

function render(){
  commitPendingNames();
  const tb=document.getElementById('pinBody');
  tb.innerHTML='';
  pinCfg.relays.forEach((r,i)=>{
    const tr=document.createElement('tr');
    tr.className='pin-row';
    const isCustom = (r.activeLow !== pinCfg.globalActiveLow);
    const customIndicator = isCustom ? ' <span style="font-size:10px;color:#F9A825;">✦</span>' : '';
    tr.innerHTML='<td><strong>Relay '+(i+1)+'</strong></td>'+
      '<td><input type="text" value="'+escapeHtml(r.name||('Relay '+(i+1)))+'" maxlength="15" onchange="pinCfg.relays['+i+'].name=utf8Clamp(this.value,15)" style="width:110px;padding:5px;border:1px solid #CFD8DC;border-radius:4px"></td>'+
      '<td><select onchange="var v=parseInt(this.value); if(!isNaN(v)) updatePin('+i+',v);" style="padding:5px;border:1px solid #CFD8DC;border-radius:4px">'+getPinOpts(r.pin)+'</select></td>'+
      '<td><select onchange="pinCfg.relays['+i+'].activeLow=this.value===\'1\'" style="padding:5px;border:1px solid #CFD8DC;border-radius:4px;'+(isCustom?'background:#FFF8E1;':'')+'"><option value="1" '+(r.activeLow?'selected':'')+'>Active LOW</option><option value="0" '+(!r.activeLow?'selected':'')+'>Active HIGH</option></select>'+customIndicator+'</td>'+
      '<td><button class="btn bremove" onclick="removeRelay('+i+')" '+(pinCfg.relays.length<2?'disabled':'')+'>\u2715</button></td>';
    tb.appendChild(tr);
  });
  renderAvail();
}

function renderAvail(){
  const availablePins = getAvailablePins();
  const c=document.getElementById('availPins');
  c.innerHTML='';
  if (availablePins.length === 0) {
    const msg = document.createElement('span');
    msg.style.cssText = 'color:#C62828;font-size:12px;padding:8px;';
    msg.textContent = '⚠️ No pins available!';
    c.appendChild(msg);
  } else {
    availablePins.forEach(p=>{
      const d=document.createElement('span');
      d.className='pin-chip available';
      d.textContent=pn(p);
      d.onclick=()=>addRelayOnPin(p);
      d.title='Click to add relay on '+pn(p);
      c.appendChild(d);
    });
  }
}

function updatePin(idx,pin){
  pinCfg.relays[idx].pin=pin;
  markPinsDirty();
  render();
}

function addRelayOnPin(pin){
  if(pinCfg.relays.length>=16){toast('Max 16 relays',false);return;}
  const availablePins = getAvailablePins();
  if (!availablePins.includes(pin)) {
    toast('Pin ' + pn(pin) + ' is not available', false);
    return;
  }
  commitPendingNames();
  pinCfg.relays.push({
    name:'Relay '+(pinCfg.relays.length+1),
    pin:pin,
    activeLow:pinCfg.globalActiveLow===true
  });
  pinCfg.numRelays=pinCfg.relays.length;
  markPinsDirty();
  render();
  toast('Added relay on '+pn(pin));
}

function addRelay(){
  const availablePins = getAvailablePins();
  if(availablePins.length === 0){
    toast('No pins available! Remove a relay first.', false);
    return;
  }
  addRelayOnPin(availablePins[0]);
}

function removeRelay(i){
  if(pinCfg.relays.length<2){toast('Need at least 1 relay',false);return;}
  commitPendingNames();
  pinCfg.relays.splice(i,1);
  pinCfg.numRelays=pinCfg.relays.length;
  markPinsDirty();
  render();
}

function savePins(){
  if(!confirm('Save pins and apply changes immediately?'))return;
  commitPendingNames();
  pinCfg.globalActiveLow=document.getElementById('globalActiveLow').value==='1';
  pinCfg.numRelays=pinCfg.relays.length;
  
  for (let i = 0; i < pinCfg.relays.length; i++) {
      if (pinCfg.relays[i].activeLow === undefined) {
          pinCfg.relays[i].activeLow = pinCfg.globalActiveLow;
      }
  }
  
  const btns = document.querySelectorAll('button.sys-action');
  let saveBtn = null;
  for(let bi = 0; bi < btns.length; bi++){
    if((btns[bi].getAttribute('onclick')||'').indexOf('savePins')>=0){ saveBtn = btns[bi]; break; }
  }
  if(saveBtn){ saveBtn.disabled = true; saveBtn.dataset.prevText = saveBtn.innerHTML; saveBtn.textContent = 'Saving...'; }

  fetch('/api/pins',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(pinCfg)})
  .then(r=>r.json()).then(d=>{
    if(saveBtn){ saveBtn.disabled = false; if(saveBtn.dataset.prevText) saveBtn.innerHTML = saveBtn.dataset.prevText; }
    if(d && d.success){markPinsClean();toast('Pins saved & applied!');load();}
    else toast('Failed: '+((d&&d.error)?d.error:'unknown'),false);
  }).catch(()=>{
    if(saveBtn){ saveBtn.disabled = false; if(saveBtn.dataset.prevText) saveBtn.innerHTML = saveBtn.dataset.prevText; }
    toast('Error',false);
  });
}

function resetPinsToDefault(){
  if(!confirm('Reset ALL GPIO pins to default (D0,D5,D6,D7,RX,TX)?\n\nRelay names and schedules will be preserved.\nChanges will apply immediately.')) return;
  
  const btn = document.getElementById('resetPinsBtn');
  if(btn) {
    btn.disabled = true;
    btn.textContent = 'Resetting...';
  }
  
  fetch('/api/pins/reset', {method: 'POST'})
  .then(r => r.json())
  .then(d => {
    if(btn) {
      btn.disabled = false;
      btn.textContent = '🔄 Reset All';
    }
    if(d && d.success) {
      toast('GPIO pins reset!');
      setTimeout(() => load(), 1000);
    } else {
      toast('Reset failed: ' + ((d && d.error) ? d.error : 'unknown'), false);
    }
  })
  .catch(() => {
    if(btn) {
      btn.disabled = false;
      btn.textContent = '🔄 Reset All';
    }
    toast('Error resetting pins', false);
  });
}

let _pinsDirty = false;
function markPinsDirty(){ _pinsDirty = true; }
function markPinsClean(){ _pinsDirty = false; }
window.addEventListener('beforeunload', function(e){
  if(_pinsDirty){ e.preventDefault(); e.returnValue = ''; return ''; }
});
load();
</script></body></html>)raw";

const char system_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>System</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header>
<nav>
<a href="/">Relays</a>
<a href="/wifi">WiFi</a>
<a href="/ntp">Time</a>
<a href="/ap">AP</a>
<a href="/pins">Pins</a>
<a href="/system" class="cur">System</a>
<a href="/mqtt">MQTT</a>
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">System Information &amp; Settings</p>
<div class="ibar" id="ibar">
<div class="ibox"><div class="l">STA IP</div><div class="v" id="sip">&hellip;</div></div>
<div class="ibox"><div class="l">AP IP</div><div class="v" id="sap">&hellip;</div></div>
<div class="ibox"><div class="l">Free Heap</div><div class="v" id="shp">&hellip;</div></div>
<div class="ibox"><div class="l">Uptime</div><div class="v" id="sup">&hellip;</div></div>
<div class="ibox"><div class="l">mDNS</div><div class="v" id="smd">&hellip;</div></div>
<div class="ibox"><div class="l">WiFi RSSI</div><div class="v" id="srs">&hellip;</div></div>
<div class="ibox"><div class="l">NTP Last Sync</div><div class="v" id="snt">&hellip;</div></div>
<div class="ibox"><div class="l">NTP Server</div><div class="v" id="sns" style="font-size:12px">&hellip;</div></div>
<div class="ibox"><div class="l">DS3231 RTC</div><div class="v" id="srtc">&hellip;</div></div>
<div class="ibox"><div class="l">DS3231 Temp</div><div class="v" id="stmp">&hellip;</div></div>
<div class="ibox"><div class="l">WiFi Station</div><div class="v" id="sws">&hellip;</div></div>
</div>

<div class="card fcrd" style="margin-bottom:14px">
<div class="alert as" id="rtcStatusAlert" style="display:none"></div>
<p style="font-weight:700;margin-bottom:12px">mDNS</p>
<div class="fg">
<small>Browser accessible as http://esp8266relay.local</small>
<input type="text" id="hn" maxlength="31" placeholder="esp8266relay" pattern="[a-z0-9\-]+" title="lowercase letters, digits, hyphens only">
</div>
<button class="btn bsave" onclick="saveHn()">&#x1F4BE; Save Hostname</button>
</div>

<div class="card fcrd">
<p style="font-weight:700;margin-bottom:12px">Device Control</p>
<div style="display:flex;gap:8px;flex-wrap:wrap;justify-content:center">
<button class="btn bwarn sys-action" onclick="rst()">&#x1F504; Reconnect WiFi</button>
<button class="btn bdanger sys-action" onclick="fct()">&#x26A0; Factory Reset</button>
</div>
<p style="color:#90A4AE;font-size:12px;margin-top:10px">Factory reset clears all settings. Alternatively hold the FLASH button for 5 seconds.</p>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var _de=document.getElementById('day');if(_de)_de.textContent=d.day||'---';var _dte=document.getElementById('date');if(_dte)_dte.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
function fmtUp(s){
  if(s === undefined || s === null || !isFinite(s) || s < 0) s = 0;
  const d = Math.floor(s/86400);
  const h = Math.floor((s%86400)/3600);
  const m = Math.floor((s%3600)/60);
  const ss = Math.floor(s%60);
  if(d > 0) return d+'d '+h+'h '+m+'m '+ss+'s';
  return h+'h '+m+'m '+ss+'s';
}
function rssiDesc(r){if(r===null||r===undefined||!isFinite(r))return'\u2014';return r+'dBm ('+( r>=-50?'Excellent':r>=-60?'Good':r>=-70?'Fair':'Weak')+')';}
function loadSys(){
  fetch('/api/system').then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(d=>{
    if(!d||typeof d!=='object'||typeof d.wifiConnected==='undefined')return;
    document.getElementById('sip').textContent=d.wifiConnected?d.ip:'(not connected)';
    document.getElementById('sap').textContent=d.ap_ip;
    document.getElementById('shp').textContent=(d.freeHeap/1024).toFixed(1)+' KB';
    let upSec = (typeof d.uptime === 'number' && isFinite(d.uptime) && d.uptime >= 0) ? d.uptime : 0;
    if (typeof d.uptimeHi === 'number' && typeof d.uptimeLo === 'number' && isFinite(d.uptimeHi) && isFinite(d.uptimeLo)) {
      const totalMs = (d.uptimeHi * 4294967296) + (d.uptimeLo >>> 0);
      const precise = Math.floor(totalMs / 1000);
      if (isFinite(precise) && precise >= 0) upSec = precise;
    }
    document.getElementById('sup').textContent=fmtUp(upSec);
    document.getElementById('smd').textContent=d.mdnsHostname;
    document.getElementById('srs').textContent=d.wifiConnected?rssiDesc(d.rssi):'\u2014';
    document.getElementById('snt').textContent=d.ntpSynced?(d.ntpSyncAge>=60?Math.floor(d.ntpSyncAge/60)+' min ago':'Just now'):'Never';
    document.getElementById('sns').textContent=d.ntpServer||'\u2014';
    document.getElementById('hn').value=d.hostname||'esp8266relay';
    document.getElementById('sws').textContent=d.wifiStationEnabled?'Enabled':'Disabled';
    
    const rtcDiv = document.getElementById('srtc');
    const tmpDiv = document.getElementById('stmp');
    const rtcAlert = document.getElementById('rtcStatusAlert');
    
    if(d.ds3231Present){
      rtcDiv.innerHTML = '<span class="rtc-status rtc-ok">&#x2705; Connected</span>';
      tmpDiv.textContent = (d.ds3231Temp !== null && d.ds3231Temp !== undefined && isFinite(d.ds3231Temp)) ? d.ds3231Temp.toFixed(1)+'°C' : 'N/A';
      
    } else {
      rtcDiv.innerHTML = '<span class="rtc-status rtc-na">&#x274C; Not Detected</span>';
      tmpDiv.textContent = 'N/A';
      
    }
  }).catch(()=>{});
}
loadSys();setInterval(loadSys,5000);
function saveHn(){
  const h=document.getElementById('hn').value.trim().toLowerCase().replace(/[^a-z0-9\-]/g,'');
  if(!h){toast('Invalid hostname',false);return;}
  fetch('/api/system',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hostname:h})})
  .then(r=>r.json()).then(d=>{if(d.success)toast('Hostname saved \u2014 mDNS restarted');else toast('Failed',false);})
  .catch(()=>toast('Error',false));
}
function rst(){
  if(!confirm('Reconnect WiFi now?'))return;
  fetch('/api/reset',{method:'POST'}).then(r=>r.json()).then(d=>{if(d.success)toast('WiFi reconnecting\u2026');else toast('Failed',false);}).catch(()=>toast('Error',false));
}
function fct(){
  if(!confirm('FACTORY RESET \u2014 ALL settings will be erased. Continue?'))return;
  const t = prompt('Type RESET (uppercase) to confirm factory reset:');
  if(t !== 'RESET'){ toast('Factory reset cancelled', false); return; }
  fetch('/api/factory-reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({confirm:'RESET'})})
    .then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json();})
    .then(d=>{
      if(!d||!d.success){ toast('Factory reset rejected by device', false); return; }
      toast('Factory reset \u2014 reconnect to default AP');
      setTimeout(()=>window.location.href='/',7000);
    })
    .catch(()=>toast('Error sending factory reset',false));
}
</script></body></html>)raw";

// MQTT configuration and runtime state
#define MQTT_CONFIG_FILE "/mqtt.cfg"
#define MQTT_CONFIG_MAGIC 0x4D515454UL
#define MQTT_CONFIG_VERSION 1
#define MQTT_RX_TOPIC_MAX 192
#define MQTT_RX_PAYLOAD_MAX 1800
#define MQTT_PACKET_BUFFER 2048
#define MQTT_TLS_MIN_HEAP 20000UL
#define MQTT_RECONNECT_MIN 5000UL
#define MQTT_RECONNECT_MAX 300000UL
#define MQTT_STATE_INTERVAL 500UL
#define MQTT_SYSTEM_INTERVAL 30000UL
#define MQTT_SCHEDULES_PER_RELAY 8
#define MQTT_MAX_ROOT_CA 2048

struct MqttConfig {
    uint32_t magic;
    uint8_t version;
    uint8_t enabled;
    uint8_t remoteAdminEnabled;
    uint8_t commandQos;
    uint8_t useTLS;
    uint16_t port;
    char host[64];
    char username[32];
    char password[64];
    char baseTopic[64];
    char clientId[32];
    char rootCA[MQTT_MAX_ROOT_CA];
    uint8_t reserved[16];
};

struct MqttIncomingMessage {
    char topic[MQTT_RX_TOPIC_MAX];
    char payload[MQTT_RX_PAYLOAD_MAX + 1];
    uint16_t length;
};

MqttConfig mqttConfig;
WiFiClient mqttTcpTransport;
BearSSL::WiFiClientSecure mqttTlsTransport;
PubSubClient mqttClient;
BearSSL::X509List* mqttTrustAnchors = nullptr;
MqttIncomingMessage mqttIncomingMessage;
bool mqttMessagePending = false;
bool mqttConnectedFlag = false;
bool mqttStatePublishPending = false;
bool mqttScanPublishPending = false;
volatile uint16_t mqttWiFiScanGeneration = 0;
bool mqttTransportReady = false;
bool mqttWasStationReady = false;
unsigned long mqttLastAttempt = 0;
unsigned long mqttRetryDelay = MQTT_RECONNECT_MIN;
unsigned long mqttLastStatePoll = 0;
unsigned long mqttLastSystemPublish = 0;
unsigned long mqttLastSnapshotStep = 0;
uint8_t mqttSnapshotRelay = 0;
uint8_t mqttSnapshotClearRelay = 0;
uint8_t mqttSnapshotRelayCount = 0;
uint8_t mqttLastRelayCount = 0;
bool mqttClearRetiredRelays = false;
bool mqttCachedState[MAX_RELAYS];
bool mqttCachedMode[MAX_RELAYS];
bool mqttCachedRelayValid[MAX_RELAYS];
uint32_t mqttCachedNameHash[MAX_RELAYS];
uint32_t mqttCachedScheduleHash[MAX_RELAYS];
char mqttLastError[64] = "disabled";
char mqttAvailabilityTopic[MQTT_RX_TOPIC_MAX];

void initMqttDefaults();
void loadMqttConfig();
bool saveMqttConfig(const MqttConfig* cfg);
void initializeMqttClient();
void requestMqttReconfigure();
void shutdownMqtt();
void mqttService();
void mqttProcessCommand();
void mqttPublishPeriodic();
void mqttPublishMetadata();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleMqttPage();
void handleGetMqtt();
void handleSaveMqtt();

static bool mqttValidBaseTopic(const char* s) {
    if (!s) return false;
    size_t n = strnlen(s, 64);
    if (n == 0 || n >= 64 || s[0] == '/' || s[n - 1] == '/') return false;
    bool slash = false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/';
        if (!ok || (c == '/' && slash)) return false;
        slash = (c == '/');
    }
    return true;
}

static bool mqttValidClientId(const char* s) {
    if (!s) return false;
    size_t n = strnlen(s, 32);
    if (n == 0 || n >= 32) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

static bool mqttValidBrokerHost(const char* s) {
    if (!s) return false;
    size_t n = strnlen(s, 64);
    if (n >= 64) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok || (c == '.' && (i == 0 || s[i - 1] == '.'))) return false;
    }
    return true;
}

static bool mqttBuildTopic(char* out, size_t outSize, const char* suffix) {
    if (!out || outSize == 0 || !mqttValidBaseTopic(mqttConfig.baseTopic) || !suffix) return false;
    int n = snprintf(out, outSize, "%s/%s", mqttConfig.baseTopic, suffix);
    return n > 0 && (size_t)n < outSize;
}

static uint32_t mqttHashText(const char* s) {
    uint32_t h = 2166136261UL;
    if (!s) return h;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619UL; }
    return h;
}

static void copyNameUtf8Safe(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t maxBytes = dstSize - 1;
    size_t i = 0;
    while (src[i] != '\0' && i < maxBytes) {
        uint8_t c = (uint8_t)src[i];
        size_t seqLen;
        if (c < 0x80)        seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else                 seqLen = 1;
        if (i + seqLen > maxBytes) break;
        for (size_t k = 0; k < seqLen && src[i + k] != '\0'; ++k) dst[i + k] = src[i + k];
        i += seqLen;
    }
    dst[i] = '\0';
}

static uint32_t mqttHashSchedule(const RelayConfig& r) {
    uint32_t h = 2166136261UL;
    const TimerSchedule& s = r.schedule;
    for (uint8_t i = 0; i < MQTT_SCHEDULES_PER_RELAY; ++i) {
        h = (h ^ s.startHour[i]) * 16777619UL;
        h = (h ^ s.startMinute[i]) * 16777619UL;
        h = (h ^ s.startSecond[i]) * 16777619UL;
        h = (h ^ s.stopHour[i]) * 16777619UL;
        h = (h ^ s.stopMinute[i]) * 16777619UL;
        h = (h ^ s.stopSecond[i]) * 16777619UL;
        h = (h ^ (s.enabled[i] ? 1 : 0)) * 16777619UL;
        h = (h ^ s.days[i]) * 16777619UL;
        h = (h ^ (uint8_t)(s.monthDays[i] & 0xFF)) * 16777619UL;
        h = (h ^ (uint8_t)((s.monthDays[i] >> 8) & 0xFF)) * 16777619UL;
        h = (h ^ (uint8_t)((s.monthDays[i] >> 16) & 0xFF)) * 16777619UL;
        h = (h ^ (uint8_t)((s.monthDays[i] >> 24) & 0xFF)) * 16777619UL;
        h = (h ^ (uint8_t)(s.monthMask[i] & 0xFF)) * 16777619UL;
        h = (h ^ (uint8_t)(s.monthMask[i] >> 8)) * 16777619UL;
    }
    return h;
}

void initMqttDefaults() {
    memset(&mqttConfig, 0, sizeof(mqttConfig));
    mqttConfig.magic = MQTT_CONFIG_MAGIC;
    mqttConfig.version = MQTT_CONFIG_VERSION;
    mqttConfig.port = 1883;
    mqttConfig.commandQos = 0;
    if (mqttValidBaseTopic(sysConfig.hostname)) {
        strncpy(mqttConfig.baseTopic, sysConfig.hostname, sizeof(mqttConfig.baseTopic) - 1);
    } else {
        strcpy(mqttConfig.baseTopic, "esp8266relay");
    }
    snprintf(mqttConfig.clientId, sizeof(mqttConfig.clientId), "esp8266-%06X", (unsigned int)(ESP.getChipId() & 0xFFFFFF));
}

void loadMqttConfig() {
    initMqttDefaults();
    if (LittleFS.exists(MQTT_CONFIG_FILE)) {
        loadFromFile(MQTT_CONFIG_FILE, &mqttConfig, sizeof(mqttConfig));
        if (mqttConfig.magic != MQTT_CONFIG_MAGIC || mqttConfig.version != MQTT_CONFIG_VERSION) {
            initMqttDefaults();
        }
    }
    mqttConfig.host[sizeof(mqttConfig.host) - 1] = '\0';
    mqttConfig.username[sizeof(mqttConfig.username) - 1] = '\0';
    mqttConfig.password[sizeof(mqttConfig.password) - 1] = '\0';
    mqttConfig.baseTopic[sizeof(mqttConfig.baseTopic) - 1] = '\0';
    mqttConfig.clientId[sizeof(mqttConfig.clientId) - 1] = '\0';
    mqttConfig.rootCA[sizeof(mqttConfig.rootCA) - 1] = '\0';
    if (!mqttValidBrokerHost(mqttConfig.host)) {
        mqttConfig.host[0] = '\0';
        mqttConfig.enabled = 0;
    }
    if (mqttConfig.port == 0) mqttConfig.port = mqttConfig.useTLS ? 8883 : 1883;
    if (mqttConfig.commandQos > 1) mqttConfig.commandQos = 0;
    if (!mqttValidBaseTopic(mqttConfig.baseTopic)) {
        if (mqttValidBaseTopic(sysConfig.hostname)) strncpy(mqttConfig.baseTopic, sysConfig.hostname, sizeof(mqttConfig.baseTopic) - 1);
        else strcpy(mqttConfig.baseTopic, "esp8266relay");
    }
    if (!mqttValidClientId(mqttConfig.clientId)) {
        snprintf(mqttConfig.clientId, sizeof(mqttConfig.clientId), "esp8266-%06X", (unsigned int)(ESP.getChipId() & 0xFFFFFF));
    }
    mqttConfig.enabled = mqttConfig.enabled ? 1 : 0;
    mqttConfig.remoteAdminEnabled = mqttConfig.remoteAdminEnabled ? 1 : 0;
    mqttConfig.useTLS = mqttConfig.useTLS ? 1 : 0;
    mqttConfig.magic = MQTT_CONFIG_MAGIC;
    mqttConfig.version = MQTT_CONFIG_VERSION;
    mqttBuildTopic(mqttAvailabilityTopic, sizeof(mqttAvailabilityTopic), "state/availability");
}

bool saveMqttConfig(const MqttConfig* cfg) {
    if (!cfg) return false;
    return saveToFileAtomic(MQTT_CONFIG_FILE, cfg, sizeof(MqttConfig));
}

static void mqttSetError(const char* s) {
    if (!s) s = "unknown";
    strncpy(mqttLastError, s, sizeof(mqttLastError) - 1);
    mqttLastError[sizeof(mqttLastError) - 1] = '\0';
}

static bool mqttPreflightFileWrites(uint8_t count) {
    if (count == 0) return true;
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_SAVE) return false;
    uint16_t used = (elapsedSince(flashWriteHourStart) >= 3600000UL) ? 0 : flashWriteCounter;
    return (uint16_t)(used + count) <= 100;
}

static bool mqttPublishText(const char* suffix, const char* payload, bool retained) {
    if (!mqttClient.connected() || !suffix || !payload) return false;
    char topic[MQTT_RX_TOPIC_MAX];
    if (!mqttBuildTopic(topic, sizeof(topic), suffix)) return false;
    if (strlen(topic) + strlen(payload) + 8 >= MQTT_PACKET_BUFFER) return false;
    return mqttClient.publish(topic, payload, retained);
}

static void mqttPublishCommandResult(const char* command, bool ok, const char* message) {
    String out;
    if (!out.reserve(640)) return;
    out = "{\"success\":";
    out += ok ? "true" : "false";
    out += ",\"command\":\"";
    appendJsonEscaped(out, command ? command : "");
    out += "\",\"message\":\"";
    appendJsonEscaped(out, message ? message : "");
    out += "\"}";
    mqttPublishText("state/command_result", out.c_str(), false);
}

static bool mqttPrepareTransport() {
    mqttTcpTransport.stop();
    mqttTlsTransport.stop();
    if (mqttTrustAnchors) {
        delete mqttTrustAnchors;
        mqttTrustAnchors = nullptr;
    }
    if (mqttConfig.useTLS) {
        if (mqttConfig.rootCA[0] == '\0') {
            mqttSetError("TLS requires a root CA certificate");
            return false;
        }
        if (ESP.getFreeHeap() < MQTT_TLS_MIN_HEAP) {
            mqttSetError("TLS deferred: insufficient free heap");
            return false;
        }
        epoch64_t nowEpoch = getCurrentEpoch();
        if (nowEpoch < EPOCH64_RTC_MIN || nowEpoch > EPOCH64_APP_MAX) {
            mqttSetError("TLS requires a valid device time");
            return false;
        }
        mqttTrustAnchors = new BearSSL::X509List(mqttConfig.rootCA);
        if (!mqttTrustAnchors) {
            mqttSetError("Unable to allocate TLS trust anchor");
            return false;
        }
        mqttTlsTransport.setTrustAnchors(mqttTrustAnchors);
        mqttTlsTransport.setX509Time((time_t)nowEpoch);
        mqttTlsTransport.setTimeout(5000);
        mqttClient.setClient(mqttTlsTransport);
    } else {
        mqttTcpTransport.setTimeout(3000);
        mqttClient.setClient(mqttTcpTransport);
    }
    mqttClient.setServer(mqttConfig.host, mqttConfig.port);
    return true;
}

void initializeMqttClient() {
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(25);
    mqttClient.setSocketTimeout(3);
    mqttTransportReady = mqttClient.setBufferSize(MQTT_PACKET_BUFFER);
    if (!mqttTransportReady) mqttSetError("Unable to allocate MQTT packet buffer");
    mqttRetryDelay = MQTT_RECONNECT_MIN;
    mqttLastAttempt = 0;
    mqttConnectedFlag = false;
    mqttStatePublishPending = false;
    mqttLastStatePoll = millis();
    mqttLastSystemPublish = millis();
}

void requestMqttReconfigure() {
    if (mqttClient.connected()) {
        mqttPublishText("state/availability", "offline", true);
        mqttClient.disconnect();
    }
    mqttTcpTransport.stop();
    mqttTlsTransport.stop();
    mqttConnectedFlag = false;
    mqttRetryDelay = MQTT_RECONNECT_MIN;
    mqttLastAttempt = 0;
    mqttStatePublishPending = false;
    mqttMessagePending = false;
    mqttScanPublishPending = false;
    memset(mqttCachedRelayValid, 0, sizeof(mqttCachedRelayValid));
    memset(mqttCachedNameHash, 0, sizeof(mqttCachedNameHash));
    memset(mqttCachedScheduleHash, 0, sizeof(mqttCachedScheduleHash));
}

void shutdownMqtt() {
    if (mqttClient.connected()) {
        mqttPublishText("state/availability", "offline", true);
        mqttClient.disconnect();
    }
    mqttTcpTransport.stop();
    mqttTlsTransport.stop();
    mqttConnectedFlag = false;
    mqttStatePublishPending = false;
    mqttMessagePending = false;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (mqttMessagePending) return;
    if (!topic || length > MQTT_RX_PAYLOAD_MAX || (length > 0 && !payload)) {
        mqttSetError("Incoming MQTT message is invalid or too large");
        return;
    }
    if (length > 0 && memchr(payload, '\0', length) != nullptr) {
        mqttSetError("Binary MQTT command payload is not supported");
        return;
    }
    size_t topicLength = strnlen(topic, MQTT_RX_TOPIC_MAX);
    if (topicLength == 0 || topicLength >= MQTT_RX_TOPIC_MAX) {
        mqttSetError("Incoming MQTT topic too large");
        return;
    }
    memcpy(mqttIncomingMessage.topic, topic, topicLength);
    mqttIncomingMessage.topic[topicLength] = '\0';
    if (length && payload) memcpy(mqttIncomingMessage.payload, payload, length);
    mqttIncomingMessage.payload[length] = '\0';
    mqttIncomingMessage.length = (uint16_t)length;
    mqttMessagePending = true;
}

static bool mqttPayloadIs(const char* payload, const char* expected) {
    if (!payload || !expected) return false;
    while (*payload && isspace((unsigned char)*payload)) ++payload;
    size_t n = strlen(payload);
    while (n > 0 && isspace((unsigned char)payload[n - 1])) --n;
    if (strlen(expected) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (toupper((unsigned char)payload[i]) != toupper((unsigned char)expected[i])) return false;
    }
    return true;
}

static bool mqttJsonError(DeserializationError err, char* message, size_t size) {
    if (!err) return false;
    if (message && size) snprintf(message, size, "Invalid JSON: %s", err.c_str());
    return true;
}

static bool mqttSetRelaySchedule(uint8_t relay, const char* payload, char* message, size_t messageSize) {
    if (relay >= numRelays || relay >= MAX_RELAYS) {
        snprintf(message, messageSize, "Relay out of range");
        return false;
    }
    if (ESP.getFreeHeap() < 12000UL) {
        snprintf(message, messageSize, "Insufficient free heap for schedule update");
        return false;
    }
    DynamicJsonDocument doc(3072);
    DeserializationError err = deserializeJson(doc, payload);
    if (mqttJsonError(err, message, messageSize)) return false;
    JsonArray schedules = doc["schedules"].as<JsonArray>();
    if (schedules.isNull() || schedules.size() > MQTT_SCHEDULES_PER_RELAY) {
        snprintf(message, messageSize, "Expected schedules array with at most 8 entries");
        return false;
    }
    TimerSchedule newSchedule = relayConfigs[relay].schedule;
    uint8_t s = 0;
    for (JsonObject sc : schedules) {
        int sh = sc["startHour"] | 0;
        int sm = sc["startMinute"] | 0;
        int ss = sc["startSecond"] | 0;
        int eh = sc["stopHour"] | 0;
        int em = sc["stopMinute"] | 0;
        int es = sc["stopSecond"] | 0;
        int days = sc["days"] | 0x7F;
        uint32_t monthDays = sc["monthDays"] | 0UL;
        uint32_t monthMask = sc["monthMask"] | 0UL;
        if (sh < 0 || sh > 23 || eh < 0 || eh > 23 || sm < 0 || sm > 59 ||
            em < 0 || em > 59 || ss < 0 || ss > 59 || es < 0 || es > 59 ||
            days < 0 || days > 0x7F || monthDays > 0x7FFFFFFFUL || monthMask > 0x0FFFUL) {
            snprintf(message, messageSize, "Schedule field out of range");
            return false;
        }
        newSchedule.startHour[s] = (uint8_t)sh;
        newSchedule.startMinute[s] = (uint8_t)sm;
        newSchedule.startSecond[s] = (uint8_t)ss;
        newSchedule.stopHour[s] = (uint8_t)eh;
        newSchedule.stopMinute[s] = (uint8_t)em;
        newSchedule.stopSecond[s] = (uint8_t)es;
        newSchedule.enabled[s] = sc["enabled"] | false;
        newSchedule.days[s] = (uint8_t)days;
        newSchedule.monthDays[s] = monthDays;
        newSchedule.monthMask[s] = (uint16_t)monthMask;
        ++s;
    }
    for (; s < MQTT_SCHEDULES_PER_RELAY; ++s) {
        newSchedule.startHour[s] = 0;
        newSchedule.startMinute[s] = 0;
        newSchedule.startSecond[s] = 0;
        newSchedule.stopHour[s] = 0;
        newSchedule.stopMinute[s] = 0;
        newSchedule.stopSecond[s] = 0;
        newSchedule.enabled[s] = false;
        newSchedule.days[s] = 0x7F;
        newSchedule.monthDays[s] = 0;
        newSchedule.monthMask[s] = 0;
    }
    relayConfigs[relay].schedule = newSchedule;
    markConfigDirty();
    processRelaySchedules();
    snprintf(message, messageSize, "Schedule saved");
    return true;
}

static bool mqttSetWifiConfig(const char* payload, char* message, size_t messageSize) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload);
    if (mqttJsonError(err, message, messageSize)) return false;
    const char* ssid = doc["ssid"];
    const char* password = doc["password"];
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 31 || !password || strlen(password) > 63) {
        snprintf(message, messageSize, "SSID and password are required and must fit their limits");
        return false;
    }
    char oldSsid[sizeof(sysConfig.sta_ssid)];
    char oldPassword[sizeof(sysConfig.sta_password)];
    memcpy(oldSsid, sysConfig.sta_ssid, sizeof(oldSsid));
    memcpy(oldPassword, sysConfig.sta_password, sizeof(oldPassword));
    strncpy(sysConfig.sta_ssid, ssid, sizeof(sysConfig.sta_ssid) - 1);
    sysConfig.sta_ssid[sizeof(sysConfig.sta_ssid) - 1] = '\0';
    strncpy(sysConfig.sta_password, password, sizeof(sysConfig.sta_password) - 1);
    sysConfig.sta_password[sizeof(sysConfig.sta_password) - 1] = '\0';
    if (!saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig))) {
        memcpy(sysConfig.sta_ssid, oldSsid, sizeof(oldSsid));
        memcpy(sysConfig.sta_password, oldPassword, sizeof(oldPassword));
        snprintf(message, messageSize, "Unable to save WiFi settings");
        return false;
    }
    if (wifiStationEnabled) {
        wifiConnected = false;
        wcsState = WCS_IDLE;
        pendingWiFiReconnect = true;
        pendingWiFiReconnectAt = millis();
    }
    snprintf(message, messageSize, "WiFi settings saved");
    return true;
}

static bool mqttSetNtpConfig(const char* payload, char* message, size_t messageSize) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload);
    if (mqttJsonError(err, message, messageSize)) return false;
    const char* serverName = doc["ntpServer"] | sysConfig.ntp_server;
    bool serverNameValid = serverName && strlen(serverName) > 0 && strlen(serverName) < sizeof(sysConfig.ntp_server);
    if (serverNameValid) {
        for (const unsigned char* p = (const unsigned char*)serverName; *p; ++p) {
            if (isspace(*p) || *p < 0x21 || *p > 0x7E) { serverNameValid = false; break; }
        }
    }
    if (!serverNameValid) {
        snprintf(message, messageSize, "Invalid NTP server name");
        return false;
    }
    long gmt = doc["gmtOffset"] | sysConfig.gmt_offset;
    long dst = doc["daylightOffset"] | sysConfig.daylight_offset;
    long syncHours = doc["syncHours"] | extConfig.ntp_sync_hours;
    if (gmt < -43200L || gmt > 50400L || dst < -7200L || dst > 7200L || syncHours < 1 || syncHours > 24) {
        snprintf(message, messageSize, "NTP setting out of range");
        return false;
    }
    if (!mqttPreflightFileWrites(2)) {
        snprintf(message, messageSize, "Insufficient flash-write budget or free heap for NTP settings");
        return false;
    }
    SystemConfig oldSystem = sysConfig;
    ExtConfig oldExt = extConfig;
    strncpy(sysConfig.ntp_server, serverName, sizeof(sysConfig.ntp_server) - 1);
    sysConfig.ntp_server[sizeof(sysConfig.ntp_server) - 1] = '\0';
    sysConfig.gmt_offset = gmt;
    sysConfig.daylight_offset = (int)dst;
    extConfig.ntp_sync_hours = (uint8_t)syncHours;
    bool ok = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    if (ok) ok = saveToFileAtomic(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig));
    if (!ok) {
        sysConfig = oldSystem;
        extConfig = oldExt;
        snprintf(message, messageSize, "Unable to save NTP settings");
        return false;
    }
    if (wifiConnected) {
        timeClient.setPoolServerName(sysConfig.ntp_server);
        timeClient.setTimeOffset(0);
    }
    snprintf(message, messageSize, "NTP settings saved");
    return true;
}

static bool mqttSetApConfig(const char* payload, char* message, size_t messageSize) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, payload);
    if (mqttJsonError(err, message, messageSize)) return false;
    const char* ssid = doc["ap_ssid"];
    const char* password = doc["ap_password"];
    if (!password) password = sysConfig.ap_password;
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 31 || strlen(password) > 31 ||
        (strlen(password) > 0 && strlen(password) < 8)) {
        snprintf(message, messageSize, "Invalid AP SSID or password");
        return false;
    }
    if (!mqttPreflightFileWrites(2)) {
        snprintf(message, messageSize, "Insufficient flash-write budget or free heap for AP settings");
        return false;
    }
    SystemConfig oldSystem = sysConfig;
    ExtConfig oldExt = extConfig;
    strncpy(sysConfig.ap_ssid, ssid, sizeof(sysConfig.ap_ssid) - 1);
    sysConfig.ap_ssid[sizeof(sysConfig.ap_ssid) - 1] = '\0';
    strncpy(sysConfig.ap_password, password, sizeof(sysConfig.ap_password) - 1);
    sysConfig.ap_password[sizeof(sysConfig.ap_password) - 1] = '\0';
    if (doc.containsKey("ap_channel")) {
        int ch = doc["ap_channel"] | extConfig.ap_channel;
        if (ch < 1 || ch > 13) { sysConfig = oldSystem; extConfig = oldExt; snprintf(message, messageSize, "Invalid AP channel"); return false; }
        extConfig.ap_channel = (uint8_t)ch;
    }
    if (doc.containsKey("ap_hidden")) extConfig.ap_hidden = doc["ap_hidden"] ? 1 : 0;
    bool ok = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    if (ok) ok = saveToFileAtomic(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig));
    if (!ok) {
        sysConfig = oldSystem;
        extConfig = oldExt;
        snprintf(message, messageSize, "Unable to save AP settings");
        return false;
    }
    pendingAPRestart = true;
    pendingAPRestartAt = millis();
    snprintf(message, messageSize, "AP settings saved; restart scheduled");
    return true;
}

static bool mqttSetHostname(const char* payload, char* message, size_t messageSize) {
    size_t n = strnlen(payload, sizeof(sysConfig.hostname));
    if (n == 0 || n >= sizeof(sysConfig.hostname)) {
        snprintf(message, messageSize, "Hostname must be 1-31 characters");
        return false;
    }
    char oldHost[sizeof(sysConfig.hostname)];
    memcpy(oldHost, sysConfig.hostname, sizeof(oldHost));
    for (size_t i = 0; i < n; ++i) {
        char c = payload[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) c = '-';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        sysConfig.hostname[i] = c;
    }
    sysConfig.hostname[n] = '\0';
    if (!saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig))) {
        memcpy(sysConfig.hostname, oldHost, sizeof(oldHost));
        snprintf(message, messageSize, "Unable to save hostname");
        return false;
    }
    if (wifiConnected && wifiStationEnabled) startMDNS();
    snprintf(message, messageSize, "Hostname saved");
    return true;
}

static bool mqttSetPinConfig(const char* payload, char* message, size_t messageSize) {
    if (ESP.getFreeHeap() < 16000UL) {
        snprintf(message, messageSize, "Insufficient free heap for pin update");
        return false;
    }
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);
    if (mqttJsonError(err, message, messageSize)) return false;
    int requested = doc["numRelays"] | -1;
    bool globalLow = doc["globalActiveLow"] | relayActiveLow;
    JsonArray relays = doc["relays"].as<JsonArray>();
    static const uint8_t validPins[] = {16, 14, 12, 13, 3, 1};
    if (requested < 1 || requested > 6 || relays.isNull() || relays.size() != (size_t)requested) {
        snprintf(message, messageSize, "Pin configuration must contain 1-6 relays");
        return false;
    }
    bool used[6] = {false, false, false, false, false, false};
    for (JsonObject r : relays) {
        int p = r["pin"] | -1;
        int k = -1;
        for (uint8_t i = 0; i < 6; ++i) if (validPins[i] == p) { k = i; break; }
        if (k < 0 || used[k]) {
            snprintf(message, messageSize, "Invalid or duplicate GPIO pin");
            return false;
        }
        used[k] = true;
    }
    if (!mqttPreflightFileWrites(2)) {
        snprintf(message, messageSize, "Insufficient flash-write budget or free heap for pin configuration");
        return false;
    }
    RelayConfig* oldRelays = (RelayConfig*)malloc(sizeof(RelayConfig) * MAX_RELAYS);
    if (!oldRelays) { snprintf(message, messageSize, "Insufficient free heap"); return false; }
    memcpy(oldRelays, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    for (int i = 0; i < MAX_RELAYS; ++i) {
        bool kept = false;
        for (JsonObject r : relays) {
            if (relayConfigs[i].pin == (uint8_t)(r["pin"] | 0)) { kept = true; break; }
        }
        if (!kept) {
            for (uint8_t k = 0; k < 6; ++k) {
                if (relayConfigs[i].pin == validPins[k]) {
                    pinMode(relayConfigs[i].pin, OUTPUT);
                    digitalWrite(relayConfigs[i].pin, relayConfigs[i].activeLow ? HIGH : LOW);
                    break;
                }
            }
        }
    }
    PinConfig oldPinConfig = pinConfig;
    uint8_t oldCount = numRelays;
    bool oldGlobal = relayActiveLow;
    pinConfig.magic = PINS_MAGIC;
    pinConfig.numRelays = (uint8_t)requested;
    pinConfig.globalActiveLow = globalLow;
    numRelays = (uint8_t)requested;
    relayActiveLow = globalLow;
    uint8_t idx = 0;
    for (JsonObject r : relays) {
        relayConfigs[idx].pin = (uint8_t)(r["pin"] | 0);
        relayConfigs[idx].activeLow = r.containsKey("activeLow") ? (bool)r["activeLow"] : globalLow;
        const char* name = r["name"];
        if (name && *name) {
            copyNameUtf8Safe(relayConfigs[idx].name, sizeof(relayConfigs[idx].name), name);
        }
        if (relayConfigs[idx].name[0] == '\0') snprintf(relayConfigs[idx].name, sizeof(relayConfigs[idx].name), "Relay %u", (unsigned)(idx + 1));
        ++idx;
    }
    for (; idx < MAX_RELAYS; ++idx) {
        if (idx >= requested) relayConfigs[idx].pin = 0xFF;
    }
    bool ok = saveToFileAtomic(PINS_FILE, &pinConfig, sizeof(PinConfig));
    if (ok) ok = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    if (!ok) {
        pinConfig = oldPinConfig;
        numRelays = oldCount;
        relayActiveLow = oldGlobal;
        memcpy(relayConfigs, oldRelays, sizeof(RelayConfig) * MAX_RELAYS);
        updateRelayPins();
        free(oldRelays);
        snprintf(message, messageSize, "Unable to save pin configuration");
        return false;
    }
    free(oldRelays);
    configDirty = false;
    lastConfigSave = millis();
    updateRelayPins();
    snprintf(message, messageSize, "Pin configuration saved");
    return true;
}

static bool mqttResetPinConfig(char* message, size_t messageSize) {
    if (ESP.getFreeHeap() < 16000UL) {
        snprintf(message, messageSize, "Insufficient free heap for pin reset");
        return false;
    }
    if (!mqttPreflightFileWrites(3)) {
        snprintf(message, messageSize, "Insufficient flash-write budget or free heap for pin reset");
        return false;
    }
    RelayConfig* oldRelays = (RelayConfig*)malloc(sizeof(RelayConfig) * MAX_RELAYS);
    if (!oldRelays) { snprintf(message, messageSize, "Insufficient free heap"); return false; }
    memcpy(oldRelays, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    PinConfig oldPinConfig = pinConfig;
    uint8_t oldCount = numRelays;
    bool oldGlobal = relayActiveLow;
    static const uint8_t defaultPins[] = {16, 14, 12, 13, 3, 1};
    for (uint8_t i = 0; i < 6; ++i) {
        relayConfigs[i].pin = defaultPins[i];
        relayConfigs[i].activeLow = true;
        if (relayConfigs[i].name[0] == '\0') snprintf(relayConfigs[i].name, sizeof(relayConfigs[i].name), "Relay %u", (unsigned)(i + 1));
    }
    for (uint8_t i = 6; i < MAX_RELAYS; ++i) {
        memset(&relayConfigs[i], 0, sizeof(RelayConfig));
        relayConfigs[i].pin = 0xFF;
        relayConfigs[i].activeLow = true;
    }
    pinConfig.magic = PINS_MAGIC;
    pinConfig.numRelays = 6;
    pinConfig.globalActiveLow = true;
    numRelays = 6;
    relayActiveLow = true;
    bool ok = saveToFileAtomic(PINS_FILE, &pinConfig, sizeof(PinConfig));
    if (ok) ok = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    if (ok) ok = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    if (!ok) {
        pinConfig = oldPinConfig;
        numRelays = oldCount;
        relayActiveLow = oldGlobal;
        memcpy(relayConfigs, oldRelays, sizeof(RelayConfig) * MAX_RELAYS);
        updateRelayPins();
        free(oldRelays);
        snprintf(message, messageSize, "Unable to save default pin configuration");
        return false;
    }
    free(oldRelays);
    configDirty = false;
    lastConfigSave = millis();
    updateRelayPins();
    snprintf(message, messageSize, "Pins reset to defaults");
    return true;
}

static void mqttPublishRelaySnapshot(uint8_t relay, bool force) {
    if (!mqttClient.connected() || relay >= numRelays || relay >= MAX_RELAYS) return;
    RelayConfig& rc = relayConfigs[relay];
    bool state = false;
    if (rc.pin != 0xFF) {
        state = digitalRead(rc.pin);
        if (rc.activeLow) state = !state;
    }
    bool mode = rc.manualOverride;
    uint32_t nameHash = mqttHashText(rc.name);
    uint32_t scheduleHash = mqttHashSchedule(rc);
    char suffix[96];
    bool stateOk = true;
    bool modeOk = true;
    snprintf(suffix, sizeof(suffix), "state/relay/%u/state", (unsigned)(relay + 1));
    if (force || !mqttCachedRelayValid[relay] || mqttCachedState[relay] != state) {
        stateOk = mqttPublishText(suffix, state ? "ON" : "OFF", true);
        if (stateOk) mqttCachedState[relay] = state;
    }
    snprintf(suffix, sizeof(suffix), "state/relay/%u/mode", (unsigned)(relay + 1));
    if (force || !mqttCachedRelayValid[relay] || mqttCachedMode[relay] != mode) {
        modeOk = mqttPublishText(suffix, mode ? "MANUAL" : "AUTO", true);
        if (modeOk) mqttCachedMode[relay] = mode;
    }
    if (force || mqttCachedNameHash[relay] != nameHash) {
        snprintf(suffix, sizeof(suffix), "state/relay/%u/name", (unsigned)(relay + 1));
        if (mqttPublishText(suffix, rc.name, true)) mqttCachedNameHash[relay] = nameHash;
    }
    if (force || mqttCachedScheduleHash[relay] != scheduleHash) {
        String out;
        if (out.reserve(1400)) {
            out = "{\"schedules\":[";
            for (uint8_t s = 0; s < MQTT_SCHEDULES_PER_RELAY; ++s) {
                if (s) out += ',';
                out += "{\"startHour\":"; out += String(rc.schedule.startHour[s]);
                out += ",\"startMinute\":"; out += String(rc.schedule.startMinute[s]);
                out += ",\"startSecond\":"; out += String(rc.schedule.startSecond[s]);
                out += ",\"stopHour\":"; out += String(rc.schedule.stopHour[s]);
                out += ",\"stopMinute\":"; out += String(rc.schedule.stopMinute[s]);
                out += ",\"stopSecond\":"; out += String(rc.schedule.stopSecond[s]);
                out += ",\"enabled\":"; out += rc.schedule.enabled[s] ? "true" : "false";
                out += ",\"days\":"; out += String(rc.schedule.days[s]);
                out += ",\"monthDays\":"; out += String((unsigned long)rc.schedule.monthDays[s]);
                out += ",\"monthMask\":"; out += String(rc.schedule.monthMask[s]);
                out += '}';
            }
            out += "]}";
            snprintf(suffix, sizeof(suffix), "state/relay/%u/schedule", (unsigned)(relay + 1));
            if (mqttPublishText(suffix, out.c_str(), true)) mqttCachedScheduleHash[relay] = scheduleHash;
        }
    }
    if (stateOk && modeOk) mqttCachedRelayValid[relay] = true;
}

void mqttPublishMetadata() {
    if (!mqttClient.connected() || ESP.getFreeHeap() < MIN_HEAP_FOR_FULL_RESPONSE) return;
    String out;
    if (!out.reserve(1400)) return;
    out = "{\"hostname\":\""; appendJsonEscaped(out, sysConfig.hostname);
    out += "\",\"wifiConnected\":"; out += wifiConnected ? "true" : "false";
    out += ",\"stationEnabled\":"; out += wifiStationEnabled ? "true" : "false";
    out += ",\"ip\":\""; out += WiFi.localIP().toString();
    out += "\",\"apIp\":\""; out += WiFi.softAPIP().toString();
    out += "\",\"rssi\":"; out += String(wifiConnected ? (int)WiFi.RSSI() : 0);
    out += ",\"freeHeap\":"; out += String(ESP.getFreeHeap());
    out += ",\"uptimeSeconds\":"; out += String((unsigned long)(getUptimeMs64() / 1000ULL));
    epoch64_t epoch = getCurrentEpoch();
    char epochText[32];
    if (epoch64IsValid(epoch)) snprintf(epochText, sizeof(epochText), "%lld", (long long)epoch);
    else strcpy(epochText, "null");
    out += ",\"epoch\":"; out += epochText;
    out += ",\"rtcInitialized\":"; out += rtcInitialized ? "true" : "false";
    out += ",\"ds3231Present\":"; out += ds3231Present ? "true" : "false";
    out += ",\"mqttConnected\":"; out += mqttClient.connected() ? "true" : "false";
    out += "}";
    mqttPublishText("state/system", out.c_str(), true);

    out = "{\"ssid\":\""; appendJsonEscaped(out, sysConfig.sta_ssid);
    out += "\",\"connected\":"; out += wifiConnected ? "true" : "false";
    out += ",\"stationEnabled\":"; out += wifiStationEnabled ? "true" : "false";
    out += ",\"ip\":\""; out += WiFi.localIP().toString();
    out += "\",\"rssi\":"; out += String(wifiConnected ? (int)WiFi.RSSI() : 0); out += '}';
    mqttPublishText("state/wifi", out.c_str(), true);

    out = "{\"ntpServer\":\""; appendJsonEscaped(out, sysConfig.ntp_server);
    out += "\",\"gmtOffset\":"; out += String(sysConfig.gmt_offset);
    out += ",\"daylightOffset\":"; out += String(sysConfig.daylight_offset);
    out += ",\"syncHours\":"; out += String(extConfig.ntp_sync_hours);
    out += ",\"epoch\":"; out += epochText; out += '}';
    mqttPublishText("state/ntp", out.c_str(), true);

    out = "{\"ssid\":\""; appendJsonEscaped(out, sysConfig.ap_ssid);
    out += "\",\"channel\":"; out += String(extConfig.ap_channel);
    out += ",\"hidden\":"; out += extConfig.ap_hidden ? "true" : "false";
    out += ",\"hasPassword\":"; out += strlen(sysConfig.ap_password) ? "true" : "false"; out += '}';
    mqttPublishText("state/ap", out.c_str(), true);

    out = "{\"numRelays\":"; out += String(numRelays);
    out += ",\"globalActiveLow\":"; out += relayActiveLow ? "true" : "false";
    out += ",\"relays\":[";
    int safeNum = numRelays > MAX_RELAYS ? MAX_RELAYS : numRelays;
    for (int i = 0; i < safeNum; ++i) {
        if (i) out += ',';
        out += "{\"relay\":"; out += String(i + 1);
        out += ",\"name\":\""; appendJsonEscaped(out, relayConfigs[i].name);
        out += "\",\"pin\":"; out += String(relayConfigs[i].pin);
        out += ",\"activeLow\":"; out += relayConfigs[i].activeLow ? "true" : "false"; out += '}';
    }
    out += "]}";
    mqttPublishText("state/pins", out.c_str(), true);
    mqttLastSystemPublish = millis();
}

static bool mqttCompleteWifiScan(const char* payload) {
    if (!mqttPublishText("state/wifi/scan", payload, false)) return false;
    WiFi.scanDelete();
    scanResultCount = -1;
    mqttScanPublishPending = false;
    return true;
}

static void mqttPublishWifiScan() {
    if (!mqttClient.connected() || scanResultCount < 0) return;
    static const char EMPTY_SCAN[] = "{\"scanning\":false,\"networks\":[]}";
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_FULL_RESPONSE) {
        mqttCompleteWifiScan(EMPTY_SCAN);
        return;
    }
    DynamicJsonDocument doc(2048);
    doc["scanning"] = false;
    int count = scanResultCount;
    if (count > 8) count = 8;
    doc["totalFound"] = scanResultCount;
    doc["truncated"] = scanResultCount > count;
    JsonArray nets = doc.createNestedArray("networks");
    for (int i = 0; i < count; ++i) {
        JsonObject n = nets.createNestedObject();
        n["ssid"] = WiFi.SSID(i);
        n["rssi"] = WiFi.RSSI(i);
        n["enc"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    }
    if (doc.overflowed()) {
        mqttCompleteWifiScan(EMPTY_SCAN);
        return;
    }
    String out;
    if (!out.reserve(1900)) {
        mqttCompleteWifiScan(EMPTY_SCAN);
        return;
    }
    size_t jsonLength = serializeJson(doc, out);
    char topic[MQTT_RX_TOPIC_MAX];
    if (jsonLength == 0 || !mqttBuildTopic(topic, sizeof(topic), "state/wifi/scan") ||
        jsonLength + strlen(topic) + 8 >= MQTT_PACKET_BUFFER) {
        mqttCompleteWifiScan(EMPTY_SCAN);
        return;
    }
    mqttCompleteWifiScan(out.c_str());
}

static bool mqttClearRetiredRelayTopics(uint8_t relayIndex) {
    char suffix[96];
    const char* fields[] = {"state", "mode", "name", "schedule"};
    uint8_t relayNumber = (uint8_t)(relayIndex + 1);
    for (uint8_t i = 0; i < 4; ++i) {
        snprintf(suffix, sizeof(suffix), "state/relay/%u/%s", (unsigned)relayNumber, fields[i]);
        if (!mqttPublishText(suffix, "", true)) return false;
    }
    return true;
}

void mqttPublishPeriodic() {
    unsigned long now = millis();
    if (scanInProgress && elapsedSince(scanStartTime) > SCAN_TIMEOUT_MS) {
        int completedCount = WiFi.scanComplete();
        ++mqttWiFiScanGeneration;
        scanInProgress = false;
        scanResultCount = completedCount >= 0 ? completedCount : -1;
    }
    if (!mqttClient.connected()) return;
    int safeNum = numRelays > MAX_RELAYS ? MAX_RELAYS : numRelays;
    if (safeNum != mqttSnapshotRelayCount) {
        if (safeNum < mqttSnapshotRelayCount || safeNum < mqttLastRelayCount) {
            mqttClearRetiredRelays = true;
            mqttSnapshotClearRelay = (uint8_t)safeNum;
        } else if (mqttClearRetiredRelays && mqttSnapshotClearRelay < safeNum) {
            mqttSnapshotClearRelay = (uint8_t)safeNum;
        }
        mqttSnapshotRelayCount = (uint8_t)safeNum;
        mqttSnapshotRelay = 0;
        mqttStatePublishPending = true;
    }
    if (mqttScanPublishPending && !scanInProgress && scanResultCount < 0) {
        if (mqttPublishText("state/wifi/scan", "{\"scanning\":false,\"networks\":[]}", false)) {
            WiFi.scanDelete();
            mqttScanPublishPending = false;
        }
    }
    if (mqttStatePublishPending && elapsedSince(mqttLastSnapshotStep) >= 120UL) {
        mqttLastSnapshotStep = now;
        if (mqttSnapshotRelay < safeNum) {
            mqttPublishRelaySnapshot(mqttSnapshotRelay, true);
            ++mqttSnapshotRelay;
        } else if (mqttClearRetiredRelays) {
            if (mqttSnapshotClearRelay < safeNum) mqttSnapshotClearRelay = (uint8_t)safeNum;
            if (mqttSnapshotClearRelay < MAX_RELAYS) {
                if (mqttClearRetiredRelayTopics(mqttSnapshotClearRelay)) ++mqttSnapshotClearRelay;
            } else {
                mqttClearRetiredRelays = false;
            }
        } else {
            mqttPublishMetadata();
            mqttStatePublishPending = false;
            mqttLastRelayCount = (uint8_t)safeNum;
            mqttSnapshotRelayCount = (uint8_t)safeNum;
            mqttLastStatePoll = now;
        }
    }
    if (elapsedSince(mqttLastStatePoll) >= MQTT_STATE_INTERVAL) {
        mqttLastStatePoll = now;
        for (int i = 0; i < safeNum; ++i) mqttPublishRelaySnapshot((uint8_t)i, false);
    }
    if (elapsedSince(mqttLastSystemPublish) >= MQTT_SYSTEM_INTERVAL) {
        if (ESP.getFreeHeap() >= MIN_HEAP_FOR_FULL_RESPONSE) {
            mqttPublishMetadata();
        } else {
            mqttLastSystemPublish = millis() - (MQTT_SYSTEM_INTERVAL - 5000UL);
        }
    }
    if (mqttScanPublishPending && !scanInProgress && scanResultCount >= 0) mqttPublishWifiScan();
}

static bool mqttStartWifiScan(char* message, size_t messageSize) {
    int priorScan = WiFi.scanComplete();
    if (scanInProgress || priorScan == -1 || wcsState != WCS_IDLE) {
        snprintf(message, messageSize, "WiFi scan or station connection is already in progress");
        return false;
    }
    if (priorScan >= 0) WiFi.scanDelete();
    scanInProgress = true;
    scanResultCount = -1;
    scanStartTime = millis();
    mqttScanPublishPending = true;
    uint16_t generation = ++mqttWiFiScanGeneration;
    WiFi.scanNetworksAsync([generation](int n) {
        if (mqttWiFiScanGeneration != generation) return;
        scanResultCount = n;
        scanInProgress = false;
    }, false);
    if (WiFi.scanComplete() == -2) {
        ++mqttWiFiScanGeneration;
        scanInProgress = false;
        scanResultCount = -1;
        mqttScanPublishPending = false;
        snprintf(message, messageSize, "Unable to start WiFi scan");
        return false;
    }
    snprintf(message, messageSize, "WiFi scan started");
    return true;
}

static bool mqttStartRelayCommand(const char* path, const char* payload, char* message, size_t messageSize) {
    if (strncmp(path, "relay/", 6) != 0) return false;
    char* end = nullptr;
    long relayNumber = strtol(path + 6, &end, 10);
    if (end == path + 6 || relayNumber < 1 || relayNumber > numRelays || relayNumber > MAX_RELAYS || *end != '/') {
        snprintf(message, messageSize, "Invalid relay topic");
        return false;
    }
    uint8_t relay = (uint8_t)(relayNumber - 1);
    if (relayConfigs[relay].pin == 0xFF) {
        snprintf(message, messageSize, "Relay has no assigned GPIO pin");
        return false;
    }
    const char* action = end + 1;
    if (strcmp(action, "set") == 0) {
        bool state;
        if (mqttPayloadIs(payload, "ON") || mqttPayloadIs(payload, "1")) state = true;
        else if (mqttPayloadIs(payload, "OFF") || mqttPayloadIs(payload, "0")) state = false;
        else { snprintf(message, messageSize, "Expected ON or OFF"); return false; }
        relayConfigs[relay].manualOverride = true;
        relayConfigs[relay].manualState = state;
        if (relayConfigs[relay].pin != 0xFF) digitalWrite(relayConfigs[relay].pin, relayConfigs[relay].activeLow ? !state : state);
        markConfigDirty();
        snprintf(message, messageSize, "Relay set to %s in manual mode", state ? "ON" : "OFF");
        return true;
    }
    if (strcmp(action, "mode/set") == 0) {
        if (mqttPayloadIs(payload, "AUTO")) {
            relayConfigs[relay].manualOverride = false;
            processRelaySchedules();
            markConfigDirty();
            snprintf(message, messageSize, "Automatic schedule resumed");
            return true;
        }
        if (mqttPayloadIs(payload, "MANUAL")) {
            bool state = false;
            if (relayConfigs[relay].pin != 0xFF) {
                state = digitalRead(relayConfigs[relay].pin);
                if (relayConfigs[relay].activeLow) state = !state;
            }
            relayConfigs[relay].manualOverride = true;
            relayConfigs[relay].manualState = state;
            markConfigDirty();
            snprintf(message, messageSize, "Manual mode enabled");
            return true;
        }
        snprintf(message, messageSize, "Expected AUTO or MANUAL");
        return false;
    }
    if (strcmp(action, "name/set") == 0) {
        size_t n = strnlen(payload, sizeof(relayConfigs[relay].name));
        if (n == 0 || n >= sizeof(relayConfigs[relay].name)) {
            snprintf(message, messageSize, "Relay name must be 1-15 bytes");
            return false;
        }
        bool content = false;
        for (size_t i = 0; i < n; ++i) if (!isspace((unsigned char)payload[i])) { content = true; break; }
        if (!content) { snprintf(message, messageSize, "Relay name cannot be blank"); return false; }
        copyNameUtf8Safe(relayConfigs[relay].name, sizeof(relayConfigs[relay].name), payload);
        markConfigDirty();
        snprintf(message, messageSize, "Relay name saved");
        return true;
    }
    if (strcmp(action, "schedule/set") == 0) {
        return mqttSetRelaySchedule(relay, payload, message, messageSize);
    }
    snprintf(message, messageSize, "Unknown relay command");
    return false;
}

void mqttProcessCommand() {
    if (!mqttMessagePending) return;
    mqttMessagePending = false;
    char prefix[MQTT_RX_TOPIC_MAX];
    int prefixLength = snprintf(prefix, sizeof(prefix), "%s/cmd/", mqttConfig.baseTopic);
    if (prefixLength <= 0 || (size_t)prefixLength >= sizeof(prefix) ||
        strncmp(mqttIncomingMessage.topic, prefix, (size_t)prefixLength) != 0) return;
    const char* path = mqttIncomingMessage.topic + prefixLength;
    char message[128];
    message[0] = '\0';
    bool isRelayBasic = false;
    if (strncmp(path, "relay/", 6) == 0) {
        const char* slash = strchr(path + 6, '/');
        if (slash && (strcmp(slash + 1, "set") == 0 || strcmp(slash + 1, "mode/set") == 0)) isRelayBasic = true;
    }
    if (strcmp(path, "status/get") == 0 || strcmp(path, "system/get") == 0) isRelayBasic = true;
    if (!isRelayBasic && !mqttConfig.remoteAdminEnabled) {
        mqttPublishCommandResult(path, false, "Remote administration is disabled");
        return;
    }
    bool ok = false;
    if (strncmp(path, "relay/", 6) == 0) {
        ok = mqttStartRelayCommand(path, mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "status/get") == 0 || strcmp(path, "system/get") == 0) {
        mqttStatePublishPending = true;
        mqttSnapshotRelay = 0;
        ok = true;
        snprintf(message, sizeof(message), "State snapshot requested");
    } else if (strcmp(path, "time/set") == 0) {
        char* end = nullptr;
        long long value = strtoll(mqttIncomingMessage.payload, &end, 10);
        while (end && *end && isspace((unsigned char)*end)) ++end;
        if (!end || end == mqttIncomingMessage.payload || *end || value < EPOCH64_MIN_VALID || value > EPOCH64_APP_MAX) {
            snprintf(message, sizeof(message), "Expected a valid Unix epoch in seconds");
        } else {
            syncInternalRTCFromBrowser((epoch64_t)value);
            ok = true;
            snprintf(message, sizeof(message), "Device time updated");
        }
    } else if (strcmp(path, "ntp/sync") == 0) {
        if (!wifiConnected || !wifiStationEnabled || WiFi.status() != WL_CONNECTED) {
            snprintf(message, sizeof(message), "WiFi station is not connected");
        } else if (ntpState != NTP_IDLE) {
            snprintf(message, sizeof(message), "NTP sync is already in progress");
        } else {
            tryNTPSync();
            if (ntpState == NTP_REQUESTING) {
                ok = true;
                snprintf(message, sizeof(message), "NTP sync requested");
            } else {
                snprintf(message, sizeof(message), "NTP sync could not start");
            }
        }
    } else if (strcmp(path, "ntp/config/set") == 0) {
        ok = mqttSetNtpConfig(mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "wifi/station/set") == 0) {
        if (mqttPayloadIs(mqttIncomingMessage.payload, "ON") || mqttPayloadIs(mqttIncomingMessage.payload, "ENABLE")) {
            enableWiFiStation(); ok = true; snprintf(message, sizeof(message), "WiFi station enabled");
        } else if (mqttPayloadIs(mqttIncomingMessage.payload, "OFF") || mqttPayloadIs(mqttIncomingMessage.payload, "DISABLE")) {
            pendingStationDisable = true; pendingStationDisableAt = millis();
            ok = true; snprintf(message, sizeof(message), "WiFi station disable scheduled");
        } else snprintf(message, sizeof(message), "Expected ON/OFF or ENABLE/DISABLE");
    } else if (strcmp(path, "wifi/config/set") == 0) {
        ok = mqttSetWifiConfig(mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "wifi/scan") == 0) {
        if (mqttPayloadIs(mqttIncomingMessage.payload, "SCAN")) ok = mqttStartWifiScan(message, sizeof(message));
        else snprintf(message, sizeof(message), "Expected SCAN");
    } else if (strcmp(path, "ap/config/set") == 0) {
        ok = mqttSetApConfig(mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "system/hostname/set") == 0) {
        ok = mqttSetHostname(mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "system/reconnect") == 0) {
        if (!mqttPayloadIs(mqttIncomingMessage.payload, "RECONNECT")) snprintf(message, sizeof(message), "Expected RECONNECT");
        else if (!wifiStationEnabled) snprintf(message, sizeof(message), "WiFi station is disabled");
        else {
            wifiConnected = false; mdnsStarted = false; wcsState = WCS_IDLE;
            selfRecoveryState = SR_WIFI_RECONNECT;
            ok = true; snprintf(message, sizeof(message), "WiFi reconnect requested");
        }
    } else if (strcmp(path, "system/factory-reset") == 0) {
        if (!mqttPayloadIs(mqttIncomingMessage.payload, "RESET")) snprintf(message, sizeof(message), "Expected RESET");
        else { performFactoryReset(); ok = true; snprintf(message, sizeof(message), "Factory reset started"); }
    } else if (strcmp(path, "pins/config/set") == 0) {
        ok = mqttSetPinConfig(mqttIncomingMessage.payload, message, sizeof(message));
    } else if (strcmp(path, "pins/reset") == 0) {
        if (!mqttPayloadIs(mqttIncomingMessage.payload, "RESET_PINS")) snprintf(message, sizeof(message), "Expected RESET_PINS");
        else ok = mqttResetPinConfig(message, sizeof(message));
    } else {
        snprintf(message, sizeof(message), "Unknown MQTT command");
    }
    mqttPublishCommandResult(path, ok, message);
    if (ok) {
        mqttStatePublishPending = true;
        mqttSnapshotRelay = 0;
    }
}

static void mqttConnectedSetup() {
    char subscribeTopic[MQTT_RX_TOPIC_MAX];
    int n = snprintf(subscribeTopic, sizeof(subscribeTopic), "%s/cmd/#", mqttConfig.baseTopic);
    if (n > 0 && (size_t)n < sizeof(subscribeTopic)) mqttClient.subscribe(subscribeTopic, mqttConfig.commandQos);
    mqttPublishText("state/availability", "online", true);
    mqttConnectedFlag = true;
    mqttRetryDelay = MQTT_RECONNECT_MIN;
    mqttSetError("connected");
    mqttStatePublishPending = true;
    mqttSnapshotRelay = 0;
    mqttLastRelayCount = 0;
    int safeNum = numRelays > MAX_RELAYS ? MAX_RELAYS : numRelays;
    mqttSnapshotRelayCount = (uint8_t)safeNum;
    mqttSnapshotClearRelay = (uint8_t)safeNum;
    mqttClearRetiredRelays = true;
    memset(mqttCachedRelayValid, 0, sizeof(mqttCachedRelayValid));
    memset(mqttCachedNameHash, 0, sizeof(mqttCachedNameHash));
    memset(mqttCachedScheduleHash, 0, sizeof(mqttCachedScheduleHash));
    mqttLastSnapshotStep = millis() - 120UL;
    mqttLastSystemPublish = millis();
}

void mqttService() {
    bool stationReady = mqttConfig.enabled && mqttConfig.host[0] && wifiStationEnabled &&
                        wifiConnected && WiFi.status() == WL_CONNECTED;
    if (!stationReady) {
        mqttWasStationReady = false;
        if (mqttClient.connected()) {
            mqttPublishText("state/availability", "offline", true);
            mqttClient.disconnect();
        }
        mqttConnectedFlag = false;
        if (!mqttConfig.enabled) mqttSetError("MQTT disabled");
        else if (!mqttConfig.host[0]) mqttSetError("Broker host not configured");
        else if (!wifiStationEnabled) mqttSetError("WiFi station disabled");
        else mqttSetError("Waiting for WiFi station connection");
        return;
    }
    if (!mqttWasStationReady) {
        mqttWasStationReady = true;
        mqttRetryDelay = MQTT_RECONNECT_MIN;
        mqttLastAttempt = 0;
    }
    if (mqttConfig.useTLS && !mqttClient.connected() &&
        strcmp(mqttLastError, "TLS requires a valid device time") == 0) {
        epoch64_t currentEpoch = getCurrentEpoch();
        if (currentEpoch >= EPOCH64_RTC_MIN && currentEpoch <= EPOCH64_APP_MAX) {
            mqttRetryDelay = MQTT_RECONNECT_MIN;
            mqttLastAttempt = 0;
        }
    }
    if (!mqttTransportReady && ESP.getFreeHeap() >= 6000UL) {
        mqttTransportReady = mqttClient.setBufferSize(MQTT_PACKET_BUFFER);
    }
    if (!mqttTransportReady) {
        mqttSetError("MQTT packet buffer unavailable");
        return;
    }
    if (mqttClient.connected()) {
        mqttClient.loop();
        if (mqttClient.connected()) {
            if (!mqttConnectedFlag) mqttConnectedSetup();
            mqttConnectedFlag = true;
            return;
        }
        mqttConnectedFlag = false;
        mqttLastAttempt = millis();
        if (mqttRetryDelay < MQTT_RECONNECT_MAX) mqttRetryDelay = min((unsigned long)MQTT_RECONNECT_MAX, mqttRetryDelay * 2UL);
        snprintf(mqttLastError, sizeof(mqttLastError), "Connection lost; retry in %lu ms", mqttRetryDelay);
    }
    if (elapsedSince(mqttLastAttempt) < mqttRetryDelay) return;
    if (mqttConfig.useTLS && ESP.getFreeHeap() < MQTT_TLS_MIN_HEAP) {
        mqttSetError("TLS deferred: insufficient free heap");
        mqttLastAttempt = millis();
        return;
    }
    mqttLastAttempt = millis();
    if (!mqttBuildTopic(mqttAvailabilityTopic, sizeof(mqttAvailabilityTopic), "state/availability")) {
        mqttSetError("Invalid MQTT availability topic");
        return;
    }
    if (!mqttPrepareTransport()) {
        if (mqttRetryDelay < MQTT_RECONNECT_MAX) mqttRetryDelay = min((unsigned long)MQTT_RECONNECT_MAX, mqttRetryDelay * 2UL);
        return;
    }
    bool connected = false;
    if (mqttConfig.username[0]) {
        const char* password = mqttConfig.password[0] ? mqttConfig.password : nullptr;
        connected = mqttClient.connect(mqttConfig.clientId, mqttConfig.username, password,
                                       mqttAvailabilityTopic, mqttConfig.commandQos, true, "offline");
    } else {
        connected = mqttClient.connect(mqttConfig.clientId, mqttAvailabilityTopic,
                                       mqttConfig.commandQos, true, "offline");
    }
    if (connected) {
        mqttConnectedSetup();
    } else {
        mqttConnectedFlag = false;
        snprintf(mqttLastError, sizeof(mqttLastError), "Broker connection failed (state %d)", mqttClient.state());
        if (mqttRetryDelay < MQTT_RECONNECT_MAX) mqttRetryDelay = min((unsigned long)MQTT_RECONNECT_MAX, mqttRetryDelay * 2UL);
    }
}

const char mqtt_html[] PROGMEM = R"raw(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MQTT</title>
<link rel="stylesheet" href="/style.css"></head><body>
<header><nav>
<a href="/">Relays</a><a href="/wifi">WiFi</a><a href="/ntp">Time</a><a href="/ap">AP</a><a href="/pins">Pins</a><a href="/system">System</a><a href="/mqtt" class="cur">MQTT</a>
</nav><div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="day">---</span>&nbsp;<span id="date">--/--/----</span>&nbsp;<span id="clk">--:--:--</span></div></header>
<main>
<p class="ptitle">Generic MQTT</p>
<div class="card fcrd">
<div class="alert ai" id="mqttStatus">Loading MQTT status...</div>
<div class="fg"><label><input type="checkbox" id="enabled"> Enable MQTT</label></div>
<div class="fg"><label>Broker host or IP</label><input type="text" id="host" maxlength="63" placeholder="192.168.1.10 or broker.example.com"></div>
<div class="fg"><label>Port</label><input type="number" id="port" min="1" max="65535" value="1883"></div>
<div class="fg"><label>Username</label><input type="text" id="username" maxlength="31" autocomplete="off"></div>
<div class="fg"><label>Password</label><input type="password" id="password" maxlength="63" autocomplete="new-password" placeholder="Leave blank to keep saved password"><small>Password authentication requires a username; clear the saved password for username-only authentication.</small></div>
<div class="fg"><label><input type="checkbox" id="clearPassword"> Clear saved password</label></div>
<div class="fg"><label>Base topic</label><input type="text" id="baseTopic" maxlength="63" placeholder="esp8266relay"><small>Letters, numbers, slash, underscore, and hyphen only. Do not include + or #.</small></div>
<div class="fg"><label>Client ID</label><input type="text" id="clientId" maxlength="31"></div>
<div class="fg"><label>Command subscription QoS</label><select id="qos"><option value="0">QoS 0</option><option value="1">QoS 1</option></select><small>PubSubClient publishes outgoing state at QoS 0.</small></div>
<div class="fg"><label><input type="checkbox" id="useTLS"> Use TLS</label><small>TLS uses BearSSL and requires a valid device clock and a PEM root CA below. TLS may require more free heap.</small></div>
<div class="fg"><label>Root CA certificate (PEM)</label><textarea id="rootCA" rows="8" maxlength="2047" placeholder="-----BEGIN CERTIFICATE-----&#10;...&#10;-----END CERTIFICATE-----"></textarea><small id="caStatus">Maximum 2047 bytes.</small></div>
<div class="fg"><label><input type="checkbox" id="clearRootCA"> Clear saved root CA</label></div>
<div class="fg"><label><input type="checkbox" id="remoteAdmin"> Allow MQTT administrative commands</label><small>Allows MQTT commands to change schedules, WiFi/AP settings, GPIO pins, time, hostname, and factory-reset the device. Keep disabled unless needed and use broker ACLs.</small></div>
<div style="display:flex;gap:8px;flex-wrap:wrap"><button class="btn bsave" onclick="saveMqtt()">Save MQTT Settings</button><button class="btn bscan" onclick="refreshMqtt()">Refresh Status</button></div>
<div class="card" style="margin-top:14px"><strong>Topics</strong><p style="font-size:12px;line-height:1.6;margin-top:8px">Commands are under <code>&lt;base&gt;/cmd/</code>. Device state is under <code>&lt;base&gt;/state/</code>. Relay commands use one-based relay numbers. Do not retain command messages.</p></div>
</div>
</main><div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';var e=document.getElementById('day');if(e)e.textContent=d.day||'---';e=document.getElementById('date');if(e)e.textContent=d.date||'--/--/----';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot wd '+(d.wifi?'g':'r');if(n)n.className='dot nd '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
function refreshMqtt(){fetch('/api/mqtt').then(r=>r.json()).then(d=>{
  document.getElementById('enabled').checked=!!d.enabled;document.getElementById('host').value=d.host||'';document.getElementById('port').value=d.port||1883;
  document.getElementById('username').value=d.username||'';document.getElementById('baseTopic').value=d.baseTopic||'';document.getElementById('clientId').value=d.clientId||'';
  document.getElementById('qos').value=String(d.commandQos||0);document.getElementById('useTLS').checked=!!d.useTLS;document.getElementById('remoteAdmin').checked=!!d.remoteAdminEnabled;
  document.getElementById('caStatus').textContent=d.hasRootCA?'A root CA is saved. Paste a replacement or check Clear saved root CA.':'No root CA saved.';
  const s=document.getElementById('mqttStatus');s.textContent=d.connected?'Connected to broker':'MQTT '+(d.enabled?'disconnected':'disabled')+(d.lastError?': '+d.lastError:'');s.className='alert '+(d.connected?'as':'aw');
}).catch(()=>{document.getElementById('mqttStatus').textContent='Unable to read MQTT status';});}
function saveMqtt(){
  const body={enabled:document.getElementById('enabled').checked,host:document.getElementById('host').value.trim(),port:parseInt(document.getElementById('port').value),username:document.getElementById('username').value,
    password:document.getElementById('password').value,clearPassword:document.getElementById('clearPassword').checked,baseTopic:document.getElementById('baseTopic').value.trim(),clientId:document.getElementById('clientId').value.trim(),commandQos:parseInt(document.getElementById('qos').value),useTLS:document.getElementById('useTLS').checked,
    rootCA:document.getElementById('rootCA').value,clearRootCA:document.getElementById('clearRootCA').checked,remoteAdminEnabled:document.getElementById('remoteAdmin').checked};
  fetch('/api/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>r.json()).then(d=>{if(d.success){toast('MQTT settings saved');document.getElementById('password').value='';document.getElementById('rootCA').value='';document.getElementById('clearPassword').checked=false;document.getElementById('clearRootCA').checked=false;setTimeout(refreshMqtt,800);}else toast('Save failed: '+(d.error||'unknown'),false);}).catch(()=>toast('Error saving MQTT settings',false));
}
refreshMqtt();
</script></body></html>)raw";

void handleMqttPage() {
    if (!checkPageRateLimit()) { server.send(429, "text/plain", "Too Many Requests"); return; }
    server.send_P(200, "text/html", mqtt_html);
}

void handleGetMqtt() {
    if (!checkApiRateLimit()) { server.send(429, "application/json", "{\"error\":\"Too many requests\"}"); return; }
    DynamicJsonDocument doc(768);
    doc["enabled"] = mqttConfig.enabled;
    doc["host"] = mqttConfig.host;
    doc["port"] = mqttConfig.port;
    doc["username"] = mqttConfig.username;
    doc["baseTopic"] = mqttConfig.baseTopic;
    doc["clientId"] = mqttConfig.clientId;
    doc["commandQos"] = mqttConfig.commandQos;
    doc["useTLS"] = mqttConfig.useTLS;
    doc["remoteAdminEnabled"] = mqttConfig.remoteAdminEnabled;
    doc["hasPassword"] = mqttConfig.password[0] != '\0';
    doc["hasRootCA"] = mqttConfig.rootCA[0] != '\0';
    doc["connected"] = mqttClient.connected();
    doc["lastError"] = mqttLastError;
    String response;
    response.reserve(768);
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleSaveMqtt() {
    if (!checkApiRateLimit()) { server.send(429, "application/json", "{\"success\":false}"); return; }
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing body\"}"); return; }
    if (ESP.getFreeHeap() < 18000UL) { server.send(503, "application/json", "{\"success\":false,\"error\":\"Low memory\"}"); return; }
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) { server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}"); return; }
    MqttConfig* next = (MqttConfig*)malloc(sizeof(MqttConfig));
    if (!next) { server.send(503, "application/json", "{\"success\":false,\"error\":\"Low memory\"}"); return; }
    memcpy(next, &mqttConfig, sizeof(MqttConfig));
    next->enabled = doc["enabled"] | false;
    next->remoteAdminEnabled = doc["remoteAdminEnabled"] | false;
    next->useTLS = doc["useTLS"] | false;
    const char* host = doc["host"] | next->host;
    const char* username = doc["username"] | next->username;
    const char* baseTopic = doc["baseTopic"] | next->baseTopic;
    const char* clientId = doc["clientId"] | next->clientId;
    long port = doc["port"] | (long)next->port;
    int qos = doc["commandQos"] | (int)next->commandQos;
    bool clearPassword = doc["clearPassword"] | false;
    bool clearRootCA = doc["clearRootCA"] | false;
    const char* password = doc["password"];
    const char* rootCA = doc["rootCA"];
    char error[128] = "";
    if (!mqttValidBrokerHost(host) || (next->enabled && strlen(host) == 0)) snprintf(error, sizeof(error), "Broker host is invalid");
    else if (port < 1 || port > 65535) snprintf(error, sizeof(error), "Broker port must be 1-65535");
    else if (!username || strlen(username) >= sizeof(next->username)) snprintf(error, sizeof(error), "Username is too long");
    else if (!baseTopic || strlen(baseTopic) >= sizeof(next->baseTopic) || !mqttValidBaseTopic(baseTopic)) snprintf(error, sizeof(error), "Base topic contains invalid characters");
    else if (!clientId || strlen(clientId) >= sizeof(next->clientId) || !mqttValidClientId(clientId)) snprintf(error, sizeof(error), "Client ID contains invalid characters");
    else if (qos < 0 || qos > 1) snprintf(error, sizeof(error), "Command QoS must be 0 or 1");
    else if (password && strlen(password) >= sizeof(next->password)) snprintf(error, sizeof(error), "Password is too long");
    else if (rootCA && strlen(rootCA) >= sizeof(next->rootCA)) snprintf(error, sizeof(error), "Root CA exceeds 2047 bytes");
    if (error[0]) { free(next); server.send(400, "application/json", String("{\"success\":false,\"error\":\"") + error + "\"}"); return; }
    strncpy(next->host, host, sizeof(next->host) - 1); next->host[sizeof(next->host) - 1] = '\0';
    strncpy(next->username, username, sizeof(next->username) - 1); next->username[sizeof(next->username) - 1] = '\0';
    strncpy(next->baseTopic, baseTopic, sizeof(next->baseTopic) - 1); next->baseTopic[sizeof(next->baseTopic) - 1] = '\0';
    strncpy(next->clientId, clientId, sizeof(next->clientId) - 1); next->clientId[sizeof(next->clientId) - 1] = '\0';
    next->port = (uint16_t)port;
    next->commandQos = (uint8_t)qos;
    if (clearPassword) next->password[0] = '\0';
    else if (password && *password) { strncpy(next->password, password, sizeof(next->password) - 1); next->password[sizeof(next->password) - 1] = '\0'; }
    if (next->username[0] == '\0') next->password[0] = '\0';
    if (clearRootCA) next->rootCA[0] = '\0';
    else if (rootCA && *rootCA) { strncpy(next->rootCA, rootCA, sizeof(next->rootCA) - 1); next->rootCA[sizeof(next->rootCA) - 1] = '\0'; }
    if (next->useTLS && next->rootCA[0] == '\0') {
        free(next);
        server.send(400, "application/json", "{\"success\":false,\"error\":\"TLS requires a PEM root CA certificate\"}");
        return;
    }
    next->magic = MQTT_CONFIG_MAGIC;
    next->version = MQTT_CONFIG_VERSION;
    bool saved = saveMqttConfig(next);
    if (!saved) {
        free(next);
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Unable to save MQTT settings (flash or memory limit)\"}");
        return;
    }
    if (mqttClient.connected()) {
        mqttPublishText("state/availability", "offline", true);
        mqttClient.disconnect();
    }
    memcpy(&mqttConfig, next, sizeof(MqttConfig));
    free(next);
    requestMqttReconfigure();
    server.send(200, "application/json", "{\"success\":true}");
}

// =============================================================================
//  DS3231 Functions
// =============================================================================
void initDS3231() {
    Wire.begin(4, 5);     
    if (rtc.begin()) {
        ds3231Present = true;       
        if (rtc.lostPower()) {
            rtcInitialized = false;
            internalEpoch = 0;
            rtc.adjust(DateTime((uint32_t)EPOCH64_APP_MIN));
        } else {
            syncFromDS3231();
        }
    } else {
        ds3231Present = false;
    }
}
void syncFromDS3231() {
    if (!ds3231Present) return;    
    static uint8_t rtcBadReadCount = 0;
    static unsigned long lastHealthProbe = 0;
    DateTime now = rtc.now();
    if (now.year() < 2024 || now.year() > 2099) {
        if (++rtcBadReadCount >= 3) {
            ds3231Present = false;
            rtcBadReadCount = 0;
        }
        rtcInitialized = false;
        return;
    }
    rtcBadReadCount = 0;
    if (elapsedSince(lastHealthProbe) >= 600000UL) {
        lastHealthProbe = millis();
        if (rtc.lostPower()) {
            rtcInitialized = false;
            internalEpoch = 0;
            return;
        }
    }
    uint32_t rtcRaw = now.unixtime();
    epoch64_t ds3231Epoch = (epoch64_t)rtcRaw;    
    if (ds3231Epoch < EPOCH64_RTC_MIN || ds3231Epoch > EPOCH64_NTP_MAX) return;    
    unsigned long nowMs = millis();    
    if (rtcInitialized && internalEpoch > 0) {
        epoch64_t internalNow = getCurrentEpoch();
        int64_t delta = (int64_t)(ds3231Epoch - internalNow);
        int64_t window = (int64_t)extConfig.ntp_sync_hours * 3600LL;
        if (window < 3600LL)  window = 3600LL;
        if (window > 14400LL) window = 14400LL;
        if (delta < -window || delta > window) {
            return;
        }
        unsigned long elapsedMs = elapsedSince(internalMillisAtLastSync);
        if (elapsedMs > 60000UL && elapsedMs < MAX_DRIFT_CALIBRATION_INTERVAL) {
            updateCrystalCalibration(ds3231Epoch, nowMs, internalEpoch, internalMillisAtLastSync);
        }
    }    
    internalEpoch = ds3231Epoch;
    internalMillisAtLastSync = nowMs;
    rtcInitialized = true;
    lastDS3231SyncEpoch = ds3231Epoch;
    lastDS3231Sync = nowMs;
}
void syncToDS3231(epoch64_t epoch) {
    if (!ds3231Present) return;
    if (epoch < EPOCH64_APP_MIN || epoch > EPOCH64_APP_MAX) return;
    uint32_t clamped = (epoch > (epoch64_t)4102444800LL)
        ? (uint32_t)4102444800UL
        : (uint32_t)epoch;
    rtc.adjust(DateTime(clamped));
    static uint8_t writeFailCount = 0;
    Wire.beginTransmission(0x68);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        if (++writeFailCount >= 3) {
            ds3231Present = false;
            writeFailCount = 0;
        }
    } else {
        writeFailCount = 0;
    }
}

// =============================================================================
//  Enhanced Crystal Calibration
// =============================================================================
void updateCrystalCalibration(epoch64_t measuredEpoch, unsigned long measuredMs, epoch64_t previousEpoch, unsigned long previousMs) {
    if (previousEpoch == 0 || previousMs == 0) return;    
    unsigned long elapsedMs = (measuredMs >= previousMs) ? 
        (measuredMs - previousMs) : (0xFFFFFFFFUL - previousMs) + measuredMs + 1UL;    
    if (elapsedMs + 1000UL < CRYSTAL_CALIBRATION_MIN_INTERVAL) return;    
    int64_t expectedElapsed = (int64_t)(elapsedMs / 1000UL);
    int64_t actualElapsed   = (int64_t)(measuredEpoch - previousEpoch);    
    if (actualElapsed <= 0 || expectedElapsed <= 0) return;
    int64_t diffElapsed = actualElapsed - expectedElapsed;
    if (diffElapsed < 0) diffElapsed = -diffElapsed;
    if (diffElapsed > (expectedElapsed / 10LL)) return;    
    double actualSeconds   = (double)actualElapsed;
    double expectedSeconds = (double)expectedElapsed;
    double measuredRate = actualSeconds / expectedSeconds;    
    if (measuredRate > 0.9 && measuredRate < 1.1) {
        double newPPM = ((actualSeconds - expectedSeconds) / expectedSeconds) * 1000000.0;        
        if (newPPM > -50 && newPPM < 50) {
            if (crystalCalibrated) {
                crystalDriftPPM = crystalDriftPPM * 0.9f + newPPM * 0.1f;
            } else {
                crystalDriftPPM = crystalDriftPPM * 0.5f + newPPM * 0.5f;
                crystalCalibrated = true;
            }            
            if (crystalDriftPPM < -50) crystalDriftPPM = -50;
            if (crystalDriftPPM > 50) crystalDriftPPM = 50;
        }        
        if (ntpSyncCount < 10) {
            driftCompensation = driftCompensation * 0.5f + measuredRate * 0.5f;
        } else {
            driftCompensation = driftCompensation * 0.95f + measuredRate * 0.05f;
        }
        ntpSyncCount = min(ntpSyncCount + 1, 1000);        
        if (driftCompensation < 0.995f) driftCompensation = 0.995f;
        if (driftCompensation > 1.005f) driftCompensation = 1.005f;
    }
}

// =============================================================================
//  WiFi Station Control Functions
// =============================================================================
void enableWiFiStation() {
    if (wifiStationEnabled) return;    
    wifiStationEnabled = true;
    pendingStationDisable = false;
    sysConfig.wifi_station_enabled = true;
    markConfigDirty();    
    if (strlen(sysConfig.sta_ssid) > 0) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
        wcsState = WCS_PENDING;
        wcsStart = millis();
        wifiReconnectAttempts = 0;
        reconnectCountThisHour = 0;
        wifiGiveUpUntil = 0;
    }
}
void disableWiFiStation() {
    if (!wifiStationEnabled) return;
    shutdownMqtt();
    wifiStationEnabled = false;
    sysConfig.wifi_station_enabled = false;
    wifiConnected = false;
    mdnsStarted = false;
    wcsState = WCS_IDLE;
    wifiReconnectAttempts = 0;
    persistentDisconnectTime = 0;
    networkHealingInProgress = false;
    ntpFailCount = 0;    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);    
    uint8_t ch = extConfig.ap_channel;
    if (ch < 1 || ch > 13) ch = 6;
    uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
    if (strlen(sysConfig.ap_password) > 0)
        WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
    else
        WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
    
    markConfigDirty();
}
// =============================================================================
//  Self-Healing Functions
// =============================================================================
void emergencyHeapRecovery() {
    if (ESP.getFreeHeap() < EMERGENCY_HEAP_THRESHOLD) {
        server.client().stop();
        WiFi.disconnect(false);
        delay(100);        
        ESP.resetFreeContStack();
        for (int i = 0; i < 10; i++) {
            String dummy;
            dummy.reserve(32);
            dummy = "";
            yield();
        }
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_AP);
        }        
        setupWebServer();
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());        
    }
}
void monitorHeapHealth() {
    unsigned long now = millis();    
    if (elapsedSince(lastHeapCheck) < HEAP_CHECK_INTERVAL) return;
    lastHeapCheck = now;    
    uint32_t freeHeap = ESP.getFreeHeap();    
    if (freeHeap < EMERGENCY_HEAP_THRESHOLD) {
        emergencyHeapRecovery();
    } else if (freeHeap < CRITICAL_HEAP_THRESHOLD) {
        cleanupIdleConnections();
        defragmentHeap();
        selfRecoveryMemoryCleanup();        
        if (wifiConnected) {
            WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        }
    }
}
void checkWiFiStability() {
    if (!wifiConnected || !wifiStationEnabled) return;    
    static unsigned long lastStabilityCheck = 0;
    if (elapsedSince(lastStabilityCheck) < 600000UL) return;
    lastStabilityCheck = millis();    
    bool needsReconnect = false;
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        if (rssi < -90) {
            needsReconnect = true;
        }
    }    
    IPAddress ip = WiFi.localIP();
    if (ip.toString() == "0.0.0.0" || ip.toString() == "(IP unset)") {
        needsReconnect = true;
    }
    if (needsReconnect) {
        WiFi.disconnect(false);
        delay(500);
        WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
        wcsState = WCS_PENDING;
        wcsStart = millis();
    }
}
void performDeepHeal() {
    deepHealInProgress = true;    
    WiFi.disconnect(true);
    delay(1000);    
    if (strlen(sysConfig.sta_ssid) > 0 && wifiStationEnabled) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
    } else {
        WiFi.mode(WIFI_AP);
    }
    {
        uint8_t ch = extConfig.ap_channel;
        if (ch < 1 || ch > 13) ch = 6;
        uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
        if (strlen(sysConfig.ap_password) > 0)
            WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
        else
            WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
    }
    delay(100);
    dnsServer.stop();
    delay(100);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());    
    server.stop();
    server.close();
    delay(100);
    setupWebServer();    
    if (MDNS.isRunning()) MDNS.end();
    if (wifiStationEnabled) startMDNS();    
    if (ds3231Present) {
        syncFromDS3231();
    }
    if (!rtcInitialized && wifiConnected && wifiStationEnabled &&
        ESP.getFreeHeap() > MIN_HEAP_FOR_NTP) {
        tryNTPSync();
    }    
    defragmentHeap();
    cleanupIdleConnections();    
    deepHealInProgress = false;
    lastDeepHeal = millis();
}
void selfRecoveryDeepHeal() {
    performDeepHeal();
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryWifiReconnect() {
    if (wifiConnected || !wifiStationEnabled) {
        selfRecoveryState = SR_IDLE;
        return;
    }    
    WiFiMode_t currentMode = WiFi.getMode();    
    if (strlen(sysConfig.sta_ssid) > 0) {
        if (currentMode == WIFI_AP) {
            WiFi.mode(WIFI_AP_STA);
        }        
        WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
        wcsState = WCS_PENDING;
        wcsStart = millis();
        wifiReconnectAttempts = 0;
        reconnectCountThisHour = 0;
    }    
    if (WiFi.softAPgetStationNum() == 0) {
        uint8_t ch = extConfig.ap_channel;
        if (ch < 1 || ch > 13) ch = 6;
        uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
        if (strlen(sysConfig.ap_password) > 0)
            WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
        else
            WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
    }    
    networkHealingInProgress = false;
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryWebServer() {
    unsigned long now = millis();
    if (elapsedSince(lastWebServerRecovery) < WEB_SERVER_RECOVERY_INTERVAL) {
        selfRecoveryState = SR_IDLE;
        return;
    }
    lastWebServerRecovery = now;    
    server.stop();
    server.close();  
    delay(50);
    setupWebServer();
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryDNS() {
    dnsServer.stop();
    delay(50);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryRTC() {
    if (ds3231Present) {
        syncFromDS3231();
    }
    if (!rtcInitialized && wifiConnected && wifiStationEnabled && ESP.getFreeHeap() > MIN_HEAP_FOR_NTP) {
        tryNTPSync();
    }
    if (!rtcInitialized && internalEpoch > 0) {
        internalMillisAtLastSync = millis();
    }
    ntpFailCount = 0;
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryMemoryCleanup() {
    ESP.resetFreeContStack();    
    for (int i = 0; i < 5; i++) {
        String dummy;
        dummy.reserve(64);
        dummy = " ";
        dummy = "";
        yield();
    }    
    cleanupIdleConnections();    
    if (wifiConnected) {
        WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
        delay(10);
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
    }
    selfRecoveryState = SR_IDLE;
}
void selfRecoveryNetworkHeal() {
    if (!networkHealingInProgress) {
        networkHealingInProgress = true;
        if (!wifiConnected && wifiStationEnabled && strlen(sysConfig.sta_ssid) > 0) {
            WiFi.reconnect();
            wcsState = WCS_PENDING;
            wcsStart = millis();
        }
        if (wifiConnected && !mdnsStarted) {
            startMDNS();
        }
        if (elapsedSince(lastDNSRestart) > 3600000UL) {
            selfRecoveryDNS();
            lastDNSRestart = millis();
        }
        networkHealingInProgress = false;
    }
    selfRecoveryState = SR_IDLE;
}
void checkWebServerHealth() {
    unsigned long now = millis();
    if (elapsedSince(lastWebServerCheck) < 30000) return;
    lastWebServerCheck = now;    
    if (ESP.getFreeHeap() < 6144) {
        selfRecoveryWebServer();
    }
}
void checkDNSHealth() {
    unsigned long now = millis();
    if (elapsedSince(lastDNSCheck) < 60000) return;
    lastDNSCheck = now;   
    if (ESP.getFreeHeap() < 5120) {
        selfRecoveryDNS();
    }
}
void defragmentHeap() {
    unsigned long now = millis();
    if (elapsedSince(lastHeapDefrag) < HEAP_DEFRAG_INTERVAL) return;
    lastHeapDefrag = now;    
    if (ESP.getFreeHeap() > 20480) return;    
    ESP.resetFreeContStack();    
    for (int i = 0; i < 3; i++) {
        String dummy;
        dummy.reserve(128);
        dummy = " ";
        dummy = "";
        yield();
    }
}
void manageConnections() {
    unsigned long now = millis();
    if (elapsedSince(lastConnectionCleanup) < CONNECTION_CLEANUP_INTERVAL) return;
    lastConnectionCleanup = now;    
    cleanupIdleConnections();    
}
void cleanupIdleConnections() {
    if (ESP.getFreeHeap() < 10240) {
        server.client().stop();
        delay(10);
    }
}

// =============================================================================
//  ALL FUNCTIONS
// =============================================================================
void checkFactoryResetButton() {
    static unsigned long lastDebounce = 0;
    static bool lastButtonState = HIGH;
    static bool isPressing = false;    
    static bool solidSet = false;
    if (factoryResetInProgress) return;    
    bool currentState = digitalRead(FACTORY_RESET_PIN);
    unsigned long now = millis();    
    if (currentState != lastButtonState) {
        lastDebounce = now;
    }    
    if (elapsedSince(lastDebounce) > 50) {
        if (currentState == LOW && !isPressing) {
            isPressing = true;
            factoryResetPressStart = now;
            buttonHeld = true;
        }
        else if (currentState == HIGH && isPressing) {
            unsigned long holdDuration = elapsedSince(factoryResetPressStart);
            if (holdDuration >= FACTORY_RESET_HOLD_MS) {
                performFactoryReset();
            }
            isPressing = false;
            buttonHeld = false;
        }
    }    
    if (isPressing && !factoryResetInProgress) {
        unsigned long holdTime = elapsedSince(factoryResetPressStart);
        if (holdTime >= FACTORY_RESET_HOLD_MS) {
            if (!solidSet) {
                digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? LOW : HIGH);
                solidSet = true;
            }
        } else {
            solidSet = false;
            int blinkInterval = 500 - (holdTime / 11);
            if (blinkInterval < 50) blinkInterval = 50;            
            if (elapsedSince(lastFactoryResetBlink) >= (unsigned long)blinkInterval) {
                lastFactoryResetBlink = now;
                ledState = !ledState;
                digitalWrite(STATUS_LED_PIN, 
                    (STATUS_LED_ACTIVE_LOW ? !ledState : ledState) ? HIGH : LOW);
            }
        }
    }
    
    lastButtonState = currentState;
}
void performFactoryReset() {
    if (factoryResetInProgress) return;
    mqttConfig.enabled = 0;
    mqttScanPublishPending = false;
    factoryResetInProgress = true;
    frState = FR_INIT_LED;
    frStepTimer = millis();
    factoryResetLEDStep = 0;
}
void processFactoryReset() {
    if (!factoryResetInProgress) return;    
    unsigned long now = millis();    
    switch (frState) {
        case FR_IDLE:
            break;            
        case FR_INIT_LED:
            if (elapsedSince(frStepTimer) >= 100) {
                frStepTimer = now;
                digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? 
                    (factoryResetLEDStep % 2 == 0 ? LOW : HIGH) : 
                    (factoryResetLEDStep % 2 == 0 ? HIGH : LOW));
                factoryResetLEDStep++;
                if (factoryResetLEDStep >= 20) {
                    frState = FR_DELETE_FILES;
                }
            }
            break;            
        case FR_DELETE_FILES:
            if (LittleFS.exists(CONFIG_FILE)) LittleFS.remove(CONFIG_FILE);
            if (LittleFS.exists(EXTCONFIG_FILE)) LittleFS.remove(EXTCONFIG_FILE);
            if (LittleFS.exists(RELAY_FILE)) LittleFS.remove(RELAY_FILE);
            if (LittleFS.exists(PINS_FILE)) LittleFS.remove(PINS_FILE);
            if (LittleFS.exists(MQTT_CONFIG_FILE)) LittleFS.remove(MQTT_CONFIG_FILE);
            {
                const char* bases[] = {CONFIG_FILE, EXTCONFIG_FILE, RELAY_FILE, PINS_FILE, MQTT_CONFIG_FILE};
                for (uint8_t k = 0; k < 5; k++) {
                    String bak = String(bases[k]) + ".bak";
                    String tmp = String(bases[k]) + ".tmp";
                    if (LittleFS.exists(bak)) LittleFS.remove(bak);
                    if (LittleFS.exists(tmp)) LittleFS.remove(tmp);
                }
            }
            frState = FR_CLEAR_CONFIG;
            break;            
        case FR_CLEAR_CONFIG:
            memset(&sysConfig, 0, sizeof(SystemConfig));
            memset(&extConfig, 0, sizeof(ExtConfig));
            memset(&pinConfig, 0, sizeof(PinConfig));
            initDefaults();
            loadRTCState();
            initMqttDefaults();
            saveMqttConfig(&mqttConfig);
            updateRelayPins();
            frState = FR_RESET_WIFI;
            break;            
        case FR_RESET_WIFI:
            WiFi.disconnect(true);
            WiFi.softAPdisconnect(true);
            frStepTimer = now;
            frState = FR_SETUP_AP;
            break;            
        case FR_SETUP_AP:
            if (now - frStepTimer >= 200) {
                WiFi.mode(WIFI_AP);
                uint8_t ch = extConfig.ap_channel;
                if (ch < 1 || ch > 13) ch = 6;
                uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
                if (strlen(sysConfig.ap_password) > 0)
                    WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
                else
                    WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
                wifiConnected = false;
                mdnsStarted = false;
                wcsState = WCS_IDLE;
                wifiReconnectAttempts = 0;
                reconnectCountThisHour = 0;
                persistentDisconnectTime = 0;
                networkHealingInProgress = false;
                ntpFailCount = 0;
                rtcInitialized = false;
                internalEpoch = 0;
                lastNTPSync = 0;
                lastNTPSyncEpoch = 0;
                lastDS3231Sync = 0;
                lastDS3231SyncEpoch = 0;
                lastBrowserSyncMs = 0;
                lastBrowserSyncEpoch = 0;
                wifiStationEnabled = true;
                if (ds3231Present) {
                    syncFromDS3231();
                }            
                frState = FR_DNS_RESTART;
                frStepTimer = now;
            }
            break;            
        case FR_DNS_RESTART:
            if (now - frStepTimer >= 50) {
                dnsServer.stop();
                frStepTimer = now;
                frState = FR_WEB_RESTART;
            }
            break;            
        case FR_WEB_RESTART:
            if (now - frStepTimer >= 100) {
                dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
                server.stop();
                server.close();
                frStepTimer = now;
                frState = FR_COMPLETE;
            }
            break;            
        case FR_COMPLETE:
            if (now - frStepTimer >= 50) {
                setupWebServer();
                factoryResetInProgress = false;
                frState = FR_IDLE;
            }
            break;
    }
}
void tryNTPSync() {
    if (!wifiConnected || !wifiStationEnabled) return;
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_NTP) {
        selfRecoveryMemoryCleanup();
        if (ESP.getFreeHeap() < MIN_HEAP_FOR_NTP) return;
    }
    if (ntpState != NTP_IDLE) return;    
    ntpState = NTP_REQUESTING;
    ntpTimer = millis();
    lastNTPAttempt = millis();
    if (ntpServerIndex == 0) {
        if (strlen(sysConfig.ntp_server) > 0) {
            timeClient.setPoolServerName(sysConfig.ntp_server);
        } else {
            timeClient.setPoolServerName(NTP_SERVERS[0]);
        }
    } else {
        uint8_t idx = ntpServerIndex - 1;
        if (idx >= NUM_NTP_SERVERS) idx = NUM_NTP_SERVERS - 1;
        timeClient.setPoolServerName(NTP_SERVERS[idx]);
    }
}
void processNTPState() {
    switch (ntpState) {
        case NTP_IDLE:
            break;            
        case NTP_REQUESTING:
            if (elapsedSince(ntpTimer) > 1000) {
                bool ok = timeClient.forceUpdate();
                ntpState = NTP_PROCESSING;
                if (ok) {
                    syncInternalRTC();
                    ntpFailCount = 0;
                    ntpServerIndex = 0;
                } else {
                    ntpFailCount++;
                    if (ntpFailCount > 3) {
                        ntpServerIndex++;
                        if (ntpServerIndex > NUM_NTP_SERVERS) ntpServerIndex = 0;
                        ntpFailCount = 0;
                    }
                }
            }
            break;            
        case NTP_PROCESSING:
            ntpState = NTP_IDLE;
            break;
    }
}
void gentleNetworkHealing() {
    unsigned long now = millis();
    if (elapsedSince(lastGentleHealing) < GENTLE_NETWORK_HEAL_INTERVAL) return;
    lastGentleHealing = now;    
    if (!wifiConnected && wifiStationEnabled && !networkHealingInProgress && strlen(sysConfig.sta_ssid) > 0) {
        networkHealingInProgress = true;
        whState = WH_DISCONNECT;
        whTimer = now;
    }
}
void processNetworkHealing() {
    if (!networkHealingInProgress) return;    
    unsigned long now = millis();    
    switch (whState) {
        case WH_IDLE:
            break;            
        case WH_DISCONNECT:
            WiFi.disconnect();
            whState = WH_CONNECTING;
            whTimer = now;
            break;            
        case WH_CONNECTING:
            if (elapsedSince(whTimer) >= 100) {
                WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
                whState = WH_WAITING;
                whTimer = now;
            }
            break;            
        case WH_WAITING:
            if (WiFi.status() == WL_CONNECTED) {
                networkHealingInProgress = false;
                wifiConnected = true;
                startMDNS();
                whState = WH_IDLE;
            } else if (elapsedSince(whTimer) >= 30000) {
                networkHealingInProgress = false;
                whState = WH_IDLE;
            }
            break;
    }
}
void checkConnectionRateLimit() {
    unsigned long now = millis();
    if (elapsedSince(reconnectHourStart) >= 3600000UL) {
        reconnectCountThisHour = 0;
        reconnectHourStart = now;
    }
    if (reconnectCountThisHour >= MAX_RECONNECT_ATTEMPTS) {
        return; 
    }
    reconnectCountThisHour++;
}
void loadFromFile(const char* path, void* data, size_t size) {
    if (!LittleFS.exists(path)) {
        String bakPath = String(path) + ".bak";
        if (LittleFS.exists(bakPath)) {
            LittleFS.rename(bakPath, path);
        } else {
            return;
        }
    }
    File f = LittleFS.open(path, "r");
    if (!f) return;   
    size_t fileSize = f.size();
    if (fileSize > size) {
        f.close();
        return;
    }    
    if (fileSize < size) {
        memset((uint8_t*)data + fileSize, 0, size - fileSize);
    }
    size_t bytesRead = f.read((uint8_t*)data, min(size, fileSize));
    f.close();
}
bool saveToFileAtomic(const char* path, const void* data, size_t size) {
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_SAVE) {
        return false;
    }    
    if (!checkFlashWriteRate()) {
        return false;
    }    
    String tmpPath = String(path) + ".tmp";
    String bakPath = String(path) + ".bak";
    if (LittleFS.exists(tmpPath)) {
        LittleFS.remove(tmpPath);
    }    
    File f = LittleFS.open(tmpPath, "w");
    if (!f) return false;    
    size_t bytesWritten = f.write((const uint8_t*)data, size);
    f.flush();
    f.close();
    if (bytesWritten != size) {
        LittleFS.remove(tmpPath);
        return false;
    }
    if (LittleFS.exists(bakPath)) {
        LittleFS.remove(bakPath);
    }
    if (LittleFS.exists(path)) {
        if (!LittleFS.rename(path, bakPath)) {
            LittleFS.remove(tmpPath);
            return false;
        }
    }
    bool result = LittleFS.rename(tmpPath, path);
    if (!result) {
        if (LittleFS.exists(bakPath) && !LittleFS.exists(path)) {
            LittleFS.rename(bakPath, path);
        }
        LittleFS.remove(tmpPath);
    } else {
        if (LittleFS.exists(bakPath)) LittleFS.remove(bakPath);
    }
    return result;
}
void markConfigDirty() {
    if (!configDirty) {
        configDirtySince = millis();
    }
    configDirty = true;
}
void flushConfigIfNeeded() {
    if (!configDirty) return;    
    if (elapsedSince(configDirtySince) < CONFIG_SAVE_MIN_INTERVAL) {
        return;
    }    
    if (elapsedSince(lastConfigSave) < CONFIG_SAVE_INTERVAL) {
        return;
    }    
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_SAVE) {
        return;
    }
    bool rtcSaved = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    bool relaySaved = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);    
    if (rtcSaved && relaySaved) {
        configDirty = false;
        lastConfigSave = millis();
    }
}
epoch64_t getCurrentEpoch() {
    if (!rtcInitialized || internalEpoch == 0) return 0;    
    unsigned long elapsed = elapsedSince(internalMillisAtLastSync);
    double elapsedSeconds;    
    elapsedSeconds = (double)elapsed * driftCompensation / 1000.0;    
    epoch64_t result = internalEpoch + (epoch64_t)elapsedSeconds;    
    return result;
}
void syncInternalRTC() {
    unsigned long ntpRaw = timeClient.getEpochTime();
    epoch64_t ntpEpoch = (epoch64_t)ntpRaw;
    if (ntpEpoch < EPOCH64_MIN_VALID || ntpEpoch > EPOCH64_APP_MAX) return;
    unsigned long nowMs = millis();    
    if (rtcInitialized && internalEpoch > 0) {
        epoch64_t internalNow = getCurrentEpoch();
        if (internalNow >= EPOCH64_APP_MIN && internalNow <= EPOCH64_APP_MAX) {
            int64_t sanityDelta = (int64_t)(ntpEpoch - internalNow);
            if (sanityDelta < -473040000LL || sanityDelta > 473040000LL) return;
        }
        unsigned long elapsedMs = elapsedSince(internalMillisAtLastSync);
        if (elapsedMs > 60000UL && elapsedMs < MAX_DRIFT_CALIBRATION_INTERVAL) {
            updateCrystalCalibration(ntpEpoch, nowMs, internalEpoch, internalMillisAtLastSync);
        }
    }    
    internalEpoch = ntpEpoch;
    internalMillisAtLastSync = nowMs;
    rtcInitialized = true;
    lastNTPSync = nowMs;
    lastNTPSyncEpoch = ntpEpoch;
    ntpFailCount = 0;
    saveRTCState();
    syncToDS3231(ntpEpoch);
}
void syncInternalRTCFromBrowser(epoch64_t browserEpoch) {
    unsigned long nowMs = millis();    
    if (rtcInitialized && internalEpoch > 0) {
        unsigned long elapsedMs = elapsedSince(internalMillisAtLastSync);
        if (elapsedMs > 60000UL && elapsedMs < MAX_DRIFT_CALIBRATION_INTERVAL) {
            updateCrystalCalibration(browserEpoch, nowMs, internalEpoch, internalMillisAtLastSync);
        }
    }    
    internalEpoch = browserEpoch;
    internalMillisAtLastSync = nowMs;
    rtcInitialized = true;
    lastBrowserSyncEpoch = browserEpoch;
    lastBrowserSyncMs = nowMs;
    lastNTPSync = nowMs;
    lastNTPSyncEpoch = browserEpoch;
    lastNTPAttempt = nowMs;
    ntpFailCount = 0;
    saveRTCState();
    syncToDS3231(browserEpoch);
}
void saveRTCState() {
    sysConfigSetLastRtcEpoch(internalEpoch);
    sysConfig.rtc_drift = driftCompensation;
    sysConfig.crystal_ppm = crystalDriftPPM;
    sysConfig.crystal_calibrated = crystalCalibrated;
    sysConfig.wifi_station_enabled = wifiStationEnabled;
    markConfigDirty();
}
void loadRTCState() {
    if (sysConfig.rtc_drift >= 0.99f && sysConfig.rtc_drift <= 1.01f) {
        driftCompensation = sysConfig.rtc_drift;
    } else {
        driftCompensation = 1.0f;
    }
    if (sysConfig.version >= 8) {
        crystalDriftPPM = sysConfig.crystal_ppm;
        crystalCalibrated = sysConfig.crystal_calibrated;
        if (fabsf(crystalDriftPPM) > 50.0f) {
            crystalDriftPPM = 0;
            crystalCalibrated = false;
        }
    } else {
        crystalDriftPPM = 0;
        crystalCalibrated = false;
    }
    if (sysConfig.version >= 9) {
        wifiStationEnabled = sysConfig.wifi_station_enabled;
    } else {
        wifiStationEnabled = true;
    }
    if (!rtcInitialized) {
        epoch64_t saved = sysConfigGetLastRtcEpoch();
        if (saved > EPOCH64_MIN_VALID && saved < EPOCH64_APP_MAX) {
            internalEpoch = saved;
            internalMillisAtLastSync = millis();
            rtcInitialized = true;
        }
    }
}
void startMDNS() {
    if (!wifiStationEnabled || !wifiConnected) return;
    if (MDNS.isRunning()) {
        MDNS.end();
    }    
    if (strlen(sysConfig.hostname) == 0) strcpy(sysConfig.hostname, "esp8266relay");    
    if (MDNS.begin(sysConfig.hostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
    } else {
        mdnsStarted = false;
    }
}
void beginWiFiConnect() {
    if (!wifiStationEnabled || strlen(sysConfig.sta_ssid) == 0) return;
    if (wifiGiveUpUntil != 0 && (int32_t)(millis() - wifiGiveUpUntil) < 0) return;    
    checkConnectionRateLimit();
    if (reconnectCountThisHour >= MAX_RECONNECT_ATTEMPTS) {
        if (persistentDisconnectTime == 0) persistentDisconnectTime = millis();        
        if (elapsedSince(persistentDisconnectTime) > 7200000UL) {
            selfRecoveryWifiReconnect(); 
        }
        return;
    }
    persistentDisconnectTime = 0;    
    wifiReconnectAttempts++;
    WiFi.disconnect(false);    
    delay(100);
    WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
    wcsState = WCS_PENDING;
    wcsStart = millis();
}
void restartAP() {
    aprState = APR_PENDING;
    aprStartTime = millis();
}
void processAPRestart() {
    if (aprState == APR_IDLE) return;    
    unsigned long now = millis();    
    switch (aprState) {
        case APR_PENDING:
            WiFi.softAPdisconnect(false);
            aprState = APR_RESTARTING;
            aprStartTime = now;
            break;          
        case APR_RESTARTING:
            if (elapsedSince(aprStartTime) >= AP_RESTART_DELAY) {
                uint8_t ch = extConfig.ap_channel;
                if (ch < 1 || ch > 13) ch = 6;
                uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
                if (strlen(sysConfig.ap_password) > 0)
                    WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
                else
                    WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
                aprState = APR_IDLE;
            }
            break;
    }
}
void updateStatusLED() {
    if (factoryResetInProgress) return;
    if (buttonHeld) return;
    unsigned long now = millis();
    unsigned long interval;    
    if (deepHealInProgress) {
        interval = LED_BLINK_HEAL;
    } else if (selfRecoveryState != SR_IDLE) {
        interval = 100UL;
    } else if (!wifiConnected && wifiStationEnabled) {
        interval = LED_BLINK_FAST;
    } else if (!rtcInitialized ||
               (lastNTPSync == 0 && lastNTPSyncEpoch == 0 &&
                lastDS3231SyncEpoch == 0 && lastBrowserSyncEpoch == 0)) {
        interval = LED_BLINK_SLOW;
    } else {
        digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? HIGH : LOW);
        return;
    }    
    if (elapsedSince(lastLedToggle) >= interval) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, 
            (STATUS_LED_ACTIVE_LOW ? !ledState : ledState) ? HIGH : LOW);
    }
}
void performHeapCleanup() {
    ESP.resetFreeContStack();
}
void performSelfCheck() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        String bakPath = String(CONFIG_FILE) + ".bak";
        if (LittleFS.exists(bakPath) && LittleFS.rename(bakPath, CONFIG_FILE)) {
            loadConfiguration();
        } else if (sysConfig.magic != EEPROM_MAGIC) {
            initDefaults();
        }
    }
    for (int i = 0; i < numRelays; i++) {
        pinMode(relayConfigs[i].pin, OUTPUT);
    }
    if (sysConfig.magic != EEPROM_MAGIC) {
        loadConfiguration();
    }
}
void updateRelayPins() {
    for (int i = 0; i < numRelays; i++) {
        pinMode(relayConfigs[i].pin, OUTPUT);
        digitalWrite(relayConfigs[i].pin, 
            relayConfigs[i].activeLow ? HIGH : LOW);
    }
}
void proactiveMaintenance() {
    unsigned long now = millis();
    if (elapsedSince(lastProactiveMaintenance) < PROACTIVE_MAINTENANCE_INTERVAL) return;
    lastProactiveMaintenance = now;        
    if (ESP.getFreeHeap() < 10000) {
        selfRecoveryMemoryCleanup(); 
    }    
    if (wifiConnected && wifiStationEnabled && WiFi.RSSI() < -85) {
        static unsigned long lastWeakSignalRecovery = 0;
        if (elapsedSince(lastWeakSignalRecovery) > 300000) {
            WiFi.reconnect();
            lastWeakSignalRecovery = now;
        }
    }    
    if (rtcInitialized && wifiConnected && wifiStationEnabled) {
        unsigned long timeSinceSync = (lastNTPSync > 0) ? elapsedSince(lastNTPSync) : 0;
        if (timeSinceSync > 3600000UL && elapsedSince(lastNTPAttempt) > NTP_RETRY_INTERVAL) {
            tryNTPSync();
        }
    } else if (!rtcInitialized && wifiConnected && wifiStationEnabled &&
               (lastNTPSync > 0 || lastBrowserSyncMs > 0 || lastDS3231SyncEpoch > 0) &&
               ESP.getFreeHeap() > MIN_HEAP_FOR_NTP &&
               elapsedSince(lastNTPAttempt) > NTP_RETRY_INTERVAL) {
        tryNTPSync();
    }  
    static unsigned long lastFSCheck = 0;
    if (elapsedSince(lastFSCheck) > 86400000UL) {
        if (!LittleFS.exists(CONFIG_FILE)) {
            loadConfiguration();
        }
        lastFSCheck = now;
    }
    if (elapsedSince(lastDNSRestart) > 3600000UL) {
        selfRecoveryDNS(); 
        lastDNSRestart = now;
    }
    if (elapsedSince(lastDeepHeal) > DEEP_HEAL_INTERVAL) {
        selfRecoveryState = SR_DEEP_HEAL;
    }    
}

// =============================================================================
//  CONFIGURATION FUNCTIONS
// =============================================================================
void initDefaults() {
    flashWriteCounter = 0;
    flashWriteHourStart = millis();
    memset(&sysConfig, 0, sizeof(SystemConfig));
    sysConfig.magic = EEPROM_MAGIC;
    sysConfig.version = EEPROM_VERSION;
    strcpy(sysConfig.ap_ssid, "ESP8266_6CH_Timer_Switch");
    strcpy(sysConfig.ap_password, "12345678");
    strcpy(sysConfig.ntp_server, "time.google.com");
    sysConfig.gmt_offset = 28800;
    sysConfig.daylight_offset = 0;
    sysConfigSetLastRtcEpoch(0);
    sysConfig.rtc_drift = 1.0f;
    strcpy(sysConfig.hostname, "esp8266relay");
    sysConfig.crystal_ppm = 0;
    sysConfig.crystal_calibrated = false;
    sysConfig.wifi_station_enabled = true;
    memset(sysConfig.reserved, 0, sizeof(sysConfig.reserved));
    memset(&extConfig, 0, sizeof(ExtConfig));
    extConfig.magic = EXT_CFG_MAGIC;
    extConfig.ap_channel = 6;
    extConfig.ntp_sync_hours = 1;
    extConfig.ap_hidden = 0;    
    pinConfig.magic = PINS_MAGIC;
    pinConfig.numRelays = 6;
    pinConfig.globalActiveLow = true;
    numRelays = 6;
    relayActiveLow = true;    
    uint8_t defaultPins[] = {16, 14, 12, 13, 3, 1};
    for (int i = 0; i < MAX_RELAYS; i++) {
        memset(&relayConfigs[i], 0, sizeof(RelayConfig));
        if (i < 6) {
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
            sprintf(relayConfigs[i].name, "Relay %d", i + 1);
            for (int s = 0; s < 8; s++) {
                relayConfigs[i].schedule.days[s] = 0x7F;
                relayConfigs[i].schedule.monthDays[s] = 0;
                relayConfigs[i].schedule.monthMask[s] = 0;
                relayConfigs[i].schedule.enabled[s] = false;
            }
        }
    }    
    saveConfiguration();
    saveExtConfig();
    savePinConfig();
}
void loadConfiguration() {
    loadFromFile(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    if (sysConfig.magic != EEPROM_MAGIC) {
        initDefaults();
        return;
    }
    if (sysConfig.version < 8) {
        sysConfig.crystal_ppm = 0;
        sysConfig.crystal_calibrated = false;
    }
    if (sysConfig.version < 9) {
        sysConfig.wifi_station_enabled = true;
    }
    sysConfig.version = EEPROM_VERSION;
    loadFromFile(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        for (int s = 0; s < 8; s++) {
            TimerSchedule &ts = relayConfigs[i].schedule;
            ts.days[s]        &= 0x7F;
            ts.monthDays[s]   &= 0x7FFFFFFFUL;
            ts.monthMask[s]   &= 0x0FFF;
            if (ts.startHour[s]   > 23) ts.startHour[s]   = 0;
            if (ts.startMinute[s] > 59) ts.startMinute[s] = 0;
            if (ts.startSecond[s] > 59) ts.startSecond[s] = 0;
            if (ts.stopHour[s]    > 23) ts.stopHour[s]    = 0;
            if (ts.stopMinute[s]  > 59) ts.stopMinute[s]  = 0;
            if (ts.stopSecond[s]  > 59) ts.stopSecond[s]  = 0;
        }
    }
    {
        static const uint8_t VALID_PINS[] = {16, 14, 12, 13, 3, 1};
        static const uint8_t NUM_VALID_PINS = 6;
        bool used[NUM_VALID_PINS];
        memset(used, 0, sizeof(used));
        for (int i = 0; i < MAX_RELAYS; i++) {
            int idx = -1;
            for (uint8_t k = 0; k < NUM_VALID_PINS; k++) {
                if (relayConfigs[i].pin == VALID_PINS[k]) { idx = k; break; }
            }
            bool needsRepair = (idx < 0) || used[idx];
            if (needsRepair) {
                idx = -1;
                for (uint8_t k = 0; k < NUM_VALID_PINS; k++) {
                    if (!used[k]) { idx = k; break; }
                }
                if (idx < 0) {
                    relayConfigs[i].pin = 0xFF;
                    relayConfigs[i].activeLow = true;
                    continue;
                }
                relayConfigs[i].pin = VALID_PINS[idx];
                relayConfigs[i].activeLow = true;
            }
            used[idx] = true;
        }
    }
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        if (strlen(relayConfigs[i].name) == 0) {
            sprintf(relayConfigs[i].name, "Relay %d", i + 1);
        }
    }
}
void saveConfiguration() {
    bool ok1 = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    bool ok2 = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    if (!ok1 || !ok2) {
        markConfigDirty();
    }
}
void loadExtConfig() {
    loadFromFile(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig));
    if (extConfig.magic != EXT_CFG_MAGIC) {
        memset(&extConfig, 0, sizeof(ExtConfig));
        extConfig.magic = EXT_CFG_MAGIC;
        extConfig.ap_channel = 6;
        extConfig.ntp_sync_hours = 1;
        extConfig.ap_hidden = 0;
        saveExtConfig();
    }    
    if (extConfig.ap_channel < 1 || extConfig.ap_channel > 13) extConfig.ap_channel = 6;
    if (extConfig.ntp_sync_hours < 1 || extConfig.ntp_sync_hours > 24) extConfig.ntp_sync_hours = 1;
}

void saveExtConfig() {
    saveToFileAtomic(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig));
}
void loadPinConfig() {
    loadFromFile(PINS_FILE, &pinConfig, sizeof(PinConfig));
    if (pinConfig.magic != PINS_MAGIC) {
        pinConfig.magic = PINS_MAGIC;
        pinConfig.numRelays = 6;
        pinConfig.globalActiveLow = true;
        numRelays = 6;
        relayActiveLow = true;
        savePinConfig();        
        uint8_t defaultPins[] = {16, 14, 12, 13, 3, 1};
        for (int i = 0; i < 6; i++) {
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
        }
        saveConfiguration();
        return;
    }    
    if (pinConfig.numRelays < 1 || pinConfig.numRelays > MAX_RELAYS) {
        pinConfig.numRelays = 6;
    }
    numRelays = pinConfig.numRelays;
    relayActiveLow = pinConfig.globalActiveLow;
}
void savePinConfig() {
    saveToFileAtomic(PINS_FILE, &pinConfig, sizeof(PinConfig));
}

// =============================================================================
//  SETUP 
// =============================================================================
void setup() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? HIGH : LOW);    
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);    
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }
    initDS3231();    
    loadConfiguration();
    loadExtConfig();
    loadPinConfig();
    loadRTCState();
    loadMqttConfig();
    initializeMqttClient();
    updateRelayPins();    
    wifiStationEnabled = sysConfig.wifi_station_enabled;    
    if (strlen(sysConfig.sta_ssid) > 0 && wifiStationEnabled) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.setAutoReconnect(false);
        WiFi.begin(sysConfig.sta_ssid, sysConfig.sta_password);
        wcsState = WCS_PENDING;
        wcsStart = millis();        
        timeClient.setPoolServerName(sysConfig.ntp_server);
        timeClient.setTimeOffset(0);
        timeClient.begin();
    } else {
        WiFi.mode(WIFI_AP);
        wifiConnected = false;
    }
    uint8_t ch = extConfig.ap_channel;
    if (ch < 1 || ch > 13) ch = 6;
    uint8_t hidden = extConfig.ap_hidden ? 1 : 0;
    if (strlen(sysConfig.ap_password) > 0)
        WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, hidden);
    else
        WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, hidden);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    setupWebServer();
    reconnectHourStart = millis();
    lastTimeVerification = millis();
    lastHeapCheck = millis();
    lastSelfCheck = millis();
    lastInternalTimeCheck = millis();
    lastProactiveMaintenance = millis();
    lastGentleHealing = millis();
    lastDNSRestart = millis();
    lastHeapDefrag = millis();
    lastConnectionCleanup = millis();
    lastWebServerRecovery = millis();
    lastWebServerCheck = millis();
    lastDNSCheck = millis();
    lastDS3231Sync = millis();
    lastDeepHeal = millis();
    lastConfigSave = millis();
    configDirtySince = millis();
    flashWriteHourStart = millis();    
    if (ds3231Present) {
        syncFromDS3231();
    }
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
    ESP.wdtFeed();
    yield();    
    (void)getUptimeMs64();
    unsigned long now = millis();    
    if (elapsedSince(lastWatchdogFeed) >= WATCHDOG_FEED_INTERVAL) {
        ESP.wdtFeed();
        lastWatchdogFeed = now;
    }    
    monitorHeapHealth();
    checkWiFiStability();    
    defragmentHeap();
    manageConnections();
    checkWebServerHealth();
    checkDNSHealth();    
    processFactoryReset();
    processNTPState();
    processNetworkHealing();    
    checkFactoryResetButton();    
    if (selfRecoveryState != SR_IDLE) {
        switch (selfRecoveryState) {
            case SR_MEMORY_CLEANUP:
                selfRecoveryMemoryCleanup();
                break;
            case SR_WIFI_RECONNECT:
                selfRecoveryWifiReconnect();
                break;
            case SR_WEB_SERVER_RECOVER:
                selfRecoveryWebServer();
                break;
            case SR_DNS_RECOVER:
                selfRecoveryDNS();
                break;
            case SR_RTC_RECOVER: {
                static unsigned long lastRtcRecoverAttempt = 0;
                if (lastRtcRecoverAttempt == 0 ||
                    elapsedSince(lastRtcRecoverAttempt) >= 60000UL) {
                    lastRtcRecoverAttempt = millis();
                    selfRecoveryRTC();
                } else {
                    selfRecoveryState = SR_IDLE;
                }
                break;
            }
            case SR_NETWORK_HEAL:
                selfRecoveryNetworkHeal();
                break;
            case SR_DEEP_HEAL:
                selfRecoveryDeepHeal();
                break;
            default:
                selfRecoveryState = SR_IDLE;
                break;
        }
    }    
    dnsServer.processNextRequest();
    server.handleClient();
    if (mdnsStarted) MDNS.update();
    processAPRestart();
    if (pendingWiFiReconnect && elapsedSince(pendingWiFiReconnectAt) >= 800UL) {
        pendingWiFiReconnect = false;
        beginWiFiConnect();
    }
    if (pendingAPRestart && elapsedSince(pendingAPRestartAt) >= 800UL) {
        pendingAPRestart = false;
        restartAP();
    }
    if (pendingStationDisable && elapsedSince(pendingStationDisableAt) >= 500UL) {
        pendingStationDisable = false;
        disableWiFiStation();
    }
    updateStatusLED();    
    if (wifiStationEnabled && wifiConnected && WiFi.status() != WL_CONNECTED) {
        if (wifiReconnectAttempts < 3) {
            WiFi.reconnect();
            wifiReconnectAttempts++;
        } else {
            selfRecoveryState = SR_WIFI_RECONNECT;
        }
    }    
    if (ESP.getFreeHeap() < CRITICAL_HEAP_THRESHOLD) {
        selfRecoveryState = SR_MEMORY_CLEANUP;
    }    
    if (rtcInitialized) {
        epoch64_t currentEpoch = getCurrentEpoch();
        if (currentEpoch < EPOCH64_MIN_VALID || currentEpoch > EPOCH64_APP_MAX) {
            selfRecoveryState = SR_RTC_RECOVER;
        }
    } else if (wifiStationEnabled &&
               (lastNTPSync > 0 || lastBrowserSyncMs > 0 || lastDS3231SyncEpoch > 0)) {
        selfRecoveryState = SR_RTC_RECOVER;
    }    
    proactiveMaintenance();
    gentleNetworkHealing();    
    if (wcsState == WCS_PENDING) {
        if (WiFi.status() == WL_CONNECTED) {
            wcsState = WCS_IDLE;
            wifiConnected = true;
            wifiReconnectAttempts = 0;
            wifiGiveUpUntil = 0;
            reconnectCountThisHour = 0;
            persistentDisconnectTime = 0;            
            timeClient.setPoolServerName(sysConfig.ntp_server);
            timeClient.setTimeOffset(0);
            timeClient.begin();
            startMDNS();
            lastNTPSync = 0;
            lastNTPSyncEpoch = 0;
            lastNTPAttempt = 0;
            ntpFailCount = 0;
        } else if (elapsedSince(wcsStart) > WIFI_CONNECT_TIMEOUT) {
            wcsState = WCS_IDLE;
            if (wifiReconnectAttempts >= MAX_RECONNECT) {
                wifiGiveUpUntil = now + 300000UL;
                wifiReconnectAttempts = 0;
            }
        }
    }
    if (wifiStationEnabled && elapsedSince(lastWiFiCheck) >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = now;
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (wifiConnected && !connected) {
            wifiConnected = false;
            mdnsStarted = false;
            persistentDisconnectTime = now;
        } else if (!wifiConnected && !connected &&
                   wcsState == WCS_IDLE &&
                   strlen(sysConfig.sta_ssid) > 0) {
            beginWiFiConnect();
        } else if (!wifiConnected && connected) {
            wifiConnected = true;
            wifiReconnectAttempts = 0;
            wcsState = WCS_IDLE;
            persistentDisconnectTime = 0;
            startMDNS();
            lastNTPSync = 0;
            lastNTPSyncEpoch = 0;
        }
    } else if (!wifiStationEnabled && wifiConnected) {
        wifiConnected = false;
        mdnsStarted = false;
    }
    if (wifiConnected && wifiStationEnabled && ESP.getFreeHeap() > MIN_HEAP_FOR_NTP && ntpState == NTP_IDLE) {
        bool doSync = false;
        epoch64_t currentEpoch = getCurrentEpoch();        
        if (lastNTPSyncEpoch == 0 && lastNTPSync == 0) {
            if (elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
                doSync = true;
            }
        } else if (lastNTPSyncEpoch > 0 && currentEpoch > 0) {
            if (currentEpoch < lastNTPSyncEpoch) {
                if ((lastNTPSyncEpoch - currentEpoch) > 60LL &&
                    elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
                    doSync = true;
                }
            } else if ((currentEpoch - lastNTPSyncEpoch) >= (epoch64_t)(extConfig.ntp_sync_hours * 3600) &&
                       elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
                doSync = true;
            }
        } else if (ntpFailCount > 0 && elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
            doSync = true;
        }       
        if (!doSync && lastNTPSync > 0 && elapsedSince(lastNTPSync) > 86400000UL &&
            elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
            doSync = true;
        }        
        if (doSync) {
            tryNTPSync();
        }
    }
    if (wifiConnected && rtcInitialized && elapsedSince(lastTimeVerification) >= 3600000UL) {
        lastTimeVerification = now;
        epoch64_t currentEpoch = getCurrentEpoch();
        if (currentEpoch < EPOCH64_MIN_VALID || currentEpoch > EPOCH64_APP_MAX) {
            selfRecoveryState = SR_RTC_RECOVER;
        }
    }    
    if (rtcInitialized && elapsedSince(lastInternalTimeCheck) >= TIME_CHECK_INTERVAL) {
        lastInternalTimeCheck = now;
        epoch64_t currentEpoch = getCurrentEpoch();        
        if (currentEpoch < EPOCH64_MIN_VALID || currentEpoch > EPOCH64_APP_MAX) {
            selfRecoveryState = SR_RTC_RECOVER;
        }
    }    
    if (elapsedSince(lastHeapCheck) >= HEAP_CHECK_INTERVAL) {
        lastHeapCheck = now;
        performHeapCleanup();
    }    
    if (elapsedSince(lastSelfCheck) >= SELF_CHECK_INTERVAL) {
        lastSelfCheck = now;
        performSelfCheck();
    }    
    if (ds3231Present && elapsedSince(lastDS3231Sync) >= DS3231_SYNC_INTERVAL) {
        lastDS3231Sync = millis();
        syncFromDS3231();
    }    
    {
        static unsigned long lastScheduleRun = 0;
        if (elapsedSince(lastScheduleRun) >= 500UL) {
            lastScheduleRun = millis();
            processRelaySchedules();
        }
    }
    mqttService();
    mqttProcessCommand();
    mqttPublishPeriodic();
    flushConfigIfNeeded();    
    yield();
}

// =============================================================================
//  SCHEDULE PROCESSING 
// =============================================================================
void processRelaySchedules() {
    int safeNumRelays = (numRelays > MAX_RELAYS) ? MAX_RELAYS : numRelays;
    if (safeNumRelays < 1) safeNumRelays = 1;
    epoch64_t epoch = getCurrentEpoch();
    if (epoch < EPOCH64_MIN_VALID || epoch > EPOCH64_APP_MAX) {
        for (int i = 0; i < safeNumRelays; i++) {
            if (relayConfigs[i].pin == 0xFF) continue;
            if (!relayConfigs[i].manualOverride) {
                digitalWrite(relayConfigs[i].pin, 
                    relayConfigs[i].activeLow ? HIGH : LOW);
            }
        }
        return;
    }
    epoch64_t localEpoch = epoch + (epoch64_t)sysConfig.gmt_offset
                                 + (epoch64_t)sysConfig.daylight_offset;
    struct tm64 tmBuf;
    if (!epoch64ToTmUtc(localEpoch, &tmBuf)) return;
    int curTotalSeconds = tmBuf.tm_hour * 3600 + tmBuf.tm_min * 60 + tmBuf.tm_sec;
    for (int i = 0; i < safeNumRelays; i++) {
        if (relayConfigs[i].pin == 0xFF) continue;
        if (relayConfigs[i].manualOverride) {
            digitalWrite(relayConfigs[i].pin,
                relayConfigs[i].activeLow ? !relayConfigs[i].manualState
                                         :  relayConfigs[i].manualState);
            continue;
        }
        bool on = false;
        for (int s = 0; s < 8 && !on; s++) {
            if (!relayConfigs[i].schedule.enabled[s]) continue;
            int startSeconds = relayConfigs[i].schedule.startHour[s]   * 3600
                             + relayConfigs[i].schedule.startMinute[s]  *   60
                             + relayConfigs[i].schedule.startSecond[s];
            int stopSeconds  = relayConfigs[i].schedule.stopHour[s]    * 3600
                             + relayConfigs[i].schedule.stopMinute[s]   *   60
                             + relayConfigs[i].schedule.stopSecond[s];
            bool always    = (startSeconds == stopSeconds);
            bool overnight = (startSeconds >  stopSeconds);
            bool inTail    = overnight && (curTotalSeconds <  stopSeconds);
            bool inHead    = overnight && (curTotalSeconds >= startSeconds);
            bool inWindow  = (!overnight && !always)
                             && curTotalSeconds >= startSeconds
                             && curTotalSeconds <  stopSeconds;
            if (!always && !inTail && !inHead && !inWindow) continue;
            struct tm64 refTm;
            if (inTail) {
                epoch64_t refEpoch = localEpoch - 86400LL;
                if (!epoch64ToTmUtc(refEpoch, &refTm)) continue;
            } else {
                refTm = tmBuf;
            }
            uint8_t dayMask = relayConfigs[i].schedule.days[s];
            if (dayMask == 0) continue;
            if (!(dayMask & (1 << refTm.tm_wday))) continue;
            uint32_t mdMask = relayConfigs[i].schedule.monthDays[s];
            if (mdMask != 0 && !(mdMask & (1u << (refTm.tm_mday - 1)))) continue;
            uint16_t mMask = relayConfigs[i].schedule.monthMask[s];
            if (mMask != 0 && !(mMask & (1u << refTm.tm_mon))) continue;
            on = true;
        }
        if (relayConfigs[i].pin != 0xFF) {
            digitalWrite(relayConfigs[i].pin,
                relayConfigs[i].activeLow ? !on : on);
        }
    }
}

// =============================================================================
//  WEB SERVER HANDLERS
// =============================================================================
void handleRoot() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", index_html); 
}
void handleWiFiPage() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", wifi_html); 
}
void handleNtpPage() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", ntp_html); 
}
void handleApPage() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", ap_html); 
}
void handleSystemPage() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", system_html); 
}
void handlePinsPage() { 
    if (!checkPageRateLimit()) {
        server.send(429, "text/plain", "Too Many Requests");
        return;
    }
    server.send_P(200, "text/html", pins_html); 
}
void handleStyleCss() { 
    server.sendHeader("Cache-Control", "max-age=86400, public");
    server.sendHeader("ETag", "\"v1.1\"");
    server.send_P(200, "text/css", style_css); 
}
void handleCaptivePortal() { server.send_P(200, "text/html", PSTR("<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>")); }
void handleSuccess() { server.send_P(200, "text/plain", PSTR("success\n")); }
void handleConnectTest() { server.send_P(200, "text/plain", PSTR("Microsoft Connect Test")); }
void handleNcsi() { server.send_P(200, "text/plain", PSTR("Microsoft NCSI")); }
void handleRedirect() { server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true); server.send(302, "text/plain", ""); }
void handleNotFound() { 
    String host = (wifiConnected && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    server.sendHeader("Location", "http://" + host + "/", true); 
    server.send(302, "text/plain", ""); 
}
void setupWebServer() {
    if (!webServerHandlersRegistered) {
        server.on("/",       HTTP_GET, handleRoot);
        server.on("/wifi",   HTTP_GET, handleWiFiPage);
        server.on("/ntp",    HTTP_GET, handleNtpPage);
        server.on("/ap",     HTTP_GET, handleApPage);
        server.on("/pins",   HTTP_GET, handlePinsPage);
        server.on("/system", HTTP_GET, handleSystemPage);
        server.on("/mqtt",   HTTP_GET, handleMqttPage);
        server.on("/style.css", HTTP_GET, handleStyleCss);
        server.on("/api/relays",       HTTP_GET,  handleGetRelays);
        server.on("/api/relay/manual", HTTP_POST, handleManualControl);
        server.on("/api/relay/reset",  HTTP_POST, handleResetManual);
        server.on("/api/relay/save",   HTTP_POST, handleSaveRelay);
        server.on("/api/relay/name",   HTTP_POST, handleSaveRelayName);
        server.on("/api/time", HTTP_GET, handleGetTime);
        server.on("/api/time/browser-sync", HTTP_POST, handleBrowserTimeSync);
        server.on("/api/wifi",        HTTP_GET,  handleGetWiFi);
        server.on("/api/wifi",        HTTP_POST, handleSaveWiFi);
        server.on("/api/wifi/scan",   HTTP_POST, handleWiFiScanStart);
        server.on("/api/wifi/scan",   HTTP_GET,  handleWiFiScanResults);
        server.on("/api/wifi/toggle", HTTP_POST, handleWiFiToggle);
        server.on("/api/wifi/status", HTTP_GET,  handleWiFiStationStatus);
        server.on("/api/ntp",      HTTP_GET,  handleGetNTP);
        server.on("/api/ntp",      HTTP_POST, handleSaveNTP);
        server.on("/api/ntp/sync", HTTP_POST, handleSyncNTP);
        server.on("/api/ap", HTTP_GET,  handleGetAP);
        server.on("/api/ap", HTTP_POST, handleSaveAP);
        server.on("/api/pins", HTTP_GET,  handleGetPins);
        server.on("/api/pins", HTTP_POST, handleSavePins);
        server.on("/api/pins/reset", HTTP_POST, handleResetPins);  
        server.on("/api/system",        HTTP_GET,  handleGetSystem);
        server.on("/api/mqtt",          HTTP_GET,  handleGetMqtt);
        server.on("/api/mqtt",          HTTP_POST, handleSaveMqtt);
        server.on("/api/system",        HTTP_POST, handleSaveSystem);
        server.on("/api/reset",         HTTP_POST, handleReset);
        server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
        server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
        server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
        server.on("/generate_204", HTTP_GET, handleRedirect);
        server.on("/success.txt",   HTTP_GET, handleSuccess);
        server.on("/canonical.html", HTTP_GET, handleRedirect);
        server.on("/connecttest.txt", HTTP_GET, handleConnectTest);
        server.on("/ncsi.txt", HTTP_GET, handleNcsi);
        server.on("/redirect", HTTP_GET, handleRedirect);
        server.onNotFound(handleNotFound);
        webServerHandlersRegistered = true;
    }
    server.begin();
}

// =============================================================================
//  API HANDLERS
// =============================================================================
void handleGetRelays() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_FULL_RESPONSE) {
        server.send(503, "application/json", "{\"error\":\"Low memory, retry shortly\"}");
        return;
    }
    int safeNumRelays = (numRelays > MAX_RELAYS) ? MAX_RELAYS : numRelays;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    server.sendContent("[");
    for (int i = 0; i < safeNumRelays; i++) {
        bool relayState = false;
        if (relayConfigs[i].pin != 0xFF) {
            relayState = digitalRead(relayConfigs[i].pin);
            if (relayConfigs[i].activeLow) relayState = !relayState;
        }
        String entry;
        if (!entry.reserve(2048)) {
            server.sendContent("]");
            server.sendContent("");
            return;
        }
        if (i > 0) entry += ",";
        entry += "{\"name\":\"";
        for (const char* p = relayConfigs[i].name; *p; ++p) {
            char c = *p;
            if (c == '"' || c == '\\') { entry += '\\'; entry += c; }
            else if ((uint8_t)c < 0x20) { }
            else entry += c;
        }
        entry += "\",";
        entry += "\"state\":" + String(relayState ? "true" : "false") + ",";
        entry += "\"manual\":" + String(relayConfigs[i].manualOverride ? "true" : "false") + ",";
        entry += "\"schedules\":[";
        for (int s = 0; s < 8; s++) {
            if (s > 0) entry += ",";
            entry += "{";
            entry += "\"startHour\":" + String(relayConfigs[i].schedule.startHour[s]) + ",";
            entry += "\"startMinute\":" + String(relayConfigs[i].schedule.startMinute[s]) + ",";
            entry += "\"startSecond\":" + String(relayConfigs[i].schedule.startSecond[s]) + ",";
            entry += "\"stopHour\":" + String(relayConfigs[i].schedule.stopHour[s]) + ",";
            entry += "\"stopMinute\":" + String(relayConfigs[i].schedule.stopMinute[s]) + ",";
            entry += "\"stopSecond\":" + String(relayConfigs[i].schedule.stopSecond[s]) + ",";
            entry += "\"enabled\":" + String(relayConfigs[i].schedule.enabled[s] ? "true" : "false") + ",";
            entry += "\"days\":" + String(relayConfigs[i].schedule.days[s]) + ",";
            entry += "\"monthDays\":" + String(relayConfigs[i].schedule.monthDays[s]) + ",";
            entry += "\"monthMask\":" + String(relayConfigs[i].schedule.monthMask[s]);
            entry += "}";
        }
        entry += "]}";
        server.sendContent(entry);
        yield();
    }
    server.sendContent("]");
    server.sendContent("");
}
void handleSaveRelayName() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    int relay = doc["relay"];
    const char* name = doc["name"];
    if (relay >= 0 && relay < numRelays && name && strlen(name) > 0) {
        char trimmed[16];
        size_t n = strnlen(name, 15);
        memcpy(trimmed, name, n);
        trimmed[n] = '\0';
        bool hasContent = false;
        for (size_t k = 0; k < n; k++) if (trimmed[k] != ' ') { hasContent = true; break; }
        if (!hasContent) {
            server.send(400, "application/json", "{\"success\":false}");
            return;
        }
        memcpy(relayConfigs[relay].name, trimmed, n + 1);
        markConfigDirty();
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleManualControl() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    int relay = doc["relay"]; 
    bool state = doc["state"];
    if (relay >= 0 && relay < numRelays) {
        relayConfigs[relay].manualOverride = true;
        relayConfigs[relay].manualState = state;
        digitalWrite(relayConfigs[relay].pin,
            relayConfigs[relay].activeLow ? !state : state);
        markConfigDirty();
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleResetManual() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    int relay = doc["relay"];
    if (relay >= 0 && relay < numRelays) {
        relayConfigs[relay].manualOverride = false;
        processRelaySchedules();
        markConfigDirty();
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleSaveRelay() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"success\":false}"); return;
    }
    int relay = doc["relay"] | -1;
    if (relay < 0 || relay >= numRelays) {
        server.send(400, "application/json", "{\"success\":false}"); return;
    }
    JsonArray schedules = doc["schedules"];
    if (schedules.isNull()) {
        server.send(400, "application/json", "{\"success\":false}"); return;
    }
    int s = 0;
    for (JsonObject sc : schedules) {
        if (s >= 8) break;
        int sh = sc["startHour"]   | 0;
        int sm = sc["startMinute"] | 0;
        int ss = sc["startSecond"] | 0;
        int eh = sc["stopHour"]    | 0;
        int em = sc["stopMinute"]  | 0;
        int es = sc["stopSecond"]  | 0;
        if (sh < 0) sh = 0; if (sh > 23) sh = 23;
        if (sm < 0) sm = 0; if (sm > 59) sm = 59;
        if (ss < 0) ss = 0; if (ss > 59) ss = 59;
        if (eh < 0) eh = 0; if (eh > 23) eh = 23;
        if (em < 0) em = 0; if (em > 59) em = 59;
        if (es < 0) es = 0; if (es > 59) es = 59;
        relayConfigs[relay].schedule.startHour[s]   = (uint8_t)sh;
        relayConfigs[relay].schedule.startMinute[s] = (uint8_t)sm;
        relayConfigs[relay].schedule.startSecond[s] = (uint8_t)ss;
        relayConfigs[relay].schedule.stopHour[s]    = (uint8_t)eh;
        relayConfigs[relay].schedule.stopMinute[s]  = (uint8_t)em;
        relayConfigs[relay].schedule.stopSecond[s]  = (uint8_t)es;
        relayConfigs[relay].schedule.enabled[s]     = sc["enabled"]     | false;
        uint8_t  newDays  = sc.containsKey("days")
                              ? (((uint8_t)(sc["days"] | 0)) & 0x7Fu)
                              : 0x7Fu;
        uint32_t newMDays = ((uint32_t)(sc["monthDays"] | 0)) & 0x7FFFFFFFUL;
        uint16_t newMMask = ((uint16_t)(sc["monthMask"] | 0)) & 0x0FFFu;
        if (newMDays == 0x7FFFFFFFUL) newMDays = 0;
        if (newMMask == 0x0FFFu)      newMMask = 0;
        relayConfigs[relay].schedule.days[s]      = newDays;
        relayConfigs[relay].schedule.monthDays[s] = newMDays;
        relayConfigs[relay].schedule.monthMask[s] = newMMask;
        s++;
    }
    for (; s < 8; s++) {
        relayConfigs[relay].schedule.startHour[s]   = 0;
        relayConfigs[relay].schedule.startMinute[s] = 0;
        relayConfigs[relay].schedule.startSecond[s] = 0;
        relayConfigs[relay].schedule.stopHour[s]    = 0;
        relayConfigs[relay].schedule.stopMinute[s]  = 0;
        relayConfigs[relay].schedule.stopSecond[s]  = 0;
        relayConfigs[relay].schedule.enabled[s]     = false;
        relayConfigs[relay].schedule.days[s]        = 0x7F;
        relayConfigs[relay].schedule.monthDays[s]   = 0;
        relayConfigs[relay].schedule.monthMask[s]   = 0;
    }
    markConfigDirty();
    server.send(200, "application/json", "{\"success\":true}");
}
void handleGetTime() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    String ts = "--:--:--";
    String dayStr = "---";
    String dateStr = "--/--/----";
    epoch64_t ep = getCurrentEpoch();    
    if (epoch64IsValid(ep)) {
        epoch64_t localEp = ep + (epoch64_t)sysConfig.gmt_offset
                               + (epoch64_t)sysConfig.daylight_offset;
        struct tm64 t;
        if (epoch64ToTmUtc(localEp, &t)) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     t.tm_hour, t.tm_min, t.tm_sec);
            ts = buf;
            static const char* DAY_NAMES[7] = {
                "SUN","MON","TUE","WED","THU","FRI","SAT"
            };
            if (t.tm_wday >= 0 && t.tm_wday < 7) dayStr = DAY_NAMES[t.tm_wday];
            char dbuf[16];
            snprintf(dbuf, sizeof(dbuf), "%02d/%02d/%04d",
                     t.tm_mon + 1, t.tm_mday, t.tm_year + 1900);
            dateStr = dbuf;
        }
    }    
    bool ntpSynced = (lastNTPSync > 0 || lastNTPSyncEpoch > 0 ||
                      lastDS3231SyncEpoch > 0 || lastBrowserSyncEpoch > 0);
    bool ntpFresh = false;
    if (ntpSynced) {
        unsigned long oldestAge = 0xFFFFFFFFUL;
        if (lastNTPSync > 0) {
            unsigned long a = elapsedSince(lastNTPSync);
            if (a < oldestAge) oldestAge = a;
        }
        if (lastBrowserSyncMs > 0) {
            unsigned long a = elapsedSince(lastBrowserSyncMs);
            if (a < oldestAge) oldestAge = a;
        }
        if (lastDS3231Sync > 0) {
            unsigned long a = elapsedSince(lastDS3231Sync);
            if (a < oldestAge) oldestAge = a;
        }
        if (oldestAge < 172800000UL) ntpFresh = true;
    }
    server.send(200, "application/json", "{\"time\":\"" + ts + "\",\"day\":\"" + dayStr + "\",\"date\":\"" + dateStr + "\",\"wifi\":" + String(wifiConnected ? "true" : "false") + ",\"ntp\":" + String(ntpFresh ? "true" : "false") + "}");
}
void handleBrowserTimeSync() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { 
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing body\"}"); 
        return; 
    }    
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server.arg("plain"))) { 
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}"); 
        return; 
    }    
    if (!doc.containsKey("epoch") || doc["epoch"].isNull()) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing timestamp\"}");
        return;
    }
    long long rawVal = doc["epoch"] | 0LL;
    epoch64_t browserEpoch = (epoch64_t)rawVal;
    if (browserEpoch < EPOCH64_MIN_VALID || browserEpoch > EPOCH64_APP_MAX) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid timestamp\"}"); 
        return;
    }    
    if (rtcInitialized && internalEpoch > 0) {
        epoch64_t internalNow = getCurrentEpoch();
        if (internalNow >= EPOCH64_APP_MIN && internalNow <= EPOCH64_APP_MAX) {
            int64_t bsDelta = (int64_t)(browserEpoch - internalNow);
            if (bsDelta < -473040000LL || bsDelta > 473040000LL) {
                server.send(400, "application/json", "{\"success\":false,\"error\":\"Timestamp too far from current\"}"); 
                return;
            }
        }
    }
    syncInternalRTCFromBrowser(browserEpoch);    
    server.send(200, "application/json", "{\"success\":true}");
}
void handleGetWiFi() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    String resp;
    resp.reserve(256);
    String safeSsid;
    safeSsid.reserve(strlen(sysConfig.sta_ssid) * 2 + 1);
    appendJsonEscaped(safeSsid, sysConfig.sta_ssid);
    resp = "{\"ssid\":\"" + safeSsid + "\",";
    resp += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
    resp += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    resp += "\"rssi\":" + String(wifiConnected ? (int)WiFi.RSSI() : 0) + ",";
    resp += "\"stationEnabled\":" + String(wifiStationEnabled ? "true" : "false");    
    if (!wifiConnected && strlen(sysConfig.sta_ssid) > 0) {
        int status = (int)WiFi.status();
        resp += ",\"lastStatus\":" + String(status);
    }    
    resp += "}";
    server.send(200, "application/json", resp);
}
void handleSaveWiFi() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) { server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}"); return; }
    const char* ssid = doc["ssid"];
    const char* pw = doc["password"];
    if (!ssid || strlen(ssid) == 0) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
        return;
    }
    if (strlen(ssid) > 31) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID too long (max 31)\"}");
        return;
    }
    if (ssid && strlen(ssid) > 0 && strlen(ssid) < 32) {
        char oldSsid[32];
        char oldPw[64];
        strncpy(oldSsid, sysConfig.sta_ssid, 31); oldSsid[31] = '\0';
        strncpy(oldPw, sysConfig.sta_password, 63); oldPw[63] = '\0';
        bool ssidChanged = (strncmp(oldSsid, ssid, 32) != 0);
        bool clearPassword = doc["clearPassword"] | false;
        strncpy(sysConfig.sta_ssid, ssid, 31); 
        sysConfig.sta_ssid[31] = '\0';
        if (pw && strlen(pw) > 0) {
            strncpy(sysConfig.sta_password, pw, 63);
            sysConfig.sta_password[63] = '\0';
        } else if (clearPassword) {
            sysConfig.sta_password[0] = '\0';
        } else if (ssidChanged) {
            sysConfig.sta_password[0] = '\0';
        }
        bool saved = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
        bool savedRelay = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
        if (saved && savedRelay) { configDirty = false; lastConfigSave = millis(); }
        if (!saved || !savedRelay) {
            strncpy(sysConfig.sta_ssid, oldSsid, 31); sysConfig.sta_ssid[31] = '\0';
            strncpy(sysConfig.sta_password, oldPw, 63); sysConfig.sta_password[63] = '\0';
            server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed (flash rate limited or low heap)\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true}");
        wifiConnected = false;
        wifiReconnectAttempts = 0;
        wifiGiveUpUntil = 0;
        reconnectCountThisHour = 0;
        persistentDisconnectTime = 0;
        wcsState = WCS_IDLE;
        if (wifiStationEnabled) {
            pendingWiFiReconnect = true;
            pendingWiFiReconnectAt = millis();
        }
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"SSID too long\"}");
    }
}
void handleWiFiScanStart() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    if (wcsState != WCS_IDLE) { server.send(409, "application/json", "{\"scanning\":false}"); return; }
    if (!scanInProgress) {
        int scanStatus = WiFi.scanComplete();
        if (scanStatus == -1) {
            if (scanStartTime != 0 && elapsedSince(scanStartTime) > SCAN_TIMEOUT_MS) {
                ++mqttWiFiScanGeneration;
                WiFi.scanDelete();
                scanResultCount = -1;
            } else {
                server.send(409, "application/json", "{\"scanning\":true}");
                return;
            }
        }
        if (scanStatus >= 0) WiFi.scanDelete();
        scanInProgress = true;
        scanResultCount = -1;
        scanStartTime = millis();
        uint16_t scanGeneration = ++mqttWiFiScanGeneration;
        WiFi.scanNetworksAsync([scanGeneration](int n) {
            if (mqttWiFiScanGeneration != scanGeneration) return;
            scanResultCount = n;
            scanInProgress = false;
        }, false);
    }
    server.send(202, "application/json", "{\"scanning\":true}");
}
void handleWiFiScanResults() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    if (scanInProgress && elapsedSince(scanStartTime) > SCAN_TIMEOUT_MS) {
        int scanStatus = WiFi.scanComplete();
        ++mqttWiFiScanGeneration;
        scanInProgress = false;
        scanResultCount = scanStatus >= 0 ? scanStatus : -1;
    }
    if (scanInProgress) { server.send(200, "application/json", "{\"scanning\":true}"); return; }
    if (scanResultCount < 0) { server.send(200, "application/json", "{\"scanning\":false,\"networks\":[]}"); return; }
    DynamicJsonDocument doc(2048);
    doc["scanning"] = false;
    JsonArray nets = doc.createNestedArray("networks");
    for (int i = 0; i < scanResultCount && i < 20; i++) {
        JsonObject n = nets.createNestedObject();
        n["ssid"] = WiFi.SSID(i);
        n["rssi"] = WiFi.RSSI(i);
        n["enc"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    }
    if (!mqttScanPublishPending) {
        WiFi.scanDelete();
        scanResultCount = -1;
    }
    String resp; 
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}
void handleWiFiToggle() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) {
        bool willEnable = !wifiStationEnabled;
        if (willEnable) {
            enableWiFiStation();
            server.send(200, "application/json", 
                "{\"success\":true,\"enabled\":true}");
        } else {
            server.send(200, "application/json", 
                "{\"success\":true,\"enabled\":false}");
            pendingStationDisable = true;
            pendingStationDisableAt = millis();
        }
        return;
    }    
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"success\":false}");
        return;
    }    
    if (!doc.containsKey("enable")) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing enable field\"}");
        return;
    }
    bool enable = doc["enable"] | false;    
    if (enable) {
        enableWiFiStation();
        server.send(200, "application/json", 
            "{\"success\":true,\"enabled\":true}");
    } else {
        server.send(200, "application/json", 
            "{\"success\":true,\"enabled\":false}");
        pendingStationDisable = true;
        pendingStationDisableAt = millis();
    }
}
void handleWiFiStationStatus() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    server.send(200, "application/json", 
        "{\"enabled\":" + String(wifiStationEnabled ? "true" : "false") + 
        ",\"connected\":" + String(wifiConnected ? "true" : "false") + "}");
}
void handleGetNTP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }
    String safeSrv;
    safeSrv.reserve(strlen(sysConfig.ntp_server) * 2 + 1);
    appendJsonEscaped(safeSrv, sysConfig.ntp_server);
    server.send(200, "application/json", "{\"ntpServer\":\"" + safeSrv + "\",\"gmtOffset\":" + String(sysConfig.gmt_offset) + ",\"daylightOffset\":" + String(sysConfig.daylight_offset) + ",\"syncHours\":" + String(extConfig.ntp_sync_hours) + "}");
}
void handleSaveNTP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    const char* srv = doc["ntpServer"];
    bool srvValid = false;
    if (srv && strlen(srv) > 0 && strlen(srv) < 48) {
        srvValid = false;
        for (const char* p = srv; *p; ++p) { if (*p != ' ') { srvValid = true; break; } }
    }
    if (srvValid) {
        char oldSrv[48];
        long oldGmt = sysConfig.gmt_offset;
        int oldDst = sysConfig.daylight_offset;
        strncpy(oldSrv, sysConfig.ntp_server, 47); oldSrv[47] = '\0';
        strncpy(sysConfig.ntp_server, srv, 47); 
        sysConfig.ntp_server[47] = '\0';
        long gmt = doc["gmtOffset"] | sysConfig.gmt_offset;
        if (gmt < -43200L || gmt > 50400L) gmt = sysConfig.gmt_offset;
        int dst = doc["daylightOffset"] | sysConfig.daylight_offset;
        if (dst < -7200 || dst > 7200) dst = sysConfig.daylight_offset;
        sysConfig.gmt_offset = gmt;
        sysConfig.daylight_offset = dst;
        if (!saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig))) {
            strncpy(sysConfig.ntp_server, oldSrv, 47); sysConfig.ntp_server[47] = '\0';
            sysConfig.gmt_offset = oldGmt;
            sysConfig.daylight_offset = oldDst;
            server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            return;
        }
        if (wifiConnected) {
            timeClient.setPoolServerName(sysConfig.ntp_server);
            timeClient.setTimeOffset(0); 
        }
        if (doc.containsKey("syncHours") && !doc["syncHours"].isNull()) {
            long h = doc["syncHours"] | 1L;
            if (h >= 1 && h <= 24) { extConfig.ntp_sync_hours = (uint8_t)h; saveExtConfig(); }
        }
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleSyncNTP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!wifiConnected || !wifiStationEnabled) { server.send(400, "application/json", "{\"success\":false}"); return; }
    tryNTPSync();
    server.send(200, "application/json", "{\"success\":true}");
}
void handleGetAP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }
    String safeApSsid;
    appendJsonEscaped(safeApSsid, sysConfig.ap_ssid);
    bool hasPassword = (strlen(sysConfig.ap_password) > 0);
    server.send(200, "application/json", "{\"ap_ssid\":\"" + safeApSsid + "\",\"ap_password\":\"\",\"hasPassword\":" + String(hasPassword ? "true" : "false") + ",\"ap_channel\":" + String(extConfig.ap_channel) + ",\"ap_hidden\":" + String(extConfig.ap_hidden ? "true" : "false") + "}");
}
void handleSaveAP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false,\"error\":\"Too many requests\"}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing body\"}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}"); return; }
    const char* ssid = doc["ap_ssid"];
    const char* pw = doc["ap_password"];
    if (pw && strlen(pw) > 0 && strlen(pw) < 8) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"AP password must be 8-31 chars or empty\"}");
        return;
    }
    if (ssid && strlen(ssid) > 0 && strlen(ssid) < 32) {
        char oldSsid[32], oldPw[32];
        uint8_t oldCh = extConfig.ap_channel;
        uint8_t oldHidden = extConfig.ap_hidden;
        strncpy(oldSsid, sysConfig.ap_ssid, 31); oldSsid[31] = '\0';
        strncpy(oldPw, sysConfig.ap_password, 31); oldPw[31] = '\0';
        bool clearApPw = doc["clearPassword"] | false;
        strncpy(sysConfig.ap_ssid, ssid, 31); 
        sysConfig.ap_ssid[31] = '\0';
        if (pw && strlen(pw) > 0) {
            strncpy(sysConfig.ap_password, pw, 31);
            sysConfig.ap_password[31] = '\0';
        } else if (clearApPw) {
            sysConfig.ap_password[0] = '\0';
        }
        if (doc.containsKey("ap_channel")) { uint8_t ch = doc["ap_channel"]; if (ch >= 1 && ch <= 13) extConfig.ap_channel = ch; }
        if (doc.containsKey("ap_hidden")) { extConfig.ap_hidden = doc["ap_hidden"] ? 1 : 0; }
        bool okCfg = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
        bool okExt = okCfg && saveToFileAtomic(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig));
        if (!okCfg || !okExt) {
            strncpy(sysConfig.ap_ssid, oldSsid, 31); sysConfig.ap_ssid[31] = '\0';
            strncpy(sysConfig.ap_password, oldPw, 31); sysConfig.ap_password[31] = '\0';
            extConfig.ap_channel = oldCh;
            extConfig.ap_hidden = oldHidden;
            server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true}");
        pendingAPRestart = true;
        pendingAPRestartAt = millis();
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"AP SSID must be 1-31 characters\"}");
    }
}
void handleGetSystem() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    String safeHost, safeNtp, safeSsid;
    appendJsonEscaped(safeHost, sysConfig.hostname);
    appendJsonEscaped(safeNtp, sysConfig.ntp_server);
    appendJsonEscaped(safeSsid, sysConfig.sta_ssid);
    String resp = "{";
    resp += "\"hostname\":\"" + safeHost + "\",";
    resp += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    resp += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    {
        uint64_t u = getUptimeMs64();
        unsigned long sec = (unsigned long)(u / 1000ULL);
        resp += "\"uptime\":" + String(sec) + ",";
        resp += "\"uptimeHi\":" + String((uint32_t)(u >> 32)) + ",";
        resp += "\"uptimeLo\":" + String((uint32_t)(u & 0xFFFFFFFFULL)) + ",";
    }
    resp += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    bool ntpSynced = (lastNTPSync > 0 || lastNTPSyncEpoch > 0 ||
                      lastDS3231SyncEpoch > 0 || lastBrowserSyncEpoch > 0);
    bool ntpFresh = false;
    if (ntpSynced) {
        unsigned long oldestAge = 0xFFFFFFFFUL;
        if (lastNTPSync > 0) {
            unsigned long a = elapsedSince(lastNTPSync);
            if (a < oldestAge) oldestAge = a;
        }
        if (lastBrowserSyncMs > 0) {
            unsigned long a = elapsedSince(lastBrowserSyncMs);
            if (a < oldestAge) oldestAge = a;
        }
        if (lastDS3231Sync > 0) {
            unsigned long a = elapsedSince(lastDS3231Sync);
            if (a < oldestAge) oldestAge = a;
        }
        if (oldestAge < 172800000UL) ntpFresh = true;
    }
    resp += "\"ntpSynced\":" + String(ntpFresh ? "true" : "false") + ",";
    resp += "\"ntpServer\":\"" + safeNtp + "\",";
    unsigned long syncAge = 0;
    if (lastNTPSync > 0) syncAge = elapsedSince(lastNTPSync) / 1000UL;
    else if (lastBrowserSyncMs > 0) syncAge = elapsedSince(lastBrowserSyncMs) / 1000UL;
    else if (lastDS3231Sync > 0) syncAge = elapsedSince(lastDS3231Sync) / 1000UL;
    resp += "\"ntpSyncAge\":" + String(ntpSynced ? (int)syncAge : -1) + ",";
    resp += "\"wifiConnected\":" + String(wifiConnected ? "true" : "false") + ",";
    resp += "\"wifiStationEnabled\":" + String(wifiStationEnabled ? "true" : "false") + ",";
    resp += "\"wifiSSID\":\"" + safeSsid + "\",";
    resp += "\"rssi\":" + String(wifiConnected ? (int)WiFi.RSSI() : 0) + ",";
    resp += "\"mdnsHostname\":\"" + String(sysConfig.hostname) + ".local\",";
    resp += "\"version\":" + String(EEPROM_VERSION) + ",";
    resp += "\"ds3231Present\":" + String(ds3231Present ? "true" : "false") + ",";
    if (ds3231Present) {
        static unsigned long lastTempRead = 0;
        static float lastTempValue = NAN;
        if (lastTempRead == 0 || elapsedSince(lastTempRead) >= 10000UL) {
            lastTempRead = millis();
            lastTempValue = rtc.getTemperature();
        }
        if (isnan(lastTempValue) || !isfinite(lastTempValue)) {
            resp += "\"ds3231Temp\":null";
        } else {
            resp += "\"ds3231Temp\":" + String(lastTempValue, 1);
        }
    } else {
        resp += "\"ds3231Temp\":null";
    }
    resp += "}";
    server.send(200, "application/json", resp);
}
void handleSaveSystem() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    const char* h = doc["hostname"];
    if (h && strlen(h) > 0 && strlen(h) < 32) {
        char oldHost[32];
        strncpy(oldHost, sysConfig.hostname, 31); oldHost[31] = '\0';
        strncpy(sysConfig.hostname, h, 31); 
        sysConfig.hostname[31] = '\0';
        for (char* p = sysConfig.hostname; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') {
                *p = (char)(*p - 'A' + 'a');
            } else if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) {
                *p = '-';
            }
        }
        if (sysConfig.hostname[0] == '\0') strcpy(sysConfig.hostname, "esp8266relay");
        if (!saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig))) {
            strncpy(sysConfig.hostname, oldHost, 31); sysConfig.hostname[31] = '\0';
            server.send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            return;
        }
        server.send(200, "application/json", "{\"success\":true}");
        if (wifiConnected && wifiStationEnabled) startMDNS();
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleGetPins() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_FULL_RESPONSE) {
        server.send(503, "application/json", "{\"error\":\"Low memory, retry shortly\"}");
        return;
    }
    DynamicJsonDocument doc(4096);
    doc["numRelays"] = numRelays;
    doc["globalActiveLow"] = relayActiveLow;    
    JsonArray relays = doc.createNestedArray("relays");
    for (int i = 0; i < numRelays; i++) {
        JsonObject r = relays.createNestedObject();
        r["name"] = relayConfigs[i].name;
        r["pin"] = relayConfigs[i].pin;
        r["activeLow"] = relayConfigs[i].activeLow;
    }    
    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}
void handleSavePins() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false,\"error\":\"Too many requests\"}");
        return;
    }    
    if (!server.hasArg("plain")) { 
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing body\"}"); 
        return; 
    }    
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, server.arg("plain"))) { 
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}"); 
        return; 
    }    
    int newNumRelays = doc["numRelays"];
    bool newGlobalActiveLow = doc["globalActiveLow"];    
    if (newNumRelays < 1 || newNumRelays > MAX_RELAYS) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid number of relays\"}"); 
        return;
    }    
    JsonArray relays = doc["relays"];    
    if (relays.size() != (size_t)newNumRelays) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Relay count mismatch\"}");
        return;
    }
    static const uint8_t VALID_PINS[] = {16, 14, 12, 13, 3, 1};
    static const uint8_t NUM_VALID_PINS = 6;
    bool pinUsed[NUM_VALID_PINS];
    memset(pinUsed, 0, sizeof(pinUsed));
    for (JsonObject r : relays) {
        int p = r["pin"] | -1;
        int validIdx = -1;
        for (uint8_t k = 0; k < NUM_VALID_PINS; k++) {
            if (VALID_PINS[k] == p) { validIdx = k; break; }
        }
        if (validIdx < 0) {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid or reserved pin\"}");
            return;
        }
        if (pinUsed[validIdx]) {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Duplicate pin\"}");
            return;
        }
        pinUsed[validIdx] = true;
    }
    RelayConfig* oldRelays = (RelayConfig*)malloc(sizeof(RelayConfig) * MAX_RELAYS);
    if (!oldRelays) {
        server.send(503, "application/json", "{\"success\":false,\"error\":\"Low memory\"}");
        return;
    }
    memcpy(oldRelays, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    PinConfig oldPinConfig = pinConfig;
    uint8_t   oldNumRelays = numRelays;
    bool      oldRelayActiveLow = relayActiveLow;
    {
        static const uint8_t VP[] = {16, 14, 12, 13, 3, 1};
        bool newUsed[6] = {false,false,false,false,false,false};
        for (JsonObject r : relays) {
            int p = r["pin"] | -1;
            for (uint8_t k = 0; k < 6; k++) if (VP[k] == p) { newUsed[k] = true; break; }
        }
        for (int i = 0; i < MAX_RELAYS; i++) {
            uint8_t p = relayConfigs[i].pin;
            for (uint8_t k = 0; k < 6; k++) {
                if (VP[k] == p && !newUsed[k]) {
                    pinMode(p, OUTPUT);
                    digitalWrite(p, relayConfigs[i].activeLow ? HIGH : LOW);
                }
            }
        }
    }
    pinConfig.numRelays = newNumRelays;
    pinConfig.globalActiveLow = newGlobalActiveLow;
    numRelays = newNumRelays;
    relayActiveLow = newGlobalActiveLow;    
    int idx = 0;
    for (JsonObject r : relays) {
        if (idx >= MAX_RELAYS) break;
        relayConfigs[idx].pin = r["pin"];
        relayConfigs[idx].activeLow = r.containsKey("activeLow")
            ? (bool)r["activeLow"]
            : pinConfig.globalActiveLow;
        {
            const char* nm = r["name"] | (const char*)nullptr;
            if (nm) {
                copyNameUtf8Safe(relayConfigs[idx].name, sizeof(relayConfigs[idx].name), nm);
            }
            if (relayConfigs[idx].name[0] == '\0') {
                snprintf(relayConfigs[idx].name, sizeof(relayConfigs[idx].name),
                         "Relay %d", idx + 1);
            }
        }
        idx++;
    }    
    bool pinSaved = saveToFileAtomic(PINS_FILE, &pinConfig, sizeof(PinConfig));
    bool relaySaved = pinSaved && saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    if (!pinSaved || !relaySaved) {
        pinConfig = oldPinConfig;
        numRelays = oldNumRelays;
        relayActiveLow = oldRelayActiveLow;
        memcpy(relayConfigs, oldRelays, sizeof(RelayConfig) * MAX_RELAYS);
        if (pinSaved && !relaySaved) {
            saveToFileAtomic(PINS_FILE, &oldPinConfig, sizeof(PinConfig));
        }
        free(oldRelays);
        updateRelayPins();
        markConfigDirty();
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Flash save failed\"}");
        return;
    }
    free(oldRelays);
    configDirty = false;
    lastConfigSave = millis();
    updateRelayPins();
    server.send(200, "application/json", "{\"success\":true}");
}
void handleResetPins() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false,\"error\":\"Too many requests\"}");
        return;
    }    
    char (*preservedNames)[16] = (char (*)[16])malloc(MAX_RELAYS * 16);
    TimerSchedule *preservedSchedules = (TimerSchedule*)malloc(MAX_RELAYS * sizeof(TimerSchedule));
    bool *preservedManualOverride = (bool*)malloc(MAX_RELAYS * sizeof(bool));
    bool *preservedManualState = (bool*)malloc(MAX_RELAYS * sizeof(bool));
    if (!preservedNames || !preservedSchedules || !preservedManualOverride || !preservedManualState) {
        free(preservedNames);
        free(preservedSchedules);
        free(preservedManualOverride);
        free(preservedManualState);
        server.send(503, "application/json", "{\"success\":false,\"error\":\"Low memory\"}");
        return;
    }
    for (int i = 0; i < MAX_RELAYS; i++) {
        strncpy(preservedNames[i], relayConfigs[i].name, 15);
        preservedNames[i][15] = '\0';
        preservedSchedules[i] = relayConfigs[i].schedule;
        preservedManualOverride[i] = relayConfigs[i].manualOverride;
        preservedManualState[i] = relayConfigs[i].manualState;
    }    
    {
        static const uint8_t VP[] = {16, 14, 12, 13, 3, 1};
        for (int i = 0; i < MAX_RELAYS; i++) {
            uint8_t p = relayConfigs[i].pin;
            bool valid = false;
            for (uint8_t k = 0; k < 6; k++) { if (VP[k] == p) { valid = true; break; } }
            if (valid) {
                pinMode(p, OUTPUT);
                digitalWrite(p, relayConfigs[i].activeLow ? HIGH : LOW);
            }
        }
    }
    RelayConfig* rollbackRelays = (RelayConfig*)malloc(sizeof(RelayConfig) * MAX_RELAYS);
    if (!rollbackRelays) {
        free(preservedNames);
        free(preservedSchedules);
        free(preservedManualOverride);
        free(preservedManualState);
        server.send(503, "application/json", "{\"success\":false,\"error\":\"Low memory\"}");
        return;
    }
    memcpy(rollbackRelays, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    PinConfig rollbackPinCfg = pinConfig;
    uint8_t   rollbackNumRelays = numRelays;
    bool      rollbackRelayActiveLow = relayActiveLow;
    uint8_t defaultPins[] = {16, 14, 12, 13, 3, 1};  
    pinConfig.numRelays = 6;  
    pinConfig.globalActiveLow = true;
    numRelays = 6;  
    relayActiveLow = true;
    for (int i = 0; i < 6; i++) {  
        relayConfigs[i].pin = defaultPins[i];
        relayConfigs[i].activeLow = true;
        strncpy(relayConfigs[i].name, preservedNames[i], 15);
        relayConfigs[i].name[15] = '\0';
        relayConfigs[i].schedule = preservedSchedules[i];
        relayConfigs[i].manualOverride = preservedManualOverride[i];
        relayConfigs[i].manualState = preservedManualState[i];
    }
        for (int i = 6; i < MAX_RELAYS; i++) {  
        memset(&relayConfigs[i], 0, sizeof(RelayConfig));
        relayConfigs[i].activeLow = true;
        relayConfigs[i].pin = 0xFF;
    }    
    bool pinSaved = saveToFileAtomic(PINS_FILE, &pinConfig, sizeof(PinConfig));
    bool cfgSaved = pinSaved && saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    bool relaySaved = cfgSaved && saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
    if (!pinSaved || !cfgSaved || !relaySaved) {
        memcpy(relayConfigs, rollbackRelays, sizeof(RelayConfig) * MAX_RELAYS);
        pinConfig = rollbackPinCfg;
        numRelays = rollbackNumRelays;
        relayActiveLow = rollbackRelayActiveLow;
        {
            static const uint8_t VP[] = {16, 14, 12, 13, 3, 1};
            for (uint8_t k = 0; k < 6; k++) {
                pinMode(VP[k], OUTPUT);
                digitalWrite(VP[k], HIGH);
            }
        }
        updateRelayPins();
        if (pinSaved && !relaySaved) {
            saveToFileAtomic(PINS_FILE, &rollbackPinCfg, sizeof(PinConfig));
        }
        free(rollbackRelays);
        free(preservedNames);
        free(preservedSchedules);
        free(preservedManualOverride);
        free(preservedManualState);
        markConfigDirty();
        server.send(500, "application/json", "{\"success\":false,\"error\":\"Flash save failed\"}");
        return;
    }
    updateRelayPins();
    free(rollbackRelays);
    free(preservedNames);
    free(preservedSchedules);
    free(preservedManualOverride);
    free(preservedManualState);
    configDirty = false;
    lastConfigSave = millis();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"GPIO pins reset to default\"}");
}
void handleReset() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }
    if (configDirty && ESP.getFreeHeap() >= MIN_HEAP_FOR_SAVE && checkFlashWriteRate()) {
        bool rtcSaved = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
        bool relaySaved = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
        if (rtcSaved && relaySaved) {
            configDirty = false;
            lastConfigSave = millis();
        }
    }
    server.send(200, "application/json", "{\"success\":true}");
    if (wifiStationEnabled) {
        wifiConnected = false;
        mdnsStarted = false;
        wcsState = WCS_IDLE;
        selfRecoveryState = SR_WIFI_RECONNECT;
    }
}
void handleFactoryReset() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Confirmation required\"}");
        return;
    }
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* confirm = doc["confirm"];
    if (!confirm || strcmp(confirm, "RESET") != 0) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Confirmation required\"}");
        return;
    }
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset initiated\"}");
    performFactoryReset();
}


