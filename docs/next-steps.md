> **What this is:** a self-contained brief for the next engineer or AI agent that continues this
> project - context, rules, planned improvements with acceptance criteria and the verification
> steps. It is written in English on purpose, like everything else in this repository. Nothing
> described here is implemented yet.

# Task: harden and polish the ESP32-POE2 environment logger

You are taking over an existing, working Arduino project. Do NOT rewrite it from scratch.
Make focused, reviewable changes, verify each one on the real device, and report measured
numbers.

## 1. Project context

- Repository: https://github.com/sdumanic/esp32-poe2-environment-logger (branch `main`).
  Local checkout on Windows: `C:\Projects\okolina` (use PowerShell).
- Firmware: one sketch, `arduino/pms5003_poe2_logger/pms5003_poe2_logger.ino` (~3,400 lines).
  It contains the C++ firmware AND the whole web UI: a single HTML page in PROGMEM
  (`PAGE_HTML`), an admin page built in code (`handleAdminGet`/`handleAdminPost` with
  `ADMIN_STYLE`), inline CSS/JS and canvas charts. There is NO CDN and NO external JS/CSS
  library - keep it that way (the device may be offline and only reachable from a LAN).
- Board: Olimex ESP32-POE2 (ESP32-WROVER-E, 4 MB flash, 8 MB PSRAM), powered over PoE,
  reachable at `192.168.3.225` (DHCP) from the same LAN as your machine.
- Sensors: PMS5003 on `Serial2` (9600 8N1, RX=GPIO33, TX=GPIO13); AM2302 (DHT22 clone) on
  GPIO4; onboard microSD through 1-bit SD_MMC (CLK=14, CMD=15, D0=2); LAN8720 Ethernet
  (MDC=23, MDIO=18, PHY power=12, clock out on GPIO0).
- Serial output is intentionally disabled (`SERIAL_DEBUG 0`). Never rely on the serial
  console for diagnostics: expose state through the web endpoints instead.
- Endpoints: `/` (chart page), `/admin` (settings), `/api/status`, `/api/data`, `/api/files`,
  `/download`, `/delete`, `/export`, `/update`. `/admin`, `/delete` and `/update` are
  protected by HTTP Basic auth (user `admin`, password stored in NVS).
- CSV on the card: `/pms_YYYYMMDD_HHMMSS.csv` with header
  `timestamp,time_source,pm1_0,pm2_5,pm10,temp_c,humidity`. `timestamp` is a Unix epoch
  when the clock is valid (NTP or MANUAL), otherwise `millis()`. A file created before the
  clock was valid is named `/pms_millis_NNNNNNNNNN.csv` and is re-dated + renamed as soon as
  a real clock appears. A new file starts at boot and after midnight.

## 2. Working rules

- Build: `arduino-cli compile --fqbn esp32:esp32:esp32wrover:PartitionScheme=min_spiffs arduino/pms5003_poe2_logger`
  (esp32 core 3.3.11). Baseline: 1,231,321 B flash (62 %), 82,120 B RAM (25 %).
  Do not exceed roughly 1,400,000 B flash or 130,000 B RAM.
- Flashing is OTA over HTTP only (ArduinoOTA/UDP does not work on this network):
  `curl -u admin:<PASSWORD> -F "firmware=@<build>/pms5003_poe2_logger.ino.bin;type=application/octet-stream" http://192.168.3.225/update`
  Then wait ~30 s and confirm that `boot` changed in `/api/status`.
- ASK THE USER for the current admin password before flashing. Never write that password
  into any file, commit, log or the repository.
- Everything you write (code, comments, commit messages, README) must be in English.
  The sketch keeps CRLF line endings and 2-space indentation. Follow the existing naming
  and comment style.
- Never delete, rename or truncate any `/pms_*.csv` on the card. The active file must stay
  protected (it already answers 403 to `/delete`).
- New options must be backward compatible: with everything left at its default, the
  firmware must behave exactly as it does today.
- Avoid adding third-party libraries; the ESP32 core plus the current includes are enough.
- If a change cannot be verified on the device, revert it instead of shipping it.
- Settings live in NVS through `Preferences` (`prefs`, namespace `NVS_NAMESPACE`), loaded in
  `loadSettings()` and written in `saveSettings()`. Add new keys in the same short style
  (`ethdhcp`, `apssid`, `aploz`, `adminpwd`, `theme`, ...).

## 3. Code map (so you do not have to rediscover it)

- Settings and clock: `struct Settings`, `loadSettings()`, `saveSettings()`, `applyTimeZone()`,
  `epochValid()`, `currentTimestamp()`, `setManualTime()`, `nameStamp()`.
- File naming and rotation: `buildFileName()`, `prepareCsv()`, `rotateFile()`,
  `checkRotation()`, `rememberRotationDate()`, `repairActiveFile()` (re-dates the
  `millis` era file), `mountSD()`.
