/*
  ESP32-POE2 + PMS5003 + microSD + NTP + web server + Wi-Fi AP
  ============================================================

  Board:  Olimex ESP32-POE2 (ESP32-WROVER-E, 4 MB flash, PSRAM)

  In Arduino IDE / arduino-cli select a board with PSRAM enabled
  (e.g. "ESP32 Wrover Module") - POE2 uses a WROVER module.
  Note: the Arduino core 3.3.11 has no board named "esp32-poe2".

  Web server (port 80) - reachable over Ethernet and over the Wi-Fi AP:
    /               HTML: graph, current values, file list
    /admin          administration: network settings and manual time
    /api/status     JSON: time, networks, records, current values, OTA,
                    file-list version ("filesVer") and free heap
    /api/data       JSON: records for the graph (?f=file[&since=ts] or ?from=&to=)
    /api/files      JSON: files on the card (cached, see below)
    /download?f=    download a file
    /delete          authenticated POST: delete f (active file is protected)

  Networks:
    - Ethernet (LAN8720) is the primary link; DHCP or static IP
    - Wi-Fi AP is available when enabled in /admin (SSID, password,
      channel); clients get an address from 192.168.4.0/24
    - web server and OTA work on both networks

  Time:
    - NTP when the network is available ("NTP" in the CSV)
    - manually set time from /admin when NTP is not available ("MANUAL")
    - otherwise millis() ("MILLIS")

  Settings are stored in NVS (Preferences) and survive a restart.
  Changing network settings restarts the device.

  Data is written to a separate CSV file per creation date and time:
      /pms_YYYYMMDD_HHMMSS.csv
  The file rotates automatically at midnight (no board restart needed).

  Performance notes:
    - Listing the card costs about half a second, so it is done once and kept
      in RAM (FILE_LIST_MAX = 512 newest files). The cache is dropped whenever
      this program creates, rotates, re-dates or deletes a file, and
      /api/status publishes that as "filesVer" so the page only reloads the
      file table when something really changed.
    - A history query or a CSV range export opens only the files whose span
      overlaps the requested range, and CSV rows are parsed by hand instead of
      with sscanf() (a full day is tens of thousands of lines).

  OTA: the board can be flashed over the network (ArduinoOTA), no USB:
      arduino-cli upload -p <IP> --fqbn esp32:esp32:esp32wrover \
          --protocol network <path_to_sketch>

  ------------------------------------------------------------------
  PINS (verified against Olimex documentation for ESP32-POE2)
  ------------------------------------------------------------------
  Ethernet - LAN8720: PHY_ADDR=0, MDC=GPIO23, MDIO=GPIO18, POWER=GPIO12,
  CLK=GPIO0 (CLK_OUT) - REQUIRED for POE2, GPIO16/17 are used by PSRAM.

  microSD (onboard slot, 1-bit SDMMC): CLK=GPIO14, CMD=GPIO15, D0=GPIO2.

  PMS5003: VCC=5V (EXT1 pin 5), GND=GND, TX=GPIO33 -> ESP32 RX,
  RX=GPIO13 <- ESP32 TX (optional), SET/RESET not connected.

  NOTE: POE2 has no galvanic isolation between PoE power and USB.
  Disconnect the Ethernet cable while programming over USB if the board
  is powered over PoE.
*/

// ---------------------------------------------------------------------------
// Ethernet definitions MUST come before #include <ETH.h>
// ---------------------------------------------------------------------------
#ifndef ETH_PHY_TYPE
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  0
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 12
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_OUT
#endif

#include <SD_MMC.h>
#include <ETH.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>
#include <sys/time.h>
#include <time.h>
#include <algorithm>

// ============================ CONFIGURATION ============================

// ---- Serial output ----
// 0 = completely off (everything is on the web page)
// 1 = on, 115200 baud (diagnostics)
#define SERIAL_DEBUG 0

// ---- PMS5003 (UART2) ----
#define PMS_RX_PIN   33     // ESP32 RX  <- PMS5003 TX
#define PMS_TX_PIN   13     // ESP32 TX  -> PMS5003 RX (optional)
#define PMS_BAUD     9600

// ---- AM2302 / DHT22 (temperature + humidity) ----
#define DHT_PIN        4       // DATA on EXT1/UEXT (free in this project)
#define DHT_READ_MS    3000    // sensor supports ~0.5 Hz max

// ---- Time zone ----
// The "timestamp" CSV column is always a Unix epoch in UTC; the time zone is
// used only for the readable file name and the time shown on the page.
#define TZ_INFO      "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Zagreb
#define CSV_PREFIX   "/pms_"

// ---- Network and time ----
#define NTP_SERVER              "pool.ntp.org"
#define ETH_TIMEOUT_MS          10000UL
#define NET_TOTAL_BUDGET_MS     20000UL
#define NTP_TIMEOUT_MS          10000UL
#define NTP_EPOCH_MIN           1000000000UL
#define PMS_SILENCE_WARN_MS     5000UL

// ---- Clock recovery after a power outage ----
// The router can boot after the board, so Ethernet and NTP are retried in the
// background instead of only in setup().
#define NET_RETRY_MS            5000UL   // re-check link/DHCP while offline
#define NTP_RETRY_MS           15000UL   // retry NTP while the clock is invalid
#define NTP_RETRY_SLOW_MS      60000UL   // ... slower after the fast attempts
#define NTP_RETRY_FAST_ATTEMPTS     20   // ~5 min at 15 s, then once a minute
#define NTP_ATTEMPT_TIMEOUT_MS  8000UL   // one SNTP attempt may take this long
#define REPAIR_TMP_PATH  "/pms_fix.tmp"  // scratch file for the re-dating pass

// ---- Web server and graph ----
#define WEB_PORT                80
#define RING_MAX                500     // records kept in RAM for a single file
#define HIST_MAX                1200    // max records returned for a history query

// ---- OTA (network update) ----
#define OTA_ENABLE      1
#define OTA_HOSTNAME    "esp32-poe2-pms"
#define ADMIN_USER      "admin"
#define DEF_ADMIN_PASSWORD "pms5003admin"

// ---- Default network settings (editable on /admin) ----
#define DEF_ETH_IP      "192.168.3.50"
#define DEF_ETH_MASK    "255.255.255.0"
#define DEF_ETH_GW      "192.168.3.1"
#define DEF_ETH_DNS     "8.8.8.8"
#define DEF_AP_SSID     "esp32-poe2-pms"
#define DEF_AP_PASSWORD "pms5003pms"    // "" or <8 chars = open network
#define DEF_AP_CHANNEL  6
#define NVS_NAMESPACE   "pms5003"

// ---------------------------- SERIAL OUTPUT ----------------------------
#if SERIAL_DEBUG
  #define SLOG Serial
#else
  class NullPrint : public Print {
   public:
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *, size_t n) override { return n; }
  };
  static NullPrint nullPrint;
  #define SLOG nullPrint
#endif

// ========================= DEVICE SETTINGS =========================

struct DeviceSettings {
  bool    ethDhcp;
  char    ethIp[16];
  char    ethMask[16];
  char    ethGw[16];
  char    ethDns[16];
  bool    apEnabled;
  char    apSsid[33];
  char    apPassword[65];
  uint8_t apChannel;
  char    ntpServer[64];
  char    theme[16];
  char    adminPassword[65];
};

static DeviceSettings settings;
static Preferences    prefs;

// ======================= PMS5003 DATA STRUCTURE =======================

struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard;
  uint16_t pm25_standard;
  uint16_t pm100_standard;
  uint16_t pm10_env;      // atmospheric values - these are logged
  uint16_t pm25_env;
  uint16_t pm100_env;
  uint16_t particles_03um;
  uint16_t particles_05um;
  uint16_t particles_10um;
  uint16_t particles_25um;
  uint16_t particles_50um;
  uint16_t particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

static pms5003data pms;

// ============================ RUNTIME STATE ============================

static fs::FS     *sdFs         = nullptr;
static bool        sdReady      = false;
static char        csvPath[48]  = "";

static bool        ethActive    = false;      // Ethernet link + IP
static bool        ntpSynced    = false;
static bool        manualTime   = false;      // time set on /admin
static const char *netMode      = "offline";

static uint32_t    bootEpoch       = 0;       // 0 = unknown
static uint32_t    restartScheduled = 0;      // 0 = not scheduled

static uint16_t    lastWrittenPm25  = 0xFFFF; // last WRITTEN value (for compare)
static uint16_t    lastWrittenPm100 = 0xFFFF;
static bool        firstRecord      = true;

static uint32_t    recordCount = 0;
static bool        haveSample  = false;
static uint32_t    lastTs      = 0;
static uint16_t    lastPm1     = 0;
static uint16_t    lastPm25    = 0;
static uint16_t    lastPm100   = 0;

static uint32_t    lastPmsMs    = 0;
static uint32_t    lastWarnMs   = 0;
static uint32_t    netStartMs   = 0;

// ---- Background network / clock recovery (router may boot after the board) ----
static uint32_t    ntpAttempts   = 0;      // SNTP attempts made after setup()
static uint32_t    lastNetCheck  = 0;
static uint32_t    lastNtpTry    = 0;
static uint32_t    ntpTryStartMs = 0;
static bool        ntpPending    = false;  // an SNTP attempt is in flight
static bool        ethBegan      = false;  // ETH.begin() succeeded
static bool        webStarted    = false;  // server.begin() was called
static bool        timeWasValid  = false;  // edge detector: clock became valid

static uint32_t    framesOk  = 0;   // valid 32-byte frames
static uint32_t    framesBad = 0;   // frames with bad header/checksum

// ---- AM2302 state ----
static float    dhtTempC   = NAN;
static float    dhtHumidity = NAN;
static uint32_t dhtLastGoodMs = 0;
static uint32_t dhtGood = 0;
static uint32_t dhtBad  = 0;
static bool     dhtEverOk = false;
static uint32_t lastDhtRead = 0;
static float    lastWrittenTemp = NAN;
static float    lastWrittenHum  = NAN;

// ---- Daily file rotation ----
static int         rotDay    = -1;
static int         rotMonth  = -1;
static int         rotYear   = -1;
static uint32_t    lastRotCheck = 0;

static WebServer   server(WEB_PORT);

// ---- In-RAM ring of records (single file view) ----
struct LogRow {
  uint32_t ts;
  uint16_t pm1;
  uint16_t pm25;
  uint16_t pm100;
  float    temp;
  float    hum;
  uint8_t  source;
};

enum LogTimeSource : uint8_t {
  LOG_TIME_UNKNOWN = 0,
  LOG_TIME_MILLIS,
  LOG_TIME_NTP,
  LOG_TIME_MANUAL
};

// Sink used by scanCsvRange(): it receives every matching record plus an opaque
// context. A plain function pointer is used instead of a function template
// because the Arduino preprocessor inserts generated prototypes near the top of
// the sketch, which breaks templates - and this typedef has to be visible to
// those prototypes, so it lives here with the other types.
typedef void (*RowSink)(const LogRow &r, void *ctx);

static uint8_t parseTimeSource(const char *source) {
  if (strcmp(source, "MILLIS") == 0) return LOG_TIME_MILLIS;
  if (strcmp(source, "NTP") == 0)    return LOG_TIME_NTP;
  if (strcmp(source, "MANUAL") == 0) return LOG_TIME_MANUAL;
  return LOG_TIME_UNKNOWN;
}

static const char *logTimeSourceName(uint8_t source) {
  switch (source) {
    case LOG_TIME_MILLIS: return "MILLIS";
    case LOG_TIME_NTP:    return "NTP";
    case LOG_TIME_MANUAL: return "MANUAL";
    default:              return "UNKNOWN";
  }
}

static LogRow  ring[RING_MAX];
static int     ringCount = 0;
static int     ringHead  = 0;
static bool    ringValid = false;
static String  ringFile  = "";

// ============================== TIME ==============================

static const char *timeSourceName() {
  if (ntpSynced)  return "NTP";
  if (manualTime) return "MANUAL";
  return "MILLIS";
}

// configTime() resets the TZ variable to UTC, but the local zone is needed for
// the CSV file names, the page display and for interpreting the manual time
// entered on /admin. Re-applied after every configTime() call and once at boot.
static void applyTimeZone() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
}

static bool epochValid() {
  return (ntpSynced || manualTime) && time(nullptr) > (time_t)NTP_EPOCH_MIN;
}

static uint32_t currentTimestamp() {
  if (epochValid()) {
    return (uint32_t)time(nullptr);
  }
  return millis();
}

static void formatEpochTs(uint32_t e, char *buf, size_t n) {
  if (e == 0) {
    snprintf(buf, n, "unknown");
    return;
  }
  struct tm tmInfo;
  time_t t = (time_t)e;
  localtime_r(&t, &tmInfo);
  snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
           tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
           tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
}

static void formatNow(char *buf, size_t n) {
  if (epochValid()) {
    formatEpochTs((uint32_t)time(nullptr), buf, n);
  } else {
    snprintf(buf, n, "uptime %lu s", (unsigned long)(millis() / 1000UL));
  }
}

// Manual time (used when NTP is not available). Time is not kept across a
// restart because the board has no battery-backed RTC.
static void setManualTime(time_t epoch) {
  struct timeval tv;
  tv.tv_sec  = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  manualTime = true;
  if (bootEpoch == 0) {
    bootEpoch = (uint32_t)epoch - (millis() / 1000UL);
  }
  SLOG.print("[TIME] Manually set to ");
  SLOG.println((uint32_t)epoch);
}

// ===================== SETTINGS: LOAD / SAVE =====================

static void loadSettings() {
  settings.ethDhcp = true;
  strlcpy(settings.ethIp,       DEF_ETH_IP,       sizeof(settings.ethIp));
  strlcpy(settings.ethMask,     DEF_ETH_MASK,     sizeof(settings.ethMask));
  strlcpy(settings.ethGw,       DEF_ETH_GW,       sizeof(settings.ethGw));
  strlcpy(settings.ethDns,      DEF_ETH_DNS,      sizeof(settings.ethDns));
  settings.apEnabled = true;
  strlcpy(settings.apSsid,      DEF_AP_SSID,      sizeof(settings.apSsid));
  strlcpy(settings.apPassword,  DEF_AP_PASSWORD,  sizeof(settings.apPassword));
  settings.apChannel = DEF_AP_CHANNEL;
  strlcpy(settings.ntpServer,   NTP_SERVER,       sizeof(settings.ntpServer));
  strlcpy(settings.theme,       "light",          sizeof(settings.theme));
  strlcpy(settings.adminPassword, DEF_ADMIN_PASSWORD, sizeof(settings.adminPassword));

  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return;                                   // no stored settings
  }
  settings.ethDhcp   = prefs.getBool("ethdhcp", settings.ethDhcp);
  settings.apEnabled = prefs.getBool("ap",      settings.apEnabled);
  settings.apChannel = prefs.getUChar("apkanal", settings.apChannel);

  String s;
  s = prefs.getString("ethip",    settings.ethIp);       strlcpy(settings.ethIp,      s.c_str(), sizeof(settings.ethIp));
  s = prefs.getString("ethmaska", settings.ethMask);     strlcpy(settings.ethMask,    s.c_str(), sizeof(settings.ethMask));
  s = prefs.getString("ethgw",    settings.ethGw);       strlcpy(settings.ethGw,      s.c_str(), sizeof(settings.ethGw));
  s = prefs.getString("ethdns",   settings.ethDns);      strlcpy(settings.ethDns,     s.c_str(), sizeof(settings.ethDns));
  s = prefs.getString("apssid",   settings.apSsid);      strlcpy(settings.apSsid,     s.c_str(), sizeof(settings.apSsid));
  s = prefs.getString("aploz",    settings.apPassword);  strlcpy(settings.apPassword, s.c_str(), sizeof(settings.apPassword));
  s = prefs.getString("ntp",      settings.ntpServer);   strlcpy(settings.ntpServer,  s.c_str(), sizeof(settings.ntpServer));
  s = prefs.getString("theme",    settings.theme);      strlcpy(settings.theme,      s.c_str(), sizeof(settings.theme));
  s = prefs.getString("adminpwd", settings.adminPassword); strlcpy(settings.adminPassword, s.c_str(), sizeof(settings.adminPassword));
  prefs.end();

  if (settings.apChannel < 1 || settings.apChannel > 13) {
    settings.apChannel = DEF_AP_CHANNEL;
  }
  if (strlen(settings.adminPassword) < 8) {
    strlcpy(settings.adminPassword, DEF_ADMIN_PASSWORD, sizeof(settings.adminPassword));
  }
}

