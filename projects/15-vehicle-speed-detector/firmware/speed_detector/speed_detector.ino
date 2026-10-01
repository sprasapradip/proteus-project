/*
 * ============================================================================
 *  Road Vehicle Speed Detector v2 - classification, tailgating, statistics
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  Two infrared beams across the lane, a known distance apart.
 *
 *  Measures
 *    - speed from the time between beam A and beam B (microsecond timing)
 *    - direction (A>B or B>A)
 *    - vehicle LENGTH from how long the beams stay broken x speed, which
 *      gives the class: BIKE (< 2.5 m), CAR (2.5 - 6.5 m), HEAVY (> 6.5 m)
 *    - HEADWAY (front-to-front time to the vehicle ahead, same direction)
 *
 *  Acts
 *    - big 4-digit roadside display (MAX7219) shows each driver's speed,
 *      flashing when over the limit
 *    - SLOW DOWN sign + beep over the limit. Heavy vehicles have their own,
 *      lower limit.
 *    - KEEP DISTANCE lamp when a vehicle follows closer than 2 s
 *    - green THANK YOU lamp for everyone within the limit
 *
 *  Records
 *    - every vehicle as a CSV line over USB (for traffic surveys)
 *    - the last 80 violations in EEPROM, kept through power cuts, read
 *      back with the LOG command
 *    - statistics: count per class, violations, tailgating, average, top
 *      speed, 85th percentile speed (the number traffic engineers use to set
 *      speed limits) and vehicles per hour of the day
 *
 *  Settings (limits, beam gap, tailgating time, PIN) are stored in EEPROM and
 *  changed over the serial port after entering a PIN. See HELP.
 *
 *  Pin map
 *    D2  beam A receiver (LOW = broken)   D3  beam B receiver
 *    D7  RESET statistics button to GND
 *    D8  green THANK YOU lamp   D10 SLOW DOWN sign   D5 KEEP DISTANCE lamp
 *    D9  buzzer                 D13 status LED
 *    A4 / A5 / D4  MAX7219 DIN / CLK / LOAD (4-digit speed display)
 *    D12 D11 A0 A1 A2 A3  LCD RS EN D4 D5 D6 D7
 *    D0 / D1  USB serial, 9600 baud: CSV log + commands
 * ============================================================================
 */

#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <avr/wdt.h>

// ============================================================================
//  FIXED SETTINGS
// ============================================================================
const float    MAX_PLAUSIBLE_KMH = 200.0f;
const uint16_t PASS_TIMEOUT_MS   = 2000;   // second beam must follow within this
const uint16_t STOPPED_MS        = 10000;  // vehicle standing in the beams
const uint16_t CLEAR_MS          = 200;    // both beams clear before re-arming
const uint16_t WARN_MS           = 5000;
const uint16_t SHOW_MS           = 5000;   // big display shows a speed this long
const uint32_t BEAM_FAULT_MS     = 30000;  // beam blocked this long = fault
const float    BIKE_MAX_M        = 2.5f;
const float    CAR_MAX_M         = 6.5f;
const uint32_t UNLOCK_MS         = 120000; // PIN unlock lasts 2 minutes
const uint8_t  LOG_SLOTS         = 80;
const uint16_t LOG_BASE          = 32;     // EEPROM address of the violation log

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_A = 2, PIN_B = 3, PIN_RESET = 7;
const uint8_t PIN_OK = 8, PIN_BUZZER = 9, PIN_SIGN = 10, PIN_KEEPDIST = 5, PIN_LED = 13;
const uint8_t PIN_MAX_DIN = A4, PIN_MAX_CLK = A5, PIN_MAX_LOAD = 4;

LiquidCrystal lcd(12, 11, A0, A1, A2, A3);