- CSV I/O: `writeRow()` (opens the file with FILE_APPEND for every row, closes it),
  `parseCsvLine()` (hand written, no sscanf), `scanCsvRange(name, from, till, sink, ctx)`,
  `sinkCsv()`, `sinkHist()`, `loadIntoRing()`.
- RAM buffers: `ring[RING_MAX=500]` (records of the active file), `fileList[FILE_LIST_MAX=512]`
  (cached, sorted listing), `filesVersion`, `histBuf`/`histCount`/`histTotal`/`histDecim`.
- Web handlers: `handleRoot()`, `handleStatus()`, `handleData()`, `handleFiles()`,
  `handleDownload()`, `handleDelete()`, `handleExport()`, `handleAdminGet()`,
  `handleAdminPost()`, `handleUpdateUpload()`; page JavaScript lives inside `PAGE_HTML`.
- Sensors and background work: `readPmsData()`, `readDht22()`, `handleSample()` (decides when
  to write), `pollNetwork()`, `pollNtp()`, `checkRotation()`, all called from `loop()`.
- Current write trigger (in `handleSample()`): a row is written when PM2.5, PM10, temperature
  or humidity differ from the last written value.

## 4. What to implement

### P1 - SD card health and write monitoring (highest value: prevents silent data loss)

Today, if the card is missing, full or failing, `writeRow()` returns false and nobody notices
because serial output is disabled; the CSV simply stops growing.

1. Add to `/api/status`: `sdOk` (bool), `sdFreeMb`, `sdUsedPct`, `lastWriteAgeSec`
   (-1 when nothing has been written yet), `writeFails` (count of failed writes since boot).
   Use `SD_MMC.totalBytes()` / `SD_MMC.usedBytes()`; make sure the JSON stays within the
   existing buffer size in `handleStatus()`.
2. Count failed writes in `writeRow()` and remember the timestamp of the last successful one.
3. On the page: a red warning banner above the charts when `sdOk` is false, when
   `lastWriteAgeSec > 60`, or when free space is below ~50 MB or 10 %. The banner text must
   say what is wrong, e.g. `SD card problem: no record written for 3 min (free 42 MB)`.
   Hide the banner completely when everything is fine.
4. Acceptance: the fields appear in `/api/status` with sane values, and the banner logic can
   be verified by temporarily forcing the condition in the page (simulate in JS) - do not
   damage the real card.

### P2 - Optional "one file per day"

Today every restart (including every OTA) opens a new CSV named after the boot time, so the
card is full of tiny files (currently 68 files, 42 of them below 20 KB).

1. Add an NVS setting `dailyFile` (default OFF) exposed as a checkbox on `/admin`:
   "One file per day (reuse today's file after a restart)".
2. When enabled, `buildFileName()` must produce `/pms_YYYYMMDD_000000.csv` (keep the
   `_HHMMSS` field, always 000000). Do not invent a new name pattern: keeping the existing
   pattern keeps `fileNameEpoch()`, the name sort and the history span logic correct.
3. `prepareCsv()` must continue an existing file of that name (it already checks
   `sdFs->exists()`), and `rotateFile()` / `repairActiveFile()` must use the same scheme.
4. Guard the one ambiguous case: if a file and its name-neighbour have the same date
   (e.g. `/pms_20261007_000000.csv` next to `/pms_20261007_115602.csv`, which happens only on
   the day the option is switched on), do not skip the older-named file in the range checks
   of `loadHistoryRange()` / `handleExport()` - read it anyway.
5. The file list must stay chronological and `/admin` must let the user switch back.
6. Acceptance: with the option on, restart the board twice on the same day and prove that the
   same file receives the new records (record count grows, no new file appears), and that the
   graph and `/export` still return the day's data.

### P3 - Fewer writes (card wear and log size)

Measured today: 555 records in 28 minutes (~1 record every 3 s, ~1.5 MB/day) because the
AM2302 dithers by 0.1 degC and every change is logged.

1. Add settings: `writeDeltaTemp` (default 0.2 degC), `writeDeltaHum` (default 0.5 %).
2. Change the write condition in `handleSample()`: PM2.5 and PM10 changes are always written
   immediately; temperature and humidity only count as a change when they differ from the last
   written value by at least the configured delta.
3. Expose both thresholds as numeric fields on `/admin` and document the defaults.
4. Acceptance: measure the record rate over 10 minutes before and after (compare `records`
   in `/api/status` against uptime) and report the reduction, together with the trade-off
   (temperature/humidity resolution in the CSV).

### P4 - Make the live chart window explicit

`RING_MAX` is 500 records, which at 3 s per record is only ~25 minutes, and the page does not
say so.

1. Raise `RING_MAX` to 2000 (16 B per record, +24 KB RAM) and confirm the compile RAM figure
   stays healthy.
2. In the Live caption show the real window, e.g. `live: 1842 records, 12:04:11 - 12:51:32`
   (first and last timestamp of the buffer), plus the current file name and time source as
   today.

