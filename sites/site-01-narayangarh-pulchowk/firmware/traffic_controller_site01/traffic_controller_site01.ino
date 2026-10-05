/*
 * ============================================================================
 *  SITE 1  -  Narayangarh Pulchowk (Narayani bridge chowk), Chitwan
 * ============================================================================
 *  Author    : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board     : Arduino Uno / ATmega328P @ 16 MHz
 *  Version   : 2.2.0-site01
 *
 *  Location  : Pulchowk, Narayangarh, Bharatpur, Chitwan, Nepal
 *              the chowk at the east end of the Narayani bridge, where the
 *              Mahendra Highway meets the road north to the Pokhara bus park
 *
 *  Arms (true compass)
 *    North  road to the Pokhara bus park            pole P1, NE corner
 *    East   Mahendra Highway to Birendra Campus     pole P2, SE corner
 *           and Tandi
 *    South  road to Rampur                          pole P3, SW corner
 *    West   Mahendra Highway to the Narayani bridge pole P4, NW corner
 *
 *  Timing plan (see README): split phasing N -> E -> S -> W,
 *  highway (E, W) green 35 s, Pokhara bus park road (N) 30 s, Rampur road
 *  (S) 20 s, yellow 4 s, all-red 3 s, pedestrian walk 10 s + flashing 15 s
 *  on request. Cycle 148 s. These are design values for approval by the
 *  traffic police / road authority before switch-on.
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
 *    - Countdown link on A5: one wire, 9600 baud, sends the seconds left
 *      for every arm to the countdown display unit (firmware/
 *      countdown_display). Never affects the lamps.
 *    - T-junction support through the approach mask
 *
 *  Pin map (same as the main project; see the site README)
 *    D2  D3  D4   North  R Y G
 *    D5  D6  D7   East   R Y G
 *    D8  D9  D10  South  R Y G
 *    D11 D12 D13  West   R Y G
 *    A0  Night mode input      (to GND = night flash)
 *    A1  Emergency hold input  (to GND = all red)
 *    A2  Pedestrian button     (to GND = request)
 *    A3  Pedestrian WALK lamp
 *    A4  Pedestrian DON'T WALK lamp
 *    A5  Countdown link out    (to RX of the countdown display unit)
 *    D0/D1 Serial (USB) for logging
 * ============================================================================
 */

// Peripheral Configuration Code (do not edit)
//---CONFIG_BEGIN---
//---CONFIG_END---

#include <avr/wdt.h>

// ============================================================================
//  SITE  -  this sketch is built for Site 1 only
// ============================================================================
#define SITE_ID 1

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
// Countdown link (A5 = PC5). Written straight to the port by a Timer2
// interrupt, so it never blocks the state machine.
#define CD_PORT PORTC
#define CD_DDR  DDRC
#define CD_BIT  5

const char APPROACH_CHAR[NUM_APPROACHES] = { 'N', 'E', 'S', 'W' };

// Safety limits. Profiles outside these are clamped at boot.
const uint8_t MIN_GREEN_SEC   = 7;
const uint8_t MAX_GREEN_SEC   = 120;
const uint8_t MIN_YELLOW_SEC  = 3;
const uint8_t MIN_ALLRED_SEC  = 1;
const uint8_t STARTUP_RED_SEC = 5;
const uint16_t FLASH_HALF_MS  = 500;

// ============================================================================
//  SITE 1 PROFILE
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

const SiteProfile SITES[1] = {
  //  name           arms       plan          N   E   S   W   Y  AR  PW  PF
  { "NGH-PULCHOWK", APP_CROSS, PLAN_SPLIT, { 30, 35, 20, 35 }, 4, 3, 10, 15 },
};

// What each firmware arm is on the ground (printed at boot and in status).
const char *const ARM_NAME[NUM_APPROACHES] = {
  "N  to Pokhara bus park            (P1, NE corner)",
  "E  Mahendra Hwy, Birendra Campus / Tandi side  (P2, SE corner)",
  "S  to Rampur                      (P3, SW corner)",
  "W  Mahendra Hwy, Narayani bridge side  (P4, NW corner)",
};

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

// Elapsed time in controller seconds. In a TIME_SCALE_PERCENT=20 build the
// controller runs 5x faster, and the log still shows the same seconds as the
// timing plan (a 35 s green reads as 35 s, not 7 s).
static unsigned long ctrlSeconds() {
  return millis() / (TIME_SCALE_PERCENT * 10UL);
}