static void saveSettings() {
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    SLOG.println("[NVS] Cannot open for writing");
    return;
  }
  prefs.putBool("ethdhcp",  settings.ethDhcp);
  prefs.putBool("ap",       settings.apEnabled);
  prefs.putUChar("apkanal", settings.apChannel);
  prefs.putString("ethip",    settings.ethIp);
  prefs.putString("ethmaska", settings.ethMask);
  prefs.putString("ethgw",    settings.ethGw);
  prefs.putString("ethdns",   settings.ethDns);
  prefs.putString("apssid",   settings.apSsid);
  prefs.putString("aploz",    settings.apPassword);
  prefs.putString("ntp",      settings.ntpServer);
  prefs.putString("theme",    settings.theme);
  prefs.putString("adminpwd", settings.adminPassword);
  prefs.end();
}

// ============================== RECORD RING ==============================

static void ringReset(const String &file) {
  ringCount = 0;
  ringHead  = 0;
  ringFile  = file;
  ringValid = true;
}

static void ringPush(uint32_t ts, uint16_t a, uint16_t b, uint16_t c,
                     float tf, float hf, uint8_t source) {
  int idx;
  if (ringCount < RING_MAX) {
    idx = (ringHead + ringCount) % RING_MAX;
    ringCount++;
  } else {
    idx = ringHead;
    ringHead = (ringHead + 1) % RING_MAX;
  }
  ring[idx].ts    = ts;
  ring[idx].pm1   = a;
  ring[idx].pm25  = b;
  ring[idx].pm100 = c;
  ring[idx].temp  = tf;
  ring[idx].hum   = hf;
  ring[idx].source = source;
}

// Loads a CSV file into the ring (last RING_MAX records). If the ring already
// holds that file, nothing is read again.
// Parses one CSV data line into a LogRow; false for headers or bad lines.
// Hand written instead of sscanf(): a history query or an export parses a whole
// day (tens of thousands of lines) and printf-style parsing is a large part of
// that cost.
static bool parseCsvLine(const char *line, LogRow &r) {
  const char *p = line;
  char *end = nullptr;

  if (*p < '0' || *p > '9') {
    return false;                                  // header line or junk
  }
  unsigned long ts = strtoul(p, &end, 10);
  if (*end != ',') {
    return false;
  }
  p = end + 1;

  const char *srcStart = p;
  while (*p != 0 && *p != ',') {
    p++;
  }
  if (*p != ',') {
    return false;                                  // no time_source column
  }
  size_t srcLen = (size_t)(p - srcStart);
  if (srcLen > 15) {
    srcLen = 15;
  }
  char source[16];
  memcpy(source, srcStart, srcLen);
  source[srcLen] = 0;
  p++;

  unsigned long value[3];
  for (int i = 0; i < 3; i++) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value[i] = strtoul(p, &end, 10);
    if (i < 2) {
      if (*end != ',') {
        return false;
      }
      p = end + 1;
    } else {
      p = end;
    }
  }

  r.ts    = (uint32_t)ts;
  r.pm1   = (uint16_t)value[0];
  r.pm25  = (uint16_t)value[1];
  r.pm100 = (uint16_t)value[2];
  r.temp  = NAN;
  r.hum   = NAN;

  // The two environment columns are optional (a file written before the AM2302
  // existed, or a read that failed, leaves them empty).
  if (*p == ',') {
    p++;
    if (*p != ',' && *p != 0) {
      float v = strtof(p, &end);
      if (end != p) {
        r.temp = v;
        p = end;
      }
    }
  }
  if (*p == ',') {
    p++;
    if (*p != 0) {
      float v = strtof(p, &end);
      if (end != p) {
        r.hum = v;
      }
    }
  }

  r.source = parseTimeSource(source);
  return true;
}

// Streams every record of one CSV file inside [from,till] to "sink".
// Records are written in chronological order, so the scan stops at the first
// record past "till" instead of reading the rest of the file. Returns true
// when at least one record was handed to the sink.
static bool scanCsvRange(const char *name, uint32_t from, uint32_t till,
                         RowSink sink, void *ctx) {
  File f = sdFs->open(name, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) {
      f.close();
    }
    return false;
  }

  bool got = false;
  char buf[512];
  size_t pos = 0;
  int rd;
  bool stop = false;
  while (!stop && (rd = f.read((uint8_t *)buf + pos, sizeof(buf) - pos - 1)) > 0) {
    pos += (size_t)rd;
    buf[pos] = 0;
    char *line = buf;
    char *nl;
    while ((nl = strchr(line, char(10))) != nullptr) {
      *nl = 0;
      LogRow r;
      if (parseCsvLine(line, r)) {
        if (till != 0 && r.ts > till) {
          stop = true;
          break;
        }
        if ((from == 0 || r.ts >= from) && (till == 0 || r.ts <= till)) {
          sink(r, ctx);
          got = true;
        }
      }
      line = nl + 1;
    }
    if (stop) {
      break;
    }
    size_t rest = pos - (size_t)(line - buf);
    memmove(buf, line, rest);
    pos = rest;
  }
  if (!stop && pos > 0) {                          // last line without a newline
    buf[pos] = 0;
    LogRow r;
    if (parseCsvLine(buf, r) &&
        (from == 0 || r.ts >= from) && (till == 0 || r.ts <= till)) {
      sink(r, ctx);
      got = true;
    }
  }
  f.close();
  return got;
}

// Writes one record as a CSV line into a caller supplied buffer, flushing it
// through the HTTP connection whenever the buffer fills up.
struct CsvOut {
  char    *buf;
  size_t   cap;
  size_t   pos;
  unsigned blocks;          // sendContent() calls since the last delay()
};

static void sinkCsv(const LogRow &r, void *ctx) {
  CsvOut *o = (CsvOut *)ctx;
  char row[128];
  char tb[16] = "";
  char hb[16] = "";
  if (!isnan(r.temp)) { snprintf(tb, sizeof(tb), "%.1f", r.temp); }
  if (!isnan(r.hum))  { snprintf(hb, sizeof(hb), "%.1f", r.hum);  }
  snprintf(row, sizeof(row), "%lu,%s,%u,%u,%u,%s,%s\r\n",
           (unsigned long)r.ts, logTimeSourceName(r.source),
           r.pm1, r.pm25, r.pm100, tb, hb);
  size_t lr = strlen(row);
  if (o->pos + lr > o->cap - 1) {
    o->buf[o->pos] = 0;
    server.sendContent(o->buf);
    if ((++o->blocks & 7) == 0) {   // let the TCP stack drain now and then
      delay(1);
    }
    o->pos = 0;
  }
  memcpy(o->buf + o->pos, row, lr);
  o->pos += lr;
}

static bool loadIntoRing(const String &file) {
  if (ringValid && ringFile == file) {
    return true;
  }
  if (sdFs == nullptr) {
    return false;
  }
  File f = sdFs->open(file, FILE_READ);
  if (!f || f.isDirectory()) {
    return false;
  }

  ringReset(file);
  char buf[512];
  size_t pos = 0;
  int rd;
  while ((rd = f.read((uint8_t *)buf + pos, sizeof(buf) - pos - 1)) > 0) {
    pos += (size_t)rd;
    buf[pos] = 0;
    char *line = buf;
    char *nl;
    while ((nl = strchr(line, char(10))) != nullptr) {
      *nl = 0;
      if (line[0] >= 48 && line[0] <= 57) {
        LogRow r;
        if (parseCsvLine(line, r)) {
          ringPush(r.ts, r.pm1, r.pm25, r.pm100, r.temp, r.hum, r.source);
        }
      }
      line = nl + 1;
    }
    size_t rest = pos - (size_t)(line - buf);
    memmove(buf, line, rest);
    pos = rest;
  }
  if (pos > 0) {
    buf[pos] = 0;
    if (buf[0] >= 48 && buf[0] <= 57) {
      LogRow r;
      if (parseCsvLine(buf, r)) {
        ringPush(r.ts, r.pm1, r.pm25, r.pm100, r.temp, r.hum, r.source);
      }
    }
  }
  f.close();
  return true;
}

// ====================== HISTORY QUERY (MULTIPLE FILES) ======================
//
// For a requested time range this reads every CSV file that covers it,
// filters records by time and returns them sorted. If there are more records
// than HIST_MAX the buffer is halved and every other record is kept from then
// on (even decimation) so the graph always covers the whole range.

static LogRow  *histBuf    = nullptr;   // allocated in PSRAM when possible
static int      histCount  = 0;
static int      histDecim  = 1;
static int      histCnt2   = 0;
static uint32_t histTotal  = 0;         // records in range (before decimation)
static String   histFiles  = "";
static uint32_t histCacheFrom = 0;
static uint32_t histCacheTill = 0;
static bool     histCacheValid = false;

static bool histEnsure() {
  if (histBuf == nullptr) {
    histBuf = (LogRow *)ps_malloc(sizeof(LogRow) * HIST_MAX);
    if (histBuf == nullptr) {
      histBuf = (LogRow *)malloc(sizeof(LogRow) * HIST_MAX);
    }
  }
  return histBuf != nullptr;
}

static void histReset() {
  histCount = 0;
  histDecim = 1;
  histCnt2  = 0;
  histTotal = 0;
  histFiles = "";
}

static void histPush(const LogRow &r) {
  histTotal++;
  if (histBuf == nullptr) {
    return;
  }
  if (histCount >= HIST_MAX) {
    int w = 0;
    for (int i = 0; i < histCount; i += 2) {
      histBuf[w++] = histBuf[i];
    }
    histCount = w;
    histDecim *= 2;
    histCnt2 = 0;
  }
  if (histCnt2 == 0) {
    histBuf[histCount++] = r;
  }
  histCnt2 = (histCnt2 + 1) % histDecim;
}

static int cmpLogRow(const void *a, const void *b) {
  uint32_t ta = ((const LogRow *)a)->ts;
  uint32_t tb = ((const LogRow *)b)->ts;
  if (ta < tb) return -1;
  if (ta > tb) return 1;
  return 0;
}

// ============================ CSV FILE LIST ============================
//
// Listing the card costs about half a second (66 directory entries on SD_MMC),
// and the web UI needs that list for the file table, for a history query and
// for a range export. It is therefore scanned once, kept in RAM, and dropped
// whenever this program itself changes the card. Sorted by name, which for
// /pms_YYYYMMDD_HHMMSS.csv means chronological order.
#define FILE_LIST_MAX 512

struct FileEntry {
  char     name[28];        // "/pms_YYYYMMDD_HHMMSS.csv"
  uint32_t size;
};

static FileEntry fileList[FILE_LIST_MAX];
static int       fileListCount     = -1;      // -1 = the cache needs a scan
static bool      fileListTruncated = false;   // card holds more than FILE_LIST_MAX
static uint32_t  filesVersion      = 0;       // bumped on every card change
static uint32_t  fileScanMs        = 0;       // duration of the last full scan

// Marks the cached listing stale and tells the web UI to fetch it again.
static void fileListInvalidate() {
  fileListCount = -1;
  filesVersion++;
}

static int fileListEnsure() {
  if (fileListCount >= 0) {
    return fileListCount;
  }
  uint32_t t0 = millis();
  fileListCount     = 0;
  fileListTruncated = false;

  File root = sdFs ? sdFs->open("/") : File();
  if (root && root.isDirectory()) {
    const size_t prefixLen = strlen(CSV_PREFIX) - 1;   // without the leading slash
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        const char *raw = f.name();
        const char *nm  = (raw[0] == '/') ? raw + 1 : raw;
        size_t len = strlen(nm);
        if (len > prefixLen + 4 && strncmp(nm, CSV_PREFIX + 1, prefixLen) == 0 &&
            strcmp(nm + len - 4, ".csv") == 0) {
          int idx = fileListCount++;
          if (idx >= FILE_LIST_MAX) {
            idx %= FILE_LIST_MAX;                 // a full card keeps the newest
            fileListTruncated = true;
          }
          FileEntry &e = fileList[idx];
          snprintf(e.name, sizeof(e.name), "/%s", nm);
          e.size = (uint32_t)f.size();
        }
      }
      f = root.openNextFile();
    }
    root.close();
  }

  if (fileListCount > FILE_LIST_MAX) {                // undo the rolling write
    std::rotate(fileList, fileList + (fileListCount % FILE_LIST_MAX),
                fileList + FILE_LIST_MAX);
    fileListCount = FILE_LIST_MAX;
  }
  std::sort(fileList, fileList + fileListCount, [](const FileEntry &a, const FileEntry &b) {
    return strcmp(a.name, b.name) < 0;
  });
  fileScanMs = millis() - t0;
  return fileListCount;
}

// Epoch (local time zone) from a /pms_YYYYMMDD_HHMMSS.csv name; 0 if the name
// has no timestamp (for example /pms_millis_0000123456.csv).
static uint32_t fileNameEpoch(const char *name) {
  const char *p = strstr(name, CSV_PREFIX);
  if (p == nullptr) return 0;
  const char *c = p + strlen(CSV_PREFIX);
  for (int i = 0; i < 15; i++) {
    if (i == 8) {
      if (c[i] != '_') return 0;                          // the date/time separator
    } else if (c[i] < '0' || c[i] > '9') {
      return 0;
    }
  }

  struct tm t = {};
  t.tm_year  = (c[0]-48)*1000 + (c[1]-48)*100 + (c[2]-48)*10 + (c[3]-48) - 1900;
  t.tm_mon   = (c[4]-48)*10 + (c[5]-48) - 1;
  t.tm_mday  = (c[6]-48)*10 + (c[7]-48);
  t.tm_hour  = (c[9]-48)*10 + (c[10]-48);
  t.tm_min   = (c[11]-48)*10 + (c[12]-48);
  t.tm_sec   = (c[13]-48)*10 + (c[14]-48);
  t.tm_isdst = -1;
  time_t e = mktime(&t);
  return (e > (time_t)NTP_EPOCH_MIN) ? (uint32_t)e : 0;
}

// Row sink for a history query: decimated push into the history buffer.
static void sinkHist(const LogRow &r, void *ctx) {
  (void)ctx;
  histPush(r);
}

static bool loadHistoryRange(uint32_t from, uint32_t till) {
  if (sdFs == nullptr || !histEnsure()) {
    return false;
  }

  // A closed range never changes, so the previous result can be reused
  if (histCacheValid && till != 0 && from == histCacheFrom && till == histCacheTill) {
    return true;
  }
  histReset();
  histCacheValid = false;

  const int n = fileListEnsure();   // cached listing: one scan for many queries

  // Timestamped names sort chronologically, so a file covers the span from its
  // own creation until the next file starts. Only files whose span overlaps the
  // requested range are actually read - this is what keeps queries fast.
  for (int i = 0; i < n; i++) {
    const char *name = fileList[i].name;
    uint32_t start = fileNameEpoch(name);
    uint32_t end   = (i + 1 < n) ? fileNameEpoch(fileList[i + 1].name) : 0;

    if (start == 0) {
      if (from != 0) continue;                   // MILLIS era file, no wall clock
    } else if (from >= NTP_EPOCH_MIN) {
      if (end != 0 && end <= from) continue;     // this file ended before the range
      if (till != 0 && start > till) continue;   // this file starts after the range
    }

    uint32_t before = histTotal;
    scanCsvRange(name, from, till, sinkHist, nullptr);

    if (histTotal > before && histFiles.length() < 120) {
      if (histFiles.length() > 0) {
        histFiles += ",";
      }
      histFiles += name;
    }
  }

  if (histCount > 1) {
    qsort(histBuf, histCount, sizeof(LogRow), cmpLogRow);
  }

  if (till != 0) {                 // remember the result of a closed range
    histCacheFrom  = from;
    histCacheTill  = till;
    histCacheValid = true;
  }
  return true;
}

// ============================== SD CARD ==============================

