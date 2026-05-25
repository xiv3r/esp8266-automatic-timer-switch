/*
============================================================
 *  ESP8266 8-Channel Relay Smart Switch w/ LED Status Indicator
 *  Author: Raff Alds
 *  Github: https://www.github.com/xiv3r
 *  Project: Home, Business, Farm Automation etc...
============================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── LittleFS ──────────────────────────────────────────────────────
#define CONFIG_FILE    "/system.cfg"
#define EXTCONFIG_FILE "/ext.cfg"
#define RELAY_FILE     "/relays.cfg"
#define PINS_FILE      "/pins.cfg"
#define STATE_BACKUP   "/state.bak"

// ─── Magic numbers ────────────────────────────────────────────────────────────
#define EEPROM_MAGIC   0x1234
#define EEPROM_VERSION 7  
#define EXT_CFG_MAGIC  0xEC
#define PINS_MAGIC     0x50
#define STATE_MAGIC    0xAB

// ─── Timing ────────────────────────────────────────────────────────
static const unsigned long NTP_RETRY_INTERVAL   =   30000UL;
static const unsigned long WIFI_CHECK_INTERVAL  =    5000UL;
static const unsigned long WIFI_CONNECT_TIMEOUT =   15000UL;
static const unsigned long RTC_UPDATE_INTERVAL  =     100UL;
static const unsigned long CONFIG_SAVE_INTERVAL =   10000UL; 
static const unsigned long MAX_DRIFT_CALIBRATION_INTERVAL = 2592000000UL;
static const unsigned long PROACTIVE_MAINTENANCE_INTERVAL = 60000UL;
static const unsigned long GENTLE_NETWORK_HEAL_INTERVAL = 30000UL;
static const unsigned long HEAP_DEFRAG_INTERVAL = 7200000UL;    
static const unsigned long CONNECTION_CLEANUP_INTERVAL = 300000UL; 
static const unsigned long IDLE_CONNECTION_TIMEOUT = 60000UL;    

// ─── Stability ──────────────────────────────────────────
static const unsigned long MIN_HEAP_FOR_SAVE    =   5120UL; 
static const unsigned long MIN_HEAP_FOR_NTP     =   8192UL;  
static const uint8_t       MAX_RECONNECT_ATTEMPTS = 20;     
static const unsigned long RECONNECT_COOLDOWN   = 3600000UL; 
static const unsigned long HEAP_CHECK_INTERVAL  = 3600000UL;
static const unsigned long SELF_CHECK_INTERVAL  = 86400000UL;
static const unsigned long TIME_CHECK_INTERVAL  = 60000UL;
static const unsigned long LOOP_WATCHDOG_TIMEOUT = 300000UL;
static const unsigned long AUTO_HEAL_INTERVAL   = 60000UL;
static const uint8_t       MAX_SIMULTANEOUS_CONNECTIONS = 4;
static const uint8_t       API_RATE_LIMIT = 10;           
static const unsigned long RATE_LIMIT_WINDOW = 1000UL;    
static const unsigned long MIN_HEAP_FOR_FULL_RESPONSE = 8192UL;
static const unsigned long WEB_SERVER_RECOVERY_INTERVAL = 30000UL;
static const unsigned long DNS_RECOVERY_INTERVAL = 300000UL;

// ─── LED ──────────────────────────────────────────────────────────────
#define STATUS_LED_PIN       D4
#define STATUS_LED_ACTIVE_LOW true
static const unsigned long LED_BLINK_FAST = 200UL;
static const unsigned long LED_BLINK_SLOW = 1000UL;

// ─── Factory Reset Button ───────────────────────────────────────────
#define FACTORY_RESET_PIN    0  
static const unsigned long FACTORY_RESET_HOLD_MS = 5000UL;  
unsigned long factoryResetPressStart = 0;
bool factoryResetButtonPressed = false;
bool factoryResetInProgress = false;
uint8_t factoryResetLEDStep = 0;
unsigned long factoryResetLEDTimer = 0;

// ─── Non-blocking state machine enums ──────────────────────────────
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

// ─── Self-recovery states ─────────────────────────────────────────────────
enum SelfRecoveryState {
    SR_IDLE,
    SR_MEMORY_CLEANUP,
    SR_WIFI_RECONNECT,
    SR_WEB_SERVER_RECOVER,
    SR_DNS_RECOVER,
    SR_RTC_RECOVER,
    SR_TIMER_RECOVER,
    SR_NETWORK_HEAL
};
SelfRecoveryState selfRecoveryState = SR_IDLE;
unsigned long selfRecoveryTimer = 0;
uint8_t selfRecoveryAttempts = 0;
static const uint8_t MAX_SELF_RECOVERY_ATTEMPTS = 3;

// ─── Watchdog ─────────────────────────────────────────────────────
static const unsigned long WATCHDOG_FEED_INTERVAL = 500UL;
unsigned long lastWatchdogFeed = 0;

// ─── NTP Fallback───────────────────────────────────────────────────────
static const char* NTP_SERVERS[] = {
    "time.google.com",
    "time.windows.com",
    "time.cloudflare.com",
    "time.facebook.com"
};
static const uint8_t NUM_NTP_SERVERS = 4;

// ─── DNS + Web server ────────────────────────────────────────────────────────
DNSServer        dnsServer;
ESP8266WebServer server(80);
const byte       DNS_PORT = 53;

// ─── NTP Client ──────────────────────────────────────────────────────────────
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVERS[0], 28800, 3600000UL);

// ─── Dynamic Relay ────────────────────────────────────────────────────
#define MAX_RELAYS 16
uint8_t       numRelays = 8; 
bool          relayActiveLow = true;

// ─── Days ──────────────────────────────────────────────────────────────
const char* DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ─── Connection Management ───────────────────────────────────────────────────
unsigned long lastConnectionCleanup = 0;
uint8_t activeConnections = 0;

// ─── Rate Limiting ───────────────────────────────────────────────────────────
struct RateLimiter {
    unsigned long windowStart = 0;
    uint8_t requestCount = 0;
};
RateLimiter apiLimiter;
RateLimiter pageLimiter;

// ─── Web Server Health ──────────────────────────────────────────────────────
unsigned long lastWebServerCheck = 0;
bool webServerHealthy = true;
unsigned long lastDNSCheck = 0;

// ─── Data structures ─────────────────────────────────────────────────────────
struct TimerSchedule {
    uint8_t  startHour[8], startMinute[8], startSecond[8];
    uint8_t  stopHour[8],  stopMinute[8],  stopSecond[8];
    bool     enabled[8];
    uint8_t  days[8];       
    uint32_t monthDays[8];  
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
    time_t   last_rtc_epoch;
    float    rtc_drift;
    char     hostname[32]; 
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
struct RelayStateBackup {
    uint8_t magic;
    bool    states[MAX_RELAYS];
    bool    manual[MAX_RELAYS];
    uint8_t reserved[13];
};

// ─── Globals ──────────────────────────────────────────────────────────────────
SystemConfig sysConfig;
ExtConfig    extConfig;
PinConfig    pinConfig;
RelayConfig  relayConfigs[MAX_RELAYS];
time_t        internalEpoch            = 0;
unsigned long internalMillisAtLastSync = 0;
float         driftCompensation        = 1.0f;
bool          rtcInitialized           = false;
unsigned long lastRTCUpdate            = 0;
uint8_t       ntpServerIndex  = 0;
uint8_t       ntpFailCount    = 0;
unsigned long lastNTPSync     = 0;
unsigned long lastNTPAttempt  = 0;
time_t        lastNTPSyncEpoch = 0;    
bool          wifiConnected         = false;
bool          mdnsStarted           = false;
unsigned long lastWiFiCheck         = 0;
uint8_t       wifiReconnectAttempts = 0;
unsigned long wifiGiveUpUntil       = 0;
static const uint8_t MAX_RECONNECT  = 10;
uint8_t       reconnectCountThisHour = 0;
unsigned long reconnectHourStart     = 0;
unsigned long persistentFailTime     = 0;
enum WifiConnState { WCS_IDLE, WCS_PENDING };
WifiConnState wcsState = WCS_IDLE;
unsigned long wcsStart = 0;
enum APRestartState { APR_IDLE, APR_PENDING, APR_RESTARTING };
APRestartState aprState = APR_IDLE;
unsigned long aprStartTime = 0;
static const unsigned long AP_RESTART_DELAY = 50UL;
volatile bool scanInProgress  = false;
volatile int  scanResultCount = -1;
bool          configDirty    = false;
unsigned long lastConfigSave = 0;
unsigned long lastMarkTime   = 0;
bool          savePendingOnLowMem = false; 
bool          relaysNeedLoading = false;
char ap_ssid[32]     = "ESP8266_8CH_Timer_Switch";  
char ap_password[32] = "ESP8266-admin";
unsigned long lastLedToggle = 0;
bool          ledState      = false;
unsigned long lastTimeVerification = 0;
unsigned long lastHeapCheck        = 0;
unsigned long lastSelfCheck        = 0;
unsigned long lastInternalTimeCheck = 0;
unsigned long lastLoopHeartbeat    = 0;
unsigned long lastSuccessfulLoop   = 0;
int           ntpSyncCount = 0;
unsigned long persistentDisconnectTime = 0;
unsigned long lastProactiveMaintenance = 0;
unsigned long lastGentleHealing = 0;
bool networkHealingInProgress = false;
unsigned long lastStableTime = 0;
unsigned long lastDNSRestart = 0;
unsigned long lastFactoryResetBlink = 0;
unsigned long lastHeapDefrag = 0;
unsigned long lastWebServerRecovery = 0;
uint8_t loopTimeoutCounter = 0;  

// ─── Inline helper ───────────────────────────────────────────────────────────
inline unsigned long elapsedSince(unsigned long since) {
    unsigned long now = millis();    
    if (now >= since) {
        return now - since;
    }  
    return (0xFFFFFFFFUL - since) + now + 1UL;
}
inline unsigned long getNTPInterval() {
    uint8_t h = extConfig.ntp_sync_hours;
    if (h < 1 || h > 24) h = 1;
    return (unsigned long)h * 3600000UL;
}
inline const char* getPinName(uint8_t pin) {
    switch(pin) {
        case 16: return "D0";
        case 5:  return "D1";
        case 4:  return "D2";
        case 14: return "D5";
        case 12: return "D6";
        case 13: return "D7";
        case 3:  return "RX";
        case 1:  return "TX";
        default: return "??";
    }
}
inline bool shouldSimplifyResponse() {
    return ESP.getFreeHeap() < MIN_HEAP_FOR_FULL_RESPONSE;
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

// ─── Prototypes ──────────────────────────────────────────────────────────────
time_t getCurrentEpoch();
void syncInternalRTC();
void syncInternalRTCFromBrowser(time_t browserEpoch);
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
int extractJsonInt(const String& json, const char* key);
bool extractJsonBool(const String& json, const char* key);
uint8_t extractJsonByte(const String& json, const char* key);
uint32_t extractJsonUInt32(const String& json, const char* key);
bool loadFromFile(const char* path, void* data, size_t size);
bool saveToFileAtomic(const char* path, const void* data, size_t size);
void markConfigDirty();
void flushConfigIfNeeded();
void checkConnectionRateLimit();
void performHeapCleanup();
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
void checkWebServerHealth();
void checkDNSHealth();
void preserveRelayState();
void restoreRelayState();

// ─── Web server ────────────────────────────────────────────────
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

// ─── API────────────────────────────────────────────────────────────
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
void handleSoftReset();

// ─────────────────────────────────────────────────────────────────────────────
//  SHARED CSS
// ─────────────────────────────────────────────────────────────────────────────
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
.g{background:#69F0AE}.r{background:#FF5252}.y{background:#FFD740}
main{max-width:1200px;margin:0 auto;padding:16px}
.ptitle{font-size:17px;font-weight:700;color:#1565C0;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:14px}
.card{background:#fff;border-radius:10px;box-shadow:0 2px 8px rgba(0,0,0,.08);padding:16px;transition:box-shadow .2s}
.card:hover{box-shadow:0 4px 18px rgba(0,0,0,.13)}
.card-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}
.ctitle{font-weight:700;font-size:15px;cursor:pointer;transition:background .15s;padding:2px 4px;border-radius:4px}
.ctitle:hover{background:#E3F2FD;color:#1565C0}
.badge{padding:3px 9px;border-radius:20px;font-size:11px;font-weight:700}
.bon{background:#E8F5E9;color:#2E7D32}.boff{background:#FFEBEE;color:#C62828}.bman{background:#FFF3E0;color:#E65100}
.brow{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px}
.btn{border:none;padding:7px 12px;border-radius:6px;cursor:pointer;font-size:12px;font-weight:600;transition:.15s}
.btn:hover{filter:brightness(1.1)}.btn:disabled{opacity:.5;cursor:default}
.bon-b{background:#43A047;color:#fff}.boff-b{background:#E53935;color:#fff}.baut{background:#546E7A;color:#fff}
.bsave{background:#1565C0;color:#fff;width:100%;padding:9px;font-size:13px;border-radius:6px;margin-top:8px}
.bsync{background:#FB8C00;color:#fff}.bdanger{background:#B71C1C;color:#fff}.bwarn{background:#F9A825;color:#212121}
.bscan{background:#0288D1;color:#fff}.bbrowser{background:#7B1FA2;color:#fff}
.badd{background:#43A047;color:#fff}.bremove{background:#E53935;color:#fff}
.slist{display:flex;flex-direction:column;gap:6px;margin-bottom:8px;max-height:500px;overflow-y:auto;padding-right:2px}
.si{border:1px solid #E3E8EF;border-radius:7px;padding:9px}
.si.act{border-color:#90CAF9;background:#F0F7FF}
.shdr{display:flex;align-items:center;gap:7px;margin-bottom:7px;font-size:11px;font-weight:700;color:#607D8B;text-transform:uppercase}
.shdr label{display:flex;align-items:center;gap:4px;cursor:pointer;font-size:12px;font-weight:700;color:#1A1A2E;text-transform:none}
.trow{display:flex;align-items:center;gap:8px;font-size:12px;margin-top:5px}
.trow .l{color:#90A4AE;font-weight:600;width:32px;flex-shrink:0}
.days{display:flex;gap:3px;margin-top:5px;flex-wrap:wrap}
.day{width:28px;height:24px;border-radius:4px;border:1px solid #CFD8DC;display:flex;align-items:center;justify-content:center;font-size:11px;font-weight:600;cursor:pointer;background:#FAFAFA;transition:.15s;user-select:none}
.day:hover{border-color:#90CAF9;background:#E3F2FD}
.day.on{background:#1565C0;color:#fff;border-color:#1565C0}
.mdays{display:flex;gap:2px;margin-top:5px;flex-wrap:wrap}
.mday{width:26px;height:22px;border-radius:3px;border:1px solid #CFD8DC;display:flex;align-items:center;justify-content:center;font-size:10px;font-weight:600;cursor:pointer;background:#FAFAFA;transition:.15s;user-select:none}
.mday:hover{border-color:#CE93D8;background:#F3E5F5}
.mday.on{background:#7B1FA2;color:#fff;border-color:#7B1FA2}
.sched-section{margin-top:4px;font-size:10px;font-weight:600;color:#90A4AE;text-transform:uppercase;margin-bottom:2px}
.night{font-size:10px;color:#7B1FA2;background:#F3E5F5;padding:2px 6px;border-radius:4px;margin-left:auto}
.night.always{background:#E8F5E9;color:#2E7D32}
input[type=time]{flex:1;padding:5px 8px;border:1px solid #CFD8DC;border-radius:5px;font-size:14px;font-family:monospace;background:#FAFAFA;cursor:pointer;min-width:0}
input[type=time]:focus{outline:none;border-color:#1565C0;box-shadow:0 0 0 3px rgba(21,101,192,.15);background:#fff}
.ibar{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:16px}
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
#toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%) translateY(80px);background:#323232;color:#fff;padding:10px 20px;border-radius:8px;font-size:13px;transition:transform .28s;z-index:999;pointer-events:none;box-shadow:0 4px 16px rgba(0,0,0,.3);min-width:180px;text-align:center}
#toast.show{transform:translateX(-50%) translateY(0)}
#toast.ok{background:#2E7D32}#toast.er{background:#C62828}
@media(max-width:500px){.grid{grid-template-columns:1fr}.ibar{grid-template-columns:1fr}.input-row{flex-direction:column}.day{width:24px;height:22px;font-size:10px}.mday{width:22px;height:20px;font-size:9px}}
)css";

// ─────────────────────────────────────────────────────────────────────────────
//  HTML PAGES
// ─────────────────────────────────────────────────────────────────────────────

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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">Relay Controls &amp; Schedules</p>
<div class="grid" id="grid"></div>
</main>
<div id="toast"></div>
<script>
const D=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();

const NS=8;
let relays=[],busy=false;
let editingRelay = -1;
let editingInput = null;

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function load(){
  if(busy)return;
  fetch('/api/relays').then(r=>r.json()).then(d=>{relays=d;render();}).catch(()=>toast('Load error',false));
}

function toTS(h,m,s){
  h = parseInt(h) % 24;
  m = parseInt(m) % 60;
  s = parseInt(s) % 60;
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
  if(d===0x7F) return 'Everyday';
  let s='';
  for(let i=0;i<7;i++) if(d&(1<<i)) s+=D[i]+' ';
  return s.trim()||'None';
}

function monthDayMaskToStr(md){
  if(md===0) return '';
  if(md===0xFFFFFFFF) return 'All month days';
  let s='';
  for(let i=0;i<31;i++) if(md&(1<<i)) s+=(i+1)+',';
  return s.replace(/,$/,'')||'None';
}

function nightBadge(sc){
  if(!sc.enabled)return'';
  const a=sc.startHour*3600+sc.startMinute*60+sc.startSecond;
  const b=sc.stopHour*3600+sc.stopMinute*60+sc.stopSecond;
  const ds=dayMaskToStr(sc.days);
  const ms=monthDayMaskToStr(sc.monthDays||0);
  let info=ds;
  if(ms) info+=' | Days:'+ms;
  if(a===b)return'<span class="night always">&#x25CF; Always ON ('+info+')</span>';
  if(a>b) return'<span class="night">&#x1F319; Overnight ('+info+')</span>';
  return'<span class="night">&#x1F319; '+info+'</span>';
}

function startEditName(relayIdx) {
  if (editingRelay !== -1) cancelEdit();
  
  const nameSpan = document.getElementById('name_' + relayIdx);
  if (!nameSpan) return;
  
  const currentName = relays[relayIdx].name || ('Relay ' + (relayIdx + 1));
  
  const input = document.createElement('input');
  input.type = 'text';
  input.value = currentName;
  input.maxLength = 15;
  input.style.cssText = 'font-size:15px;font-weight:700;padding:2px 6px;border:1px solid #1565C0;border-radius:5px;width:120px;background:#fff;color:#1A1A2E;';
  input.id = 'edit_' + relayIdx;
  
  input.onblur = () => saveNameEdit(relayIdx, input.value);
  input.onkeydown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      saveNameEdit(relayIdx, input.value);
    } else if (e.key === 'Escape') {
      cancelEdit();
    }
  };
  
  nameSpan.style.display = 'none';
  nameSpan.parentNode.insertBefore(input, nameSpan.nextSibling);
  
  editingRelay = relayIdx;
  editingInput = input;
  input.focus();
  input.select();
}

function cancelEdit() {
  if (editingRelay !== -1) {
    const nameSpan = document.getElementById('name_' + editingRelay);
    if (nameSpan) nameSpan.style.display = '';
    if (editingInput) editingInput.remove();
    editingRelay = -1;
    editingInput = null;
  }
}

function saveNameEdit(relayIdx, newName) {
  newName = newName.trim();
  if (newName.length === 0) {
    newName = 'Relay ' + (relayIdx + 1);
  }
  
  const nameSpan = document.getElementById('name_' + relayIdx);
  
  relays[relayIdx].name = newName;
  if (nameSpan) {
    nameSpan.textContent = newName;
    nameSpan.style.display = '';
  }
  if (editingInput) editingInput.remove();
  editingRelay = -1;
  editingInput = null;
  
  fetch('/api/relay/name', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({relay: relayIdx, name: newName})
  })
  .then(r => r.json())
  .then(d => {
    if (d.success) {
      toast('Name saved!');
    } else {
      toast('Failed to save name', false);
    }
  })
  .catch(() => toast('Error saving name', false));
}

function render(){
  const g=document.getElementById('grid');
  g.innerHTML='';
  relays.forEach((r,i)=>{
    const sl=r.manual?'MANUAL':r.state?'ON':'OFF';
    const sc=r.manual?'bman':r.state?'bon':'boff';
    const displayName = r.name || ('Relay '+(i+1));
    let html=`<div class="card">
<div class="card-hdr">
<span class="ctitle" id="name_${i}" ondblclick="startEditName(${i})" title="Double-click to rename">${escapeHtml(displayName)}</span>
<span class="badge ${sc}">${sl}</span>
</div>
<div class="brow">
<button class="btn bon-b" onclick="mc(${i},true)">ON</button>
<button class="btn boff-b" onclick="mc(${i},false)">OFF</button>
<button class="btn baut" onclick="ra(${i})">Auto</button>
</div>
<div class="slist">`;
    for(let s=0;s<NS;s++){
      const sc2=r.schedules[s];
      const dayBits = sc2.days || 0x7F;
      const monthDayBits = sc2.monthDays || 0;
      html+=`<div class="si${sc2.enabled?' act':''}" id="si_${i}_${s}">
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
      for(let d=0;d<7;d++){
        const mask = 1<<d;
        html+=`<div class="day${(dayBits&mask)?' on':''}" onclick="toggleDay(${i},${s},${d})">${D[d]}</div>`;
      }
      html+=`</div>
<div class="sched-section">Days of Month</div>
<div class="mdays" id="mday_${i}_${s}">`;
      for(let d=0;d<31;d++){
        const mask = 1<<d;
        html+=`<div class="mday${(monthDayBits&mask)?' on':''}" onclick="toggleMonthDay(${i},${s},${d})" title="Day ${d+1}">${d+1}</div>`;
      }
      html+=`</div>
</div>`;
    }
    html+=`</div><button class="btn bsave" onclick="save(${i})">&#x1F4BE; Save ${escapeHtml(displayName)}</button></div>`;
    const el=document.createElement('div');
    el.innerHTML=html;
    g.appendChild(el.firstChild);
  });
}

function toggleDay(ri,si,dayIdx){
  const mask = 1<<dayIdx;
  relays[ri].schedules[si].days ^= mask;
  const dayEl = document.getElementById('day_'+ri+'_'+si).children[dayIdx];
  if(dayEl) dayEl.className = 'day' + ((relays[ri].schedules[si].days & mask)?' on':'');
  const nb=document.getElementById('nb_'+ri+'_'+si);
  if(nb)nb.innerHTML=nightBadge(relays[ri].schedules[si]);
}

function toggleMonthDay(ri,si,dayIdx){
  const mask = 1<<dayIdx;
  if(!relays[ri].schedules[si].monthDays) relays[ri].schedules[si].monthDays = 0;
  relays[ri].schedules[si].monthDays ^= mask;
  const mdayEl = document.getElementById('mday_'+ri+'_'+si).children[dayIdx];
  if(mdayEl) mdayEl.className = 'mday' + ((relays[ri].schedules[si].monthDays & mask)?' on':'');
  const nb=document.getElementById('nb_'+ri+'_'+si);
  if(nb)nb.innerHTML=nightBadge(relays[ri].schedules[si]);
}

function uf(ri,si,field,val){
  const sc=relays[ri].schedules[si];
  if(field==='en'){
    sc.enabled=val;
    const el=document.getElementById('si_'+ri+'_'+si);
    if(el)el.className='si'+(val?' act':'');
  }else if(field==='start'){
    const t=fromTS(val);sc.startHour=t.h;sc.startMinute=t.m;sc.startSecond=t.s;
  }else if(field==='stop'){
    const t=fromTS(val);sc.stopHour=t.h;sc.stopMinute=t.m;sc.stopSecond=t.s;
  }
  const nb=document.getElementById('nb_'+ri+'_'+si);
  if(nb)nb.innerHTML=nightBadge(sc);
}

function mc(ri,state){
  fetch('/api/relay/manual',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:ri,state})})
  .then(r=>r.json()).then(d=>{if(d.success){toast((relays[ri].name||('Relay '+(ri+1)))+' '+(state?'ON':'OFF'));load();}else toast('Failed',false);})
  .catch(()=>toast('Error',false));
}
function ra(ri){
  fetch('/api/relay/reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:ri})})
  .then(r=>r.json()).then(d=>{if(d.success){toast((relays[ri].name||('Relay '+(ri+1)))+' \u2192 Auto');load();}else toast('Failed',false);})
  .catch(()=>toast('Error',false));
}
function save(ri){
  busy=true;
  fetch('/api/relay/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:ri,schedules:relays[ri].schedules})})
  .then(r=>r.json()).then(d=>{busy=false;if(d.success)toast((relays[ri].name||('Relay '+(ri+1)))+' saved!');else toast('Save failed',false);})
  .catch(()=>{busy=false;toast('Error',false);});
}
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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
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
<div class="fg"><label>Password</label><input type="password" id="pw" placeholder="Enter wifi password"></div>
<button class="btn bsave" onclick="save()">&#x1F4BE; Save &amp; Connect</button>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
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

function loadWiFiInfo(){
  fetch('/api/wifi').then(r=>r.json()).then(d=>{
    document.getElementById('ssid').value=d.ssid||'';
    const s=document.getElementById('status');
    s.style.display='';
    if(d.connected){
      const bars=rssiBar(d.rssi||0);
      s.innerHTML='&#x2705; Connected to: <strong>'+escapeHtml(d.ssid)+'</strong> ('+escapeHtml(d.ip)+') &nbsp;'+bars+' '+d.rssi+'dBm';
      s.className='alert ai';
    }else{
      s.className='alert aw';
      if(d.lastStatus !== undefined && d.ssid) {
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
setInterval(loadWiFiInfo, 5000);

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
  .then(()=>{ scanTimer=setInterval(pollScan,2500); })
  .catch(()=>endScan());
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
  
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password:document.getElementById('pw').value})})
  .then(r=>r.json()).then(d=>{
    if(d.success){
      toast('Saved! Connecting to '+escapeHtml(ssid)+'...');
      setTimeout(loadWiFiInfo, 3000);
      setTimeout(loadWiFiInfo, 8000);
      setTimeout(loadWiFiInfo, 15000);
    } else {
      toast('Failed: '+escapeHtml(d.error||'unknown'),false);
      s.innerHTML='&#x26A0;&#xFE0F; Save failed';
      s.className='alert aw';
    }
  }).catch(()=>toast('Error',false));
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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
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
<button class="btn bsave" style="flex:1;margin-top:0" onclick="save()">&#x1F4BE; Save Settings</button>
<button class="btn bsync" id="sbtn" onclick="sync()" style="padding:9px 16px;border-radius:6px;font-size:13px;font-weight:600;margin-top:8px;white-space:nowrap">&#x1F504; Sync NTP</button>
<button class="btn bbrowser" id="bbtn" onclick="syncFromBrowser()" style="padding:9px 16px;border-radius:6px;font-size:13px;font-weight:600;margin-top:8px;white-space:nowrap">&#x1F310; Sync Browser</button>
</div>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
fetch('/api/ntp').then(r=>r.json()).then(d=>{
  document.getElementById('srv').value=d.ntpServer||'time.google.com';
  document.getElementById('gmt').value=d.gmtOffset||28800;
  document.getElementById('dst').value=d.daylightOffset||0;
  document.getElementById('shi').value=d.syncHours||1;
}).catch(()=>{});
function save(){
  const h=parseInt(document.getElementById('shi').value);
  if(h<1||h>24){toast('Sync interval must be 1\u201324 h',false);return;}
  fetch('/api/ntp',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    ntpServer:document.getElementById('srv').value,
    gmtOffset:parseInt(document.getElementById('gmt').value),
    daylightOffset:parseInt(document.getElementById('dst').value),
    syncHours:h
  })}).then(r=>r.json()).then(d=>{if(d.success)toast('NTP settings saved!');else toast('Failed: '+d.error,false);})
  .catch(()=>toast('Error',false));
}
function sync(){
  const b=document.getElementById('sbtn');b.disabled=true;b.textContent='Syncing\u2026';
  fetch('/api/ntp/sync',{method:'POST'}).then(r=>r.json()).then(d=>{
    b.disabled=false;b.innerHTML='&#x1F504; Sync Now';
    if(d.success)toast('Time synced successfully!');else toast('Sync failed \u2014 check WiFi',false);
  }).catch(()=>{b.disabled=false;b.innerHTML='&#x1F504; Sync Now';toast('Error',false);});
}
function syncFromBrowser(){
  const b=document.getElementById('bbtn');b.disabled=true;b.textContent='Syncing\u2026';
  const epoch=Math.floor(Date.now()/1000);
  fetch('/api/time/browser-sync',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:epoch})})
  .then(r=>r.json()).then(d=>{
    b.disabled=false;b.innerHTML='&#x1F310; Sync from Browser';
    if(d.success){
      toast('Time synced from browser successfully!');
      tick();
      setTimeout(() => {
        fetch('/api/time').then(r=>r.json()).then(d=>{
          document.getElementById('clk').textContent=d.time||'--:--:--';
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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
</header>
<main>
<p class="ptitle">Access Point Settings</p>
<div class="card fcrd">
<div class="alert aw">&#x26A0;&#xFE0F; Changes will apply immediately without restarting the device.</div>
<div class="fg"><label>AP SSID</label><input type="text" id="ssid" maxlength="31" required></div>
<div class="fg"><label>AP Password</label><input type="password" id="pw" minlength="8" placeholder="Enter wifi password"></div>
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
<button class="btn bsave" onclick="save()">&#x1F4BE; Save &amp; Apply</button>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
fetch('/api/ap').then(r=>r.json()).then(d=>{
  document.getElementById('ssid').value=d.ap_ssid||'';
  document.getElementById('ch').value=d.ap_channel||6;
  document.getElementById('hidden').value=d.ap_hidden?'1':'0';
}).catch(()=>{});
function save(){
  const pw=document.getElementById('pw').value;
  if(pw.length>0&&pw.length<8){toast('Password must be 8+ chars or blank',false);return;}
  fetch('/api/ap',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
    ap_ssid:document.getElementById('ssid').value,
    ap_password:pw,
    ap_channel:parseInt(document.getElementById('ch').value),
    ap_hidden:document.getElementById('hidden').value==='1'
  })}).then(r=>r.json()).then(d=>{
    if(d.success){toast('AP settings applied!');}else toast('Failed: '+d.error,false);
  }).catch(()=>toast('Error',false));
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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
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
<button class="btn badd" onclick="addRelay()" style="padding:9px 16px">&#x2795; Add Relay</button>
<button class="btn bsave" onclick="savePins()" style="padding:9px 16px">&#x1F4BE; Save &amp; Apply</button>
<button class="btn bwarn" id="resetPinsBtn" onclick="resetPinsToDefault()" style="padding:9px 16px;background:#F9A825;color:#212121">&#x1F504; Reset All</button>
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
let pinCfg={numRelays:8,globalActiveLow:true,relays:[]};
const allValidPins = [16, 5, 4, 14, 12, 13, 3, 1];
const pinNames={16:'D0',5:'D1',4:'D2',14:'D5',12:'D6',13:'D7',3:'RX',1:'TX'};

function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();

function pn(p){return pinNames[p]||('GPIO'+p);}

function getAvailablePins() {
    const usedPins = pinCfg.relays.map(r => r.pin);
    return allValidPins.filter(p => !usedPins.includes(p));
}

function load(){
  fetch('/api/pins').then(r=>r.json()).then(d=>{
    pinCfg=d;
    if (!pinCfg.relays) pinCfg.relays = [];
    for (let i = 0; i < pinCfg.relays.length; i++) {
        if (pinCfg.relays[i].activeLow === undefined) pinCfg.relays[i].activeLow = pinCfg.globalActiveLow;
    }
    document.getElementById('globalActiveLow').value=pinCfg.globalActiveLow?'1':'0';
    render();
  }).catch(()=>toast('Failed to load',false));
}

function applyGlobalLogicToAll() {
    const newGlobalValue = document.getElementById('globalActiveLow').value === '1';
    if (confirm('Apply "' + (newGlobalValue ? 'Active LOW' : 'Active HIGH') + '" to ALL relays? Individual relay logic will be overwritten.')) {
        for (let i = 0; i < pinCfg.relays.length; i++) {
            pinCfg.relays[i].activeLow = newGlobalValue;
        }
        pinCfg.globalActiveLow = newGlobalValue;
        render();
        toast('Applied to all relays');
    } else {
        document.getElementById('globalActiveLow').value = pinCfg.globalActiveLow ? '1' : '0';
    }
}

function getPinOpts(cur){
  const used=pinCfg.relays.map(r=>r.pin);
  return allValidPins.map(p=>{
    const usedByOther=used.includes(p) && p!==cur;
    return '<option value="'+p+'" '+(p===cur?'selected':'')+' '+(usedByOther?'disabled':'')+'>'+pn(p)+' (GPIO'+p+')'+(usedByOther?' [USED]':'')+'</option>';
  }).join('');
}

function render(){
  const tb=document.getElementById('pinBody');
  tb.innerHTML='';
  pinCfg.relays.forEach((r,i)=>{
    const tr=document.createElement('tr');
    tr.className='pin-row';
    const isCustom = (r.activeLow !== pinCfg.globalActiveLow);
    const customIndicator = isCustom ? ' <span style="font-size:10px;color:#F9A825;">✦</span>' : '';
    tr.innerHTML='<td><strong>Relay '+(i+1)+'</strong></td>'+
      '<td><input type="text" value="'+(r.name||'Relay '+(i+1))+'" maxlength="15" onchange="pinCfg.relays['+i+'].name=this.value" style="width:110px;padding:5px;border:1px solid #CFD8DC;border-radius:4px"></td>'+
      '<td><select onchange="updatePin('+i+',parseInt(this.value))" style="padding:5px;border:1px solid #CFD8DC;border-radius:4px">'+getPinOpts(r.pin)+'</select></td>'+
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
  render();
}

function addRelayOnPin(pin){
  if(pinCfg.relays.length>=16){toast('Max 16 relays',false);return;}
  const availablePins = getAvailablePins();
  if (!availablePins.includes(pin)) {
    toast('Pin ' + pn(pin) + ' is not available', false);
    return;
  }
  pinCfg.relays.push({
    name:'Relay '+(pinCfg.relays.length+1),
    pin:pin,
    activeLow:pinCfg.globalActiveLow===true
  });
  pinCfg.numRelays=pinCfg.relays.length;
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
  pinCfg.relays.splice(i,1);
  pinCfg.numRelays=pinCfg.relays.length;
  render();
}

function savePins(){
  if(!confirm('Save pins and apply changes immediately?'))return;
  pinCfg.globalActiveLow=document.getElementById('globalActiveLow').value==='1';
  pinCfg.numRelays=pinCfg.relays.length;
  
  for (let i = 0; i < pinCfg.relays.length; i++) {
      if (pinCfg.relays[i].activeLow === undefined) {
          pinCfg.relays[i].activeLow = pinCfg.globalActiveLow;
      }
  }
  
  fetch('/api/pins',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(pinCfg)})
  .then(r=>r.json()).then(d=>{
    if(d.success){toast('Pins saved & applied!');load();}else toast('Failed: '+(d.error||''),false);
  }).catch(()=>toast('Error',false));
}

function resetPinsToDefault(){
  if(!confirm('Reset ALL GPIO pins to default (D0,D1,D2,D5,D6,D7,RX,TX)?\n\nRelay names and schedules will be preserved.\nChanges will apply immediately.')) return;
  
  const btn = document.getElementById('resetPinsBtn');
  if(btn) {
    btn.disabled = true;
    btn.textContent = 'Resetting...';
  }
  
  fetch('/api/pins/reset', {method: 'POST'})
  .then(r => r.json())
  .then(d => {
    if(d.success) {
      toast('GPIO pins reset!');
      setTimeout(() => load(), 1000);
    } else {
      toast('Reset failed: ' + (d.error || 'unknown'), false);
      if(btn) {
        btn.disabled = false;
        btn.textContent = '🔄 Reset GPIO to Default';
      }
    }
  })
  .catch(() => {
    toast('Error resetting pins', false);
    if(btn) {
      btn.disabled = false;
      btn.textContent = '🔄 Reset GPIO to Default';
    }
  });
}

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
</nav>
<div class="hdr-r"><span class="dot wd"></span><span class="dot nd"></span>&nbsp;<span id="clk">--:--:--</span></div>
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
</div>

<div class="card fcrd" style="margin-bottom:14px">
<p style="font-weight:700;margin-bottom:12px">mDNS</p>
<div class="fg">
<small>Browser accessible as http://esp8266relay.local</small>
<input type="text" id="hn" maxlength="31" placeholder="esp8266relay" pattern="[a-z0-9\-]+" title="lowercase letters, digits, hyphens only">
</div>
<button class="btn bsave" onclick="saveHn()">&#x1F4BE; Save Hostname</button>
</div>

<div class="card fcrd">
<p style="font-weight:700;margin-bottom:12px">Device Control</p>
<div style="display:flex;gap:8px;flex-wrap:wrap">
<button class="btn bwarn" onclick="rst()" style="padding:9px 18px;border-radius:6px;font-size:13px;font-weight:600">&#x1F504; Reconnect WiFi</button>
<button class="btn bdanger" onclick="fct()" style="padding:9px 18px;border-radius:6px;font-size:13px;font-weight:600">&#x26A0; Factory Reset</button>
</div>
<p style="color:#90A4AE;font-size:12px;margin-top:10px">Factory reset clears all settings. Alternatively hold the FLASH button for 5 seconds.</p>
</div>
</main>
<div id="toast"></div>
<script>
function toast(m,ok=true){const t=document.getElementById('toast');t.textContent=m;t.className='show '+(ok?'ok':'er');clearTimeout(t._t);t._t=setTimeout(()=>t.className='',3000);}
function tick(){fetch('/api/time').then(r=>r.json()).then(d=>{document.getElementById('clk').textContent=d.time||'--:--:--';const w=document.querySelector('.wd'),n=document.querySelector('.nd');if(w)w.className='dot '+(d.wifi?'g':'r');if(n)n.className='dot '+(d.ntp?'g':'y');}).catch(()=>{});}
setInterval(tick,1000);tick();
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;return h+'h '+m+'m '+ss+'s';}
function rssiDesc(r){if(!r)return'\u2014';return r+'dBm ('+( r>=-50?'Excellent':r>=-60?'Good':r>=-70?'Fair':'Weak')+')';}
function loadSys(){
  fetch('/api/system').then(r=>r.json()).then(d=>{
    document.getElementById('sip').textContent=d.wifiConnected?d.ip:'(not connected)';
    document.getElementById('sap').textContent=d.ap_ip;
    document.getElementById('shp').textContent=(d.freeHeap/1024).toFixed(1)+' KB';
    document.getElementById('sup').textContent=fmtUp(d.uptime);
    document.getElementById('smd').textContent=d.mdnsHostname;
    document.getElementById('srs').textContent=d.wifiConnected?rssiDesc(d.rssi):'\u2014';
    document.getElementById('snt').textContent=d.ntpSynced?(d.ntpSyncAge>0?Math.floor(d.ntpSyncAge/60)+' min ago':'Just now'):'Never';
    document.getElementById('sns').textContent=d.ntpServer||'\u2014';
    document.getElementById('hn').value=d.hostname||'esp8266relay';
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
  fetch('/api/factory-reset',{method:'POST'}).then(()=>toast('Factory reset \u2014 reconnect to default AP')).catch(()=>{});
  setTimeout(()=>window.location.href='/',7000);
}
</script></body></html>)raw";

// =============================================================================
//  Self-Recovery Functions
// =============================================================================
void selfRecoveryWifiReconnect() {
    if (wifiConnected) {
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
    networkHealingInProgress = false;
}
void selfRecoveryWebServer() {
    unsigned long now = millis();
    if (now - lastWebServerRecovery < WEB_SERVER_RECOVERY_INTERVAL) return;
    lastWebServerRecovery = now;
    server.stop();
    server.close();
    delay(50);
    setupWebServer();
    webServerHealthy = true;
}
void selfRecoveryDNS() {
    dnsServer.stop();
    delay(50);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}
void selfRecoveryRTC() {
    internalEpoch = 0;
    rtcInitialized = false;
    lastNTPSync = 0;
    lastNTPSyncEpoch = 0;
    driftCompensation = 1.0f;
    ntpSyncCount = 0;
    ntpFailCount = 0;    
    if (wifiConnected && ESP.getFreeHeap() > MIN_HEAP_FOR_NTP) {
        tryNTPSync();
    }
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
}
void selfRecoveryNetworkHeal() {
    if (!networkHealingInProgress) {
        networkHealingInProgress = true;        
        if (!wifiConnected && strlen(sysConfig.sta_ssid) > 0) {
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
    }
}
void checkWebServerHealth() {
    unsigned long now = millis();
    if (now - lastWebServerCheck < 30000) return;
    lastWebServerCheck = now;    
    if (ESP.getFreeHeap() < 6144) {
        webServerHealthy = false;
        selfRecoveryWebServer();
    }
}
void checkDNSHealth() {
    unsigned long now = millis();
    if (now - lastDNSCheck < 60000) return;
    lastDNSCheck = now;   
    if (ESP.getFreeHeap() < 5120) {
        selfRecoveryDNS();
    }
}
void defragmentHeap() {
    unsigned long now = millis();
    if (now - lastHeapDefrag < HEAP_DEFRAG_INTERVAL) return;
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
    if (now - lastConnectionCleanup < CONNECTION_CLEANUP_INTERVAL) return;
    lastConnectionCleanup = now;    
    cleanupIdleConnections();    
    activeConnections = 0;
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
    if (factoryResetInProgress) return;    
    bool currentState = digitalRead(FACTORY_RESET_PIN);
    unsigned long now = millis();    
    if (currentState != lastButtonState) {
        lastDebounce = now;
    }    
    if ((now - lastDebounce) > 50) {
        if (currentState == LOW && !isPressing) {
            isPressing = true;
            factoryResetPressStart = now;
        }
        else if (currentState == HIGH && isPressing) {
            unsigned long holdDuration = now - factoryResetPressStart;
            if (holdDuration >= FACTORY_RESET_HOLD_MS) {
                performFactoryReset();
            }
            isPressing = false;
        }
    }    
    if (isPressing && !factoryResetInProgress) {
        unsigned long holdTime = now - factoryResetPressStart;
        if (holdTime >= FACTORY_RESET_HOLD_MS) {
            digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? LOW : HIGH);
        } else {
            int blinkInterval = 500 - (holdTime / 11);
            if (blinkInterval < 50) blinkInterval = 50;            
            if (now - lastFactoryResetBlink >= (unsigned long)blinkInterval) {
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
            if (now - frStepTimer >= 100) {
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
            if (LittleFS.exists(STATE_BACKUP)) LittleFS.remove(STATE_BACKUP);
            frState = FR_CLEAR_CONFIG;
            break;            
        case FR_CLEAR_CONFIG:
            memset(&sysConfig, 0, sizeof(SystemConfig));
            memset(&extConfig, 0, sizeof(ExtConfig));
            memset(&pinConfig, 0, sizeof(PinConfig));
            initDefaults();
            loadRTCState();
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
                if (strlen(sysConfig.ap_password) > 0)
                WiFi.softAP(sysConfig.ap_ssid, sysConfig.ap_password, ch, 0);
                else
                WiFi.softAP(sysConfig.ap_ssid, nullptr, ch, 0);               
                wifiConnected = false;
                mdnsStarted = false;
                wcsState = WCS_IDLE;
                wifiReconnectAttempts = 0;
                reconnectCountThisHour = 0;
                persistentDisconnectTime = 0;
                networkHealingInProgress = false;
                ntpFailCount = 0;
                selfRecoveryAttempts = 0;
                loopTimeoutCounter = 0;
                rtcInitialized = false;
                internalEpoch = 0;
                lastNTPSync = 0;
                lastNTPSyncEpoch = 0;               
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
    if (!wifiConnected) return;
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_NTP) return;
    if (ntpState != NTP_IDLE) return;    
    ntpState = NTP_REQUESTING;
    ntpTimer = millis();
    lastNTPAttempt = millis();
    timeClient.setPoolServerName(NTP_SERVERS[ntpServerIndex]);
}
void processNTPState() {
    switch (ntpState) {
        case NTP_IDLE:
            break;            
        case NTP_REQUESTING:
            if (millis() - ntpTimer > 1000) {
                bool ok = timeClient.forceUpdate();
                ntpState = NTP_PROCESSING;
                if (ok) {
                    syncInternalRTC();
                    ntpFailCount = 0;
                } else {
                    ntpFailCount++;
                    if (ntpFailCount > 3) {
                        ntpServerIndex = (ntpServerIndex + 1) % NUM_NTP_SERVERS;
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
    if (now - lastGentleHealing < GENTLE_NETWORK_HEAL_INTERVAL) return;
    lastGentleHealing = now;    
    if (!wifiConnected && !networkHealingInProgress && strlen(sysConfig.sta_ssid) > 0) {
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
            if (now - whTimer >= 100) {
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
            } else if (now - whTimer >= 30000) {
                networkHealingInProgress = false;
                whState = WH_IDLE;
            }
            break;
    }
}
void preserveRelayState() {
    RelayStateBackup backup;
    backup.magic = STATE_MAGIC;    
    for (int i = 0; i < numRelays; i++) {
        backup.states[i] = relayConfigs[i].manualOverride ? 
            relayConfigs[i].manualState : 
            (digitalRead(relayConfigs[i].pin) == (relayConfigs[i].activeLow ? LOW : HIGH));
        backup.manual[i] = relayConfigs[i].manualOverride;
    }    
    saveToFileAtomic(STATE_BACKUP, &backup, sizeof(RelayStateBackup));
}
void restoreRelayState() {
    RelayStateBackup backup;    
    if (loadFromFile(STATE_BACKUP, &backup, sizeof(RelayStateBackup)) && 
        backup.magic == STATE_MAGIC) {
        for (int i = 0; i < numRelays; i++) {
            relayConfigs[i].manualOverride = backup.manual[i];
            relayConfigs[i].manualState = backup.states[i];
        }
        LittleFS.remove(STATE_BACKUP);
    }
}
void checkConnectionRateLimit() {
    unsigned long now = millis();
    if (now - reconnectHourStart >= 3600000UL) {
        reconnectCountThisHour = 0;
        reconnectHourStart = now;
    }
    if (reconnectCountThisHour >= MAX_RECONNECT_ATTEMPTS) {
        return; 
    }
    reconnectCountThisHour++;
}
bool loadFromFile(const char* path, void* data, size_t size) {
    if (!LittleFS.exists(path)) return false;    
    File f = LittleFS.open(path, "r");
    if (!f) return false;   
    size_t fileSize = f.size();
    if (fileSize != size && abs((int)(fileSize - size)) > 32) {
        f.close();
        return false;
    }    
    size_t bytesRead = f.read((uint8_t*)data, min(size, fileSize));
    f.close();    
    return bytesRead > 0;
}
bool saveToFileAtomic(const char* path, const void* data, size_t size) {
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_SAVE) {
        savePendingOnLowMem = true;
        return false;
    }    
    String tmpPath = String(path) + ".tmp";    
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
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
    }    
    bool result = LittleFS.rename(tmpPath, path);
    if (!result) {
        LittleFS.remove(tmpPath);
    }    
    savePendingOnLowMem = false;
    return result;
}
void markConfigDirty() {
    unsigned long now = millis();    
    if (now - lastMarkTime < 5000) return;
    lastMarkTime = now;    
    configDirty = true;
    lastConfigSave = now;
}
void flushConfigIfNeeded() {
    if (!configDirty) return;    
    unsigned long now = millis();
    if (now - lastConfigSave >= CONFIG_SAVE_INTERVAL) {
        FSInfo fs_info;
        if (LittleFS.info(fs_info)) {
            if (fs_info.usedBytes < fs_info.totalBytes * 0.9 && 
                ESP.getFreeHeap() > MIN_HEAP_FOR_SAVE) {               
                bool rtcSaved = saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
                bool relaySaved = saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);                
                if (rtcSaved && relaySaved) {
                    configDirty = false;
                }
            }
        }
    }
}
time_t getCurrentEpoch() {
    if (!rtcInitialized || internalEpoch == 0) return 0;    
    unsigned long elapsed = elapsedSince(internalMillisAtLastSync);  
    double elapsedSeconds = (double)elapsed * (double)driftCompensation / 1000.0;
    return internalEpoch + (time_t)elapsedSeconds;
}
void syncInternalRTC() {
    time_t ntpEpoch = timeClient.getEpochTime();
    if (ntpEpoch < 1000000000UL) return;
    unsigned long nowMs = millis();
    unsigned long elapsedMs = elapsedSince(internalMillisAtLastSync);
    if (rtcInitialized && internalEpoch > 0) {
        if (elapsedMs > 60000UL && elapsedMs < MAX_DRIFT_CALIBRATION_INTERVAL) {
            double nominalSecs = (double)elapsedMs / 1000.0;
            double actualSecs = (double)((long)ntpEpoch - (long)internalEpoch);
            double measuredRate = actualSecs / nominalSecs;            
            if (measuredRate > 0.5 && measuredRate < 2.0) {
                if (ntpSyncCount < 10) {
                    driftCompensation = driftCompensation * 0.5f + measuredRate * 0.5f;
                } else {
                    driftCompensation = driftCompensation * 0.95f + measuredRate * 0.05f;
                }
                ntpSyncCount = min(ntpSyncCount + 1, 1000);                
                if (driftCompensation < 0.9f) driftCompensation = 0.9f;
                if (driftCompensation > 1.1f) driftCompensation = 1.1f;
            }
        }
    }
    internalEpoch = ntpEpoch;
    internalMillisAtLastSync = nowMs;
    rtcInitialized = true;
    lastNTPSync = nowMs;
    lastNTPSyncEpoch = ntpEpoch; 
    ntpFailCount = 0;
    saveRTCState();
}
void syncInternalRTCFromBrowser(time_t browserEpoch) {
    unsigned long nowMs = millis();
    unsigned long elapsedMs = elapsedSince(internalMillisAtLastSync);
    if (rtcInitialized && internalEpoch > 0) {
        if (elapsedMs > 60000UL && elapsedMs < MAX_DRIFT_CALIBRATION_INTERVAL) {
            double nominalSecs = (double)elapsedMs / 1000.0;
            double actualSecs = (double)((long)browserEpoch - (long)internalEpoch);
            double measuredRate = actualSecs / nominalSecs;            
            if (measuredRate > 0.5 && measuredRate < 2.0) {
                if (ntpSyncCount < 10) {
                    driftCompensation = driftCompensation * 0.5f + measuredRate * 0.5f;
                } else {
                    driftCompensation = driftCompensation * 0.95f + measuredRate * 0.05f;
                }
                ntpSyncCount = min(ntpSyncCount + 1, 1000);                
                if (driftCompensation < 0.9f) driftCompensation = 0.9f;
                if (driftCompensation > 1.1f) driftCompensation = 1.1f;
            }
        }
    }
    internalEpoch = browserEpoch;
    internalMillisAtLastSync = nowMs;
    rtcInitialized = true;
    lastNTPSync = 0; 
    lastNTPSyncEpoch = 0;     
    saveRTCState();
}
void saveRTCState() {
    sysConfig.last_rtc_epoch = internalEpoch;
    sysConfig.rtc_drift = driftCompensation;
    markConfigDirty();
}
void loadRTCState() {
    if (sysConfig.last_rtc_epoch > 1000000000UL) {
        internalEpoch = sysConfig.last_rtc_epoch;
        driftCompensation = sysConfig.rtc_drift;
        if (driftCompensation < 0.9f || driftCompensation > 1.1f) driftCompensation = 1.0f;
        internalMillisAtLastSync = millis();
        rtcInitialized = true;
    }
}
void startMDNS() {
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
    if (strlen(sysConfig.sta_ssid) == 0) return;
    if (millis() < wifiGiveUpUntil) return;    
    checkConnectionRateLimit();
    if (reconnectCountThisHour >= MAX_RECONNECT_ATTEMPTS) {
        if (persistentDisconnectTime == 0) persistentDisconnectTime = millis();        
        if (millis() - persistentDisconnectTime > 7200000UL) {
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
            if (now - aprStartTime >= AP_RESTART_DELAY) {
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
    unsigned long now = millis();
    unsigned long interval;    
    if (selfRecoveryState != SR_IDLE) {
        interval = 100UL;
    } else if (!wifiConnected) {
        interval = LED_BLINK_FAST;
    } else if (!rtcInitialized || (lastNTPSync == 0 && lastNTPSyncEpoch == 0)) {
        interval = LED_BLINK_SLOW;
    } else {
        digitalWrite(STATUS_LED_PIN, STATUS_LED_ACTIVE_LOW ? HIGH : LOW);
        return;
    }    
    if (now - lastLedToggle >= interval) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, 
            (STATUS_LED_ACTIVE_LOW ? !ledState : ledState) ? HIGH : LOW);
    }
}
void performHeapCleanup() {
    ESP.resetFreeContStack();    
    if (wifiConnected && ESP.getFreeHeap() < 10240) {
        WiFi.forceSleepBegin();
        delay(1);
        WiFi.forceSleepWake();
        delay(1);
    }
}
void performSelfCheck() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        initDefaults();
    }    
    for (int i = 0; i < numRelays; i++) {
        pinMode(relayConfigs[i].pin, OUTPUT);
    }    
    if (sysConfig.magic != EEPROM_MAGIC) {
        loadConfiguration();
    }    
    String cleanup = "";
    cleanup.reserve(0);
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
    if (now - lastProactiveMaintenance < PROACTIVE_MAINTENANCE_INTERVAL) return;
    lastProactiveMaintenance = now;        
    if (ESP.getFreeHeap() < 10000) {
        selfRecoveryMemoryCleanup(); 
    }    
    if (wifiConnected && WiFi.RSSI() < -85) {
        static unsigned long lastWeakSignalRecovery = 0;
        if (now - lastWeakSignalRecovery > 300000) {
            WiFi.reconnect();
            lastWeakSignalRecovery = now;
        }
    }    
    if (rtcInitialized && wifiConnected) {
        unsigned long timeSinceSync = (lastNTPSync > 0) ? elapsedSince(lastNTPSync) : 0;
        if (timeSinceSync > 3600000UL && (now - lastNTPAttempt) > NTP_RETRY_INTERVAL) {
            tryNTPSync();
        }
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
}

// =============================================================================
//  CONFIGURATION FUNCTIONS
// =============================================================================
void initDefaults() {
    memset(&sysConfig, 0, sizeof(SystemConfig));
    sysConfig.magic = EEPROM_MAGIC;
    sysConfig.version = EEPROM_VERSION;
    strcpy(sysConfig.ap_ssid, "ESP8266_8CH_Timer_Switch");
    strcpy(sysConfig.ap_password, "ESP8266-admin");
    strcpy(sysConfig.ntp_server, "time.google.com");
    sysConfig.gmt_offset = 28800;
    sysConfig.daylight_offset = 0;
    sysConfig.last_rtc_epoch = 0;
    sysConfig.rtc_drift = 1.0f;
    strcpy(sysConfig.hostname, "esp8266relay");
    memset(&extConfig, 0, sizeof(ExtConfig));
    extConfig.magic = EXT_CFG_MAGIC;
    extConfig.ap_channel = 6;
    extConfig.ntp_sync_hours = 1;
    extConfig.ap_hidden = 0;    
    pinConfig.magic = PINS_MAGIC;
    pinConfig.numRelays = 8;
    pinConfig.globalActiveLow = true;
    numRelays = 8;
    relayActiveLow = true;    
    uint8_t defaultPins[] = {16, 5, 4, 14, 12, 13, 3, 1};
    for (int i = 0; i < MAX_RELAYS; i++) {
        memset(&relayConfigs[i], 0, sizeof(RelayConfig));
        if (i < 8) {
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
            sprintf(relayConfigs[i].name, "Relay %d", i + 1);
            for (int s = 0; s < 8; s++) {
                relayConfigs[i].schedule.days[s] = 0x7F;
                relayConfigs[i].schedule.monthDays[s] = 0;
                relayConfigs[i].schedule.enabled[s] = false;
            }
        }
    }    
    saveConfiguration();
    saveExtConfig();
    savePinConfig();
}
void loadConfiguration() {
    if (!loadFromFile(CONFIG_FILE, &sysConfig, sizeof(SystemConfig))) {
        initDefaults();
        return;
    }    
    if (sysConfig.magic != EEPROM_MAGIC) {
        initDefaults();
        return;
    }    
    sysConfig.version = EEPROM_VERSION;    
    if (!loadFromFile(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS)) {
        uint8_t defaultPins[] = {16, 5, 4, 14, 12, 13, 3, 1};
        for (int i = 0; i < numRelays && i < 8; i++) {
            memset(&relayConfigs[i], 0, sizeof(RelayConfig));
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
            sprintf(relayConfigs[i].name, "Relay %d", i + 1);
            for (int s = 0; s < 8; s++) {
                relayConfigs[i].schedule.days[s] = 0x7F;
                relayConfigs[i].schedule.monthDays[s] = 0;
                relayConfigs[i].schedule.enabled[s] = false;
            }
        }
    }   
    for (int i = 0; i < numRelays; i++) {
        if (strlen(relayConfigs[i].name) == 0) {
            sprintf(relayConfigs[i].name, "Relay %d", i + 1);
        }
    }
    strcpy(ap_ssid, sysConfig.ap_ssid);
    strcpy(ap_password, sysConfig.ap_password);
}
void saveConfiguration() {
    saveToFileAtomic(CONFIG_FILE, &sysConfig, sizeof(SystemConfig));
    saveToFileAtomic(RELAY_FILE, relayConfigs, sizeof(RelayConfig) * MAX_RELAYS);
}
void loadExtConfig() {
    if (!loadFromFile(EXTCONFIG_FILE, &extConfig, sizeof(ExtConfig))) {
        memset(&extConfig, 0, sizeof(ExtConfig));
        extConfig.magic = EXT_CFG_MAGIC;
        extConfig.ap_channel = 6;
        extConfig.ntp_sync_hours = 1;
        extConfig.ap_hidden = 0;
        saveExtConfig();
        return;
    }    
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
    if (!loadFromFile(PINS_FILE, &pinConfig, sizeof(PinConfig))) {
        pinConfig.magic = PINS_MAGIC;
        pinConfig.numRelays = 8;
        pinConfig.globalActiveLow = true;
        numRelays = 8;
        relayActiveLow = true;
        savePinConfig();        
        uint8_t defaultPins[] = {16, 5, 4, 14, 12, 13, 3, 1};
        for (int i = 0; i < numRelays && i < 8; i++) {
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
        }
        saveConfiguration();
        return;
    }    
    if (pinConfig.magic != PINS_MAGIC) {
        pinConfig.magic = PINS_MAGIC;
        pinConfig.numRelays = 8;
        pinConfig.globalActiveLow = true;
        numRelays = 8;
        relayActiveLow = true;
        savePinConfig();        
        uint8_t defaultPins[] = {16, 5, 4, 14, 12, 13, 3, 1};
        for (int i = 0; i < 8; i++) {
            relayConfigs[i].pin = defaultPins[i];
            relayConfigs[i].activeLow = true;
        }
        saveConfiguration();
        return;
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
        if (!LittleFS.begin()) {
        }
    }
    loadConfiguration();
    loadExtConfig();
    loadPinConfig();
    loadRTCState();
    restoreRelayState();     
    updateRelayPins();
    if (strlen(sysConfig.sta_ssid) > 0) {
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
    lastLoopHeartbeat = millis();
    lastSuccessfulLoop = millis();
    lastProactiveMaintenance = millis();
    lastGentleHealing = millis();
    lastStableTime = millis();
    lastDNSRestart = millis();
    lastHeapDefrag = millis();
    lastConnectionCleanup = millis();
    lastWebServerRecovery = millis();
    lastWebServerCheck = millis();
    lastDNSCheck = millis();
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
    ESP.wdtFeed();
    yield();    
    unsigned long now = millis();    
    if (now - lastWatchdogFeed >= WATCHDOG_FEED_INTERVAL) {
        ESP.wdtFeed();
        lastWatchdogFeed = now;
    }    
    lastLoopHeartbeat = now;    
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
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
                break;               
            case SR_WIFI_RECONNECT:
                selfRecoveryWifiReconnect();
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
                break;                
            case SR_WEB_SERVER_RECOVER:
                selfRecoveryWebServer();
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
                break;                
            case SR_DNS_RECOVER:
                selfRecoveryDNS();
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
                break;                
            case SR_RTC_RECOVER:
                selfRecoveryRTC();
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
                break;                
            case SR_NETWORK_HEAL:
                selfRecoveryNetworkHeal();
                selfRecoveryState = SR_IDLE;
                selfRecoveryAttempts = 0;
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
    updateStatusLED();    
    if (wifiConnected && WiFi.status() != WL_CONNECTED) {
        if (wifiReconnectAttempts < 3) {
            WiFi.reconnect();
            wifiReconnectAttempts++;
        } else if (selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_WIFI_RECONNECT;
        }
    }    
    if (ESP.getFreeHeap() < 8192 && selfRecoveryState == SR_IDLE) {
        selfRecoveryState = SR_MEMORY_CLEANUP;
    }    
    if (rtcInitialized) {
        time_t currentEpoch = getCurrentEpoch();
        if ((currentEpoch < 1000000000UL || currentEpoch > 5000000000UL) && selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_RTC_RECOVER;
        }
    }    
    proactiveMaintenance();
    gentleNetworkHealing();    
    if (lastSuccessfulLoop == 0) lastSuccessfulLoop = now;
    if (elapsedSince(lastSuccessfulLoop) < 10000) {
        lastSuccessfulLoop = now;
    }
    if (elapsedSince(lastSuccessfulLoop) > LOOP_WATCHDOG_TIMEOUT) {
        loopTimeoutCounter++;        
        if (loopTimeoutCounter == 1 && selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_MEMORY_CLEANUP;
        } else if (loopTimeoutCounter == 2 && selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_WEB_SERVER_RECOVER;
        } else if (selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_WIFI_RECONNECT;
            loopTimeoutCounter = 0;
        }        
        lastSuccessfulLoop = now;
        return;
    }    
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
        } else if (now - wcsStart > WIFI_CONNECT_TIMEOUT) {
            wcsState = WCS_IDLE;
            if (wifiReconnectAttempts >= MAX_RECONNECT) {
                wifiGiveUpUntil = now + 300000UL;
                wifiReconnectAttempts = 0;
            }
        }
    }
    if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
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
    }
    if (wifiConnected && ESP.getFreeHeap() > MIN_HEAP_FOR_NTP && ntpState == NTP_IDLE) {
        bool doSync = false;
        time_t currentEpoch = getCurrentEpoch();        
        if (lastNTPSyncEpoch == 0 && lastNTPSync == 0) {
            doSync = true;
        } else if (lastNTPSyncEpoch > 0 && currentEpoch > 0) {
            if (currentEpoch - lastNTPSyncEpoch >= (time_t)(extConfig.ntp_sync_hours * 3600)) {
                doSync = true;
            }
        } else if (ntpFailCount > 0 && elapsedSince(lastNTPAttempt) >= NTP_RETRY_INTERVAL) {
            doSync = true;
        }        
        if (!doSync && lastNTPSync > 0 && elapsedSince(lastNTPSync) > 86400000UL) {
            doSync = true;
            lastNTPSync = 0; 
        }        
        if (doSync) {
            tryNTPSync();
        }
    }
    if (wifiConnected && rtcInitialized && elapsedSince(lastTimeVerification) >= 3600000UL) {
        lastTimeVerification = now;
        time_t currentEpoch = getCurrentEpoch();
        if ((currentEpoch < 1000000000UL || currentEpoch > 5000000000UL) && selfRecoveryState == SR_IDLE) {
            selfRecoveryState = SR_RTC_RECOVER;
        }
    }    
    if (rtcInitialized && elapsedSince(lastInternalTimeCheck) >= TIME_CHECK_INTERVAL) {
        lastInternalTimeCheck = now;
        time_t currentEpoch = getCurrentEpoch();        
        if ((currentEpoch < 1000000000UL || currentEpoch > 5000000000UL) && selfRecoveryState == SR_IDLE) {
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
    processRelaySchedules();
    flushConfigIfNeeded();
}

// =============================================================================
//  SCHEDULE PROCESSING 
// =============================================================================
void processRelaySchedules() {
    time_t epoch = getCurrentEpoch();
    if (epoch < 1000000000UL) {
        for (int i = 0; i < numRelays; i++) {
            if (!relayConfigs[i].manualOverride) {
                digitalWrite(relayConfigs[i].pin, 
                    relayConfigs[i].activeLow ? HIGH : LOW);
            }
        }
        return;
    }
    time_t localEpoch = epoch + sysConfig.gmt_offset + sysConfig.daylight_offset;    
    struct tm* ti = gmtime(&localEpoch);
    if (!ti) return;    
    int curTotalSeconds = ti->tm_hour * 3600 + ti->tm_min * 60 + ti->tm_sec;
    int weekday = ti->tm_wday;
    int monthDay = ti->tm_mday;
    for (int i = 0; i < numRelays; i++) {
        if (relayConfigs[i].manualOverride) {
            digitalWrite(relayConfigs[i].pin,
                relayConfigs[i].activeLow ? !relayConfigs[i].manualState
                                         :  relayConfigs[i].manualState);
            continue;
        }
        bool on = false;
        for (int s = 0; s < 8; s++) {
            if (!relayConfigs[i].schedule.enabled[s]) continue;            
            uint8_t dayMask = relayConfigs[i].schedule.days[s];
            if (dayMask == 0) continue;
            if (!(dayMask & (1 << weekday))) continue;            
            uint32_t monthDayMask = relayConfigs[i].schedule.monthDays[s];
            if (monthDayMask != 0) {
                if (!(monthDayMask & (1 << (monthDay - 1)))) continue;
            }
            int startSeconds = relayConfigs[i].schedule.startHour[s]   * 3600
                             + relayConfigs[i].schedule.startMinute[s]  *   60
                             + relayConfigs[i].schedule.startSecond[s];
            int stopSeconds  = relayConfigs[i].schedule.stopHour[s]    * 3600
                             + relayConfigs[i].schedule.stopMinute[s]   *   60
                             + relayConfigs[i].schedule.stopSecond[s];
            if (startSeconds == stopSeconds) {
                on = true;
            } else if (startSeconds < stopSeconds) {
                if (curTotalSeconds >= startSeconds && curTotalSeconds < stopSeconds) {
                    on = true;
                }
            } else {
                if (curTotalSeconds >= startSeconds || curTotalSeconds < stopSeconds) {
                    on = true;
                }
            }            
            if (on) break;
        }
        digitalWrite(relayConfigs[i].pin, 
            relayConfigs[i].activeLow ? !on : on);
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
    server.sendHeader("ETag", "\"v1.0\"");
    server.send_P(200, "text/css", style_css); 
}
void handleCaptivePortal() { server.send_P(200, "text/html", PSTR("<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>")); }
void handleSuccess() { server.send_P(200, "text/plain", PSTR("success\n")); }
void handleConnectTest() { server.send_P(200, "text/plain", PSTR("Microsoft Connect Test")); }
void handleNcsi() { server.send_P(200, "text/plain", PSTR("Microsoft NCSI")); }
void handleRedirect() { server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true); server.send(302, "text/plain", ""); }
void handleNotFound() { server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true); server.send(302, "text/plain", ""); }
void setupWebServer() {
    server.on("/",       HTTP_GET, handleRoot);
    server.on("/wifi",   HTTP_GET, handleWiFiPage);
    server.on("/ntp",    HTTP_GET, handleNtpPage);
    server.on("/ap",     HTTP_GET, handleApPage);
    server.on("/pins",   HTTP_GET, handlePinsPage);
    server.on("/system", HTTP_GET, handleSystemPage);
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
    server.on("/api/ntp",      HTTP_GET,  handleGetNTP);
    server.on("/api/ntp",      HTTP_POST, handleSaveNTP);
    server.on("/api/ntp/sync", HTTP_POST, handleSyncNTP);
    server.on("/api/ap", HTTP_GET,  handleGetAP);
    server.on("/api/ap", HTTP_POST, handleSaveAP);
    server.on("/api/pins", HTTP_GET,  handleGetPins);
    server.on("/api/pins", HTTP_POST, handleSavePins);
    server.on("/api/pins/reset", HTTP_POST, handleResetPins);  
    server.on("/api/system",        HTTP_GET,  handleGetSystem);
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
    server.begin();
}

// =============================================================================
//  JSON PARSING
// =============================================================================
int extractJsonInt(const String& json, const char* key) {
    if (json.length() > 2048) return 0;    
    String search = "\"" + String(key) + "\":";
    int idx = json.indexOf(search);
    if (idx == -1) return 0;
    idx += search.length();
    while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\t')) idx++;    
    if (idx >= (int)json.length()) return 0;    
    bool negative = false;
    if (json[idx] == '-') { 
        negative = true; 
        idx++;
        if (idx >= (int)json.length()) return 0;
    }   
    int val = 0;
    int digits = 0;
    while (idx < (int)json.length() && json[idx] >= '0' && json[idx] <= '9' && digits < 10) {
        val = val * 10 + (json[idx] - '0');
        idx++;
        digits++;
    }
    return negative ? -val : val;
}
bool extractJsonBool(const String& json, const char* key) {
    String search = "\"" + String(key) + "\":";
    int idx = json.indexOf(search);
    if (idx == -1) return false;
    idx += search.length();
    while (idx < (int)json.length() && (json[idx] == ' ' || json[idx] == '\t')) idx++;
    if (idx >= (int)json.length()) return false;
    return (json[idx] == 't' || json[idx] == 'T');
}
uint8_t extractJsonByte(const String& json, const char* key) {
    return (uint8_t)extractJsonInt(json, key);
}
uint32_t extractJsonUInt32(const String& json, const char* key) {
    return (uint32_t)extractJsonInt(json, key);
}

// =============================================================================
//  API HANDLERS
// =============================================================================
void handleGetRelays() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    if (shouldSimplifyResponse()) {
        String resp;
        resp.reserve(512);
        resp = "[";
        for (int i = 0; i < numRelays; i++) {
            if (i > 0) resp += ",";
            bool relayState = digitalRead(relayConfigs[i].pin);
            if (relayConfigs[i].activeLow) relayState = !relayState;
            resp += "{\"n\":\"" + String(relayConfigs[i].name) + "\",";
            resp += "\"s\":" + String(relayState ? "true" : "false") + ",";
            resp += "\"m\":" + String(relayConfigs[i].manualOverride ? "true" : "false") + "}";
        }
        resp += "]";
        server.send(200, "application/json", resp);
        return;
    }    
    String resp;
    resp.reserve(2048);
    resp = "[";
    for (int i = 0; i < numRelays; i++) {
        if (i > 0) resp += ",";
        bool relayState = digitalRead(relayConfigs[i].pin);
        if (relayConfigs[i].activeLow) relayState = !relayState;
        resp += "{\"name\":\"" + String(relayConfigs[i].name) + "\",";
        resp += "\"state\":" + String(relayState ? "true" : "false") + ",";
        resp += "\"manual\":" + String(relayConfigs[i].manualOverride ? "true" : "false") + ",";
        resp += "\"schedules\":[";
        for (int s = 0; s < 8; s++) {
            if (s > 0) resp += ",";
            resp += "{";
            resp += "\"startHour\":" + String(relayConfigs[i].schedule.startHour[s]) + ",";
            resp += "\"startMinute\":" + String(relayConfigs[i].schedule.startMinute[s]) + ",";
            resp += "\"startSecond\":" + String(relayConfigs[i].schedule.startSecond[s]) + ",";
            resp += "\"stopHour\":" + String(relayConfigs[i].schedule.stopHour[s]) + ",";
            resp += "\"stopMinute\":" + String(relayConfigs[i].schedule.stopMinute[s]) + ",";
            resp += "\"stopSecond\":" + String(relayConfigs[i].schedule.stopSecond[s]) + ",";
            resp += "\"enabled\":" + String(relayConfigs[i].schedule.enabled[s] ? "true" : "false") + ",";
            resp += "\"days\":" + String(relayConfigs[i].schedule.days[s]) + ",";
            resp += "\"monthDays\":" + String(relayConfigs[i].schedule.monthDays[s]);
            resp += "}";
        }
        resp += "]}";
    }
    resp += "]";
    server.send(200, "application/json", resp);
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
        strncpy(relayConfigs[relay].name, name, 15);
        relayConfigs[relay].name[15] = '\0';
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
    String body = server.arg("plain");
    int relayIdx = body.indexOf("\"relay\":");
    if (relayIdx == -1) { server.send(400, "application/json", "{\"success\":false}"); return; }
    relayIdx += 8;
    int relay = body.substring(relayIdx, body.indexOf(',', relayIdx)).toInt();
    if (relay < 0 || relay >= numRelays) { server.send(400, "application/json", "{\"success\":false}"); return; }    
    int schedStart = body.indexOf("\"schedules\":[");
    if (schedStart == -1) { server.send(400, "application/json", "{\"success\":false}"); return; }    
    int pos = schedStart + 12;
    for (int s = 0; s < 8; s++) {
        pos = body.indexOf('{', pos);
        if (pos == -1) break;
        int endPos = body.indexOf('}', pos);
        if (endPos == -1) break;
        String schedStr = body.substring(pos + 1, endPos);
        relayConfigs[relay].schedule.startHour[s] = extractJsonInt(schedStr, "startHour");
        relayConfigs[relay].schedule.startMinute[s] = extractJsonInt(schedStr, "startMinute");
        relayConfigs[relay].schedule.startSecond[s] = extractJsonInt(schedStr, "startSecond");
        relayConfigs[relay].schedule.stopHour[s] = extractJsonInt(schedStr, "stopHour");
        relayConfigs[relay].schedule.stopMinute[s] = extractJsonInt(schedStr, "stopMinute");
        relayConfigs[relay].schedule.stopSecond[s] = extractJsonInt(schedStr, "stopSecond");
        relayConfigs[relay].schedule.enabled[s] = extractJsonBool(schedStr, "enabled");
        relayConfigs[relay].schedule.days[s] = extractJsonByte(schedStr, "days");
        relayConfigs[relay].schedule.monthDays[s] = extractJsonUInt32(schedStr, "monthDays");
        pos = endPos + 1;
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
    time_t ep = getCurrentEpoch();    
    if (ep > 1000000000UL) {
        time_t localEp = ep + sysConfig.gmt_offset + sysConfig.daylight_offset;
        struct tm* t = gmtime(&localEp);
        if (t) {
            char buf[10];
            sprintf(buf, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            ts = buf;
        }
    }    
    bool ntpSynced = (lastNTPSync > 0 || lastNTPSyncEpoch > 0);
    server.send(200, "application/json", "{\"time\":\"" + ts + "\",\"wifi\":" + String(wifiConnected ? "true" : "false") + ",\"ntp\":" + String(ntpSynced ? "true" : "false") + "}");
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
    long browserEpoch = doc["epoch"];
    if (browserEpoch < 1000000000UL || browserEpoch > 5000000000UL) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid timestamp\"}"); 
        return;
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
    resp = "{\"ssid\":\"" + String(sysConfig.sta_ssid) + "\",";
    resp += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
    resp += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    resp += "\"rssi\":" + String(wifiConnected ? (int)WiFi.RSSI() : 0);    
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
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 31) {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid SSID\"}");
        return;
    }
    if (ssid && strlen(ssid) > 0 && strlen(ssid) < 32) {
        strncpy(sysConfig.sta_ssid, ssid, 31); 
        sysConfig.sta_ssid[31] = '\0';
        if (pw && strlen(pw) > 0) { strncpy(sysConfig.sta_password, pw, 63); sysConfig.sta_password[63] = '\0'; }
        else { sysConfig.sta_password[0] = '\0'; }
        saveConfiguration();
        server.send(200, "application/json", "{\"success\":true}");
        wifiConnected = false;
        wifiReconnectAttempts = 0;
        wifiGiveUpUntil = 0;
        reconnectCountThisHour = 0;
        persistentDisconnectTime = 0;
        wcsState = WCS_IDLE;
        beginWiFiConnect();
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
        scanInProgress = true;
        scanResultCount = -1;
        WiFi.scanNetworksAsync([](int n) { scanResultCount = n; scanInProgress = false; }, false);
    }
    server.send(202, "application/json", "{\"scanning\":true}");
}
void handleWiFiScanResults() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
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
    WiFi.scanDelete();
    scanResultCount = -1;
    String resp; 
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
}
void handleGetNTP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }
    server.send(200, "application/json", "{\"ntpServer\":\"" + String(sysConfig.ntp_server) + "\",\"gmtOffset\":" + String(sysConfig.gmt_offset) + ",\"daylightOffset\":" + String(sysConfig.daylight_offset) + ",\"syncHours\":" + String(extConfig.ntp_sync_hours) + "}");
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
    if (srv && strlen(srv) > 0 && strlen(srv) < 48) {
        strncpy(sysConfig.ntp_server, srv, 47); 
        sysConfig.ntp_server[47] = '\0';
        sysConfig.gmt_offset = doc["gmtOffset"] | sysConfig.gmt_offset;
        sysConfig.daylight_offset = doc["daylightOffset"] | sysConfig.daylight_offset;
        saveConfiguration();
        if (wifiConnected) {
            timeClient.setPoolServerName(sysConfig.ntp_server);
            timeClient.setTimeOffset(0); 
        }
        if (doc.containsKey("syncHours")) {
            uint8_t h = doc["syncHours"];
            if (h >= 1 && h <= 24) { extConfig.ntp_sync_hours = h; saveExtConfig(); }
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
    if (!wifiConnected) { server.send(400, "application/json", "{\"success\":false}"); return; }
    tryNTPSync();
    server.send(200, "application/json", "{\"success\":true}");
}
void handleGetAP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }
    server.send(200, "application/json", "{\"ap_ssid\":\"" + String(sysConfig.ap_ssid) + "\",\"ap_password\":\"" + String(sysConfig.ap_password) + "\",\"ap_channel\":" + String(extConfig.ap_channel) + ",\"ap_hidden\":" + String(extConfig.ap_hidden ? "true" : "false") + "}");
}
void handleSaveAP() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"success\":false}"); return; }
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"success\":false}"); return; }
    const char* ssid = doc["ap_ssid"];
    const char* pw = doc["ap_password"];
    if (ssid && strlen(ssid) > 0 && strlen(ssid) < 32) {
        strncpy(sysConfig.ap_ssid, ssid, 31); 
        sysConfig.ap_ssid[31] = '\0';
        strcpy(ap_ssid, sysConfig.ap_ssid);
        if (pw && strlen(pw) > 0) { strncpy(sysConfig.ap_password, pw, 31); sysConfig.ap_password[31] = '\0'; strcpy(ap_password, sysConfig.ap_password); }
        else { sysConfig.ap_password[0] = '\0'; ap_password[0] = '\0'; }
        if (doc.containsKey("ap_channel")) { uint8_t ch = doc["ap_channel"]; if (ch >= 1 && ch <= 13) extConfig.ap_channel = ch; }
        if (doc.containsKey("ap_hidden")) { extConfig.ap_hidden = doc["ap_hidden"] ? 1 : 0; }
        saveConfiguration();
        saveExtConfig();
        server.send(200, "application/json", "{\"success\":true}");
        restartAP();
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleGetSystem() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
        return;
    }    
    if (shouldSimplifyResponse()) {
        char buf[128];
        snprintf(buf, sizeof(buf), 
            "{\"freeHeap\":%d,\"uptime\":%lu,\"wifiConnected\":%s}",
            ESP.getFreeHeap(), millis()/1000,
            wifiConnected ? "true" : "false");
        server.send(200, "application/json", buf);
        return;
    }    
    String resp = "{";
    resp += "\"hostname\":\"" + String(sysConfig.hostname) + "\",";
    resp += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    resp += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    resp += "\"uptime\":" + String(millis() / 1000UL) + ",";
    resp += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    bool ntpSynced = (lastNTPSync > 0 || lastNTPSyncEpoch > 0);
    resp += "\"ntpSynced\":" + String(ntpSynced ? "true" : "false") + ",";
    resp += "\"ntpServer\":\"" + String(sysConfig.ntp_server) + "\",";
    unsigned long syncAge = lastNTPSync > 0 ? (millis() - lastNTPSync) / 1000UL : (lastNTPSyncEpoch > 0 ? (millis() / 1000UL) : 0);
    resp += "\"ntpSyncAge\":" + String(ntpSynced ? (int)syncAge : -1) + ",";
    resp += "\"wifiConnected\":" + String(wifiConnected ? "true" : "false") + ",";
    resp += "\"wifiSSID\":\"" + String(sysConfig.sta_ssid) + "\",";
    resp += "\"rssi\":" + String(wifiConnected ? (int)WiFi.RSSI() : 0) + ",";
    resp += "\"mdnsHostname\":\"" + String(sysConfig.hostname) + ".local\",";
    resp += "\"version\":" + String(EEPROM_VERSION);
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
        strncpy(sysConfig.hostname, h, 31); 
        sysConfig.hostname[31] = '\0';
        saveConfiguration();
        server.send(200, "application/json", "{\"success\":true}");
        if (wifiConnected) startMDNS();
    } else {
        server.send(400, "application/json", "{\"success\":false}");
    }
}
void handleGetPins() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"error\":\"Too many requests\"}");
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
    pinConfig.numRelays = newNumRelays;
    pinConfig.globalActiveLow = newGlobalActiveLow;
    numRelays = newNumRelays;
    relayActiveLow = newGlobalActiveLow;    
    int idx = 0;
    for (JsonObject r : relays) {
        relayConfigs[idx].pin = r["pin"];
        relayConfigs[idx].activeLow = r["activeLow"];
        if (r.containsKey("name")) {
            strncpy(relayConfigs[idx].name, r["name"], 15);
            relayConfigs[idx].name[15] = '\0';
        }
        idx++;
    }    
    savePinConfig();
    saveConfiguration();    
    updateRelayPins();
    server.send(200, "application/json", "{\"success\":true}");
}
void handleResetPins() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false,\"error\":\"Too many requests\"}");
        return;
    }    
    char preservedNames[MAX_RELAYS][16];
    TimerSchedule preservedSchedules[MAX_RELAYS];
    bool preservedManualOverride[MAX_RELAYS];
    bool preservedManualState[MAX_RELAYS];   
    for (int i = 0; i < MAX_RELAYS; i++) {
        strncpy(preservedNames[i], relayConfigs[i].name, 15);
        preservedNames[i][15] = '\0';
        preservedSchedules[i] = relayConfigs[i].schedule;
        preservedManualOverride[i] = relayConfigs[i].manualOverride;
        preservedManualState[i] = relayConfigs[i].manualState;
    }    
    uint8_t defaultPins[] = {16, 5, 4, 14, 12, 13, 3, 1};  
    pinConfig.numRelays = 8;  
    pinConfig.globalActiveLow = true;
    numRelays = 8;  
    relayActiveLow = true;
    for (int i = 0; i < 8; i++) {  
        relayConfigs[i].pin = defaultPins[i];
        relayConfigs[i].activeLow = true;
        strncpy(relayConfigs[i].name, preservedNames[i], 15);
        relayConfigs[i].name[15] = '\0';
        relayConfigs[i].schedule = preservedSchedules[i];
        relayConfigs[i].manualOverride = preservedManualOverride[i];
        relayConfigs[i].manualState = preservedManualState[i];
    }
        for (int i = 8; i < MAX_RELAYS; i++) {  
        memset(&relayConfigs[i], 0, sizeof(RelayConfig));
    }    
    savePinConfig();
    saveConfiguration();   
    updateRelayPins();
    server.send(200, "application/json", "{\"success\":true,\"message\":\"GPIO pins reset to default\"}");
}
void handleReset() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    flushConfigIfNeeded();
    server.send(200, "application/json", "{\"success\":true}");
    
    selfRecoveryState = SR_WIFI_RECONNECT;
}
void handleFactoryReset() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset initiated\"}");
    performFactoryReset();
}
void handleSoftReset() {
    if (!checkApiRateLimit()) {
        server.send(429, "application/json", "{\"success\":false}");
        return;
    }    
    selfRecoveryState = SR_NETWORK_HEAL;
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Self-recovery initiated\"}");
}