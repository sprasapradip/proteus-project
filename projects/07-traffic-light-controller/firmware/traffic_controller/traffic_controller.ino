/*
 * ============================================================================
 *  4-Way Junction Traffic Signal Controller
 * ============================================================================
 *  Author    : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board     : Arduino Uno / ATmega328P @ 16 MHz
 *  Version   : 2.0.0
 *
 *  One Arduino runs one junction. A "+" cross road has 4 signal heads
 *  (North, East, South, West), each with Red / Yellow / Green lamps.
 *  The same firmware runs on all 8 sites; only SITE_ID changes.
 *
 *  Features
 *    - Non-blocking state machine (no delay() in the control path)
 *    - Split phasing (one approach at a time) or opposing phasing (N+S, E+W)
 *    - Yellow + all-red clearance on every change
 *    - Pedestrian request button with WALK / DON'T WALK lamps
 *    - Night mode: flashing yellow on all approaches (switch or timer on A0)
 *    - Emergency / police hold: all red while A1 is active
 *    - Conflict monitor with pin read-back; any conflict latches FAULT
 *      (all heads flash red) until a technician power-cycles the box
 *    - Hardware watchdog (2 s) and reset-cause logging
 *    - Read-only serial status port (9600 baud, send 's' or 'h')
 *    - T-junction support through the approach mask
 *
 *  Pin map (same on every site, see docs/WIRING.md)
 *    D2  D3  D4   North  R Y G
 *    D5  D6  D7   East   R Y G
 *    D8  D9  D10  South  R Y G
 *    D11 D12 D13  West   R Y G
 *    A0  Night mode input      (to GND = night flash)
 *    A1  Emergency hold input  (to GND = all red)
 *    A2  Pedestrian button     (to GND = request)
 *    A3  Pedestrian WALK lamp
 *    A4  Pedestrian DON'T WALK lamp
 *    A5  Spare
 *    D0/D1 Serial (USB) for logging
 * ============================================================================
 */

// Peripheral Configuration Code (do not edit)
//---CONFIG_BEGIN---
//---CONFIG_END---

#include <avr/wdt.h>

// ============================================================================
//  SITE SELECTION  -  change this before uploading to each junction (1..8)
// ============================================================================
#ifndef SITE_ID
#define SITE_ID 1
#endif

// ============================================================================
//  BUILD OPTIONS
// ============================================================================
// 1 = output HIGH turns the lamp on (LEDs, MOSFET / ULN2803 drivers, SSR)
// 0 = output LOW turns the lamp on (most cheap blue 5 V relay boards)
#define OUTPUT_ACTIVE_HIGH 1

// 100 = real time. Set 20 in Proteus to watch a full cycle 5x faster.
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100
#endif

// Lamp pattern used when the conflict monitor trips.
// 1 = flash red on all heads (recommended), 0 = flash yellow.
#define FAULT_FLASH_RED 1

#define SERIAL_BAUD 9600

// ============================================================================
//  CONSTANTS
// ============================================================================
enum Approach : uint8_t { NORTH = 0, EAST, SOUTH, WEST, NUM_APPROACHES };

enum LampBit : uint8_t {
  LAMP_OFF    = 0,
  LAMP_RED    = 1 << 0,
  LAMP_YELLOW = 1 << 1,
  LAMP_GREEN  = 1 << 2
};

enum PlanType : uint8_t {
  PLAN_SPLIT    = 0,  // N -> E -> S -> W, one approach green at a time
  PLAN_OPPOSING = 1   // N+S together, then E+W together
};

#define APP_N (1 << NORTH)
#define APP_E (1 << EAST)
#define APP_S (1 << SOUTH)
#define APP_W (1 << WEST)
#define APP_CROSS (APP_N | APP_E | APP_S | APP_W)

const uint8_t LAMP_PIN[NUM_APPROACHES][3] = {
  // RED  YELLOW GREEN
  {  2,    3,     4 },   // North
  {  5,    6,     7 },   // East
  {  8,    9,    10 },   // South
  { 11,   12,    13 }    // West
};