// Real milliseconds -> controller milliseconds.
static unsigned long realToCtrlMs(unsigned long ms) {
  return ms / TIME_SCALE_PERCENT * 100UL + (ms % TIME_SCALE_PERCENT) * 100UL / TIME_SCALE_PERCENT;
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
  Serial.print(ctrlSeconds());
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

// Time left in the current state, in controller milliseconds.
static unsigned long stateRemainingCtrlMs() {
  unsigned long el = stateElapsedMs();
  return el >= stateDurationMs ? 0 : realToCtrlMs(stateDurationMs - el);
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
//  COUNTDOWN LINK (A5)
//  Once a second, and whenever a number changes, the controller sends one
//  line for the countdown display unit:
//
//      $CD,G,G030,R037,R079,R106*77
//
//  field 1  state: U startup, G green, Y yellow, A all-red, W walk,
//           P walk flashing, C walk clear, N night, E emergency, F fault
//  fields 2-5  arms N, E, S, W: a letter and three digits
//           G030  green, 30 s left        Y004  yellow, 4 s left
//           R037  red, green in 37 s      R---  red, no time (emergency)
//           F---  night flash             X---  dark / fault / no arm
//  *HH      XOR of the characters between '$' and '*', in hex
//
//  The times are controller seconds, the same as the timing plan. A waiting
//  arm counts down to the start of its own green, including any pedestrian
//  phase that has already been requested. Night or emergency switched on
//  later can still stop a countdown early; the display then shows dashes.
//  9600 baud 8N1, idle high. On site, run it through an RS485 driver
//  (MAX485) when the display is more than a few metres from the cabinet.
// ============================================================================
#define CD_FRAME_LEN 31            // "$CD,s,aaaa,bbbb,cccc,dddd*HH\r\n" + NUL
const uint16_t CD_CHECK_MS     = 50;    // how often the numbers are recomputed
const uint16_t CD_HEARTBEAT_MS = 1000;  // resend even when nothing changed
const char STATE_CODE[] = { 'U', 'G', 'Y', 'A', 'W', 'P', 'C', 'N', 'E', 'F' };

volatile uint8_t cdTxBuf[CD_FRAME_LEN];
volatile uint8_t cdTxLen = 0;      // bytes in the buffer, 0 = link idle
volatile uint8_t cdTxPos = 0;
volatile uint8_t cdTxBit = 0;      // 0 start, 1..8 data, 9 stop
char cdLast[CD_FRAME_LEN] = "";
unsigned long cdCheckedAt = 0;
unsigned long cdSentAt = 0;

// Timer2 in CTC mode at 9615 baud (16 MHz / 8 / 208). One bit per interrupt.
ISR(TIMER2_COMPA_vect) {
  uint8_t b = cdTxBuf[cdTxPos];
  if (cdTxBit == 0) {
    CD_PORT &= ~_BV(CD_BIT);                          // start bit
  } else if (cdTxBit <= 8) {
    if (b & (1 << (cdTxBit - 1))) CD_PORT |= _BV(CD_BIT);
    else                          CD_PORT &= ~_BV(CD_BIT);
  } else {
    CD_PORT |= _BV(CD_BIT);                           // stop bit
  }
  if (++cdTxBit > 9) {
    cdTxBit = 0;
    if (++cdTxPos >= cdTxLen) {
      cdTxLen = 0;
      TIMSK2 &= ~_BV(OCIE2A);                         // done, line stays high
    }
  }
}

static void countdownBegin() {
  CD_PORT |= _BV(CD_BIT);
  CD_DDR  |= _BV(CD_BIT);
  // Timer2 is free here: D3 and D11 are plain lamp outputs, never PWM.
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21);
  OCR2A  = 207;
  TIMSK2 = 0;
}

static void countdownSend(const char *frame) {
  if (cdTxLen) return;
  uint8_t n = 0;
  while (frame[n] && n < CD_FRAME_LEN) { cdTxBuf[n] = frame[n]; n++; }
  cdTxPos = 0;
  cdTxBit = 0;
  TCNT2 = 0;
  TIFR2 = _BV(OCF2A);
  cdTxLen = n;
  TIMSK2 |= _BV(OCIE2A);
}

static unsigned long phaseCycleMs(uint8_t idx) {
  return ((unsigned long)phases[idx].greenSec + site.yellowSec + site.allRedSec) * 1000UL;
}

// Controller ms until approach a next turns green. False if it can't be told.
static bool msUntilGreen(uint8_t a, unsigned long &ms) {
  unsigned long t = stateRemainingCtrlMs();
  uint8_t idx = phaseIndex;
  bool ped = pedRequest && site.pedWalkSec > 0;
  const unsigned long Y  = site.yellowSec * 1000UL;
  const unsigned long AR = site.allRedSec * 1000UL;
  const unsigned long PED = (site.pedWalkSec + site.pedFlashSec + site.allRedSec) * 1000UL;
  bool checkPed = true;

  switch (state) {
    case ST_STARTUP_RED: idx = phaseCount - 1; checkPed = false; break;
    case ST_GREEN:       t += Y + AR; break;
    case ST_YELLOW:      t += AR; break;
    case ST_ALL_RED:     break;
    case ST_PED_WALK:    t += site.pedFlashSec * 1000UL + AR; checkPed = false; break;
    case ST_PED_FLASH:   t += AR; checkPed = false; break;
    case ST_PED_CLEAR:   checkPed = false; break;
    default:             return false;
  }

  for (uint8_t step = 0; step <= phaseCount; step++) {
    if (checkPed && ped) { t += PED; ped = false; }
    checkPed = true;
    idx = (idx + 1) % phaseCount;
    if (phases[idx].greenMask & (1 << a)) { ms = t; return true; }
    t += phaseCycleMs(idx);
  }
  return false;
}

static void armField(uint8_t a, char *f) {
  char mode;
  bool timed = false;
  unsigned long ms = 0;

  if (!approachPresent(a)) {
    mode = 'X';
  } else {
    switch (state) {
      case ST_NIGHT_FLASH: mode = 'F'; break;
      case ST_FAULT:       mode = 'X'; break;
      case ST_EMERGENCY:   mode = 'R'; break;
      default: {
        bool inPhase = phases[phaseIndex].greenMask & (1 << a);
        if (state == ST_GREEN && inPhase) {
          mode = 'G'; ms = stateRemainingCtrlMs(); timed = true;
        } else if (state == ST_YELLOW && inPhase) {
          mode = 'Y'; ms = stateRemainingCtrlMs(); timed = true;
        } else {
          mode = 'R'; timed = msUntilGreen(a, ms);
        }
      }
    }
  }

  f[0] = mode;
  if (timed) {
    unsigned long sec = (ms + 999UL) / 1000UL;   // 35.0 s left shows 35
    if (sec > 999) sec = 999;
    f[1] = '0' + sec / 100;
    f[2] = '0' + (sec / 10) % 10;
    f[3] = '0' + sec % 10;
  } else {
    f[1] = f[2] = f[3] = '-';
  }
}

static void buildCountdownFrame(char *out) {
  char *p = out;
  *p++ = '$'; *p++ = 'C'; *p++ = 'D'; *p++ = ',';
  *p++ = STATE_CODE[state];
  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    *p++ = ',';
    armField(a, p);
    p += 4;
  }
  uint8_t x = 0;
  for (char *q = out + 1; q < p; q++) x ^= (uint8_t)*q;
  const char HEX_DIGIT[] = "0123456789ABCDEF";
  *p++ = '*';
  *p++ = HEX_DIGIT[x >> 4];
  *p++ = HEX_DIGIT[x & 0x0F];
  *p++ = '\r';
  *p++ = '\n';
  *p = '\0';
}