static bool mountSD() {
  if (SD_MMC.begin("/sdcard", true)) {   // 1-bit: CLK=14, CMD=15, D0=2
    sdFs = &SD_MMC;
    return true;
  }
  return false;
}

static void buildFileName() {
  time_t now = time(nullptr);

  if (epochValid()) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    snprintf(csvPath, sizeof(csvPath),
             CSV_PREFIX "%04d%02d%02d_%02d%02d%02d.csv",
             tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
             tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
  } else {
    snprintf(csvPath, sizeof(csvPath),
             CSV_PREFIX "millis_%010lu.csv", (unsigned long)millis());
  }
}

static bool prepareCsv() {
  if (csvPath[0] == '\0') {
    buildFileName();
  }

  if (sdFs->exists(csvPath)) {
    SLOG.print("[SD] File already exists, continuing with: ");
    SLOG.println(csvPath);
    return true;
  }

  File f = sdFs->open(csvPath, FILE_WRITE);
  if (!f) {
    SLOG.print("[SD] ERROR: cannot create ");
    SLOG.println(csvPath);
    return false;
  }
  f.println("timestamp,time_source,pm1_0,pm2_5,pm10,temp_c,humidity");
  f.close();
  fileListInvalidate();                 // the new file appears in the file list
  SLOG.print("[SD] Created file with header: ");
  SLOG.println(csvPath);
  return true;
}

// ---- Daily rotation ----

static void rememberRotationDate() {
  if (!epochValid()) {
    return;
  }
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  rotDay   = t.tm_mday;
  rotMonth = t.tm_mon;
  rotYear  = t.tm_year;
}

// Opens a new file named after the current date and time. There is no open
// handle to close because the file is opened per write.
static void rotateFile() {
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);

  buildFileName();
  recordCount      = 0;
  firstRecord      = true;
  lastWrittenPm25  = 0xFFFF;     // first change in the new file is written
  lastWrittenPm100 = 0xFFFF;
  ringValid        = false;      // the graph will switch to the new file

  if (sdReady) {
    prepareCsv();
  }
  fileListInvalidate();                 // the active file (and the list) changed

  rotDay   = t.tm_mday;
  rotMonth = t.tm_mon;
  rotYear  = t.tm_year;

  SLOG.print("[SD] Daily rotation, new file: ");
  SLOG.println(csvPath);
}

static void checkRotation() {
  if (!epochValid() || !sdReady || rotDay < 0) {
    return;
  }
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  if (t.tm_mday != rotDay || t.tm_mon != rotMonth || t.tm_year != rotYear) {
    rotateFile();
  }
}

static bool writeRow(uint32_t ts, const char *source,
                     uint16_t pm1, uint16_t pm25, uint16_t pm100,
                     float temp, float hum) {
  if (sdFs == nullptr) {
    return false;
  }
  File f = sdFs->open(csvPath, FILE_APPEND);
  if (!f) {
    SLOG.print("[SD] ERROR: cannot open ");
    SLOG.print(csvPath);
    SLOG.println(" for writing");
    return false;
  }
  // The whole line is formatted once and written with a single call: a dozen
  // f.print() calls would each go through the FAT layer (and its lock) alone.
  char line[128];
  int  len;
  if (dhtEverOk) {
    len = snprintf(line, sizeof(line), "%lu,%s,%u,%u,%u,%.1f,%.1f\r\n",
                   (unsigned long)ts, source, pm1, pm25, pm100,
                   (double)temp, (double)hum);
  } else {
    len = snprintf(line, sizeof(line), "%lu,%s,%u,%u,%u,,\r\n",
                   (unsigned long)ts, source, pm1, pm25, pm100);
  }
  if (len > 0) {
    f.write((const uint8_t *)line, (size_t)((len < (int)sizeof(line)) ? len : (int)sizeof(line) - 1));
  }
  f.close();
  recordCount++;

  if (ringValid && strcmp(ringFile.c_str(), csvPath) == 0) {
    ringPush(ts, pm1, pm25, pm100, temp, hum, parseTimeSource(source));
  }
  return true;
}

// ============ CLOCK RECOVERY: RE-DATE THE EARLY MILLIS RECORDS ============
//
// After a power outage the board can be ready before the router, so there is no
// NTP yet and the first records are written with millis() into a file called
// /pms_millis_NNNNNNNNNN.csv. As soon as a real clock exists - NTP came up
// later, or the time was set on /admin - the boot epoch is known, so every
// millis() row can be re-dated with  epoch = bootEpoch + millis/1000  and the
// file renamed after its real start time. Runs once per boot, from loop().

static uint32_t redatedRows = 0;      // records re-dated since boot (diagnostics)

// Rewrites one data line: the millis() timestamp becomes a real epoch and the
// time_source column is set to the clock that made the reconstruction possible
// ("NTP" or "MANUAL"), so the CSV stays consistent with what the page shows.
// Returns the new length, or 0 when the line is not a millis() data row.
static size_t redateLine(const char *line, uint32_t epoch, const char *source,
                         char *out, size_t n) {
  if (line[0] < '0' || line[0] > '9') {
    return 0;                                     // header or junk
  }
  char *e1 = nullptr;
  unsigned long v = strtoul(line, &e1, 10);
  if (e1 == nullptr || *e1 != ',' || v >= (unsigned long)NTP_EPOCH_MIN) {
    return 0;                                     // already a Unix epoch
  }
  const char *rest = e1 + 1;                      // "<source>,pm1,pm2.5,..."
  const char *e2 = strchr(rest, ',');
  if (e2 == nullptr) {
    return 0;
  }
  unsigned long ts = (unsigned long)(epoch + (uint32_t)(v / 1000UL));
  int w = snprintf(out, n, "%lu,%s%s", ts, source, e2);
  return (w > 0 && (size_t)w < n) ? (size_t)w : 0;
}

// Copies the active file into a scratch file with the millis() rows re-dated,
// then renames it after its real start time. Nothing is deleted before the new
// file exists, so a failure never loses data.
static bool repairActiveFile() {
  if (!sdReady || sdFs == nullptr || csvPath[0] == '\0' || bootEpoch == 0) {
    return false;
  }

  File src = sdFs->open(csvPath, FILE_READ);
  if (!src || src.isDirectory()) {
    if (src) { src.close(); }
    return false;
  }

  // Cheap pre-check: the first data line decides whether there is work to do.
  char head[256];
  int rd = src.read((uint8_t *)head, sizeof(head) - 1);
  if (rd <= 0) {
    src.close();
    return false;
  }
  head[rd] = 0;
  char *nl = strchr(head, char(10));               // end of the header line
  if (nl == nullptr) {
    src.close();
    return false;
  }
  const char *firstRow = nl + 1;
  char *endp = nullptr;
  unsigned long firstTs = strtoul(firstRow, &endp, 10);
  if (endp == firstRow || firstTs >= (unsigned long)NTP_EPOCH_MIN) {
    src.close();
    return false;                                  // no millis rows -> nothing to do
  }
  uint32_t startEpoch = bootEpoch + (uint32_t)(firstTs / 1000UL);

  src.seek(0);
  File dst = sdFs->open(REPAIR_TMP_PATH, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }

  char buf[512];
  char out[640];
  size_t pos = 0;
  bool headerDone = false;
  const char *srcName = timeSourceName();
  uint32_t rows = 0, changed = 0;
  int n;
  while ((n = src.read((uint8_t *)buf + pos, sizeof(buf) - pos - 1)) > 0) {
    pos += (size_t)n;
    buf[pos] = 0;
    char *line = buf;
    char *eol;
    while ((eol = strchr(line, char(10))) != nullptr) {
      *eol = 0;
      size_t len = strlen(line);
      if (len > 0 && line[len - 1] == '\r') { line[--len] = 0; }
      if (!headerDone) {
        headerDone = true;                          // header is copied verbatim
        dst.write((const uint8_t *)line, len);
      } else if (len > 0) {
        size_t w = redateLine(line, bootEpoch, srcName, out, sizeof(out));
        if (w > 0) { changed++; dst.write((const uint8_t *)out, w); }
        else       { dst.write((const uint8_t *)line, len); }
        rows++;
      }
      dst.write((const uint8_t *)"\r\n", 2);
      line = eol + 1;
    }
    size_t rest = pos - (size_t)(line - buf);
    memmove(buf, line, rest);
    pos = rest;
  }
  if (pos > 0 && headerDone) {                      // last line without newline
    buf[pos] = 0;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\r') { buf[--len] = 0; }
    size_t w = redateLine(buf, bootEpoch, srcName, out, sizeof(out));
    if (w > 0) { changed++; dst.write((const uint8_t *)out, w); rows++; }
  }
  dst.close();
  src.close();

  if (rows == 0 || changed == 0) {
    sdFs->remove(REPAIR_TMP_PATH);
    return false;
  }

  // Name the file after its real first record (local time zone), like a file
  // that was created when the clock was already valid.
  char newPath[52];
  struct tm t;
  time_t te = (time_t)startEpoch;
  localtime_r(&te, &t);
  snprintf(newPath, sizeof(newPath), CSV_PREFIX "%04d%02d%02d_%02d%02d%02d.csv",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);

  String target = String(newPath);
  if (sdFs->exists(target)) {                        // never overwrite a file
    target = target.substring(0, target.length() - 4) + "_b.csv";
  }
  if (!sdFs->rename(REPAIR_TMP_PATH, target.c_str())) {
    SLOG.print("[TIME] Rename failed, repaired data kept in ");
    SLOG.println(REPAIR_TMP_PATH);
    return false;
  }

  String old = String(csvPath);
  sdFs->remove(old.c_str());                         // the data now lives in target
  strlcpy(csvPath, target.c_str(), sizeof(csvPath));
  ringValid = false;                                 // the graph reloads from the file
  fileListInvalidate();                              // old name gone, new one added
  redatedRows += changed;

  SLOG.print("[TIME] Re-dated ");
  SLOG.print(changed);
  SLOG.print(" records, active file renamed ");
  SLOG.print(old);
  SLOG.print(" -> ");
  SLOG.println(csvPath);
  return true;
}

// ============================== PMS5003 ==============================

// Reads one 32-byte frame. The loop re-syncs on the 0x42 0x4D header, so a
// loss of sync repairs itself - otherwise every following frame would stay
// misaligned and data would stop arriving.
static bool readPmsData(Stream *s) {
  if (s->available() < 32) {
    return false;
  }

  uint8_t buffer[32];

  while (s->available() >= 32) {
    if (s->peek() != 0x42) {      // look for the first header byte
      s->read();
      continue;
    }

    s->readBytes(buffer, 32);

    if (buffer[1] != 0x4D) {      // false header, keep looking
      framesBad++;
      continue;
    }

    uint16_t sum = 0;
    for (uint8_t i = 0; i < 30; i++) {
      sum += buffer[i];
    }
    uint16_t receivedChecksum = ((uint16_t)buffer[30] << 8) | buffer[31];
    if (sum != receivedChecksum) {
      framesBad++;
      continue;
    }

    pms.framelen        = ((uint16_t)buffer[2]  << 8) | buffer[3];
    pms.pm10_standard   = ((uint16_t)buffer[4]  << 8) | buffer[5];
    pms.pm25_standard   = ((uint16_t)buffer[6]  << 8) | buffer[7];
    pms.pm100_standard  = ((uint16_t)buffer[8]  << 8) | buffer[9];
    pms.pm10_env        = ((uint16_t)buffer[10] << 8) | buffer[11];
    pms.pm25_env        = ((uint16_t)buffer[12] << 8) | buffer[13];
    pms.pm100_env       = ((uint16_t)buffer[14] << 8) | buffer[15];
    pms.particles_03um  = ((uint16_t)buffer[16] << 8) | buffer[17];
    pms.particles_05um  = ((uint16_t)buffer[18] << 8) | buffer[19];
    pms.particles_10um  = ((uint16_t)buffer[20] << 8) | buffer[21];
    pms.particles_25um  = ((uint16_t)buffer[22] << 8) | buffer[23];
    pms.particles_50um  = ((uint16_t)buffer[24] << 8) | buffer[25];
    pms.particles_100um = ((uint16_t)buffer[26] << 8) | buffer[27];
    pms.unused          = ((uint16_t)buffer[28] << 8) | buffer[29];
    pms.checksum        = receivedChecksum;

    framesOk++;
    return true;
  }

  return false;
}

// ============================== AM2302 (DHT22) ==============================

// Bit-banged read of one 40-bit frame. The sensor needs a >=1 ms low pulse to
// start, then sends 40 bits (16 hum, 16 temp, 8 checksum). Interrupts are
// disabled during the transfer because the bit timing is ~30 us.
static bool readDht22(float &tempC, float &humidity) {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(2);                       // start signal (>= 1 ms)
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  noInterrupts();

  // sensor response: ~80 us low, then ~80 us high
  uint32_t t = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t > 200) { interrupts(); return false; } }
  t = micros();
  while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t > 200) { interrupts(); return false; } }
  t = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t > 200) { interrupts(); return false; } }

  // 40 data bits: 50 us low, then 26-28 us (0) or ~70 us (1) high
  for (int i = 0; i < 40; i++) {
    t = micros();
    while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t > 100) { interrupts(); return false; } }
    t = micros();
    while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t > 100) { interrupts(); return false; } }
    uint32_t highUs = micros() - t;
    data[i / 8] <<= 1;
    if (highUs > 45) {
      data[i / 8] |= 1;
    }
  }

  interrupts();

  uint8_t sum = data[0] + data[1] + data[2] + data[3];
  if (sum != data[4]) {
    return false;
  }

  uint16_t rawHum  = ((uint16_t)data[0] << 8) | data[1];
  uint16_t rawTemp = ((uint16_t)data[2] << 8) | data[3];
  humidity = rawHum / 10.0f;
  tempC    = (rawTemp & 0x8000) ? -((rawTemp & 0x7FFF) / 10.0f)
                                :  (rawTemp / 10.0f);
  return true;
}

// ============================== NETWORK ==============================

static bool waitEthernet() {
  // A static IP must be configured before begin().
  if (!settings.ethDhcp) {
    IPAddress ip, gw, mask, dns;
    ip.fromString(settings.ethIp);
    gw.fromString(settings.ethGw);
    mask.fromString(settings.ethMask);
    dns.fromString(settings.ethDns);
    bool okCfg = ETH.config(ip, gw, mask, dns);
    SLOG.print("[NET] ETH.config(static): ");
    SLOG.println(okCfg ? "OK" : "FAILED");
  }

  bool ok = ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR, ETH_PHY_MDC,
                      ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLOCK_GPIO0_OUT);
  SLOG.print("[NET] ETH.begin():        ");
  SLOG.println(ok ? "OK" : "FAILED");
  ethBegan = ok;
  if (!ok) {
    return false;
  }

  uint32_t start = millis();
  while ((millis() - start) < ETH_TIMEOUT_MS &&
         (millis() - netStartMs) < NET_TOTAL_BUDGET_MS) {
    if (ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
      return true;
    }
    delay(100);
  }
  return false;
}

static void startAP() {
  if (!settings.apEnabled) {
    SLOG.println("[AP] Disabled in settings");
    return;
  }

  WiFi.mode(WIFI_AP);
  bool ok;
  if (strlen(settings.apPassword) >= 8) {
    ok = WiFi.softAP(settings.apSsid, settings.apPassword, settings.apChannel);
  } else {
    ok = WiFi.softAP(settings.apSsid, NULL, settings.apChannel);   // open network
  }

  SLOG.print("[AP] ");
  SLOG.print(settings.apSsid);
  SLOG.print(" channel ");
  SLOG.print(settings.apChannel);
  SLOG.print(" -> ");
  SLOG.print(WiFi.softAPIP());
  SLOG.println(ok ? "" : " (ERROR)");
}

static bool syncNtp() {
  SLOG.print("[NTP] configTime(");
  SLOG.print(settings.ntpServer);
  SLOG.println(")");
  configTime(0, 0, settings.ntpServer);
  applyTimeZone();             // configTime() would leave TZ at UTC

  uint32_t start = millis();
  while (true) {
    if (time(nullptr) > (time_t)NTP_EPOCH_MIN) {
      return true;
    }
    uint32_t now = millis();
    if ((now - start) >= NTP_TIMEOUT_MS) {
      return false;
    }
    if ((now - netStartMs) >= NET_TOTAL_BUDGET_MS) {
      return false;
    }
    delay(200);
  }
}