const uint8_t PIN_NIGHT      = A0;
const uint8_t PIN_EMERGENCY  = A1;
const uint8_t PIN_PED_BUTTON = A2;
const uint8_t PIN_PED_WALK   = A3;
const uint8_t PIN_PED_STOP   = A4;

const char APPROACH_CHAR[NUM_APPROACHES] = { 'N', 'E', 'S', 'W' };

// Safety limits. Profiles outside these are clamped at boot.
const uint8_t MIN_GREEN_SEC   = 7;
const uint8_t MAX_GREEN_SEC   = 120;
const uint8_t MIN_YELLOW_SEC  = 3;
const uint8_t MIN_ALLRED_SEC  = 1;
const uint8_t STARTUP_RED_SEC = 5;
const uint16_t FLASH_HALF_MS  = 500;

// ============================================================================
//  SITE PROFILES  -  edit names and timings for your 8 junctions
// ============================================================================
struct SiteProfile {
  const char *name;
  uint8_t approachMask;               // which arms exist (APP_CROSS for "+")
  uint8_t plan;                       // PLAN_SPLIT or PLAN_OPPOSING
  uint8_t greenSec[NUM_APPROACHES];   // N, E, S, W
  uint8_t yellowSec;
  uint8_t allRedSec;
  uint8_t pedWalkSec;                 // 0 disables the pedestrian phase
  uint8_t pedFlashSec;                // WALK flashing before DON'T WALK
};

const SiteProfile SITES[8] = {
  //  name        arms       plan           N   E   S   W   Y  AR  PW  PF
  { "SITE-01", APP_CROSS, PLAN_SPLIT,    { 30, 25, 30, 25 }, 3, 2, 12, 6 },
  { "SITE-02", APP_CROSS, PLAN_SPLIT,    { 30, 25, 30, 25 }, 3, 2, 12, 6 },
  { "SITE-03", APP_CROSS, PLAN_SPLIT,    { 25, 25, 25, 25 }, 3, 2, 12, 6 },
  { "SITE-04", APP_CROSS, PLAN_SPLIT,    { 25, 25, 25, 25 }, 3, 2, 12, 6 },
  { "SITE-05", APP_CROSS, PLAN_OPPOSING, { 35, 30, 35, 30 }, 4, 2, 15, 6 },
  { "SITE-06", APP_CROSS, PLAN_OPPOSING, { 35, 30, 35, 30 }, 4, 2, 15, 6 },
  { "SITE-07", APP_CROSS, PLAN_SPLIT,    { 20, 20, 20, 20 }, 3, 2, 10, 5 },
  { "SITE-08", APP_CROSS, PLAN_SPLIT,    { 20, 20, 20, 20 }, 3, 2, 10, 5 },
};

#if (SITE_ID < 1) || (SITE_ID > 8)
#error "SITE_ID must be between 1 and 8"
#endif

// ============================================================================
//  TYPES
// ============================================================================
enum State : uint8_t {
  ST_STARTUP_RED,
  ST_GREEN,
  ST_YELLOW,
  ST_ALL_RED,
  ST_PED_WALK,
  ST_PED_FLASH,
  ST_PED_CLEAR,
  ST_NIGHT_FLASH,
  ST_EMERGENCY,
  ST_FAULT
};

const char *const STATE_NAME[] = {
  "STARTUP_RED", "GREEN", "YELLOW", "ALL_RED", "PED_WALK",
  "PED_FLASH", "PED_CLEAR", "NIGHT_FLASH", "EMERGENCY", "FAULT"
};

struct Phase {
  uint8_t greenMask;     // approaches that get green in this phase
  uint8_t greenSec;
};

class DebouncedInput {
 public:
  void begin(uint8_t pin, uint16_t settleMs) {
    pin_ = pin;
    settleMs_ = settleMs;
    pinMode(pin_, INPUT_PULLUP);
    stable_ = raw();
    last_ = stable_;
    changedAt_ = millis();
  }

  // Returns true once on each inactive -> active edge.
  bool update() {
    bool r = raw();
    unsigned long now = millis();
    if (r != last_) {
      last_ = r;
      changedAt_ = now;
    }
    if (r != stable_ && (now - changedAt_) >= settleMs_) {
      stable_ = r;
      return stable_;
    }
    return false;
  }