// ============================================================================
//  SETTINGS IN EEPROM
// ============================================================================
struct Settings {
  uint16_t magic;
  uint16_t gapMm;          // beam spacing
  uint16_t limitKmh;       // bikes and cars
  uint16_t heavyKmh;       // buses and trucks
  uint16_t tailgateMs;     // minimum safe headway
  uint16_t pin;            // 4-digit PIN for changes
  uint8_t  crc;
};
const uint16_t SETTINGS_MAGIC = 0x5D02;
Settings cfg;

static uint8_t crc8(const uint8_t *p, uint8_t n) {
  uint8_t c = 0x5A;
  while (n--) { c ^= *p++; for (uint8_t i = 0; i < 8; i++) c = c & 0x80 ? (c << 1) ^ 0x07 : c << 1; }
  return c;
}

static void saveSettings() {
  cfg.crc = crc8((const uint8_t *)&cfg, offsetof(Settings, crc));
  EEPROM.put(0, cfg);
}

static void loadSettings() {
  EEPROM.get(0, cfg);
  if (cfg.magic != SETTINGS_MAGIC || cfg.crc != crc8((const uint8_t *)&cfg, offsetof(Settings, crc))) {
    cfg = { SETTINGS_MAGIC, 1000, 40, 30, 2000, 1234, 0 };
    saveSettings();
  }
}

// ============================================================================
//  CLOCK (set over serial with TIME hh:mm:ss)
// ============================================================================
bool clockSet = false;
uint32_t clockBase = 0;            // seconds since day 0 00:00 at clockMillis
unsigned long clockMillis = 0;

static uint32_t nowStamp() {
  return clockBase + (millis() - clockMillis) / 1000UL;
}

static void printStamp(uint32_t t, bool withDay = true) {
  char buf[16];
  if (!clockSet) {
    snprintf(buf, sizeof(buf), "+%lus", (unsigned long)t);
  } else {
    uint32_t tod = t % 86400UL;
    if (withDay)
      snprintf(buf, sizeof(buf), "D%lu %02u:%02u:%02u", (unsigned long)(t / 86400UL),
               (unsigned)(tod / 3600), (unsigned)(tod / 60 % 60), (unsigned)(tod % 60));
    else
      snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(tod / 3600), (unsigned)(tod / 60 % 60),
               (unsigned)(tod % 60));
  }
  Serial.print(buf);
}

// ============================================================================
//  VIOLATION LOG IN EEPROM (ring, newest found by sequence number)
// ============================================================================
struct LogRec {
  uint16_t seq;            // 0xFFFF = empty slot
  uint32_t stamp;
  uint16_t kmh10;
  uint8_t  cls;
  uint8_t  flags;          // bit0 direction B>A, bit1 tailgating, bit2 clock set
};
uint16_t logSeq = 0;

static uint16_t logAddr(uint8_t slot) { return LOG_BASE + slot * sizeof(LogRec); }

static void logScan() {
  logSeq = 0;
  for (uint8_t i = 0; i < LOG_SLOTS; i++) {
    LogRec r;
    EEPROM.get(logAddr(i), r);
    if (r.seq != 0xFFFF && r.seq >= logSeq) logSeq = r.seq + 1;
  }
  if (logSeq >= 0xFFF0) logSeq = 0;    // practically never: wrap safely
}

static void logAppend(const LogRec &r) {
  EEPROM.put(logAddr(r.seq % LOG_SLOTS), r);
}

static void logClear() {
  for (uint8_t i = 0; i < LOG_SLOTS; i++) EEPROM.put(logAddr(i), (uint16_t)0xFFFF);
  logSeq = 0;
}

// ============================================================================
//  STATISTICS
// ============================================================================
enum VClass : uint8_t { C_BIKE, C_CAR, C_HEAVY, C_UNKNOWN };
const char *const CLASS_NAME[] = { "BIKE", "CAR", "HEAVY", "?" };

struct Stats {
  uint32_t count[4];
  uint32_t violations, tailgates, rejected;
  float sumKmh, topKmh;
  uint16_t hist[30];       // 5 km/h bins, 0 .. 150 km/h
  uint16_t perHour[24];
} st;

