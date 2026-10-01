/*
 * ============================================================================
 *  Water Tank Level Controller with pump protection
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  The usual Nepali house setup: an underground sump (or boring) and a
 *  roof tank. The controller fills the roof tank automatically and protects
 *  the pump, which is the expensive part.
 *
 *  Protections
 *    - dry run: stops the pump when the sump runs dry, waits 5 min after
 *      water returns before trying again
 *    - no rise: pump running 10 min but the tank level hasn't moved
 *      (airlock, broken pipe, closed valve) -> stop and latch a fault
 *    - max run: 45 min continuous -> stop and latch a fault
 *    - short cycling: at least 60 s off between starts
 *    - probe fault: an impossible probe pattern (75% wet but 25% dry)
 *
 *  Pin map
 *    D2 D3 D4 D5  tank probes 25 / 50 / 75 / 100 %  (LOW = water)
 *    D6           sump probe                         (LOW = water in sump)
 *    D7           AUTO / MANUAL switch               (LOW = manual)
 *    D10          push button: manual start/stop, fault reset
 *    D8           pump relay / contactor coil driver
 *    D9           buzzer
 *    D13          status LED
 *    A0..A5       16x2 LCD: RS, EN, D4, D5, D6, D7
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100     // 10 = long timers 10x faster for demos
#endif

const uint8_t  START_BELOW_LEVEL = 2;     // start when fewer than 2 probes wet (< 50 %)
const uint8_t  FULL_LEVEL        = 4;     // stop when all 4 probes wet (100 %)
const uint16_t MIN_OFF_SEC       = 60;
const uint16_t DRY_RESTART_SEC   = 300;
const uint16_t NO_RISE_SEC       = 600;
const uint16_t MAX_RUN_SEC       = 2700;
const uint16_t INPUT_SETTLE_MS   = 2000;  // waves and splashes on the probes

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_PROBE[4] = { 2, 3, 4, 5 };
const uint8_t PIN_SUMP   = 6;
const uint8_t PIN_MODE   = 7;
const uint8_t PIN_PUMP   = 8;
const uint8_t PIN_BUZZER = 9;
const uint8_t PIN_BUTTON = 10;
const uint8_t PIN_LED    = 13;

LiquidCrystal lcd(A0, A1, A2, A3, A4, A5);

// ============================================================================
//  STATE
// ============================================================================
enum State : uint8_t { ST_IDLE, ST_PUMPING, ST_FULL, ST_DRY_RUN, ST_FAULT };
const char *const STATE_NAME[] = { "IDLE", "PUMPING", "TANK FULL", "SUMP DRY", "FAULT" };

enum Fault : uint8_t { F_NONE, F_NO_RISE, F_MAX_RUN, F_PROBES };
const char *const FAULT_NAME[] = { "", "NO RISE", "MAX RUN TIME", "PROBE ERROR" };

State state = ST_IDLE;
Fault fault = F_NONE;
unsigned long stateSince = 0;
unsigned long pumpOffSince = 0;
bool pumpEverRan = false;
bool manualMode = false;
bool manualRequest = false;

// Debounced inputs
uint8_t level = 0;              // 0..4 wet probes
bool probesValid = true;
bool sumpWet = true;
unsigned long sumpWetSince = 0;
uint8_t rawLevelMask = 0;
unsigned long levelChangedAt = 0, sumpChangedAt = 0;
uint8_t pendingMask = 0;
bool pendingSump = true;

uint8_t levelAtLastRise = 0;
unsigned long lastRiseAt = 0;

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long secs(unsigned long s) {
  return s * 1000UL * TIME_SCALE_PERCENT / 100UL;
}

static bool since(unsigned long t0, unsigned long dur) { return (millis() - t0) >= dur; }

static void logMsg(const char *what) {
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(what);
}

static void setState(State s) {
  if (s == state) return;
  state = s;
  stateSince = millis();
  char buf[40];
  snprintf(buf, sizeof(buf), "state -> %s", STATE_NAME[s]);
  logMsg(buf);
}

static void setPump(bool on) {
  bool isOn = digitalRead(PIN_PUMP) == HIGH;
  if (on == isOn) return;
  digitalWrite(PIN_PUMP, on ? HIGH : LOW);
  if (on) {
    pumpEverRan = true;
    levelAtLastRise = level;
    lastRiseAt = millis();
    logMsg("pump ON");
  } else {
    pumpOffSince = millis();
    logMsg("pump OFF");
  }
}

static void latchFault(Fault f) {
  fault = f;
  setPump(false);
  setState(ST_FAULT);
  char buf[48];
  snprintf(buf, sizeof(buf), "FAULT: %s (press button to reset)", FAULT_NAME[f]);
  logMsg(buf);
}

// ============================================================================
//  INPUTS
// ============================================================================
static void readInputs() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(PIN_PROBE[i]) == LOW) mask |= 1 << i;
  }
  bool sump = digitalRead(PIN_SUMP) == LOW;

  if (mask != pendingMask) { pendingMask = mask; levelChangedAt = millis(); }
  if (sump != pendingSump) { pendingSump = sump; sumpChangedAt = millis(); }

  if (pendingMask != rawLevelMask && since(levelChangedAt, INPUT_SETTLE_MS)) {
    rawLevelMask = pendingMask;
    // Valid patterns are 0000, 0001, 0011, 0111, 1111: water fills bottom up.
    probesValid = ((rawLevelMask + 1) & rawLevelMask) == 0;
    uint8_t n = 0;
    while (n < 4 && (rawLevelMask & (1 << n))) n++;
    level = n;
    char buf[40];
    snprintf(buf, sizeof(buf), "level %u%%%s", level * 25, probesValid ? "" : " (bad pattern)");
    logMsg(buf);
  }
  if (pendingSump != sumpWet && since(sumpChangedAt, INPUT_SETTLE_MS)) {
    sumpWet = pendingSump;
    if (sumpWet) sumpWetSince = millis();
    logMsg(sumpWet ? "sump has water" : "sump DRY");
  }

  bool m = digitalRead(PIN_MODE) == LOW;
  if (m != manualMode) {
    manualMode = m;
    manualRequest = false;
    logMsg(manualMode ? "mode MANUAL" : "mode AUTO");
  }
}

static bool buttonPressed() {
  static bool last = false, stable = false;
  static unsigned long changed = 0;
  bool raw = digitalRead(PIN_BUTTON) == LOW;
  if (raw != last) { last = raw; changed = millis(); }
  if (raw != stable && since(changed, 50)) { stable = raw; return stable; }
  return false;
}

// ============================================================================
//  CONTROL
// ============================================================================
static bool offLongEnough() {
  return !pumpEverRan || since(pumpOffSince, secs(MIN_OFF_SEC));
}

static void control() {
  bool pressed = buttonPressed();

  if (state == ST_FAULT) {
    if (pressed) {
      logMsg("fault reset by user");
      fault = F_NONE;
      setState(ST_IDLE);
    }
    return;
  }

  if (!probesValid) {
    latchFault(F_PROBES);
    return;
  }

  if (pressed && manualMode) {
    manualRequest = !manualRequest;
    logMsg(manualRequest ? "manual start requested" : "manual stop requested");
  }

  bool pumpOn = digitalRead(PIN_PUMP) == HIGH;

  // Hard stops that apply in every mode.
  if (level >= FULL_LEVEL) {
    setPump(false);
    manualRequest = false;
    setState(ST_FULL);
    return;
  }
  if (!sumpWet) {
    setPump(false);
    setState(ST_DRY_RUN);
    return;
  }

  if (pumpOn) {
    if (level > levelAtLastRise) {
      levelAtLastRise = level;
      lastRiseAt = millis();
    }
    if (since(lastRiseAt, secs(NO_RISE_SEC))) { latchFault(F_NO_RISE); return; }
    if (since(stateSince, secs(MAX_RUN_SEC))) { latchFault(F_MAX_RUN); return; }
    if (manualMode && !manualRequest) { setPump(false); setState(ST_IDLE); }
    return;
  }

  // Pump is off. Decide whether to start.
  // After a dry run, give the sump time to really refill before restarting.
  if (state == ST_DRY_RUN && !since(sumpWetSince, secs(DRY_RESTART_SEC))) return;
  if (state == ST_DRY_RUN || state == ST_FULL) setState(ST_IDLE);

  bool want = manualMode ? manualRequest : level < START_BELOW_LEVEL;
  if (want && offLongEnough()) {
    setState(ST_PUMPING);
    setPump(true);
  }
}

// ============================================================================
//  OUTPUTS
// ============================================================================
static void updateDisplay() {
  static unsigned long last = 0;
  if (!since(last, 250)) return;
  last = millis();

  char line[17];
  // Line 1: level bar
  char bar[5];
  for (uint8_t i = 0; i < 4; i++) bar[i] = i < level ? '\xff' : '_';
  bar[4] = '\0';
  snprintf(line, sizeof(line), "Tank %s %3u%%  ", bar, level * 25);
  lcd.setCursor(0, 0);
  lcd.print(line);

  // Line 2: mode + state or fault
  lcd.setCursor(0, 1);
  if (state == ST_FAULT) {
    snprintf(line, sizeof(line), "!%-15s", FAULT_NAME[fault]);
  } else if (state == ST_PUMPING) {
    unsigned long s = (millis() - stateSince) / 1000UL;
    snprintf(line, sizeof(line), "%s PUMP %02lu:%02lu ", manualMode ? "MAN " : "AUTO",
             s / 60, s % 60);
  } else {
    snprintf(line, sizeof(line), "%s %-11s", manualMode ? "MAN " : "AUTO", STATE_NAME[state]);
  }
  lcd.print(line);
}

static void updateAlerts() {
  unsigned long t = millis();
  bool fault_ = state == ST_FAULT;
  bool dry = state == ST_DRY_RUN;
  digitalWrite(PIN_LED, fault_ ? (t / 250) & 1 : dry ? (t / 1000) & 1 : digitalRead(PIN_PUMP));

  static bool toneOn = false;
  bool want = fault_ ? (t % 2000) < 300 : dry ? (t % 30000) < 150 : false;
  if (want && !toneOn) tone(PIN_BUZZER, 2200);
  if (!want && toneOn) noTone(PIN_BUZZER);
  toneOn = want;
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  for (uint8_t i = 0; i < 4; i++) pinMode(PIN_PROBE[i], INPUT_PULLUP);
  pinMode(PIN_SUMP, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Water Level Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Pradip Subedi");

  // Take the first reading as-is so we don't wait for the settle time.
  for (uint8_t i = 0; i < 4; i++) {
    if (digitalRead(PIN_PROBE[i]) == LOW) rawLevelMask |= 1 << i;
  }
  pendingMask = rawLevelMask;
  probesValid = ((rawLevelMask + 1) & rawLevelMask) == 0;
  while (level < 4 && (rawLevelMask & (1 << level))) level++;
  sumpWet = pendingSump = digitalRead(PIN_SUMP) == LOW;
  manualMode = digitalRead(PIN_MODE) == LOW;

  logMsg("Water level controller v1.0");
  delay(1500);
  lcd.clear();
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  readInputs();
  control();
  updateDisplay();
  updateAlerts();
}