// Re-checks the Ethernet link in the background. After a power outage the
// router (and with it DHCP and the uplink) may only be available minutes after
// the board booted, so the state is not latched in setup() any more. As soon as
// the interface has an IP the web server is started if it never was, and the
// NTP retry loop below gets a route to the Internet.
static void pollNetwork() {
  uint32_t now = millis();
  if ((now - lastNetCheck) < NET_RETRY_MS) {
    return;
  }
  lastNetCheck = now;

  if (!ethBegan) {
    // The PHY itself may have been dead at boot (switch/router without power).
    ethBegan = ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR, ETH_PHY_MDC,
                         ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLOCK_GPIO0_OUT);
    SLOG.print("[NET] ETH.begin() retry:  ");
    SLOG.println(ethBegan ? "OK" : "FAILED");
  }

  if (!ethActive && ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
    ethActive = true;
    netMode   = "Ethernet";
    ETH.setDefault();                    // make it the outbound interface
    SLOG.print("[NET] Ethernet came up late, IP=");
    SLOG.print(ETH.localIP());
    SLOG.println(" (DHCP)");
    if (!webStarted && (ethActive || settings.apEnabled)) {
      startWebServer();
    }
  }
}

// Retries the NTP synchronisation in the background. Without this a board that
// booted before the router would keep millis() timestamps for its whole uptime.
// The first attempts are quick, later ones are spread out to stay quiet.
static void pollNtp() {
  if (epochValid()) {                    // real time already known (NTP or manual)
    ntpPending = false;
    return;
  }
  uint32_t now = millis();

  if (ntpPending) {
    if (time(nullptr) > (time_t)NTP_EPOCH_MIN) {
      ntpPending = false;
      ntpSynced  = true;
      applyTimeZone();
      if (bootEpoch == 0) {
        bootEpoch = (uint32_t)time(nullptr) - (millis() / 1000UL);
      }
      SLOG.print("[NTP] Synchronised after retry, epoch=");
      SLOG.println((uint32_t)time(nullptr));
      return;
    }
    if ((now - ntpTryStartMs) >= NTP_ATTEMPT_TIMEOUT_MS) {
      ntpPending = false;
      SLOG.println("[NTP] Retry attempt timed out");
    }
    return;
  }

  if (!ethActive) {                      // no uplink, nothing to ask
    return;
  }
  uint32_t wait = (ntpAttempts < (uint32_t)NTP_RETRY_FAST_ATTEMPTS)
                  ? NTP_RETRY_MS : NTP_RETRY_SLOW_MS;
  if (lastNtpTry != 0 && (now - lastNtpTry) < wait) {
    return;
  }

  lastNtpTry    = now;
  ntpTryStartMs = now;
  ntpAttempts++;
  ntpPending    = true;
  configTime(0, 0, settings.ntpServer);
  applyTimeZone();             // configTime() would leave TZ at UTC
  SLOG.print("[NTP] Retry #");
  SLOG.println(ntpAttempts);
}

// ============================== SAMPLE HANDLING ==============================

static void handleSample() {
  // Atmospheric values = frame bytes 10-15
  uint16_t pm1   = pms.pm10_env;
  uint16_t pm25  = pms.pm25_env;
  uint16_t pm100 = pms.pm100_env;

  haveSample = true;
  uint32_t ts       = currentTimestamp();
  const char *src   = timeSourceName();
  lastTs    = ts;
  lastPm1   = pm1;
  lastPm25  = pm25;
  lastPm100 = pm100;

  bool changed = (pm25 != lastWrittenPm25) || (pm100 != lastWrittenPm100) ||
                 (!isnan(dhtTempC)    && dhtTempC    != lastWrittenTemp) ||
                 (!isnan(dhtHumidity) && dhtHumidity != lastWrittenHum);

  if (changed) {
    if (!sdReady) {
      sdReady = prepareCsv();
    }
    if (sdReady && writeRow(ts, src, pm1, pm25, pm100, dhtTempC, dhtHumidity)) {
      lastWrittenPm25  = pm25;
      lastWrittenPm100 = pm100;
      lastWrittenTemp  = dhtTempC;
      lastWrittenHum   = dhtHumidity;
      if (firstRecord) {
        firstRecord = false;
        SLOG.print("[SD] First recorded entry: ");
        SLOG.print(src);
        SLOG.print(" ");
        SLOG.println(ts);
      }
    }
  }
}

// ============================== MAIN PAGE ==============================

static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-POE2 &middot; PMS5003</title>
<style>
:root{
  --bg:#f5f7fa; --card:#ffffff; --text:#111827; --muted:#6b7280; --line:#e5e7eb;
  --line-soft:#f1f2f4; --hover:#fafbfc;
  --accent:#2563eb; --accent-dark:#1d4ed8; --accent-soft:#eef4ff;
  --ok:#15803d; --warn:#b45309; --err:#b91c1c;
  --pm1:#16a34a; --pm25:#ea580c; --pm10:#dc2626; --temp:#7c3aed; --hum:#0891b2; --dew:#0284c7;
  --r:14px; --sh:0 1px 2px rgba(16,24,40,.05),0 10px 28px rgba(16,24,40,.06);
}
body.theme-dark{
  --bg:#0f1319; --card:#171d26; --text:#e7ebf2; --muted:#94a3b8; --line:#252d3a;
  --line-soft:#1f2733; --hover:#1d2430;
  --accent:#4f8cff; --accent-dark:#3a74e6; --accent-soft:#1b2534;
  --pm1:#22c55e; --pm25:#f97316; --pm10:#ef4444; --temp:#a78bfa; --hum:#22d3ee; --dew:#38bdf8;
  --sh:0 1px 2px rgba(0,0,0,.5),0 12px 30px rgba(0,0,0,.45);
}
body.theme-contrast{
  --bg:#000000; --card:#000000; --text:#ffffff; --muted:#ffd400; --line:#ffffff;
  --line-soft:#3a3a3a; --hover:#141414;
  --accent:#00e5ff; --accent-dark:#00b8cc; --accent-soft:#00323a;
  --pm1:#00ff66; --pm25:#ffcc00; --pm10:#ff3b30; --temp:#c084fc; --hum:#22d3ee; --dew:#38bdf8;
  --sh:none;
}
body.theme-contrast .card,body.theme-contrast .val{border-width:2px}
*{box-sizing:border-box}
body{margin:0;padding:18px;background:var(--bg);color:var(--text);
  font:14px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;-webkit-font-smoothing:antialiased}
h1{display:flex;align-items:center;flex-wrap:wrap;gap:8px 14px;font-size:20px;font-weight:650;margin:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--sh);padding:16px;margin-bottom:14px}
.row{display:flex;flex-wrap:wrap;gap:8px 24px;align-items:baseline}
.kv{font-size:13px;color:var(--muted)}
.kv b{color:var(--text);font-weight:600}
.tag{display:inline-block;padding:2px 10px;border-radius:999px;background:var(--accent-soft);color:var(--accent-dark);font-size:12px;font-weight:600}
.ctrl{display:flex;flex-wrap:wrap;gap:10px 14px;align-items:center;margin:4px 0 10px}
select,input[type=datetime-local]{font:inherit;font-size:13px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--text)}
select:focus,input:focus{outline:2px solid var(--accent-soft);border-color:var(--accent)}
button{font:inherit;font-size:13px;font-weight:600;padding:8px 14px;border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--text);cursor:pointer;transition:.15s}
button:hover{background:var(--hover);border-color:var(--accent)}
button:active{transform:translateY(1px)}
#apply{background:var(--accent);border-color:var(--accent);color:#fff}
button.on{background:var(--accent);border-color:var(--accent);color:#fff}
#apply:hover{background:var(--accent-dark);border-color:var(--accent-dark)}
a{color:var(--accent);text-decoration:none;font-weight:500}
a:hover{text-decoration:underline}
.del{color:var(--err)}
.help{font-size:13px;font-weight:500;color:var(--muted)}
.help:hover{color:var(--accent)}
.muted{color:var(--muted);font-size:12px}
.stale{opacity:.45}
canvas{width:100%;height:320px;display:block;border:1px solid var(--line);border-radius:12px;background:var(--card)}
#chartEnv{height:220px}
.vals{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:12px;margin-top:8px}
.val{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 12px;border-top:3px solid var(--line)}
.val .muted{font-size:11.5px;text-transform:uppercase;letter-spacing:.05em}
.big{font-size:30px;font-weight:700;line-height:1.15;font-variant-numeric:tabular-nums}
#cvals .val:nth-child(1){border-top-color:var(--pm1)}
#cvals .val:nth-child(2){border-top-color:var(--pm25)}
#cvals .val:nth-child(3){border-top-color:var(--pm10)}
#cvals .val:nth-child(4){border-top-color:var(--temp)}
#cvals .val:nth-child(5){border-top-color:var(--hum)}
#cvals .val:nth-child(6){border-top-color:var(--dew)}
table{border-collapse:separate;border-spacing:0;width:100%;font-size:13.5px}
th{text-align:left;font-size:11.5px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);font-weight:600;padding:8px 10px;border-bottom:1px solid var(--line)}
td{padding:9px 10px;border-bottom:1px solid var(--line-soft);vertical-align:middle}
tbody tr:hover td{background:var(--hover)}
tbody tr:last-child td{border-bottom:none}
.modal{display:none;position:fixed;inset:0;background:rgba(15,23,42,.5);z-index:10;padding:18px;overflow:auto;-webkit-backdrop-filter:blur(2px);backdrop-filter:blur(2px)}
.modal.open{display:block}
.modalbox{background:var(--card);border:1px solid var(--line);border-radius:16px;max-width:820px;margin:0 auto;overflow:hidden;box-shadow:0 24px 60px rgba(0,0,0,.35)}
.modalhead{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px 18px;border-bottom:1px solid var(--line);background:var(--card)}
.modalbody{padding:8px 18px 20px;font-size:13.5px;line-height:1.55}
.modalbody h3{font-size:14px;margin:18px 0 6px}
.modalbody table{margin-bottom:10px;font-size:13px}
.modalbody td,.modalbody th{padding:5px 8px}
.modalbody ul{margin:6px 0 10px 18px;padding:0}
.modalbody li{margin-bottom:4px}
@media(max-width:640px){body{padding:10px}.card{padding:13px;border-radius:12px}h1{font-size:17px}.big{font-size:24px}canvas{height:250px}.ctrl{gap:8px}}
</style>
</head>
<body>

<div class="card">
  <h1>ESP32-POE2 &middot; PMS5003
    <a href="#" id="help" class="help">[ help / reference values ]</a>
    <a href="/admin" class="help">[ administration ]</a>
  </h1>
  <div class="row">
    <div class="kv">Board time: <b id="clock">-</b> <span class="tag" id="tsrc">-</span></div>
    <div class="kv">Started: <b id="boot">-</b></div>
    <div class="kv">File: <b id="file">-</b></div>
    <div class="kv">Records: <b id="recs">0</b></div>
    <div class="kv">OTA: <b id="ota">-</b></div>
  </div>
  <div class="row" style="margin-top:6px">
    <div class="kv">Wired network: <b id="ethinfo">-</b></div>
    <div class="kv">Wireless (AP): <b id="apinfo">-</b></div>
  </div>
</div>

<div class="card">
  <div class="kv" style="margin-bottom:8px">Graph of PM1.0 / PM2.5 / PM10 changes (refreshes on every new record)</div>
  <div class="ctrl">
    <label class="kv">Range:
      <select id="range">
        <option value="0">all</option>
        <option value="300" selected>last 5 min</option>
        <option value="1800">last 30 min</option>
        <option value="3600">last 1 h</option>
        <option value="21600">last 6 h</option>
        <option value="86400">last 24 h</option>
      </select>
    </label>
    <label class="kv">From: <input type="datetime-local" id="from" step="1"></label>
    <label class="kv">To: <input type="datetime-local" id="till" step="1"></label>
    <button id="apply">Apply range</button>
    <button id="live">Live</button>
    <button id="csv">Download CSV</button>
    <button id="png">Download PNG</button>
  </div>
  <div class="muted" id="rangeinfo" style="margin-bottom:6px">-</div>
  <canvas id="chart"></canvas>
  <div class="muted" id="chartinfo" style="margin-bottom:10px">-</div>
  <canvas id="chartEnv"></canvas>
  <div class="muted" id="chartinfoEnv">-</div>
</div>

<div class="card">
  <div class="muted">Current values (updated every 5 s)</div>
  <div class="kv" id="cage" style="margin:6px 0">sensor: -</div>
  <div class="kv" id="dhtstate" style="margin:0 0 6px">AM2302: -</div>
  <div class="vals" id="cvals">
    <div class="val"><div class="muted">PM1.0</div><div class="big" id="c1">-</div></div>
    <div class="val"><div class="muted">PM2.5</div><div class="big" id="c25">-</div></div>
    <div class="val"><div class="muted">PM10</div><div class="big" id="c10">-</div></div>
    <div class="val"><div class="muted">Temperature &deg;C</div><div class="big" id="ctemp">-</div></div>
    <div class="val"><div class="muted">Humidity %</div><div class="big" id="chum">-</div></div>
    <div class="val"><div class="muted">Dew point &deg;C</div><div class="big" id="cdew">-</div></div>
  </div>
  <div class="muted" id="cupd">-</div>
</div>

<div class="card">
  <div class="kv" style="margin-bottom:6px">Files on the card</div>
  <table>
    <thead><tr><th>File</th><th>Size</th><th>Actions</th></tr></thead>
    <tbody id="fbody"></tbody>
  </table>
  <div class="ctrl" style="margin-top:10px">
    <button id="prevpage">Previous</button>
    <span class="muted" id="filepage">-</span>
    <button id="nextpage">Next</button>
  </div>
</div>

<div id="modal" class="modal">
  <div class="modalbox">
    <div class="modalhead">
      <b>Reference PM2.5 / PM10 values</b>
      <a href="#" id="close">&times; close</a>
    </div>
    <div class="modalbody">

      <h3>Baseline (reference)</h3>
      <table>
        <tr><th>Situation</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Clean indoor air</td><td>5 - 15</td></tr>
        <tr><td>Urban outdoor background</td><td>10 - 30</td></tr>
        <tr><td>Polluted city / smog</td><td>50 - 150</td></tr>
        <tr><td>Cooking without an extractor hood</td><td>50 - 300</td></tr>
      </table>

      <h3>Cigarette smoke</h3>
      <table>
        <tr><th>Position</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Right next to the smoke (within 1 m)</td><td>300 - 1500+</td></tr>
        <tr><td>Room average, one cigarette, closed room</td><td>20 - 100</td></tr>
        <tr><td>Room average, several cigarettes, smoky</td><td>150 - 500</td></tr>
        <tr><td>Small closed space, continuous smoking</td><td>300 - 1000</td></tr>
      </table>
      <p style="margin:4px 0 0">Rises within seconds, falls over 5 - 30 min depending on ventilation.</p>

      <h3>Fire smoke</h3>
      <table>
        <tr><th>Situation</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Match, blown-out candle, incense</td><td>100 - 1000 (short peak)</td></tr>
        <tr><td>Fireplace / wood stove in the room</td><td>50 - 500</td></tr>
        <tr><td>Wildfire smoke outdoors, visible smoke</td><td>200 - 1000+</td></tr>
        <tr><td>Dense smoke, close range</td><td>1000 - 10000+</td></tr>
        <tr><td>Developed fire in a room</td><td>tens of thousands (sensor saturated)</td></tr>
      </table>

      <h3>Temperature / humidity reference (AM2302)</h3>
      <table>
        <tr><th>Condition</th><th>Value</th></tr>
        <tr><td>Comfortable living space</td><td>20 - 24 &deg;C, 40 - 60 % RH</td></tr>
        <tr><td>Too dry (dry eyes and throat, static)</td><td>below 30 % RH</td></tr>
        <tr><td>Too humid (mould risk)</td><td>above 60 % RH, high above 70 %</td></tr>
        <tr><td>Condensation on cold surfaces</td><td>dew point within ~3 &deg;C of the surface</td></tr>
      </table>
      <ul>
        <li><b>Dew point:</b> below 10 &deg;C comfortable, 16 - 20 &deg;C muggy,
            above 20 &deg;C oppressive.</li>
        <li><b>Sensor accuracy:</b> AM2302 is specified as &plusmn;0.5 &deg;C and
            &plusmn;2 - 5 % RH, with a resolution of 0.1.</li>
        <li><b>Polling:</b> it is read every 3 s here; do not poll it faster than
            once every 2 s.</li>
        <li><b>Mould:</b> grows when relative humidity next to a surface stays above
            ~80 % for days - watch the dew point, not only the RH.</li>
        <li><b>Note:</b> the AM2302 is not suitable for condensing environments.</li>
      </ul>

      <h3>PMS5003 sensor limits</h3>
      <ul>
        <li>Effective range: <b>0 - 500 &micro;g/m&sup3;</b>, maximum around
            <b>1000 &micro;g/m&sup3;</b>.</li>
        <li>Above that readings are unreliable and saturate - typically the value
            &quot;sticks&quot; to a constant (e.g. ~1000) while the smoke lasts.</li>
        <li>Cigarette smoke and moderate fire smoke are measured well; dense
            smoke indoors is seen only as a maximum.</li>
      </ul>

      <h3>Telling smoke types apart from the log</h3>
      <ul>
        <li><b>PM2.5 / PM10 ratio:</b> ~0.9 - 1.0 = fine aerosol (cigarette, fresh
            smoke, exhaust); ~0.5 - 0.8 = coarse fraction present (dust, ash,
            smoke close to the source).</li>
        <li><b>Rise time:</b> cigarette and match jump within seconds, dust rises
            gradually.</li>
        <li><b>Decay time:</b> cigarette smoke in a ventilated room halves in
            1 - 5 min; dust settles more slowly.</li>
      </ul>

      <h3>Humidity note</h3>
      <p style="margin:4px 0">The PMS5003 has no heated inlet, so above roughly 80 %
        relative humidity (fog, rain, early morning outdoors) it counts droplets as
        particles and shows falsely high values. For outdoor installations this is
        the most common source of &quot;phantom smoke&quot; in the data.</p>

      <h3>Practical chain test</h3>
      <p style="margin:4px 0">Light and blow out a match ~30 cm from the sensor - the
        log should show a peak within a few seconds and a gradual fall over
        1 - 3 minutes.</p>

    </div>
  </div>