static uint32_t totalVehicles() { return st.count[0] + st.count[1] + st.count[2] + st.count[3]; }

static float percentile(float p) {
  uint32_t n = 0;
  for (uint8_t i = 0; i < 30; i++) n += st.hist[i];
  if (!n) return 0;
  float want = p * n, cum = 0;
  for (uint8_t i = 0; i < 30; i++) {
    if (cum + st.hist[i] >= want && st.hist[i]) {
      return i * 5.0f + 5.0f * (want - cum) / st.hist[i];   // linear inside the bin
    }
    cum += st.hist[i];
  }
  return 150;
}

// ============================================================================
//  BEAM TIMING (interrupts)
// ============================================================================
volatile unsigned long tBreakA, tBreakB, tClearA, tClearB;
volatile bool brokeA, brokeB, clearedA, clearedB;
volatile bool armed = true;

void isrA() {
  unsigned long t = micros();
  if (!armed) return;
  if (digitalRead(PIN_A) == LOW) { if (!brokeA) { tBreakA = t; brokeA = true; } }
  else if (brokeA && !clearedA) { tClearA = t; clearedA = true; }
}

void isrB() {
  unsigned long t = micros();
  if (!armed) return;
  if (digitalRead(PIN_B) == LOW) { if (!brokeB) { tBreakB = t; brokeB = true; } }
  else if (brokeB && !clearedB) { tClearB = t; clearedB = true; }
}

// ============================================================================
//  MAX7219 4-DIGIT DISPLAY (bit-banged, no library needed)
// ============================================================================
static void maxSend(uint8_t reg, uint8_t val) {
  digitalWrite(PIN_MAX_LOAD, LOW);
  shiftOut(PIN_MAX_DIN, PIN_MAX_CLK, MSBFIRST, reg);
  shiftOut(PIN_MAX_DIN, PIN_MAX_CLK, MSBFIRST, val);
  digitalWrite(PIN_MAX_LOAD, HIGH);
}

static void maxInit() {
  pinMode(PIN_MAX_DIN, OUTPUT);
  pinMode(PIN_MAX_CLK, OUTPUT);
  pinMode(PIN_MAX_LOAD, OUTPUT);
  digitalWrite(PIN_MAX_LOAD, HIGH);
  maxSend(0x0F, 0);      // display test off
  maxSend(0x09, 0x0F);   // code-B decode on digits 0..3
  maxSend(0x0B, 3);      // scan 4 digits
  maxSend(0x0A, 10);     // brightness
  maxSend(0x0C, 1);      // normal operation
  for (uint8_t d = 1; d <= 4; d++) maxSend(d, 0x0F);   // blank
}

static void maxShow(int value) {     // value < 0 = blank
  for (uint8_t d = 0; d < 4; d++) {
    uint8_t code = 0x0F;              // 0x0F = blank in code-B
    if (value >= 0 && (d == 0 || value > 0)) code = value % 10;   // no leading zeros
    if (value > 0) value /= 10;
    maxSend(d + 1, code);
  }
}

// ============================================================================
//  STATE
// ============================================================================
enum Phase : uint8_t { WAITING, MEASURING, TIMED, CLEARING };
Phase phase = WAITING;
unsigned long phaseSince = 0, clearSince = 0, frontMs = 0;
bool clearActive = false;

float curKmh = 0;
char curDir = '>';
bool curWarned = false;
unsigned long lastFrontMs[2] = { 0, 0 };   // per direction, for headway
bool haveLastFront[2] = { false, false };
uint16_t curHeadwayMs = 0xFFFF;

float lastKmh = 0;
VClass lastClass = C_UNKNOWN;
unsigned long warnUntil = 0, keepDistUntil = 0, showUntil = 0;
bool displayFlash = false;

