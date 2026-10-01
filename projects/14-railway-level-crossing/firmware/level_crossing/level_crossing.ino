/*
 * ============================================================================
 *  Automatic Railway Level Crossing
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  Road / rail crossing automation, for example where the Janakpur railway
 *  meets village roads. Sensors on the track on both sides detect a train
 *  long before it reaches the road.
 *
 *    - works for trains from either direction, and for two trains at once
 *      (one each way): the road opens only when every train has passed
 *    - warning first: red lights flash and the bell rings for 6 s before
 *      the barriers start to move, so cars already on the crossing get off
 *    - barriers lower slowly (6 s) and never rise while a train is on the
 *      crossing (island sensor)
 *    - gateman key: force the crossing closed at any time; turning the key
 *      off also clears a train that never arrived (stopped or reversed)
 *    - a sensor stuck active for 10 min is a FAULT: the crossing stays
 *      closed and flashes until a gateman checks it
 *    - every event is logged over USB with a timestamp
 *
 *  Pin map
 *    D2  west approach sensor (LOW = train)
 *    D3  east approach sensor (LOW = train)
 *    D4  island sensor at the road (LOW = train on the crossing)
 *    D12 gateman key switch (LOW = force closed)
 *    D5 D6 road red lights (flash alternately)
 *    D7  bell / buzzer        D9  barrier servo (both barriers)
 *    D13 status LED
 * ============================================================================
 */

#include <Servo.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100
#endif

const uint16_t WARNING_MS      = 6000;   // lights + bell before barriers move
const uint16_t BARRIER_MOVE_MS = 6000;   // full travel time
const uint16_t OPEN_DELAY_MS   = 3000;   // after the last train has cleared
const uint16_t SENSOR_FILTER_MS = 100;
const uint16_t STUCK_SENSOR_S  = 600;    // scaled
const uint8_t  BARRIER_UP = 90, BARRIER_DOWN = 0;
const uint8_t  MAX_TRAINS = 2;

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_WEST = 2, PIN_EAST = 3, PIN_ISLAND = 4, PIN_KEY = 12;
const uint8_t PIN_LIGHT_A = 5, PIN_LIGHT_B = 6, PIN_BELL = 7, PIN_SERVO = 9, PIN_LED = 13;

// ============================================================================
//  STATE
// ============================================================================
enum Phase : uint8_t { P_OPEN, P_WARNING, P_LOWERING, P_CLOSED, P_RAISING, P_FAULT };
const char *const PHASE_NAME[] = { "OPEN", "WARNING", "LOWERING", "CLOSED", "RAISING", "FAULT" };

struct Input {
  uint8_t pin;
  bool stable, pending;
  unsigned long changedAt, activeSince;
};

Input west = { PIN_WEST, false, false, 0, 0 };
Input east = { PIN_EAST, false, false, 0, 0 };
Input island = { PIN_ISLAND, false, false, 0, 0 };
Input key = { PIN_KEY, false, false, 0, 0 };

struct Train { uint8_t exitPin; bool onCrossing; bool passedIsland; };
Train trains[MAX_TRAINS];
uint8_t trainCount = 0;

Phase phase = P_OPEN;
unsigned long phaseSince = 0, clearSince = 0;
bool clearActive = false;
Servo barrier;
float barrierPos = BARRIER_UP;

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long secs(unsigned long s) { return s * 1000UL * TIME_SCALE_PERCENT / 100UL; }
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }

static void logMsg(const char *msg) {
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

static void setPhase(Phase p) {
  if (p == phase) return;
  phase = p;
  phaseSince = millis();
  char buf[32];
  snprintf(buf, sizeof(buf), "crossing %s", PHASE_NAME[p]);
  logMsg(buf);
}

// Returns +1 on a rising edge (becomes active), -1 on falling, 0 otherwise.
static int8_t updateInput(Input &in) {
  bool raw = digitalRead(in.pin) == LOW;
  if (raw != in.pending) { in.pending = raw; in.changedAt = millis(); }
  if (in.pending != in.stable && since(in.changedAt, SENSOR_FILTER_MS)) {
    in.stable = in.pending;
    if (in.stable) in.activeSince = millis();
    return in.stable ? 1 : -1;
  }
  return 0;
}

static const char *sideName(uint8_t pin) { return pin == PIN_WEST ? "west" : "east"; }

// ============================================================================
//  TRAIN TRACKING
// ============================================================================
static void approachEdge(uint8_t pin) {
  // Is this a tracked train leaving through its exit sensor? A long train
  // can reach the far sensor while its tail is still on the road, so a
  // train on the crossing counts too (the island sensor keeps the road
  // closed until the tail has gone).
  for (uint8_t i = 0; i < trainCount; i++) {
    if (trains[i].exitPin == pin && (trains[i].passedIsland || trains[i].onCrossing)) {
      char buf[40];
      snprintf(buf, sizeof(buf), "train passed %s sensor, cleared", sideName(pin));
      logMsg(buf);
      for (uint8_t k = i; k + 1 < trainCount; k++) trains[k] = trains[k + 1];
      trainCount--;
      return;
    }
  }
  // Otherwise a new train is approaching from this side.
  if (trainCount < MAX_TRAINS) {
    trains[trainCount++] = { (uint8_t)(pin == PIN_WEST ? PIN_EAST : PIN_WEST), false, false };
    char buf[40];
    snprintf(buf, sizeof(buf), "train approaching from %s", sideName(pin));
    logMsg(buf);
  } else {
    logMsg("extra train detected (tracking full), staying closed");
  }
}

static void islandEdge(int8_t edge) {
  if (edge > 0) {
    for (uint8_t i = 0; i < trainCount; i++) {
      if (!trains[i].passedIsland && !trains[i].onCrossing) {
        trains[i].onCrossing = true;
        logMsg("train on the crossing");
        return;
      }
    }
    logMsg("island occupied with no tracked train");
  } else if (edge < 0) {
    for (uint8_t i = 0; i < trainCount; i++) {
      if (trains[i].onCrossing) {
        trains[i].onCrossing = false;
        trains[i].passedIsland = true;
        logMsg("crossing clear of train");
        return;
      }
    }
  }
}

static bool stuckSensor() {
  Input *all[] = { &west, &east, &island };
  for (Input *in : all) {
    if (in->stable && since(in->activeSince, secs(STUCK_SENSOR_S))) return true;
  }
  return false;
}

// ============================================================================
//  CONTROL
// ============================================================================
static void control() {
  int8_t w = updateInput(west), e = updateInput(east), isl = updateInput(island);
  int8_t k = updateInput(key);
  if (isl) islandEdge(isl);            // island first: a tail leaving and a
  if (w > 0) approachEdge(PIN_WEST);   // front reaching the far sensor can
  if (e > 0) approachEdge(PIN_EAST);   // happen in the same instant

  // Gateman key released: forget trains that never arrived, if the crossing
  // itself is clear. Also the only way out of FAULT.
  if (k < 0) {
    if (!island.stable && !stuckSensor()) {
      if (trainCount) logMsg("gateman cleared pending trains");
      trainCount = 0;
      if (phase == P_FAULT) setPhase(P_CLOSED);
    }
  }

  if (phase != P_FAULT && stuckSensor()) {
    setPhase(P_FAULT);
    logMsg("FAULT: sensor stuck active, crossing held closed");
  }

  bool needClosed = trainCount > 0 || island.stable || key.stable || phase == P_FAULT;
  bool clear = !needClosed;
  if (clear && !clearActive) clearSince = millis();
  clearActive = clear;

  switch (phase) {
    case P_OPEN:
      if (needClosed) setPhase(P_WARNING);
      break;
    case P_WARNING:
      if (since(phaseSince, WARNING_MS)) setPhase(P_LOWERING);
      break;
    case P_LOWERING:
      if (barrierPos <= BARRIER_DOWN) setPhase(P_CLOSED);
      break;
    case P_CLOSED:
      if (clear && since(clearSince, OPEN_DELAY_MS)) setPhase(P_RAISING);
      break;
    case P_RAISING:
      if (needClosed) setPhase(P_LOWERING);          // another train: back down
      else if (barrierPos >= BARRIER_UP) setPhase(P_OPEN);
      break;
    case P_FAULT:
      break;
  }
}

static void outputs() {
  static unsigned long lastMove = 0;
  unsigned long now = millis();
  float step = (float)(BARRIER_UP - BARRIER_DOWN) * (now - lastMove) / BARRIER_MOVE_MS;
  lastMove = now;
  bool goingDown = phase == P_LOWERING || phase == P_CLOSED || phase == P_FAULT;
  bool goingUp = phase == P_RAISING || phase == P_OPEN;
  if (goingDown && barrierPos > BARRIER_DOWN) barrierPos = max((float)BARRIER_DOWN, barrierPos - step);
  if (goingUp && barrierPos < BARRIER_UP) barrierPos = min((float)BARRIER_UP, barrierPos + step);
  barrier.write((int)(barrierPos + 0.5f));

  bool flashing = phase != P_OPEN;
  bool tick = (now / 500) & 1;
  digitalWrite(PIN_LIGHT_A, flashing && tick);
  digitalWrite(PIN_LIGHT_B, flashing && !tick);

  static bool bellOn = false;
  bool bell = (phase == P_WARNING || phase == P_LOWERING || phase == P_FAULT) && ((now / 250) & 1);
  if (bell && !bellOn) tone(PIN_BELL, 1200);
  if (!bell && bellOn) noTone(PIN_BELL);
  bellOn = bell;

  digitalWrite(PIN_LED, phase == P_FAULT ? tick : trainCount > 0);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  const uint8_t inputs[] = { PIN_WEST, PIN_EAST, PIN_ISLAND, PIN_KEY };
  const uint8_t outputs_[] = { PIN_LIGHT_A, PIN_LIGHT_B, PIN_BELL, PIN_LED };
  for (uint8_t p : inputs) pinMode(p, INPUT_PULLUP);
  for (uint8_t p : outputs_) pinMode(p, OUTPUT);
  barrier.attach(PIN_SERVO);
  barrier.write(BARRIER_UP);
  Serial.begin(9600);
  logMsg("Level crossing controller v1.0");

  // A train already on the crossing at power-up: close straight away.
  if (digitalRead(PIN_ISLAND) == LOW) {
    island.stable = island.pending = true;
    island.activeSince = millis();
    logMsg("train on the crossing at start-up");
  }
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  control();
  outputs();
}