  bool active() const { return stable_; }

 private:
  bool raw() const { return digitalRead(pin_) == LOW; }  // pulled up, active low

  uint8_t pin_ = 0;
  uint16_t settleMs_ = 50;
  bool stable_ = false;
  bool last_ = false;
  unsigned long changedAt_ = 0;
};

// ============================================================================
//  GLOBALS
// ============================================================================
SiteProfile site;                      // working copy after clamping
Phase phases[NUM_APPROACHES];
uint8_t phaseCount = 0;
uint8_t phaseIndex = 0;

State state = ST_STARTUP_RED;
unsigned long stateStartMs = 0;
unsigned long stateDurationMs = 0;

uint8_t lampCmd[NUM_APPROACHES];       // commanded lamp bits per approach
bool pedWalkCmd = false;
bool pedStopCmd = true;

bool pedRequest = false;
uint32_t cycleCount = 0;
uint8_t readbackFailures = 0;
char faultReason[40] = "";

DebouncedInput inNight;
DebouncedInput inEmergency;
DebouncedInput inPedButton;

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long secToMs(uint16_t sec) {
  return (unsigned long)sec * 1000UL * TIME_SCALE_PERCENT / 100UL;
}

static uint8_t clampU8(uint8_t v, uint8_t lo, uint8_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static uint8_t bitCount(uint8_t v) {
  uint8_t n = 0;
  while (v) { n += v & 1; v >>= 1; }
  return n;
}

static bool flashOn() {
  return ((millis() / FLASH_HALF_MS) & 1) == 0;
}

static void writePin(uint8_t pin, bool on) {
  digitalWrite(pin, (on == (bool)OUTPUT_ACTIVE_HIGH) ? HIGH : LOW);
}

static bool readPinOn(uint8_t pin) {
  return (digitalRead(pin) == HIGH) == (bool)OUTPUT_ACTIVE_HIGH;
}

static bool approachPresent(uint8_t a) {
  return site.approachMask & (1 << a);
}

static void logPrefix() {
  Serial.print(F("[S"));
  if (SITE_ID < 10) Serial.print('0');
  Serial.print(SITE_ID);
  Serial.print(' ');
  Serial.print(site.name);
  Serial.print(F(" t="));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
}

static void printMask(uint8_t mask) {
  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    if (mask & (1 << a)) Serial.print(APPROACH_CHAR[a]);
  }
}

// ============================================================================
//  CONFIGURATION
// ============================================================================
static void loadSiteProfile() {
  site = SITES[SITE_ID - 1];
  site.approachMask &= APP_CROSS;
  if (bitCount(site.approachMask) < 2) site.approachMask = APP_CROSS;

  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    site.greenSec[a] = clampU8(site.greenSec[a], MIN_GREEN_SEC, MAX_GREEN_SEC);
  }
  if (site.yellowSec < MIN_YELLOW_SEC) site.yellowSec = MIN_YELLOW_SEC;
  if (site.allRedSec < MIN_ALLRED_SEC) site.allRedSec = MIN_ALLRED_SEC;
  if (site.plan != PLAN_OPPOSING) site.plan = PLAN_SPLIT;
}

static void buildPhases() {
  phaseCount = 0;

  if (site.plan == PLAN_OPPOSING) {
    const uint8_t groups[2] = { APP_N | APP_S, APP_E | APP_W };
    for (uint8_t g = 0; g < 2; g++) {
      uint8_t mask = groups[g] & site.approachMask;
      if (!mask) continue;
      uint8_t sec = 0;
      for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
        if ((mask & (1 << a)) && site.greenSec[a] > sec) sec = site.greenSec[a];
      }
      phases[phaseCount++] = { mask, sec };
    }
  } else {
    for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
      if (approachPresent(a)) {
        phases[phaseCount++] = { (uint8_t)(1 << a), site.greenSec[a] };
      }
    }
  }
}

// ============================================================================
//  CONFLICT MONITOR
// ============================================================================
static bool movingSetAllowed(uint8_t moving) {
  if (moving == 0) return true;
  for (uint8_t p = 0; p < phaseCount; p++) {
    if ((moving & ~phases[p].greenMask) == 0) return true;
  }
  return false;
}