bool beamFault = false;
char faultBeam = ' ';
unsigned long blockedSinceA = 0, blockedSinceB = 0, faultClearSince = 0;

unsigned long unlockedUntil = 0;

// ============================================================================
//  HELPERS
// ============================================================================
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }
static bool before(unsigned long until) { return (long)(until - millis()) > 0; }

static void note(const __FlashStringHelper *msg) {
  Serial.print(F("# "));
  printStamp(nowStamp());
  Serial.print(' ');
  Serial.println(msg);
}

static void rearm() {
  noInterrupts();
  brokeA = brokeB = clearedA = clearedB = false;
  armed = true;
  interrupts();
  phase = WAITING;
}

static void warn() {
  warnUntil = millis() + WARN_MS;
  displayFlash = true;
}

static uint16_t limitFor(VClass c) { return c == C_HEAVY ? cfg.heavyKmh : cfg.limitKmh; }

// ============================================================================
//  MEASUREMENT
// ============================================================================
static void finishVehicle() {
  noInterrupts();
  unsigned long bA = tBreakA, bB = tBreakB, cA = tClearA, cB = tClearB;
  bool okA = clearedA, okB = clearedB;
  armed = false;
  interrupts();

  // Length = speed x time the beams were blocked (average of both beams).
  float ms = curKmh / 3.6f;
  float occ = 0;
  uint8_t n = 0;
  if (okA) { occ += (cA - bA) / 1e6f; n++; }
  if (okB) { occ += (cB - bB) / 1e6f; n++; }
  VClass cls = C_UNKNOWN;
  float lengthM = 0;
  if (n) {
    lengthM = ms * occ / n;
    cls = lengthM < BIKE_MAX_M ? C_BIKE : lengthM < CAR_MAX_M ? C_CAR : C_HEAVY;
  }

  bool over = curKmh > limitFor(cls);
  bool tailgate = curHeadwayMs < cfg.tailgateMs && curKmh >= 20;
  if (over && !curWarned) warn();          // e.g. a bus over the heavy limit
  if (tailgate) keepDistUntil = millis() + WARN_MS;

  // Statistics
  st.count[cls]++;
  st.sumKmh += curKmh;
  if (curKmh > st.topKmh) st.topKmh = curKmh;
  uint8_t bin = curKmh >= 150 ? 29 : (uint8_t)(curKmh / 5);
  st.hist[bin]++;
  if (clockSet) st.perHour[(nowStamp() % 86400UL) / 3600]++;
  if (over) st.violations++;
  if (tailgate) st.tailgates++;
  lastKmh = curKmh;
  lastClass = cls;

  // VEHICLE,time,direction,kmh,class,length_m,headway_s,over_limit,tailgating
  Serial.print(F("VEHICLE,"));
  printStamp(nowStamp());
  Serial.print(',');
  Serial.print(curDir == '>' ? F("A>B") : F("B>A"));
  Serial.print(',');
  Serial.print(curKmh, 1);
  Serial.print(',');
  Serial.print(CLASS_NAME[cls]);
  Serial.print(',');
  Serial.print(lengthM, 1);
  Serial.print(',');
  if (curHeadwayMs == 0xFFFF) Serial.print('-'); else Serial.print(curHeadwayMs / 1000.0f, 2);
  Serial.print(',');
  Serial.print(over ? 1 : 0);
  Serial.print(',');
  Serial.println(tailgate ? 1 : 0);

  if (over || tailgate) {
    LogRec r = { logSeq++, nowStamp(), (uint16_t)(curKmh * 10 + 0.5f), (uint8_t)cls,
                 (uint8_t)((curDir == '<' ? 1 : 0) | (tailgate ? 2 : 0) | (clockSet ? 4 : 0)) };
    logAppend(r);
  }
}