### P5 - Reset diagnostics

Nobody can tell today whether the board rebooted because of a power cut, a panic or the
watchdog.

1. Report `resetReason` as text in `/api/status`, derived from `esp_reset_reason()`
   (power-on, software/OTA, panic, task watchdog, interrupt watchdog, brownout, external,
   deep sleep, unknown).
2. Show it in the page header next to `Started:` in a small, subdued style.

### P6 - Same-origin check for state-changing requests

`/delete` is a normal form POST behind HTTP Basic auth; browsers resend cached Basic
credentials, so a malicious page on the same LAN could trigger a delete.

1. Validate the request in `handleDelete()` (and in every other state-changing POST handler,
   i.e. `handleAdminPost()`): accept it only when the `Origin` header, or the `Referer` host,
   matches the device host (the `Host` header). Reject everything else with 403 and a short
   reason.
2. Keep it dependency free (no secret, no session storage) and document the rule in the
   README.

### P7 - Factory credential warning

The default admin password and the AP password are documented publicly.

1. Report `defaultAdminPwd` and `defaultApPwd` (bools) in `/api/status`.
2. When either is true, show a yellow banner on the page with a link to `/admin`.
3. Add a button on `/admin`: "Generate new AP password" that fills the AP password field with
   a random 12-character WPA2-safe password (no ambiguous characters), still saved through the
   normal POST handler.

### P8 - AM2302 retry

`dhtBad` occasionally increments by one for no reason.

1. In `readDht22()`, retry a failed read up to 3 times (a few ms apart) before counting it as
   bad. Keep the 3 s minimum read interval and the non-blocking behaviour of `loop()`.

### P9 - File list: span and retention

1. In the file table show the covered time span derived from the file names (start from the
   file name, end = the next file's start, as the history code does) - no need to open the
   files.
2. Add an optional retention setting `retentionDays` (default 0 = off): a real delete action
   on `/admin` ("Delete files older than N days", with a confirmation that lists how many
   files would be removed) and, when enabled, the same cleanup during boot. Never touch the
   active file and never delete anything while retention is 0.

### P10 - Small UI/network polish

1. Serve `/` with an `ETag` (hash of the page) and answer `304 Not Modified` when the browser
   already has it. The page is ~38 KB.
2. Stop the 2 s `/api/status` polling while the tab is hidden (`document.hidden` +
   `visibilitychange`) and refresh once as soon as it becomes visible again. The board should
   not be polled all night by an open browser tab.
3. Truncate every `/api/status` field that could grow with uptime, and keep the JSON valid
   when values are missing.

### P11 - Optional (phase 2, only after P1-P10 are verified): range analysis

Add an "Analyze range" button that takes the From/To range currently selected and:

1. Downloads `/export?from=&to=` inside the browser.
2. Computes simple local anomaly flags in JavaScript (median +/- 3 x MAD for PM1.0, PM2.5,
   PM10, temperature, humidity) and marks those points on the chart.
3. Optionally sends the flagged events (not the whole CSV) to an LLM API configured from the
   browser: the API key is stored in `localStorage` only, never on the device and never in the
   repository. Show a short preview of what leaves the device before sending.

Design this so it works fully offline when the key is not configured.

## 5. Verification you must perform and report

1. Compile with the FQBN above; report flash and RAM numbers before/after.
2. Upload over OTA, wait for the reboot, confirm `boot` changed and the new fields are present
   in `/api/status`.
3. Measure and report with `curl -w "%{time_total}"`: `/`, `/api/status`, `/api/files` (first
   and cached call), `/api/data?f=...` (with and without `since`), and `/export` for a short
   and a long range. Baseline to beat: `/api/files` 9 ms (first call ~270 ms, it scans the
   card), `/api/status` 9-24 ms, `/export` 2 min 0.06 s, `/export` 24 h 6.7 s for 1.1 MB.
4. Confirm nothing regressed: the page loads and draws both charts, the file list pages, the
   Live view updates, download and range export produce valid CSV, `/delete` refuses the
   active file with 403 and still deletes an old file when authenticated.
5. If the page changed visually, take a screenshot and update `docs/screenshot.png`
   (it is referenced from the README).
6. Update `README.md` (English): new settings and their defaults, new `/api/status` fields, the
   retained-file behaviour, the same-origin rule, and a note about the write-rate trade-off.
7. Commit in English with a descriptive message that includes the measured numbers and push
   to `main`. Report the commit hash.

## 6. Definition of done

- All P1-P10 items implemented, or explicitly reported as not done with the reason.
- Firmware compiles, is flashed over OTA, and the board keeps logging afterwards.
- No data on the card was deleted or modified.
- The password never appears in any file, commit or log.
- The final report lists: what changed (per item), measured numbers before/after, files
  touched, the OTA upload result, the commit hash, and anything you could not verify.