static void enterFault(const char *reason);

// Checks the commanded pattern before it reaches the pins.
static bool commandIsSafe() {
  uint8_t moving = 0;  // approaches showing green or yellow

  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    uint8_t c = lampCmd[a];
    if (!approachPresent(a) && c != LAMP_OFF) return false;
    if (c & (LAMP_GREEN | LAMP_YELLOW)) moving |= (1 << a);
    // Green must never share a head with red or yellow.
    if ((c & LAMP_GREEN) && (c & (LAMP_RED | LAMP_YELLOW))) return false;
  }

  // Flashing yellow at night is a caution pattern, not a right of way.
  bool nightPattern = (state == ST_NIGHT_FLASH);
  if (!nightPattern && !movingSetAllowed(moving)) return false;

  // WALK is only allowed while every vehicle head is red.
  if (pedWalkCmd && moving) return false;
  if (pedWalkCmd && pedStopCmd) return false;
  return true;
}

static void applyOutputs() {
  if (state != ST_FAULT && !commandIsSafe()) {
    enterFault("conflicting command");
    return;
  }

  // Two passes: every lamp that goes off first, then every lamp that comes
  // on. A head changing colour is never lit in two colours, not even for the
  // few microseconds between two pin writes.
  const uint8_t LAMP_BIT[3] = { LAMP_RED, LAMP_YELLOW, LAMP_GREEN };
  for (uint8_t pass = 0; pass < 2; pass++) {
    bool on = pass == 1;
    for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
      for (uint8_t l = 0; l < 3; l++) {
        if ((bool)(lampCmd[a] & LAMP_BIT[l]) == on) writePin(LAMP_PIN[a][l], on);
      }
    }
    if (pedWalkCmd == on) writePin(PIN_PED_WALK, on);
    if (pedStopCmd == on) writePin(PIN_PED_STOP, on);
  }

  if (state == ST_FAULT) return;

  // Read back the real pin levels. A shorted or stuck output shows up here.
  bool ok = true;
  for (uint8_t a = 0; a < NUM_APPROACHES && ok; a++) {
    if (readPinOn(LAMP_PIN[a][2]) != (bool)(lampCmd[a] & LAMP_GREEN)) ok = false;
    if (readPinOn(LAMP_PIN[a][1]) != (bool)(lampCmd[a] & LAMP_YELLOW)) ok = false;
    if (readPinOn(LAMP_PIN[a][0]) != (bool)(lampCmd[a] & LAMP_RED)) ok = false;
  }
  if (readPinOn(PIN_PED_WALK) != pedWalkCmd) ok = false;

  if (ok) {
    readbackFailures = 0;
  } else if (++readbackFailures >= 3) {
    enterFault("output read-back mismatch");
  }
}

// ============================================================================
//  STATE MACHINE
// ============================================================================
static void setState(State s, uint16_t durationSec) {
  state = s;
  stateStartMs = millis();
  stateDurationMs = secToMs(durationSec);

  logPrefix();
  Serial.print(STATE_NAME[s]);
  if (s == ST_GREEN || s == ST_YELLOW) {
    Serial.print(' ');
    printMask(phases[phaseIndex].greenMask);
  }
  if (durationSec) {
    Serial.print(F(" for "));
    Serial.print(durationSec);
    Serial.print('s');
  }
  Serial.println();
}

static bool stateExpired() {
  return (millis() - stateStartMs) >= stateDurationMs;
}

static unsigned long stateElapsedMs() {
  return millis() - stateStartMs;
}

static void enterFault(const char *reason) {
  strncpy(faultReason, reason, sizeof(faultReason) - 1);
  faultReason[sizeof(faultReason) - 1] = '\0';
  setState(ST_FAULT, 0);
  logPrefix();
  Serial.print(F("FAULT LATCHED: "));
  Serial.println(faultReason);
}

static void startGreen() {
  setState(ST_GREEN, phases[phaseIndex].greenSec);
}