static void measure() {
  noInterrupts();
  bool a = brokeA, b = brokeB;
  unsigned long ta = tBreakA, tb = tBreakB;
  interrupts();
  bool beamsClear = digitalRead(PIN_A) == HIGH && digitalRead(PIN_B) == HIGH;

  switch (phase) {
    case WAITING:
      if (a || b) {
        phase = MEASURING;
        phaseSince = frontMs = millis();
      }
      break;

    case MEASURING:
      if (a && b) {
        unsigned long dt = ta < tb ? tb - ta : ta - tb;
        curDir = ta < tb ? '>' : '<';
        curKmh = dt ? (cfg.gapMm / 1000.0f) / (dt / 1e6f) * 3.6f : 9999;
        if (curKmh > MAX_PLAUSIBLE_KMH) {
          st.rejected++;
          note(F("rejected: impossible speed (beam glitch)"));
          noInterrupts(); armed = false; interrupts();
          phase = CLEARING;
          clearActive = false;
          break;
        }
        // Headway to the previous vehicle in the same direction.
        uint8_t d = curDir == '>' ? 0 : 1;
        curHeadwayMs = 0xFFFF;                        // "no vehicle ahead"
        if (haveLastFront[d] && frontMs - lastFrontMs[d] < 60000UL)
          curHeadwayMs = (uint16_t)(frontMs - lastFrontMs[d]);
        lastFrontMs[d] = frontMs;
        haveLastFront[d] = true;

        curWarned = curKmh > cfg.limitKmh;
        if (curWarned) warn();
        maxShow((int)(curKmh + 0.5f));
        showUntil = millis() + SHOW_MS;
        lastKmh = curKmh;
        phase = TIMED;
        phaseSince = millis();
        clearActive = false;
      } else if (since(phaseSince, PASS_TIMEOUT_MS)) {
        st.rejected++;
        note(F("ignored: only one beam broken (pedestrian / animal)"));
        noInterrupts(); armed = false; interrupts();
        phase = CLEARING;
        clearActive = false;
      }
      break;

    case TIMED:
      // Wait for the whole vehicle to pass, so its length can be measured.
      if (beamsClear && !clearActive) clearSince = millis();
      clearActive = beamsClear;
      if ((beamsClear && since(clearSince, CLEAR_MS)) || since(phaseSince, STOPPED_MS)) {
        finishVehicle();
        phase = CLEARING;
        clearActive = false;
      }
      break;

    case CLEARING:
      if (beamsClear && !clearActive) clearSince = millis();
      clearActive = beamsClear;
      if (beamsClear && since(clearSince, CLEAR_MS)) rearm();
      break;
  }
}

static void checkBeams() {
  unsigned long now = millis();
  bool blockedA = digitalRead(PIN_A) == LOW, blockedB = digitalRead(PIN_B) == LOW;
  if (!blockedA) blockedSinceA = now;
  if (!blockedB) blockedSinceB = now;
  if (!beamFault && (now - blockedSinceA > BEAM_FAULT_MS || now - blockedSinceB > BEAM_FAULT_MS)) {
    beamFault = true;
    faultBeam = now - blockedSinceA > BEAM_FAULT_MS ? 'A' : 'B';
    note(faultBeam == 'A' ? F("BEAM FAULT: beam A blocked 30 s (misaligned / dirty / parked vehicle)")
                          : F("BEAM FAULT: beam B blocked 30 s (misaligned / dirty / parked vehicle)"));
    noInterrupts(); armed = false; interrupts();
  }
  if (beamFault) {
    if (blockedA || blockedB) faultClearSince = now;
    else if (now - faultClearSince > 2000) {
      beamFault = false;
      note(F("beams OK again"));
      rearm();
    }
  }
}