</div>

<script>
var selFile = "";
var lastCount = -1;
var rows = [];
var srcLabel = "NTP";
var rangeSec = 300;                 // default range: last 5 minutes
var fromEpoch = 0;
var tillEpoch = 0;
var mode = "file";                  // "file" = single file, "range" = history range
var boardEpoch = 0;                 // current board time
var currentFile = "";               // active file on the board
var rangeFrom = 0;
var rangeTill = 0;
var lastRangeLoad = 0;
var rangeInitialised = false;
var currentTheme = "light";
var allFiles = [];
var filePage = 1;
var filesPerPage = 15;
var lastFilesVer = -1;              // /api/status "filesVer": file list changed?
var lastStatus = null;              // last /api/status payload (reused below)

function q(id){ return document.getElementById(id); }
function getJSON(u){ return fetch(u, {cache:"no-store"}).then(function(r){ return r.json(); }); }

function fmtShort(ts){
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+"s";
}

function hasEpochTime(){ return (srcLabel === "NTP" || srcLabel === "MANUAL"); }

function fmtFull(ts){
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    return ("0"+d.getDate()).slice(-2)+"."+("0"+(d.getMonth()+1)).slice(-2)+"."+d.getFullYear()+
           " "+("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+" s";
}

function fmtAxis(ts, span){
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    if (span > 6*3600) {
      return ("0"+d.getDate()).slice(-2)+"."+("0"+(d.getMonth()+1)).slice(-2)+" "+("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2);
    }
    return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+"s";
}

function rangeLabel(){
  if (rows.length > 1 && hasEpochTime()) {
    return fmtFull(rows[0][0]) + "  -  " + fmtFull(rows[rows.length-1][0]);
  }
  if (mode === "range" && rangeFrom) {
    return fmtFull(rangeFrom) + "  -  " + (rangeTill ? fmtFull(rangeTill) : "now");
  }
  return "single file";
}

/* Filter: the custom from-to range first (epoch only), then the quick range */
function applyRange(){
  return rows.slice();   // the rows already are exactly the requested range
}

function pollStatus(){
  getJSON("/api/status").then(function(s){
    lastStatus = s;
    if (s.filesVer !== undefined && s.filesVer !== lastFilesVer) {
      lastFilesVer = s.filesVer;      // the card changed: refresh the file table
      loadFiles();
    }
    q("clock").textContent = s.time;
    q("tsrc").textContent  = s.source;
    q("recs").textContent  = s.records;
    q("ota").textContent   = s.ota ? s.ota : "-";
    q("boot").textContent  = s.boot;
    if (s.theme && s.theme !== currentTheme) {
      currentTheme = s.theme;
      document.body.className = "theme-" + currentTheme;
      draw();
    }
    q("ethinfo").textContent = s.ethActive
        ? (s.ethIp + " / " + s.ethMask + "  gw " + s.ethGw + (s.ethDhcp ? "  (DHCP)" : "  (static)"))
        : "not active";
    q("apinfo").textContent = s.apEnabled
        ? (s.apSsid + "  " + s.apIp + "  channel " + s.apChannel + "  clients: " + s.apClients)
        : "disabled";
    renderSensorState(s);
    renderDhtState(s);
    if (s.epoch > 1000000000) { boardEpoch = s.epoch; }
    currentFile = s.file;
    if (selFile === "") { selFile = s.file; q("file").textContent = selFile; }

    if (!rangeInitialised && boardEpoch > 0) {
      rangeInitialised = true;
      fillRangeInputs(rangeSec > 0 ? rangeSec : 300);
      applyCustomRange();
    }

    if (s.records !== lastCount) {
      lastCount = s.records;
      if (mode === "file" && selFile === s.file) { loadData(true); }
    }
  }).catch(function(){});
}

function renderSensorState(s){
  var el = q("cage");
  var vals = q("cvals");
  var age = (typeof s.ageSec === "number") ? s.ageSec : -1;
  var frames = "  |  frames: " + s.framesOk + " (rejected " + s.framesBad + ")";

  if (!s.current || s.current.ts === 0) {
    el.textContent = "sensor: no data yet" + frames;
    el.style.color = "#b91c1c";
    vals.className = "vals stale";
  } else if (age >= 0 && age < 5) {
    el.textContent = "sensor: OK (last sample " + age + " s ago)" + frames;
    el.style.color = "#15803d";
    vals.className = "vals";
  } else if (age >= 0 && age < 15) {
    el.textContent = "sensor: delayed (last sample " + age + " s ago)" + frames;
    el.style.color = "#b45309";
    vals.className = "vals stale";
  } else {
    el.textContent = "sensor: NO DATA for " + age + " s" + frames;
    el.style.color = "#b91c1c";
    vals.className = "vals stale";
  }
}

function renderDhtState(s){
  var el = q("dhtstate");
  if (!s.dhtOk) {
    el.textContent = "AM2302: no data yet (failed reads: " + s.dhtBad + ")";
    el.style.color = "#b91c1c";
    return;
  }
  var age = (typeof s.dhtAgeSec === "number") ? s.dhtAgeSec : -1;
  var txt = "AM2302: " + s.current.temp + " \u00b0C / " + s.current.hum + " %";
  if (age >= 0 && age < 15) { el.style.color = "#15803d"; txt += "  (read " + age + " s ago)"; }
  else { el.style.color = "#b45309"; txt += "  (last read " + age + " s ago)"; }
  el.textContent = txt;
}

/* Refreshes the value tiles every 5 s from the last /api/status payload: the
   same numbers arrive with the 2 s status poll, so a second request per tick
   would only add traffic. */
function renderCurrent(){
  var s = lastStatus;
  if (!s) { return; }
  if (s.current) {
    q("c1").textContent  = s.current.pm1;
    q("c25").textContent = s.current.pm25;
    q("c10").textContent = s.current.pm100;
    q("ctemp").textContent = (s.current.temp === null || s.current.temp === undefined) ? "-" : s.current.temp;
    q("chum").textContent  = (s.current.hum  === null || s.current.hum  === undefined) ? "-" : s.current.hum;
    q("cdew").textContent  = (s.current.temp === null || s.current.temp === undefined || s.current.hum === null || s.current.hum === undefined)
                             ? "-" : dewPoint(parseFloat(s.current.temp), parseFloat(s.current.hum)).toFixed(1);
  }
  q("cupd").textContent = "read at: " + s.time + " (every 5 s)";
}

function loadRange(){
  var qs = "/api/data?from=" + rangeFrom + "&to=" + rangeTill;
  getJSON(qs).then(function(d){
    rows = d.rows || [];
    srcLabel = d.source || "NTP";
    lastRangeLoad = Date.now()/1000;
    var nf = d.files ? d.files.split(",").length : 0;
    var info = "history: " + nf + (nf === 1 ? " file" : " files");
    if (d.files) { info += " (" + d.files + ")"; }
    info += "  |  records: " + rows.length;
    if (d.decim > 1) { info += " of " + d.total + " (decimated x" + d.decim + ")"; }
    info += "  |  time source: " + srcLabel;
    q("chartinfo").textContent = info;
    q("file").textContent = "history";
    q("live").className = "";
    draw();
  }).catch(function(){});
}

/* Live view. The first load fetches the whole RAM buffer of the active file;
   every later refresh adds "since" (the newest timestamp already on screen), so
   the board answers with the new rows only - a few hundred bytes instead of the
   whole buffer. Whenever the answer cannot be appended safely (another file,
   buffer rotated, clock re-dated) the response replaces the chart instead. */
var liveSince   = 0;      // newest timestamp the chart already holds
var liveLoading = false;  // one refresh at a time (responses must stay ordered)

function loadData(incremental){
  if (liveLoading) { return; }
  var url = "/api/data?f=" + encodeURIComponent(selFile);
  if (incremental && liveSince > 0) { url += "&since=" + liveSince; }
  liveLoading = true;
  getJSON(url).then(function(d){
    if (!d || !d.rows) { return; }      // error answer: leave the chart as it is
    var got    = d.rows || [];
    var oldest = (d.oldest === undefined) ? 0 : d.oldest;
    if (incremental && liveSince > 0 && oldest > 0 && liveSince >= oldest &&
        d.file === selFile) {
      Array.prototype.push.apply(rows, got);      // only the new rows arrived
    } else {
      rows = got;                                 // full reload
    }
    if (oldest > 0) {                             // drop what the board dropped
      var keep = [];
      for (var i = 0; i < rows.length; i++) {
        if (rows[i][0] >= oldest) { keep.push(rows[i]); }
      }
      rows = keep;
    }
    if (rows.length) { liveSince = rows[rows.length - 1][0]; }
    srcLabel = d.source || "NTP";
    q("live").className = "on";
    q("chartinfo").textContent = d.file + "  |  total records: " + rows.length +
                                 "  |  time source: " + srcLabel;
    draw();
  }).catch(function(){}).then(function(){ liveLoading = false; });
}

function loadFiles(){
  getJSON("/api/files").then(function(d){
    allFiles = (d.files || []).slice().sort(function(a, b){
      if (a.name < b.name) return 1;
      if (a.name > b.name) return -1;
      return 0;
    });
    renderFiles();
  }).catch(function(){});
}

/* Shows up to filesPerPage files (newest first); the rest are on later pages */
function renderFiles(){
  var tb = q("fbody");
  tb.innerHTML = "";
  var pages = Math.max(1, Math.ceil(allFiles.length / filesPerPage));
  if (filePage > pages) filePage = pages;
  if (filePage < 1) filePage = 1;
  var start = (filePage - 1) * filesPerPage;
  allFiles.slice(start, start + filesPerPage).forEach(function(f){
    var tr = document.createElement("tr");

    var td1 = document.createElement("td");
    td1.textContent = f.name;
    tr.appendChild(td1);

    var td2 = document.createElement("td");
    td2.textContent = f.size + " B";
    tr.appendChild(td2);

    var td3 = document.createElement("td");

    var a1 = document.createElement("a");
    a1.textContent = "graph";
    a1.href = "#";
    a1.onclick = function(e){
      e.preventDefault();
      selFile = f.name;
      mode = "file";
      fromEpoch = 0; tillEpoch = 0; rangeSec = 0; rangeFrom = 0; rangeTill = 0;
      q("from").value = ""; q("till").value = ""; q("range").value = "0";
      lastCount = -1;
      liveSince = 0;                        // a different file: full reload
      q("file").textContent = selFile;
      loadData();
    };
    td3.appendChild(a1);
    td3.appendChild(document.createTextNode(" | "));

    var a2 = document.createElement("a");
    a2.textContent = "download";
    a2.href = "/download?f=" + encodeURIComponent(f.name);
    td3.appendChild(a2);

    if (f.active) {
      td3.appendChild(document.createTextNode(" | (active)"));
    } else {
      td3.appendChild(document.createTextNode(" | "));
      var form = document.createElement("form");
      form.method = "POST";
      form.action = "/delete";
      form.style.display = "inline";
      form.onsubmit = function(){ return confirm("Delete " + f.name + " ?"); };
      var hidden = document.createElement("input");
      hidden.type = "hidden";
      hidden.name = "f";
      hidden.value = f.name;
      form.appendChild(hidden);
      var del = document.createElement("button");
      del.type = "submit";
      del.textContent = "delete";
      del.className = "del";
      form.appendChild(del);
      td3.appendChild(form);
    }

    tr.appendChild(td3);
    tb.appendChild(tr);
  });
  q("filepage").textContent = "Page " + filePage + " / " + pages + "   (" + allFiles.length + " files)";
  q("prevpage").disabled = (filePage <= 1);
  q("nextpage").disabled = (filePage >= pages);
}

/* Colour palette per theme (also used for the exported PNG) */
function palette(){
  if (currentTheme === "dark") {
    return {bg:"#171d26", grid:"#2a3342", axis:"#5b6a80", label:"#94a3b8", text:"#e7ebf2",
            pm1:"#22c55e", pm25:"#f97316", pm10:"#ef4444", temp:"#a78bfa", hum:"#22d3ee"};
  }
  if (currentTheme === "contrast") {
    return {bg:"#000000", grid:"#3a3a3a", axis:"#ffffff", label:"#ffd400", text:"#ffffff",
            pm1:"#00ff66", pm25:"#ffcc00", pm10:"#ff3b30", temp:"#c084fc", hum:"#22d3ee"};
  }
  return {bg:"#ffffff", grid:"#e6e8eb", axis:"#aaa", label:"#666", text:"#333",
          pm1:"#16a34a", pm25:"#ea580c", pm10:"#dc2626", temp:"#7c3aed", hum:"#0891b2"};
}

/* Draw the chart into the given context */
function drawChart(g, L, T, pw, ph, data, fs, lw){
  g.font = fs + "px Arial";
  var pal = palette();

  if (!data.length) {
    g.fillStyle = pal.label;
    g.fillText("No records in the selected range.", L, T + fs + 4);
    return;
  }

  var xmin = data[0][0], xmax = data[data.length-1][0];
  if (xmax <= xmin) xmax = xmin + 1;
  var ymax = 10, i, v, t, x, y;
  data.forEach(function(r){ ymax = Math.max(ymax, r[1], r[2], r[3]); });
  ymax = Math.ceil(ymax * 1.15);

  function X(tt){ return L + (tt - xmin) * pw / (xmax - xmin); }
  function Y(vv){ return T + ph - vv * ph / ymax; }

  g.strokeStyle = pal.grid;
  g.fillStyle = pal.label;
  g.lineWidth = 1;
  for (i = 0; i <= 4; i++) {
    v = ymax * i / 4; y = Y(v);
    g.beginPath(); g.moveTo(L, y); g.lineTo(L + pw, y); g.stroke();
    g.textAlign = "right"; g.fillText(v.toFixed(0), L - 6, y + fs*0.35);
  }
  for (i = 0; i <= 4; i++) {
    t = xmin + (xmax - xmin) * i / 4; x = X(t);
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, T + ph); g.stroke();
    g.textAlign = "center"; g.fillText(fmtAxis(Math.round(t), xmax - xmin), x, T + ph + fs + 4);
  }

  g.strokeStyle = pal.axis;
  g.beginPath(); g.moveTo(L, T); g.lineTo(L, T + ph); g.lineTo(L + pw, T + ph); g.stroke();

  /* Y axis title */
  g.save();
  g.translate(fs * 0.95, T + ph / 2);
  g.rotate(-Math.PI / 2);
  g.textAlign = "center";
  g.fillStyle = pal.label;
  g.fillText("\u00b5g/m\u00b3", 0, 0);
  g.restore();

  var series = [
    {idx:1, col:pal.pm1,  name:"PM1.0"},
    {idx:2, col:pal.pm25, name:"PM2.5"},
    {idx:3, col:pal.pm10, name:"PM10"}
  ];

  series.forEach(function(s){
    g.strokeStyle = s.col;
    g.lineWidth = lw;
    g.beginPath();
    data.forEach(function(r, k){
      var px = X(r[0]), py = Y(r[s.idx]);
      if (k === 0) g.moveTo(px, py); else g.lineTo(px, py);
    });
    g.stroke();
    g.fillStyle = s.col;
    data.forEach(function(r){
      g.beginPath();
      g.arc(X(r[0]), Y(r[s.idx]), lw + 0.6, 0, 6.2832);
      g.fill();
    });
  });

  var lx = L;
  series.forEach(function(s){
    g.fillStyle = s.col; g.fillRect(lx, T - fs*1.4, fs*0.9, fs*0.9);
    g.fillStyle = pal.text; g.textAlign = "left";
    g.fillText(s.name, lx + fs*1.2, T - fs*0.5);
    lx += fs * 6.4;
  });
}

/* Magnus formula dew point in degC from temperature (degC) and RH (%) */
function dewPoint(tc, rh){
  if (!(rh > 0)) return 0;
  var a = 17.27, b = 237.7;
  var al = Math.log(rh / 100.0) + (a * tc) / (b + tc);
  return (b * al) / (a - al);
}

/* Second chart: temperature and humidity (Y axis 0..100 covers % and degC) */
function drawEnvChart(g, L, T, pw, ph, data, fs, lw){
  g.font = fs + "px Arial";
  var pal = palette();

  var d = data.filter(function(r){ return r[4] !== null && r[4] !== undefined; });
  if (!d.length) {
    g.fillStyle = pal.label;
    g.fillText("No temperature / humidity data in the selected range.", L, T + fs + 4);
    return;
  }

  var xmin = data[0][0], xmax = data[data.length-1][0];
  if (xmax <= xmin) xmax = xmin + 1;

  /* temperature has its own auto-scaled left axis, humidity a fixed 0..100 right axis */
  var tlo = d[0][4], thi = d[0][4], i, y;
  d.forEach(function(r){ tlo = Math.min(tlo, r[4]); thi = Math.max(thi, r[4]); });
  tlo = Math.floor(tlo - 1);
  thi = Math.ceil(thi + 1);
  if (thi - tlo < 4) { tlo -= 2; thi = tlo + 4; }
  var hlo = 0, hhi = 100;

  function X(tt){ return L + (tt - xmin) * pw / (xmax - xmin); }
  function YT(vv){ return T + ph - (vv - tlo) * ph / (thi - tlo); }
  function YH(vv){ return T + ph - (vv - hlo) * ph / (hhi - hlo); }

  g.strokeStyle = pal.grid;
  g.lineWidth = 1;
  for (i = 0; i <= 4; i++) {
    y = T + ph - ph * i / 4;
    g.beginPath(); g.moveTo(L, y); g.lineTo(L + pw, y); g.stroke();
    g.fillStyle = pal.temp; g.textAlign = "right";
    g.fillText((tlo + (thi - tlo) * i / 4).toFixed(1), L - 6, y + fs*0.35);
    g.fillStyle = pal.hum; g.textAlign = "left";
    g.fillText((hlo + (hhi - hlo) * i / 4).toFixed(0), L + pw + 8, y + fs*0.35);
  }
  for (i = 0; i <= 4; i++) {
    var tt = xmin + (xmax - xmin) * i / 4;
    var xx = X(tt);
    g.beginPath(); g.moveTo(xx, T); g.lineTo(xx, T + ph); g.stroke();
    g.fillStyle = pal.label; g.textAlign = "center";
    g.fillText(fmtAxis(Math.round(tt), xmax - xmin), xx, T + ph + fs + 4);
  }
  g.strokeStyle = pal.axis;
  g.beginPath(); g.moveTo(L, T); g.lineTo(L, T + ph); g.lineTo(L + pw, T + ph); g.stroke();

  g.fillStyle = pal.temp; g.textAlign = "left";
  g.fillText("\u00b0C", 6, T + 10);
  g.fillStyle = pal.hum; g.textAlign = "right";
  g.fillText("%", L + pw + 34, T + 10);

  var series = [
    {idx:4, col:pal.temp, name:"Temperature \u00b0C", Y:YT},
    {idx:5, col:pal.hum,  name:"Humidity %",      Y:YH}
  ];

  series.forEach(function(s){
    g.strokeStyle = s.col;
    g.lineWidth = lw;
    g.beginPath();
    var prvi = true;
    d.forEach(function(r){
      var vv = r[s.idx];
      if (vv === null || vv === undefined) { prvi = true; return; }
      var px = X(r[0]), py = s.Y(vv);
      if (prvi) { g.moveTo(px, py); prvi = false; } else { g.lineTo(px, py); }
    });
    g.stroke();
    g.fillStyle = s.col;
    d.forEach(function(r){
      var vv = r[s.idx];
      if (vv === null || vv === undefined) return;
      g.beginPath();
      g.arc(X(r[0]), s.Y(vv), lw + 0.6, 0, 6.2832);
      g.fill();
    });
  });

  var lx = L;
  series.forEach(function(s){
    g.fillStyle = s.col; g.fillRect(lx, T - fs*1.4, fs*0.9, fs*0.9);
    g.fillStyle = pal.text; g.textAlign = "left";
    g.fillText(s.name, lx + fs*1.2, T - fs*0.5);
    lx += fs * 12;
  });
}

function draw(){
  var cv = q("chart");
  var w = cv.clientWidth || 640;
  var h = cv.clientHeight || 320;
  var dpr = window.devicePixelRatio || 1;
  cv.width = Math.round(w*dpr);
  cv.height = Math.round(h*dpr);

  var g = cv.getContext("2d");
  g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,w,h);

  var d = applyRange();
  var info = "showing " + d.length + " of " + rows.length + " records";
  if (d.length > 1) { info += "  |  range: " + fmtFull(d[0][0]) + "  -  " + fmtFull(d[d.length-1][0]); }
  if (mode === "range") { info += "  |  requested: " + rangeLabel(); }
  q("rangeinfo").textContent = info;

  drawChart(g, 52, 26, w-52-10, h-26-28, d, 12, 2);

  var cve = q("chartEnv");
  var we = cve.clientWidth || 640;
  var he = cve.clientHeight || 220;
  cve.width = Math.round(we*dpr);
  cve.height = Math.round(he*dpr);
  var ge = cve.getContext("2d");
  ge.setTransform(dpr,0,0,dpr,0,0);
  ge.clearRect(0,0,we,he);
  drawEnvChart(ge, 52, 26, we-52-46, he-26-28, d, 12, 2);
  var envRows = d.filter(function(r){ return r[4] !== null && r[4] !== undefined; });
  var envTxt = "temperature / humidity: " + envRows.length + " of " + d.length + " records";
  if (envRows.length) {
    var tmin = envRows[0][4], tmax = envRows[0][4], hmin = envRows[0][5], hmax = envRows[0][5], ts = 0, hs = 0;
    envRows.forEach(function(r){
      tmin = Math.min(tmin, r[4]); tmax = Math.max(tmax, r[4]);
      hmin = Math.min(hmin, r[5]); hmax = Math.max(hmax, r[5]);
      ts += r[4]; hs += r[5];
    });
    var tavg = ts / envRows.length, havg = hs / envRows.length;
    envTxt += "  |  temp min/avg/max: " + tmin.toFixed(1) + " / " + tavg.toFixed(1) + " / " + tmax.toFixed(1) + " \u00b0C";
    envTxt += "  |  humidity min/avg/max: " + hmin.toFixed(1) + " / " + havg.toFixed(1) + " / " + hmax.toFixed(1) + " %";
    envTxt += "  |  dew point (avg): " + dewPoint(tavg, havg).toFixed(1) + " \u00b0C";
  }
  q("chartinfoEnv").textContent = envTxt;
}