static void advancePhase() {
  phaseIndex++;
  if (phaseIndex >= phaseCount) {
    phaseIndex = 0;
    cycleCount++;
  }
  startGreen();
}

// Decides where to go once the junction is all red and safe.
static void leaveAllRed() {
  if (inEmergency.active()) {
    setState(ST_EMERGENCY, 0);
  } else if (inNight.active()) {
    setState(ST_NIGHT_FLASH, 0);
  } else if (pedRequest && site.pedWalkSec > 0) {
    pedRequest = false;
    setState(ST_PED_WALK, site.pedWalkSec);
  } else {
    advancePhase();
  }
}

static void runStateMachine() {
  switch (state) {
    case ST_STARTUP_RED:
      if (stateExpired()) {
        if (inEmergency.active())      setState(ST_EMERGENCY, 0);
        else if (inNight.active())     setState(ST_NIGHT_FLASH, 0);
        else { phaseIndex = 0; startGreen(); }
      }
      break;

    case ST_GREEN: {
      bool minGreenDone = stateElapsedMs() >= secToMs(MIN_GREEN_SEC);
      if (inEmergency.active() ||                 // stop now, yellow still shown
          stateExpired() ||
          (inNight.active() && minGreenDone)) {
        setState(ST_YELLOW, site.yellowSec);
      }
      break;
    }

    case ST_YELLOW:
      if (stateExpired()) setState(ST_ALL_RED, site.allRedSec);
      break;

    case ST_ALL_RED:
      if (stateExpired()) leaveAllRed();
      break;

    case ST_PED_WALK:
      if (inEmergency.active()) setState(ST_PED_FLASH, site.pedFlashSec);
      else if (stateExpired())  setState(ST_PED_FLASH, site.pedFlashSec);
      break;

    case ST_PED_FLASH:
      if (stateExpired()) setState(ST_PED_CLEAR, site.allRedSec);
      break;

    case ST_PED_CLEAR:
      if (stateExpired()) {
        if (inEmergency.active())  setState(ST_EMERGENCY, 0);
        else if (inNight.active()) setState(ST_NIGHT_FLASH, 0);
        else advancePhase();
      }
      break;

    case ST_NIGHT_FLASH:
      if (inEmergency.active())  setState(ST_EMERGENCY, 0);
      else if (!inNight.active()) setState(ST_STARTUP_RED, STARTUP_RED_SEC);
      break;

    case ST_EMERGENCY:
      // Leave through a full all-red clearance, never straight to green.
      if (!inEmergency.active()) setState(ST_STARTUP_RED, STARTUP_RED_SEC);
      break;

    case ST_FAULT:
      // Latched on purpose. Only a power cycle (after inspection) clears it.
      break;
  }
}

// Turns the current state into lamp commands.
static void computeLamps() {
  bool blink = flashOn();
  pedWalkCmd = false;
  pedStopCmd = true;

  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    if (!approachPresent(a)) { lampCmd[a] = LAMP_OFF; continue; }

    bool inPhase = phases[phaseIndex].greenMask & (1 << a);
    uint8_t c = LAMP_RED;

    switch (state) {
      case ST_GREEN:       c = inPhase ? LAMP_GREEN : LAMP_RED;  break;
      case ST_YELLOW:      c = inPhase ? LAMP_YELLOW : LAMP_RED; break;
      case ST_NIGHT_FLASH: c = blink ? LAMP_YELLOW : LAMP_OFF;   break;
      case ST_FAULT:
        c = blink ? (FAULT_FLASH_RED ? LAMP_RED : LAMP_YELLOW) : LAMP_OFF;
        break;
      default:             c = LAMP_RED; break;  // all-red states
    }
    lampCmd[a] = c;
  }

  switch (state) {
    case ST_PED_WALK:
      pedWalkCmd = true;
      pedStopCmd = false;
      break;
    case ST_PED_FLASH:
      pedWalkCmd = blink;
      pedStopCmd = false;
      break;
    case ST_NIGHT_FLASH:
    case ST_FAULT:
      pedWalkCmd = false;
      pedStopCmd = blink;
      break;
    default:
      break;
  }