// ============================================================================
//  SERIAL COMMANDS
// ============================================================================
static void printStats() {
  uint32_t n = totalVehicles();
  Serial.print(F("STATS vehicles=")); Serial.print(n);
  Serial.print(F(" bike=")); Serial.print(st.count[C_BIKE]);
  Serial.print(F(" car=")); Serial.print(st.count[C_CAR]);
  Serial.print(F(" heavy=")); Serial.print(st.count[C_HEAVY]);
  Serial.print(F(" unknown=")); Serial.print(st.count[C_UNKNOWN]);
  Serial.print(F(" violations=")); Serial.print(st.violations);
  Serial.print(F(" tailgating=")); Serial.print(st.tailgates);
  Serial.print(F(" rejected=")); Serial.println(st.rejected);
  Serial.print(F("STATS avg=")); Serial.print(n ? st.sumKmh / n : 0, 1);
  Serial.print(F(" p50=")); Serial.print(percentile(0.50f), 1);
  Serial.print(F(" p85=")); Serial.print(percentile(0.85f), 1);
  Serial.print(F(" top=")); Serial.print(st.topKmh, 1);
  Serial.print(F(" limit=")); Serial.print(cfg.limitKmh);
  Serial.print(F(" heavy_limit=")); Serial.println(cfg.heavyKmh);
  if (clockSet) {
    Serial.print(F("STATS per_hour"));
    for (uint8_t h = 0; h < 24; h++) {
      if (st.perHour[h]) { Serial.print(' '); Serial.print(h); Serial.print('h'); Serial.print('='); Serial.print(st.perHour[h]); }
    }
    Serial.println();
  }
}

static void printLog() {
  Serial.println(F("LOG,seq,time,direction,kmh,class,tailgating"));
  uint16_t first = logSeq > LOG_SLOTS ? logSeq - LOG_SLOTS : 0;
  uint16_t shown = 0;
  for (uint16_t s = first; s < logSeq; s++) {
    LogRec r;
    EEPROM.get(logAddr(s % LOG_SLOTS), r);
    if (r.seq != s) continue;
    Serial.print(F("LOG,")); Serial.print(r.seq); Serial.print(',');
    if (r.flags & 4) { bool c = clockSet; clockSet = true; printStamp(r.stamp); clockSet = c; }
    else { Serial.print('+'); Serial.print(r.stamp); Serial.print('s'); }
    Serial.print(','); Serial.print(r.flags & 1 ? F("B>A") : F("A>B"));
    Serial.print(','); Serial.print(r.kmh10 / 10.0f, 1);
    Serial.print(','); Serial.print(CLASS_NAME[r.cls < 4 ? r.cls : 3]);
    Serial.print(','); Serial.println(r.flags & 2 ? 1 : 0);
    shown++;
  }
  Serial.print(F("LOG end, ")); Serial.print(shown); Serial.println(F(" records"));
}

static void printStatus() {
  Serial.print(F("STATUS time=")); printStamp(nowStamp());
  Serial.print(F(" gap_mm=")); Serial.print(cfg.gapMm);
  Serial.print(F(" limit=")); Serial.print(cfg.limitKmh);
  Serial.print(F(" heavy_limit=")); Serial.print(cfg.heavyKmh);
  Serial.print(F(" tailgate_ms=")); Serial.print(cfg.tailgateMs);
  Serial.print(F(" beams=")); Serial.print(beamFault ? F("FAULT") : F("OK"));
  Serial.print(F(" log_records=")); Serial.print(logSeq < LOG_SLOTS ? logSeq : LOG_SLOTS);
  Serial.print(F(" unlocked=")); Serial.println(before(unlockedUntil) ? 1 : 0);
}

static void printHelp() {
  Serial.println(F("# Commands (end with Enter):"));
  Serial.println(F("#  STATUS | STATS | LOG | HELP"));
  Serial.println(F("#  PIN nnnn            unlock changes for 2 minutes"));
  Serial.println(F("#  SET LIMIT kmh       limit for bikes and cars"));
  Serial.println(F("#  SET HEAVY kmh       limit for buses and trucks"));
  Serial.println(F("#  SET GAP mm          measured beam spacing"));
  Serial.println(F("#  SET TAILGATE ms     minimum safe headway"));
  Serial.println(F("#  SET PIN nnnn        new PIN"));
  Serial.println(F("#  TIME hh:mm:ss       set the clock"));
  Serial.println(F("#  CLEAR STATS | CLEAR LOG"));
}