static void serviceCountdown() {
  unsigned long now = millis();
  if (now - cdCheckedAt < CD_CHECK_MS || cdTxLen) return;
  cdCheckedAt = now;

  char frame[CD_FRAME_LEN];
  buildCountdownFrame(frame);
  if (strcmp(frame, cdLast) != 0 || now - cdSentAt >= CD_HEARTBEAT_MS) {
    countdownSend(frame);
    strcpy(cdLast, frame);
    cdSentAt = now;
  }
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
    Serial.print((stateRemainingCtrlMs() + 999UL) / 1000UL);
    Serial.println('s');
  } else {
    Serial.println(F("-"));
  }
  Serial.print(F("  cycles     : ")); Serial.println(cycleCount);
  Serial.print(F("  arms       : ")); printMask(site.approachMask); Serial.println();
  Serial.print(F("  plan       : "));
  Serial.println(site.plan == PLAN_OPPOSING ? F("OPPOSING") : F("SPLIT"));
  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    Serial.print(F("  arm        : "));
    Serial.println(ARM_NAME[a]);
  }
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
  countdownBegin();

  loadSiteProfile();
  buildPhases();

  inNight.begin(PIN_NIGHT, 300);
  inEmergency.begin(PIN_EMERGENCY, 100);
  inPedButton.begin(PIN_PED_BUTTON, 50);

  logPrefix();
  Serial.print(F("Traffic controller v2.2.0 boot, reset="));
  if (resetCause & _BV(WDRF))       Serial.println(F("WATCHDOG"));
  else if (resetCause & _BV(BORF))  Serial.println(F("BROWN-OUT"));
  else if (resetCause & _BV(EXTRF)) Serial.println(F("RESET-PIN"));
  else                              Serial.println(F("POWER-ON"));
  logPrefix();
  Serial.print(phaseCount);
  Serial.print(F(" phases, plan="));
  Serial.println(site.plan == PLAN_OPPOSING ? F("OPPOSING") : F("SPLIT"));
  for (uint8_t a = 0; a < NUM_APPROACHES; a++) {
    logPrefix();
    Serial.println(ARM_NAME[a]);
  }
#if TIME_SCALE_PERCENT != 100
  logPrefix();
  Serial.print(F("SIMULATION BUILD: runs "));
  Serial.print(100 / TIME_SCALE_PERCENT);
  Serial.println(F("x faster, times shown are controller seconds"));
#endif
  logPrefix();
  Serial.println(F("countdown link on A5, 9600 baud"));

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
  serviceCountdown();
  handleSerial();
}