function exportPNG(){
  var d = applyRange();
  var w = 1200, h = 880;
  var cv = document.createElement("canvas");
  cv.width = w; cv.height = h;
  var g = cv.getContext("2d");

  var pal = palette();
  g.fillStyle = pal.bg; g.fillRect(0, 0, w, h);
  g.fillStyle = pal.text; g.font = "bold 20px Arial"; g.textAlign = "left";
  g.fillText("PMS5003 - " + (mode === "range" ? "history" : selFile), 24, 34);
  g.font = "14px Arial"; g.fillStyle = pal.label;
  g.fillText("Range: " + rangeLabel() + "  |  records: " + d.length +
             "  |  time source: " + srcLabel +
             "  |  exported: " + new Date().toLocaleString(), 24, 58);

  drawChart(g, 70, 100, w - 70 - 40, 320, d, 14, 2.4);

  g.fillStyle = pal.text; g.font = "bold 16px Arial"; g.textAlign = "left";
  g.fillText("Temperature / humidity", 24, 480);
  drawEnvChart(g, 70, 505, w - 70 - 60, 320, d, 14, 2.4);

  var a = document.createElement("a");
  var ts = new Date().toISOString().replace(/[:T]/g, "-").slice(0, 19);
  a.download = "pms_chart_" + ts + ".png";
  a.href = cv.toDataURL("image/png");
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

/* Live view: chart of the active file, refreshed on every new record */
function goLive(){
  mode = "file";
  if (currentFile) { selFile = currentFile; }
  rangeFrom = 0;
  rangeTill = 0;
  lastCount = -1;
  liveSince = 0;                            // back to live: reload the buffer
  q("file").textContent = selFile;
  loadData();
}

/* Downloads the records of the From/To range as CSV (the chart is untouched) */
function downloadCsv(){
  var fromV = q("from").value;
  var tillV = q("till").value;
  if (!fromV || !tillV) { q("chartinfo").textContent = "Set both From and To before downloading."; return; }
  var fromEp = Math.floor(new Date(fromV).getTime()/1000);
  var tillEp = Math.floor(new Date(tillV).getTime()/1000);
  if (!(fromEp > 1000000000) || !(tillEp > 1000000000)) { q("chartinfo").textContent = "Invalid date/time."; return; }
  if (tillEp <= fromEp) { tillEp = fromEp + 60; }
  var a = document.createElement("a");
  a.href = "/export?from=" + fromEp + "&to=" + tillEp;
  a.download = "";
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

function pad2(v){ return (v < 10 ? "0" : "") + v; }

function toLocalInput(d){
  return d.getFullYear() + "-" + pad2(d.getMonth()+1) + "-" + pad2(d.getDate()) +
         "T" + pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
}

/* Fills From/To for a quick range (120 = 2 min, 300 = 5 min ...) */
function fillRangeInputs(sec){
  var now = new Date();
  q("from").value = toLocalInput(new Date(now.getTime() - sec*1000));
  q("till").value = toLocalInput(now);
}

/* Loads the chart for the From/To fields (quick ranges only fill them in) */
function applyCustomRange(){
  var fromV = q("from").value;
  var tillV = q("till").value;
  if (!fromV || !tillV) {
    q("chartinfo").textContent = "Set both From and To (or pick a quick range).";
    return;
  }
  var fromEp = Math.floor(new Date(fromV).getTime()/1000);
  var tillEp = Math.floor(new Date(tillV).getTime()/1000);
  if (!(fromEp > 1000000000) || !(tillEp > 1000000000)) {
    q("chartinfo").textContent = "Invalid date/time.";
    return;
  }
  if (tillEp <= fromEp) { tillEp = fromEp + 60; }

  mode = "range";
  rangeFrom = fromEp;
  rangeTill = tillEp;
  loadRange();
}


window.addEventListener("load", function(){
  /* Remember the selected range in the browser (localStorage) */
  try {
    var sp = localStorage.getItem("pms_range");
    if (sp !== null) {
      q("range").value = sp;
      rangeSec = parseInt(sp, 10) || 0;
    } else {
      q("range").value = "300";
      rangeSec = 300;
    }
  } catch (e) {}

  q("range").onchange = function(){
    var sec = parseInt(this.value, 10) || 0;
    try { localStorage.setItem("pms_range", this.value); } catch (e) {}
    if (sec > 0) { fillRangeInputs(sec); }   // only fills the fields, press Apply to load
  };
  q("apply").onclick = applyCustomRange;
  q("live").onclick = goLive;
  q("csv").onclick = downloadCsv;
  q("png").onclick = exportPNG;
  q("prevpage").onclick = function(){ filePage--; renderFiles(); };
  q("nextpage").onclick = function(){ filePage++; renderFiles(); };

  /* Modal with reference values */
  q("help").onclick = function(e){ e.preventDefault(); q("modal").className = "modal open"; };
  q("close").onclick = function(e){ e.preventDefault(); q("modal").className = "modal"; };
  q("modal").onclick = function(e){ if (e.target === this) q("modal").className = "modal"; };
  document.addEventListener("keydown", function(e){
    if (e.key === "Escape") q("modal").className = "modal";
  });

  pollStatus();
  loadFiles();
  setInterval(pollStatus, 2000);    // graph on change + networks + time
  setInterval(renderCurrent, 5000); // current values (from the last status poll)
  window.addEventListener("resize", draw);
});
</script>
</body>
</html>
)HTML";

// ============================== ADMIN PAGE ==============================

static const char ADMIN_STYLE[] PROGMEM = R"CSS(
:root{--bg:#f5f7fa;--card:#fff;--text:#111827;--muted:#6b7280;--line:#e5e7eb;--accent:#2563eb;--accent-dark:#1d4ed8;--r:14px}
*{box-sizing:border-box}
body{margin:0;padding:18px;background:var(--bg);color:var(--text);font:14px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);max-width:880px;margin:0 auto 14px;box-shadow:0 1px 2px rgba(16,24,40,.05),0 10px 28px rgba(16,24,40,.06);padding:18px}
h1{font-size:19px;font-weight:650;margin:0 0 6px;display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center}
h3{font-size:14.5px;font-weight:650;margin:20px 0 8px;padding-bottom:6px;border-bottom:1px solid var(--line)}
table{border-collapse:separate;border-spacing:0;font-size:13.5px;width:100%}
td{padding:6px 8px 6px 0;vertical-align:middle}
td:first-child{color:var(--muted);width:130px}
input,select{font:inherit;font-size:13px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:#fff;color:var(--text);width:100%;max-width:360px}
input[type=radio],input[type=checkbox]{width:auto;margin-right:6px;vertical-align:middle}
input:focus{outline:2px solid #eef4ff;border-color:var(--accent)}
button{font:inherit;font-size:13px;font-weight:600;padding:9px 16px;border:1px solid var(--accent);border-radius:10px;background:var(--accent);color:#fff;cursor:pointer;transition:.15s}
button:hover{background:var(--accent-dark);border-color:var(--accent-dark)}
a{color:var(--accent);text-decoration:none;font-weight:500}
a:hover{text-decoration:underline}
hr{border:none;border-top:1px solid var(--line);margin:22px 0}
.muted{color:var(--muted);font-size:12px}
p{margin:8px 0}
@media(max-width:640px){body{padding:10px}.card{padding:13px}td:first-child{width:auto}}
)CSS";

static bool requireAdminAuth() {
  if (server.authenticate(ADMIN_USER, settings.adminPassword)) {
    return true;
  }
  server.requestAuthentication(BASIC_AUTH, "ESP32-POE2 administration");
  return false;
}

static void handleAdminGet() {
  if (!requireAdminAuth()) return;
  String h = F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  h += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  h += F("<title>Administration - ESP32-POE2</title><style>");
  h += FPSTR(ADMIN_STYLE);
  h += F("</style></head><body class=\"theme-");
  h += String(settings.theme);
  h += F("\"><div class=\"card\">");
  h += F("<h1>Device administration <a href=\"/\">&larr; back to graph</a></h1>");

  // --- form 1: network settings ---
  h += F("<form method=\"POST\" action=\"/admin\">");
  h += F("<input type=\"hidden\" name=\"action\" value=\"network\">");
  h += F("<h3>Wired network (Ethernet)</h3>");
  h += F("<p><label><input type=\"radio\" name=\"ethdhcp\" value=\"1\"");
  if (settings.ethDhcp) h += F(" checked");
  h += F("> DHCP &nbsp; </label><label><input type=\"radio\" name=\"ethdhcp\" value=\"0\"");
  if (!settings.ethDhcp) h += F(" checked");
  h += F("> Static IP</label></p><table>");
  h += F("<tr><td>IP address</td><td><input name=\"ethip\" value=\"");
  h += String(settings.ethIp);
  h += F("\"></td></tr><tr><td>Netmask</td><td><input name=\"ethmaska\" value=\"");
  h += String(settings.ethMask);
  h += F("\"></td></tr><tr><td>Gateway</td><td><input name=\"ethgw\" value=\"");
  h += String(settings.ethGw);
  h += F("\"></td></tr><tr><td>DNS</td><td><input name=\"ethdns\" value=\"");
  h += String(settings.ethDns);
  h += F("\"></td></tr></table>");

  h += F("<h3>Wireless network (Wi-Fi AP)</h3>");
  h += F("<p><label><input type=\"checkbox\" name=\"ap\" value=\"1\"");
  if (settings.apEnabled) h += F(" checked");
  h += F("> Enabled</label></p><table>");
  h += F("<tr><td>SSID</td><td><input name=\"apssid\" value=\"");
  h += String(settings.apSsid);
  h += F("\"></td></tr><tr><td>Password</td><td><input name=\"aploz\" value=\"");
  h += String(settings.apPassword);
  h += F("\"> <span class=\"muted\">empty or &lt;8 chars = open network</span></td></tr>");
  h += F("<tr><td>Channel</td><td><input name=\"apkanal\" type=\"number\" min=\"1\" max=\"13\" value=\"");
  h += String(settings.apChannel);
  h += F("\"></td></tr></table>");

  h += F("<h3>NTP</h3><table><tr><td>Server</td><td><input name=\"ntp\" value=\"");
  h += String(settings.ntpServer);
  h += F("\"></td></tr></table>");

  h += F("<p class=\"muted\">Saving network settings restarts the device.</p>");
  h += F("<p><button type=\"submit\">Save network and restart</button></p>");
  h += F("</form>");

  // --- display / theme ---
  h += F("<hr><h3>Display</h3>");
  h += F("<form method=\"POST\" action=\"/admin\">");
  h += F("<input type=\"hidden\" name=\"action\" value=\"display\">");
  h += F("<p>Theme: <select name=\"theme\">");
  h += F("<option value=\"light\"");
  if (strcmp(settings.theme, "light") == 0) h += F(" selected");
  h += F(">Light</option><option value=\"dark\"");
  if (strcmp(settings.theme, "dark") == 0) h += F(" selected");
  h += F(">Dark</option><option value=\"contrast\"");
  if (strcmp(settings.theme, "contrast") == 0) h += F(" selected");
  h += F(">High contrast</option></select> ");
  h += F("<button type=\"submit\">Save theme</button></p></form>");

  // --- administration password ---
  h += F("<hr><h3>Administration access</h3>");
  h += F("<p class=\"muted\">HTTP user: admin. The same password protects ArduinoOTA. Use at least 8 characters.</p>");
  h += F("<form method=\"POST\" action=\"/admin\">");
  h += F("<input type=\"hidden\" name=\"action\" value=\"security\">");
  h += F("<p><input type=\"password\" name=\"adminpwd\" minlength=\"8\" maxlength=\"64\" autocomplete=\"new-password\" placeholder=\"New password\"> ");
  h += F("<button type=\"submit\">Change password</button></p></form>");

  // --- form 2: manual time ---
  // --- firmware update ---
  h += F("<h3>Firmware update</h3>");
  h += F("<p class=\"muted\">Upload a compiled .bin file. Do not power off the device during the update.</p>");
  h += F("<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">");
  h += F("<p><input type=\"file\" name=\"firmware\" accept=\".bin\"> <button type=\"submit\">Upload firmware</button></p>");
  h += F("</form>");
  h += F("<p class=\"muted\">ArduinoOTA hostname: ");
  h += String(OTA_HOSTNAME);
  h += F(", port 3232.</p>");

  h += F("<hr><h3>Manual time setting</h3>");
  h += F("<p class=\"muted\">Used when NTP is not available. The time is not kept across a restart.</p>");
  h += F("<form method=\"POST\" action=\"/admin\">");
  h += F("<input type=\"hidden\" name=\"action\" value=\"time\">");
  h += F("<p><input type=\"datetime-local\" name=\"manualtime\" step=\"1\"> ");
  h += F("<button type=\"submit\">Set time</button></p></form>");

  h += F("<p class=\"muted\">Current state: ");
  h += String(netMode);
  h += F(" | time source: ");
  h += String(timeSourceName());
  h += F(" | SD: ");
  h += (sdReady ? F("OK") : F("not available"));
  h += F(" | file: ");
  h += String(csvPath);
  h += F("</p>");

  h += F("</div></body></html>");

  server.send(200, "text/html; charset=utf-8", h);
}

static void handleAdminPost() {
  if (!requireAdminAuth()) return;
  String action = server.arg("action");
  String message;

  // Manual time
  if (server.hasArg("manualtime") && server.arg("manualtime").length() >= 16) {
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
    int parsed = sscanf(server.arg("manualtime").c_str(), "%d-%d-%dT%d:%d:%d",
                        &y, &mo, &d, &hh, &mi, &ss);
    if (parsed >= 5) {
      applyTimeZone();          // the field is local time: mktime() needs the zone
      struct tm t = {};
      t.tm_year  = y - 1900;
      t.tm_mon   = mo - 1;
      t.tm_mday  = d;
      t.tm_hour  = hh;
      t.tm_min   = mi;
      t.tm_sec   = ss;
      t.tm_isdst = -1;
      time_t e = mktime(&t);
      if (e > (time_t)NTP_EPOCH_MIN) {
        setManualTime(e);
        message = F("The time has been set.");
      } else {
        message = F("Invalid date/time.");
      }
    } else {
      message = F("Invalid time format.");
    }
  }

  // Display theme
  if (action == "display") {
    String th = server.arg("theme");
    if (th == "light" || th == "dark" || th == "contrast") {
      strlcpy(settings.theme, th.c_str(), sizeof(settings.theme));
      saveSettings();
      message = F("Theme saved.");
    } else {
      message = F("Invalid theme.");
    }
  }

  // Administration and ArduinoOTA password
  if (action == "security") {
    String password = server.arg("adminpwd");
    if (password.length() >= 8 && password.length() <= 64) {
      strlcpy(settings.adminPassword, password.c_str(), sizeof(settings.adminPassword));
      saveSettings();
#if OTA_ENABLE
      ArduinoOTA.setPassword(settings.adminPassword);
#endif
      message = F("Administration password changed. Use the new password next time.");
    } else {
      message = F("Password must contain between 8 and 64 characters.");
    }
  }

  // Network settings
  if (action == "network") {
    bool changed = false;

    bool newDhcp = (server.arg("ethdhcp") == "1");
    if (newDhcp != settings.ethDhcp) { settings.ethDhcp = newDhcp; changed = true; }
    if (server.hasArg("ethip") && server.arg("ethip") != settings.ethIp) {
      strlcpy(settings.ethIp, server.arg("ethip").c_str(), sizeof(settings.ethIp)); changed = true;
    }
    if (server.hasArg("ethmaska") && server.arg("ethmaska") != settings.ethMask) {
      strlcpy(settings.ethMask, server.arg("ethmaska").c_str(), sizeof(settings.ethMask)); changed = true;
    }
    if (server.hasArg("ethgw") && server.arg("ethgw") != settings.ethGw) {
      strlcpy(settings.ethGw, server.arg("ethgw").c_str(), sizeof(settings.ethGw)); changed = true;
    }
    if (server.hasArg("ethdns") && server.arg("ethdns") != settings.ethDns) {
      strlcpy(settings.ethDns, server.arg("ethdns").c_str(), sizeof(settings.ethDns)); changed = true;
    }

    bool newAp = server.hasArg("ap");
    if (newAp != settings.apEnabled) { settings.apEnabled = newAp; changed = true; }
    if (server.hasArg("apssid") && server.arg("apssid").length() > 0 &&
        server.arg("apssid") != settings.apSsid) {
      strlcpy(settings.apSsid, server.arg("apssid").c_str(), sizeof(settings.apSsid)); changed = true;
    }
    if (server.hasArg("aploz") && server.arg("aploz") != settings.apPassword) {
      strlcpy(settings.apPassword, server.arg("aploz").c_str(), sizeof(settings.apPassword)); changed = true;
    }
    if (server.hasArg("apkanal")) {
      int channel = server.arg("apkanal").toInt();
      if (channel >= 1 && channel <= 13 && (uint8_t)channel != settings.apChannel) {
        settings.apChannel = (uint8_t)channel; changed = true;
      }
    }
    if (server.hasArg("ntp") && server.arg("ntp").length() > 0 &&
        server.arg("ntp") != settings.ntpServer) {
      strlcpy(settings.ntpServer, server.arg("ntp").c_str(), sizeof(settings.ntpServer)); changed = true;
    }

    saveSettings();
    if (changed) {
      message = F("Network settings saved. The device is restarting...");
      restartScheduled = millis();
      if (restartScheduled == 0) restartScheduled = 1;
    } else if (message.length() == 0) {
      message = F("No changes in the network settings.");
    }
  }

  String h = F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">");
  h += F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  h += F("<title>Administration</title><style>");
  h += FPSTR(ADMIN_STYLE);
  h += F("</style></head><body class=\"theme-");
  h += String(settings.theme);
  h += F("\"><div class=\"card\"><h1>");
  h += message;
  h += F("</h1><p><a href=\"/\">back to graph</a> &nbsp;|&nbsp; <a href=\"/admin\">administration</a></p>");
  h += F("</div></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

// ============================== WEB HANDLERS ==============================

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(PAGE_HTML);
  server.sendContent("");
}

// Serialises one record as a JSON array:
// [ts,pm1,pm25,pm100,temp,hum,time_source]
static void rowJson(char *out, size_t n, const LogRow &r, bool first) {
  char tbuf[16] = "null";
  char hbuf[16] = "null";
  if (!isnan(r.temp)) { snprintf(tbuf, sizeof(tbuf), "%.1f", r.temp); }
  if (!isnan(r.hum))  { snprintf(hbuf, sizeof(hbuf), "%.1f", r.hum);  }
  snprintf(out, n, "%s[%lu,%u,%u,%u,%s,%s,\"%s\"]", (first ? "" : ","),
           (unsigned long)r.ts, r.pm1, r.pm25, r.pm100, tbuf, hbuf,
           logTimeSourceName(r.source));
}

static const char *jsonBool(bool v) {
  return v ? "true" : "false";
}

static void handleStatus() {
  char now[32];
  char boot[24];
  formatNow(now, sizeof(now));
  formatEpochTs(bootEpoch, boot, sizeof(boot));

  uint32_t epoch = epochValid() ? (uint32_t)time(nullptr) : 0;

  // Formatted straight into one buffer: this endpoint is polled every two
  // seconds, and dozens of String concatenations per request would churn the
  // heap for no benefit. Only the values the ESP32 API returns as String are
  // materialised.
  String macS  = ETH.macAddress();
  String ipS   = ethActive ? ETH.localIP().toString()      : String("-");
  String maskS = ethActive ? ETH.subnetMask().toString()   : String("-");
  String gwS   = ethActive ? ETH.gatewayIP().toString()    : String("-");
  String apIpS = settings.apEnabled ? WiFi.softAPIP().toString() : String("-");
  const char *otaName =
#if OTA_ENABLE
      OTA_HOSTNAME;
#else
      "disabled";
#endif

  char j[1400];
  int n = snprintf(
      j, sizeof(j),
      "{\"records\":%lu,\"file\":\"%s\",\"net\":\"%s\",\"source\":\"%s\","
      "\"ntpAttempts\":%lu,\"redated\":%lu,\"ethLink\":%s,\"epoch\":%lu,"
      "\"time\":\"%s\",\"boot\":\"%s\",\"uptime\":%lu,\"ageSec\":%lu,"
      "\"framesOk\":%lu,\"framesBad\":%lu,\"dhtOk\":%s,\"dhtGood\":%lu,"
      "\"dhtBad\":%lu,\"dhtAgeSec\":%ld,\"filesVer\":%lu,\"heap\":%lu,"
      "\"ethActive\":%s,\"ethDhcp\":%s,\"ethIp\":\"%s\",\"ethMask\":\"%s\","
      "\"ethGw\":\"%s\",\"ethMac\":\"%s\","
      "\"apEnabled\":%s,\"apSsid\":\"%s\",\"apIp\":\"%s\",\"apChannel\":%u,"
      "\"apClients\":%u,\"ota\":\"%s\",\"theme\":\"%s\",\"current\":",
      (unsigned long)recordCount, csvPath, netMode, timeSourceName(),
      (unsigned long)ntpAttempts, (unsigned long)redatedRows,
      jsonBool(ETH.linkUp()), (unsigned long)epoch, now, boot,
      (unsigned long)(millis() / 1000UL),
      (unsigned long)((millis() - lastPmsMs) / 1000UL),
      (unsigned long)framesOk, (unsigned long)framesBad, jsonBool(dhtEverOk),
      (unsigned long)dhtGood, (unsigned long)dhtBad,
      (long)(dhtEverOk ? (long)((millis() - dhtLastGoodMs) / 1000UL) : -1L),
      (unsigned long)filesVersion, (unsigned long)ESP.getFreeHeap(),
      jsonBool(ethActive), jsonBool(settings.ethDhcp), ipS.c_str(),
      maskS.c_str(), gwS.c_str(), macS.c_str(),
      jsonBool(settings.apEnabled), settings.apSsid, apIpS.c_str(),
      (unsigned)settings.apChannel,
      (unsigned)(settings.apEnabled ? WiFi.softAPgetStationNum() : 0),
      otaName, settings.theme);

  if (n < 0 || n >= (int)sizeof(j)) {
    server.send(500, "text/plain", "status too long");
    return;
  }

  if (haveSample) {
    char tt[16] = "null";
    char hh[16] = "null";
    if (!isnan(dhtTempC))    { snprintf(tt, sizeof(tt), "%.1f", dhtTempC); }
    if (!isnan(dhtHumidity)) { snprintf(hh, sizeof(hh), "%.1f", dhtHumidity); }
    n += snprintf(j + n, sizeof(j) - n,
                  "{\"ts\":%lu,\"pm1\":%u,\"pm25\":%u,\"pm100\":%u,"
                  "\"temp\":%s,\"hum\":%s}}",
                  (unsigned long)lastTs, (unsigned)lastPm1, (unsigned)lastPm25,
                  (unsigned)lastPm100, tt, hh);
  } else {
    n += snprintf(j + n, sizeof(j) - n,
                  "{\"ts\":0,\"pm1\":\"-\",\"pm25\":\"-\",\"pm100\":\"-\","
                  "\"temp\":null,\"hum\":null}}");
  }
  if (n < 0 || n >= (int)sizeof(j)) {
    server.send(500, "text/plain", "status too long");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void handleData() {
  String fromArg = server.arg("from");
  String tillArg = server.arg("to");

  // History query: data from every file covering the range
  if (fromArg.length() > 0 || tillArg.length() > 0) {
    uint32_t from = fromArg.length() ? (uint32_t)strtoul(fromArg.c_str(), nullptr, 10) : 0;
    uint32_t till = tillArg.length() ? (uint32_t)strtoul(tillArg.c_str(), nullptr, 10) : 0;
    if (from > 0 && till > 0 && till < from) {
      uint32_t t = from; from = till; till = t;
    }

    uint32_t t0 = millis();
    if (sdFs == nullptr || !loadHistoryRange(from, till)) {
      server.send(500, "application/json", "{\"error\":\"cannot read the card\"}");
      return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    char buf[400];   // header + the file list can get long
    uint32_t queryMs = millis() - t0;
    snprintf(buf, sizeof(buf),
             "{\"mode\":\"range\",\"from\":%lu,\"to\":%lu,\"source\":\"%s\","
             "\"count\":%d,\"total\":%lu,\"decim\":%d,\"ms\":%lu,\"files\":\"%s\",\"rows\":[",
             (unsigned long)from, (unsigned long)till, timeSourceName(),
             histCount, (unsigned long)histTotal, histDecim, (unsigned long)queryMs, histFiles.c_str());
    server.sendContent(buf);

    char row[96];
    char out[2048];
    size_t pos = 0;
    for (int i = 0; i < histCount; i++) {
      rowJson(row, sizeof(row), histBuf[i], (i == 0));
      size_t lr = strlen(row);
      if (pos + lr > sizeof(out) - 1) {
        out[pos] = 0;
        server.sendContent(out);
        delay(1);              // let the TCP stack drain between blocks
        pos = 0;
      }
      memcpy(out + pos, row, lr);
      pos += lr;
    }
    if (pos > 0) {
      out[pos] = 0;
      server.sendContent(out);
    }
    server.sendContent("]}");
    server.sendContent("");
    return;
  }

  String file = server.arg("f");
  if (file.length() == 0) {
    file = String(csvPath);
  }
  if (!file.startsWith("/")) {
    file = "/" + file;
  }

  if (sdFs == nullptr || file.length() < 3) {
    server.send(400, "application/json", "{\"error\":\"invalid file\"}");
    return;
  }
  if (!loadIntoRing(file)) {
    server.send(404, "application/json", "{\"error\":\"file could not be opened\"}");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  // "since" is the newest timestamp the page already holds: answer with only the
  // rows after it, so a live refresh costs a few hundred bytes instead of the
  // whole buffer. "oldest" tells the page whether the buffer still contains
  // everything it missed (it reloads completely when it does not).
  const String   sinceArg = server.arg("since");
  const uint32_t since    = sinceArg.length() ? (uint32_t)strtoul(sinceArg.c_str(), nullptr, 10) : 0;
  const uint32_t oldest   = (ringCount > 0) ? ring[ringHead].ts : 0;
  const uint32_t newest   = (ringCount > 0) ? ring[(ringHead + ringCount - 1) % RING_MAX].ts : 0;

  char buf[224];
  snprintf(buf, sizeof(buf),
           "{\"file\":\"%s\",\"source\":\"%s\",\"count\":%d,\"oldest\":%lu,"
           "\"newest\":%lu,\"rows\":[",
           file.c_str(), timeSourceName(), ringCount,
           (unsigned long)oldest, (unsigned long)newest);
  server.sendContent(buf);

  char row[96];
  char out[2048];
  size_t pos = 0;
  bool first = true;
  for (int i = 0; i < ringCount; i++) {
    LogRow r = ring[(ringHead + i) % RING_MAX];
    if (since != 0 && r.ts <= since) {
      continue;                       // one the page already has
    }
    rowJson(row, sizeof(row), r, first);
    first = false;
    size_t lr = strlen(row);
    if (pos + lr > sizeof(out) - 1) {
      out[pos] = 0;
      server.sendContent(out);
      delay(1);
      pos = 0;
    }
    memcpy(out + pos, row, lr);
    pos += lr;
  }
  if (pos > 0) {
    out[pos] = 0;
    server.sendContent(out);
  }
  server.sendContent("]}");
  server.sendContent("");
}

static void handleFiles() {
  if (sdFs == nullptr) {
    server.send(500, "application/json", "{\"files\":[]}");
    return;
  }

  // The table only changes when this program writes to the card, so the JSON is
  // rebuilt on a version change instead of scanning the directory on every
  // request (a scan costs about half a second, and the page polls this list).
  static String   cachedJson;
  static uint32_t cachedVersion = 0;
  static bool     cachedValid   = false;

  if (!cachedValid || cachedVersion != filesVersion) {
    const int n = fileListEnsure();
    cachedJson  = "";
    cachedJson.reserve(96 + n * 72);
    cachedJson += "{\"version\":";
    cachedJson += String(filesVersion);
    cachedJson += ",\"count\":";
    cachedJson += String(n);
    cachedJson += ",\"truncated\":";
    cachedJson += (fileListTruncated ? "true" : "false");
    cachedJson += ",\"scanMs\":";
    cachedJson += String(fileScanMs);
    cachedJson += ",\"files\":[";
    for (int i = 0; i < n; i++) {
      if (i > 0) {
        cachedJson += ",";
      }
      cachedJson += "{\"name\":\"";
      cachedJson += fileList[i].name;
      cachedJson += "\",\"size\":";
      cachedJson += String((unsigned long)fileList[i].size);
      cachedJson += ",\"active\":";
      cachedJson += (strcmp(fileList[i].name, csvPath) == 0 ? "true" : "false");
      cachedJson += "}";
    }
    cachedJson += "]}";
    cachedVersion = filesVersion;
    cachedValid   = true;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", cachedJson);
}

static void handleDownload() {
  String file = server.arg("f");
  if (file.length() == 0 || !file.startsWith("/")) {
    file = "/" + file;
  }
  if (sdFs == nullptr || file.length() < 3) {
    server.send(400, "text/plain", "Invalid file.");
    return;
  }

  File f = sdFs->open(file, FILE_READ);
  if (!f || f.isDirectory()) {
    server.send(404, "text/plain", "File does not exist.");
    return;
  }

  String downloadName = file.substring(file.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadName + "\"");
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleDelete() {
  if (!requireAdminAuth()) return;
  String file = server.arg("f");
  if (file.length() == 0 || !file.startsWith("/")) {
    file = "/" + file;
  }
  if (sdFs == nullptr || file.length() < 3) {
    server.send(400, "text/plain", "Invalid file.");
    return;
  }
  if (strcmp(file.c_str(), csvPath) == 0) {          // never delete the active file
    server.send(403, "text/plain",
                "The active file cannot be deleted. Restart the board with a new "
                "file or delete it on a computer.");
    return;
  }
  if (!sdFs->exists(file)) {
    server.send(404, "text/plain", "File does not exist.");
    return;
  }
  if (!sdFs->remove(file)) {
    server.send(500, "text/plain", "Delete failed.");
    return;
  }

  if (ringValid && ringFile == file) {
    ringValid = false;
  }
  fileListInvalidate();
  SLOG.print("[WEB] Deleted file: ");
  SLOG.println(file);

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "File deleted.");
}

// Compact local timestamp for file names: YYYYMMDD_HHMMSS
static void nameStamp(uint32_t e, char *out, size_t n) {
  if (e < NTP_EPOCH_MIN) { snprintf(out, n, "%lu", (unsigned long)e); return; }
  struct tm t;
  time_t tt = (time_t)e;
  localtime_r(&tt, &t);
  snprintf(out, n, "%04d%02d%02d_%02d%02d%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

// Downloads every record of the requested range as a CSV file. This deliberately
// does not use loadHistoryRange(): that buffer is decimated for chart rendering.
static void handleExport() {
  String fromArg = server.arg("from");
  String tillArg = server.arg("to");
  uint32_t from = fromArg.length() ? (uint32_t)strtoul(fromArg.c_str(), nullptr, 10) : 0;
  uint32_t till = tillArg.length() ? (uint32_t)strtoul(tillArg.c_str(), nullptr, 10) : 0;
  if (from > 0 && till > 0 && till < from) { uint32_t t = from; from = till; till = t; }

  if (sdFs == nullptr) {
    server.send(500, "text/plain", "Cannot read the card.");
    return;
  }

  char s1[24], s2[24];
  nameStamp(from, s1, sizeof(s1));
  nameStamp(till, s2, sizeof(s2));
  String fname = "pms_" + String(s1) + "_to_" + String(s2) + ".csv";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("timestamp,time_source,pm1_0,pm2_5,pm10,temp_c,humidity\r\n");

  char out[2048];
  CsvOut sink = { out, sizeof(out), 0, 0 };

  // Only the files whose span overlaps the range are opened. The listing is
  // cached and chronological, so a range that covers one day reads one file
  // instead of scanning the directory once per file.
  const int n = fileListEnsure();
  for (int i = 0; i < n; i++) {
    const char *name = fileList[i].name;
    uint32_t start = fileNameEpoch(name);
    if (start == 0) {
      if (from != 0) continue;                 // MILLIS era file, no wall clock
    } else if (from >= NTP_EPOCH_MIN) {
      uint32_t end = (i + 1 < n) ? fileNameEpoch(fileList[i + 1].name) : 0;
      if (end != 0 && end <= from) continue;   // this file ended before the range
      if (till != 0 && start > till) break;    // the listing is chronological
    }
    scanCsvRange(name, from, till, sinkCsv, &sink);
  }
  if (sink.pos > 0) {
    out[sink.pos] = 0;
    server.sendContent(out);
  }
  server.sendContent("");
}

static void handleFavicon() {
  server.send(204, "text/plain", "");
}


// HTTP firmware update (independent of ArduinoOTA, works from a browser)
static void handleUpdateDone() {
  if (!requireAdminAuth()) return;
  if (Update.hasError()) {
    server.send(500, "text/plain", String("Update failed: ") + Update.errorString());
  } else {
    server.send(200, "text/html",
                "<meta http-equiv=\"refresh\" content=\"12;url=/\">"
                "<h3>Update OK, the device is restarting...</h3>");
    delay(300);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  if (!server.authenticate(ADMIN_USER, settings.adminPassword)) return;
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    SLOG.print("[OTA] HTTP update: ");
    SLOG.println(up.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      SLOG.print("[OTA] begin failed: ");
      SLOG.println(Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      SLOG.print("[OTA] write failed: ");
      SLOG.println(Update.errorString());
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      SLOG.print("[OTA] end failed: ");
      SLOG.println(Update.errorString());
    }
  }
}

static void startWebServer() {
  if (webStarted) {                      // may be called again when the link
    return;                              // comes up after a late boot
  }
  webStarted = true;

  server.on("/", handleRoot);
  server.on("/admin", HTTP_GET,  handleAdminGet);
  server.on("/admin", HTTP_POST, handleAdminPost);
  server.on("/api/status", handleStatus);
  server.on("/api/data", handleData);
  server.on("/api/files", handleFiles);
  server.on("/download", handleDownload);
  server.on("/export", handleExport);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/favicon.ico", handleFavicon);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Unknown path.");
  });
  server.begin();
  SLOG.print("[WEB] Server started (Ethernet: ");
  SLOG.print(ETH.localIP());
  SLOG.print(", AP: ");
  SLOG.print(settings.apEnabled ? WiFi.softAPIP() : IPAddress(0,0,0,0));
  SLOG.println(")");
}

// ============================== SETUP ==============================

void setup() {
#if SERIAL_DEBUG
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32-POE2 / PMS5003 / microSD / NTP / WEB / AP ===");
  Serial.print("Chip: ");
  Serial.println(ESP.getChipModel());
#endif

  loadSettings();
  applyTimeZone();      // local zone for file names, otherwise UTC after a reset

  // ---------- 1. microSD ----------
  sdReady = mountSD();
  SLOG.print("[SD] SD_MMC.begin():     ");
  SLOG.println(sdReady ? "OK" : "FAILED");

  // ---------- 2. PMS5003 ----------
  Serial2.begin(PMS_BAUD, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);
  SLOG.print("[PMS] Serial2 @ ");
  SLOG.print(PMS_BAUD);
  SLOG.print(" 8N1  RX=GPIO");
  SLOG.print(PMS_RX_PIN);
  SLOG.print("  TX=GPIO");
  SLOG.println(PMS_TX_PIN);

  // ---------- 3. Ethernet ----------
  netStartMs = millis();
  if (waitEthernet()) {
    ethActive = true;
    netMode   = "Ethernet";
    SLOG.print("[NET] Ethernet ACTIVE, IP=");
    SLOG.print(ETH.localIP());
    SLOG.print(" (");
    SLOG.print(settings.ethDhcp ? "DHCP" : "static");
    SLOG.println(")");
  } else {
    ethActive = false;
    netMode   = "offline";
    SLOG.println("[NET] Ethernet is not available");
  }

  // ---------- 4. Wi-Fi AP ----------

  // Make Ethernet the default interface so OTA/UDP sockets bind to it
  if (ethActive) {
    ETH.setDefault();
  }
  startAP();

  // ---------- 5. NTP ----------
  if (ethActive) {
    ntpSynced = syncNtp();
    if (ntpSynced) {
      applyTimeZone();
      bootEpoch = (uint32_t)time(nullptr) - (millis() / 1000UL);
      SLOG.print("[NTP] OK, epoch=");
      SLOG.println((uint32_t)time(nullptr));
    } else {
      SLOG.println("[NTP] FAILED -> MILLIS (time can be set on /admin)");
    }
  } else {
    SLOG.println("[NTP] Skipped (no network)");
  }

  // ---------- 6. CSV file ----------
  if (sdReady) {
    sdReady = prepareCsv();
  }
  rememberRotationDate();

  // ---------- 7. Web server (Ethernet or AP) ----------
  if (ethActive || settings.apEnabled) {
    startWebServer();
  } else {
    SLOG.println("[WEB] Server not started (no network)");
  }

  // ---------- 8. OTA ----------
#if OTA_ENABLE
  if (ethActive || settings.apEnabled) {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(settings.adminPassword);
    ArduinoOTA.begin();
    SLOG.print("[OTA] Ready: ");
    SLOG.println(OTA_HOSTNAME);
  }
#endif

  lastPmsMs      = millis();
  lastWarnMs     = millis();
  lastRotCheck   = millis();
}

// ============================== LOOP ==============================

void loop() {
  if (ethActive || settings.apEnabled) {
    server.handleClient();
#if OTA_ENABLE
    ArduinoOTA.handle();
#endif
  }

  // Scheduled restart after saving network settings
  if (restartScheduled != 0 && (millis() - restartScheduled) > 2500UL) {
    SLOG.println("[SYS] Restarting...");
    delay(50);
    ESP.restart();
  }

  // Daily file rotation (checked once per second)
  uint32_t nowMs = millis();
  if ((nowMs - lastRotCheck) >= 1000UL) {
    lastRotCheck = nowMs;
    checkRotation();
  }

  // ---- Clock / network watchdog ----
  // The router may boot after the board, so Ethernet, DHCP and NTP are retried
  // here instead of only in setup(). When the clock finally becomes valid, the
  // records written with millis() are re-dated and the file renamed.
  pollNetwork();
  pollNtp();
  if (!timeWasValid && epochValid()) {
    timeWasValid = true;
    if (repairActiveFile()) {
      SLOG.println("[TIME] Early millis records re-dated and file renamed");
    }
    rememberRotationDate();       // daily rotation starts from real time
  }

  // AM2302 read (sensor supports ~0.5 Hz, so every 3 s is plenty)
  if ((nowMs - lastDhtRead) >= DHT_READ_MS) {
    lastDhtRead = nowMs;
    float t = NAN, h = NAN;
    if (readDht22(t, h)) {
      dhtTempC      = t;
      dhtHumidity   = h;
      dhtLastGoodMs = nowMs;
      dhtEverOk     = true;
      dhtGood++;
    } else {
      dhtBad++;
    }
  }

  // No delay(): handle a sample as soon as a full 32-byte frame is available.
  if (Serial2.available() >= 32) {
    if (readPmsData(&Serial2)) {
      lastPmsMs = millis();
      handleSample();
    }
  }

  // Serial warning when the sensor stops sending data
  uint32_t now = millis();
  if ((now - lastPmsMs) >= PMS_SILENCE_WARN_MS &&
      (now - lastWarnMs) >= PMS_SILENCE_WARN_MS) {
    lastWarnMs = now;
    SLOG.print("[WARNING] PMS5003 has sent no data for ");
    SLOG.print((now - lastPmsMs) / 1000);
    SLOG.println(" s");
  }
}