static bool needUnlock() {
  if (before(unlockedUntil)) return true;
  Serial.println(F("ERR locked: send PIN nnnn first"));
  return false;
}

static void handleCommand(char *line) {
  for (char *p = line; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  long v = 0;
  unsigned hh, mm, ss;

  if (!strcmp(line, "HELP")) printHelp();
  else if (!strcmp(line, "STATUS")) printStatus();
  else if (!strcmp(line, "STATS")) printStats();
  else if (!strcmp(line, "LOG")) printLog();
  else if (sscanf(line, "PIN %ld", &v) == 1) {
    if (v == cfg.pin) { unlockedUntil = millis() + UNLOCK_MS; Serial.println(F("OK unlocked for 2 minutes")); }
    else { unlockedUntil = 0; Serial.println(F("ERR wrong PIN")); }
  }
  else if (sscanf(line, "SET LIMIT %ld", &v) == 1) {
    if (!needUnlock()) return;
    if (v < 5 || v > 150) { Serial.println(F("ERR limit must be 5..150")); return; }
    cfg.limitKmh = v; saveSettings(); Serial.println(F("OK limit saved"));
  }
  else if (sscanf(line, "SET HEAVY %ld", &v) == 1) {
    if (!needUnlock()) return;
    if (v < 5 || v > 150) { Serial.println(F("ERR limit must be 5..150")); return; }
    cfg.heavyKmh = v; saveSettings(); Serial.println(F("OK heavy limit saved"));
  }
  else if (sscanf(line, "SET GAP %ld", &v) == 1) {
    if (!needUnlock()) return;
    if (v < 200 || v > 5000) { Serial.println(F("ERR gap must be 200..5000 mm")); return; }
    cfg.gapMm = v; saveSettings(); Serial.println(F("OK gap saved"));
  }
  else if (sscanf(line, "SET TAILGATE %ld", &v) == 1) {
    if (!needUnlock()) return;
    if (v < 500 || v > 10000) { Serial.println(F("ERR tailgate must be 500..10000 ms")); return; }
    cfg.tailgateMs = v; saveSettings(); Serial.println(F("OK tailgate saved"));
  }
  else if (sscanf(line, "SET PIN %ld", &v) == 1) {
    if (!needUnlock()) return;
    if (v < 1000 || v > 9999) { Serial.println(F("ERR PIN must be 4 digits")); return; }
    cfg.pin = v; saveSettings(); Serial.println(F("OK PIN changed"));
  }
  else if (sscanf(line, "TIME %u:%u:%u", &hh, &mm, &ss) == 3) {
    if (!needUnlock()) return;
    if (hh > 23 || mm > 59 || ss > 59) { Serial.println(F("ERR time")); return; }
    uint32_t day = clockSet ? nowStamp() / 86400UL : 0;
    clockBase = day * 86400UL + hh * 3600UL + mm * 60UL + ss;
    clockMillis = millis();
    clockSet = true;
    Serial.print(F("OK time ")); printStamp(nowStamp()); Serial.println();
  }
  else if (!strcmp(line, "CLEAR STATS")) {
    if (!needUnlock()) return;
    memset(&st, 0, sizeof(st)); Serial.println(F("OK statistics cleared"));
  }
  else if (!strcmp(line, "CLEAR LOG")) {
    if (!needUnlock()) return;
    logClear(); Serial.println(F("OK violation log cleared"));
  }
  else if (*line) Serial.println(F("ERR unknown command, try HELP"));
}

static void serialTask() {
  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; handleCommand(buf); len = 0; }
    else if (len < sizeof(buf) - 1) buf[len++] = c;
  }
}