#ifdef TEST_INJECT_CONFLICT_SEC
  // Test builds only (tools/build_hex.sh with FAULT_TEST=1): ask for green on
  // every arm so the conflict monitor has something to catch. The Arduino IDE
  // never defines this, so field firmware cannot contain it.
  if (state == ST_GREEN && millis() > TEST_INJECT_CONFLICT_SEC * 1000UL) {
    for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
      if (approachPresent(a)) lampCmd[a] = LAMP_GREEN;
    }
  }
#endif
}

// ============================================================================
//  SERIAL STATUS (read only, nothing can be changed from here)
// ============================================================================
static void printStatus() {
  logPrefix();
  Serial.println(F("STATUS"));
  Serial.print(F("  state      : ")); Serial.println(STATE_NAME[state]);
  Serial.print(F("  phase      : ")); Serial.print(phaseIndex + 1);
  Serial.print('/');                 Serial.print(phaseCount);
  Serial.print(F(" green="));         printMask(phases[phaseIndex].greenMask);
  Serial.println();
  Serial.print(F("  remaining  : "));
  if (stateDurationMs) {
    unsigned long el = stateElapsedMs();
    Serial.print(el >= stateDurationMs ? 0 : (stateDurationMs - el) / 1000UL);
    Serial.println('s');
  } else {
    Serial.println(F("-"));
  }
  Serial.print(F("  cycles     : ")); Serial.println(cycleCount);
  Serial.print(F("  arms       : ")); printMask(site.approachMask); Serial.println();
  Serial.print(F("  plan       : "));
  Serial.println(site.plan == PLAN_OPPOSING ? F("OPPOSING") : F("SPLIT"));
  Serial.print(F("  inputs     : night=")); Serial.print(inNight.active());
  Serial.print(F(" emergency="));          Serial.print(inEmergency.active());
  Serial.print(F(" pedRequest="));         Serial.println(pedRequest);
  if (state == ST_FAULT) {
    Serial.print(F("  fault      : ")); Serial.println(faultReason);
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      printStatus();
    } else if (c == 'h' || c == 'H' || c == '?') {
      Serial.println(F("Commands: s = status, h = help (read only)"));
    }
  }
}

// ============================================================================
//  ARDUINO ENTRY POINTS
// ============================================================================
void setup() {
  uint8_t resetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();

  // Drive every lamp to a known state before anything else.
  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    for (uint8_t l = 0; l < 3; l++) {
      pinMode(LAMP_PIN[a][l], OUTPUT);
      writePin(LAMP_PIN[a][l], l == 0);   // red on
    }
  }
  pinMode(PIN_PED_WALK, OUTPUT);
  pinMode(PIN_PED_STOP, OUTPUT);
  writePin(PIN_PED_WALK, false);
  writePin(PIN_PED_STOP, true);

  Serial.begin(SERIAL_BAUD);

  loadSiteProfile();
  buildPhases();

  inNight.begin(PIN_NIGHT, 300);
  inEmergency.begin(PIN_EMERGENCY, 100);
  inPedButton.begin(PIN_PED_BUTTON, 50);

  logPrefix();
  Serial.print(F("Traffic controller v2.0.0 boot, reset="));
  if (resetCause & _BV(WDRF))       Serial.println(F("WATCHDOG"));
  else if (resetCause & _BV(BORF))  Serial.println(F("BROWN-OUT"));
  else if (resetCause & _BV(EXTRF)) Serial.println(F("RESET-PIN"));
  else                              Serial.println(F("POWER-ON"));
  logPrefix();
  Serial.print(phaseCount);
  Serial.print(F(" phases, plan="));
  Serial.println(site.plan == PLAN_OPPOSING ? F("OPPOSING") : F("SPLIT"));

  setState(ST_STARTUP_RED, STARTUP_RED_SEC);
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();

  inNight.update();
  inEmergency.update();
  if (inPedButton.update() && state != ST_PED_WALK && state != ST_PED_FLASH) {
    if (!pedRequest && site.pedWalkSec > 0) {
      pedRequest = true;
      logPrefix();
      Serial.println(F("pedestrian request"));
    }
  }

  runStateMachine();
  computeLamps();
  applyOutputs();
  handleSerial();
}