// ============================================================================
//  OUTPUTS
// ============================================================================
static void handleResetButton() {
  static bool last = false;
  static unsigned long changed = 0;
  bool now = digitalRead(PIN_RESET) == LOW;
  if (now != last && since(changed, 50)) {
    changed = millis();
    last = now;
    if (now) { memset(&st, 0, sizeof(st)); note(F("statistics reset (button)")); }
  }
}

static void outputs() {
  unsigned long t = millis();
  bool warnOn = before(warnUntil);
  digitalWrite(PIN_SIGN, warnOn && ((t / 400) & 1));
  digitalWrite(PIN_OK, !warnOn && !beamFault);
  digitalWrite(PIN_KEEPDIST, before(keepDistUntil) && ((t / 400) & 1));
  digitalWrite(PIN_LED, beamFault ? (t / 150) & 1 : phase != WAITING);

  static bool toneOn = false;
  bool want = warnOn && (t % 1000) < 150;
  if (want && !toneOn) tone(PIN_BUZZER, 2500);
  if (!want && toneOn) noTone(PIN_BUZZER);
  toneOn = want;

  // Big display: flash while warning, blank after SHOW_MS.
  static unsigned long lastFlash = 0;
  static bool shown = false;
  if (before(showUntil)) {
    shown = true;
    if (displayFlash && warnOn && t - lastFlash >= 300) {
      lastFlash = t;
      static bool on = true;
      on = !on;
      maxSend(0x0C, on ? 1 : 0);
    }
    if (!warnOn) { displayFlash = false; maxSend(0x0C, 1); }
  } else if (shown) {
    shown = false;
    maxSend(0x0C, 1);
    maxShow(-1);
  }
}

static void display() {
  static unsigned long last = 0;
  if (!since(last, 300)) return;
  last = millis();
  char line[17], s[6];

  dtostrf(lastKmh, 3, 0, s);
  snprintf(line, sizeof(line), "%skm/h %-5s%s", s, CLASS_NAME[lastClass],
           before(warnUntil) ? "SLOW" : "    ");
  lcd.setCursor(0, 0);
  lcd.print(line);

  lcd.setCursor(0, 1);
  if (beamFault) {
    snprintf(line, sizeof(line), "BEAM %c FAULT    ", faultBeam);
  } else {
    char a[6];
    switch ((millis() / 3000) % 3) {
      case 0:
        snprintf(line, sizeof(line), "N%-4lu V%-3lu T%-3lu", (unsigned long)totalVehicles(),
                 (unsigned long)st.violations, (unsigned long)st.tailgates);
        break;
      case 1:
        dtostrf(percentile(0.85f), 3, 0, a);
        dtostrf(totalVehicles() ? st.sumKmh / totalVehicles() : 0, 3, 0, s);
        snprintf(line, sizeof(line), "p85 %s avg %s  ", a, s);
        break;
      default:
        snprintf(line, sizeof(line), "Lim %u Heavy %u   ", cfg.limitKmh, cfg.heavyKmh);
        break;
    }
  }
  lcd.print(line);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_RESET, INPUT_PULLUP);
  const uint8_t outs[] = { PIN_OK, PIN_BUZZER, PIN_SIGN, PIN_KEEPDIST, PIN_LED };
  for (uint8_t p : outs) pinMode(p, OUTPUT);

  Serial.begin(9600);
  loadSettings();
  logScan();
  maxInit();
  lcd.begin(16, 2);
  lcd.print("Speed Detect v2");

  attachInterrupt(digitalPinToInterrupt(PIN_A), isrA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B), isrB, CHANGE);
  blockedSinceA = blockedSinceB = millis();

  note(F("Vehicle speed detector v2.0 (send HELP for commands)"));
  printStatus();
  Serial.println(F("VEHICLE,time,direction,kmh,class,length_m,headway_s,over_limit,tailgating"));
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  checkBeams();
  if (!beamFault) measure();
  serialTask();
  handleResetButton();
  outputs();
  display();
}
