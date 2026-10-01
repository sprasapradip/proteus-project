/*
 * ============================================================================
 *  Small Wind Turbine Controller (battery charging, dump load, brake)
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  For a small (300 W - 1 kW) turbine charging a 12 V battery bank, the kind
 *  people put up in windy hill villages. A wind turbine must never run with
 *  no load: if the battery is full and you just disconnect it, the rotor
 *  spins up until something breaks. So this controller:
 *
 *    - measures wind speed (cup anemometer), rotor RPM (hall sensor),
 *      battery voltage and generator voltage
 *    - when the battery is full, diverts power into a dump load (heater
 *      resistor) with PWM, instead of disconnecting the turbine
 *    - applies the electric brake (shorts the generator) on over-speed, storm
 *      wind, battery over-voltage or the manual STOP switch
 *    - releases the brake only after the wind has stayed safe for 5 minutes
 *
 *  The brake relay is wired FAIL-SAFE: its normally-closed contacts short the
 *  generator. The Arduino must actively energise it (D7 HIGH) to let the
 *  rotor turn. Power loss, a crashed board or a cut wire all brake the
 *  turbine.
 *
 *  Pin map
 *    D2  anemometer reed switch to GND (1 pulse/s = 0.667 m/s)
 *    D3  rotor hall sensor (1 pulse per revolution)
 *    D4  manual STOP switch to GND
 *    A0  battery voltage, 100k / 22k divider   (0 .. 27.7 V)
 *    A1  generator DC voltage, 100k / 10k      (0 .. 55 V)
 *    D7  brake relay coil driver (HIGH = brake released, turbine runs)
 *    D9  dump load MOSFET gate (PWM)
 *    D13 status LED
 *    D12 D11 D10 D8 D6 D5  LCD RS EN D4 D5 D6 D7
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100          // 10 = long timers 10x faster (demo)
#endif

const float ANEMO_MS_PER_HZ = 0.667f;  // SparkFun / Davis style cup anemometer
const float BATT_RATIO = 22.0f / 122.0f;
const float GEN_RATIO  = 10.0f / 110.0f;
const float VREF = 5.0f;

const float DUMP_START_V  = 14.4f;     // start diverting (absorption voltage)
const float DUMP_FULL_V   = 14.8f;     // 100 % dump load
const float OVERVOLT_V    = 15.2f;     // dump load can't keep up -> brake
const float LOW_BATT_V    = 11.0f;

const uint16_t OVERSPEED_RPM   = 600;
const float    STORM_MS        = 25.0f;
const float    SAFE_WIND_MS    = 15.0f;
const uint16_t SAFE_RPM        = 30;

const uint16_t OVERSPEED_HOLD_MS = 2000;
const uint16_t STORM_HOLD_MS     = 5000;
const uint16_t OVERVOLT_HOLD_MS  = 10000;
const uint16_t RELEASE_AFTER_S   = 300;   // calm time before releasing brake

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_ANEMO = 2, PIN_HALL = 3, PIN_STOP = 4;
const uint8_t PIN_BRAKE = 7, PIN_DUMP = 9, PIN_LED = 13;
const uint8_t PIN_VBATT = A0, PIN_VGEN = A1;

LiquidCrystal lcd(12, 11, 10, 8, 6, 5);

// ============================================================================
//  STATE
// ============================================================================
enum Brake : uint8_t { B_NONE, B_MANUAL, B_OVERSPEED, B_STORM, B_OVERVOLT };
const char *const BRAKE_NAME[] = { "RUN", "STOP SWITCH", "OVERSPEED", "STORM", "BATT OVERVOLT" };

volatile uint16_t anemoPulses = 0, hallPulses = 0;
volatile unsigned long lastAnemoUs = 0, lastHallUs = 0;

float windMs = 0, vBatt = 0, vGen = 0;
uint16_t rpm = 0;
uint8_t dumpPwm = 0;
Brake brake = B_NONE;
unsigned long safeSince = 0;
bool safeActive = false;
unsigned long overspeedSince = 0, stormSince = 0, overvoltSince = 0;
bool overspeedActive = false, stormActive = false, overvoltActive = false;

// ============================================================================
//  INTERRUPTS (with simple debounce for the reed switch)
// ============================================================================
void onAnemo() {
  unsigned long t = micros();
  if (t - lastAnemoUs > 5000UL) { anemoPulses++; lastAnemoUs = t; }
}

void onHall() {
  unsigned long t = micros();
  if (t - lastHallUs > 2000UL) { hallPulses++; lastHallUs = t; }
}

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long secs(unsigned long s) { return s * 1000UL * TIME_SCALE_PERCENT / 100UL; }
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }

static void logMsg(const char *msg) {
  Serial.print(F("# ["));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

static bool heldFor(bool cond, bool &active, unsigned long &t0, unsigned long need) {
  if (cond && !active) t0 = millis();
  active = cond;
  return cond && since(t0, need);
}

static float readVolts(uint8_t pin, float ratio) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 16; i++) sum += analogRead(pin);
  return sum / 16.0f * VREF / 1023.0f / ratio;
}

// ============================================================================
//  MEASURE (once per second)
// ============================================================================
static bool measure() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 1000) return false;
  float dt = (now - last) / 1000.0f;
  last = now;

  noInterrupts();
  uint16_t a = anemoPulses, h = hallPulses;
  anemoPulses = hallPulses = 0;
  interrupts();

  windMs = a / dt * ANEMO_MS_PER_HZ;
  rpm = (uint16_t)(h / dt * 60.0f + 0.5f);
  vBatt = readVolts(PIN_VBATT, BATT_RATIO);
  vGen = readVolts(PIN_VGEN, GEN_RATIO);
  return true;
}

// ============================================================================
//  CONTROL
// ============================================================================
static void setBrake(Brake b) {
  if (b == brake) return;
  brake = b;
  safeActive = false;
  char buf[40];
  snprintf(buf, sizeof(buf), b == B_NONE ? "brake released" : "BRAKE: %s", BRAKE_NAME[b]);
  logMsg(buf);
}

static void control() {
  bool stopSwitch = digitalRead(PIN_STOP) == LOW;

  // Faults that apply the brake (checked every second).
  if (stopSwitch) setBrake(B_MANUAL);
  else if (heldFor(rpm > OVERSPEED_RPM, overspeedActive, overspeedSince, OVERSPEED_HOLD_MS))
    setBrake(B_OVERSPEED);
  else if (heldFor(windMs > STORM_MS, stormActive, stormSince, STORM_HOLD_MS))
    setBrake(B_STORM);
  else if (heldFor(vBatt > OVERVOLT_V, overvoltActive, overvoltSince, OVERVOLT_HOLD_MS))
    setBrake(B_OVERVOLT);

  // Release only when everything has been calm for a while.
  if (brake != B_NONE && !(brake == B_MANUAL && stopSwitch)) {
    bool calm = windMs < SAFE_WIND_MS && rpm < SAFE_RPM && vBatt < DUMP_FULL_V;
    if (brake == B_MANUAL) {
      if (windMs < SAFE_WIND_MS) setBrake(B_NONE);   // operator decided
    } else if (heldFor(calm, safeActive, safeSince, secs(RELEASE_AFTER_S))) {
      setBrake(B_NONE);
    }
  }

  // Dump load: proportional between DUMP_START_V and DUMP_FULL_V. Fully on
  // while braked too, so the battery doesn't keep rising from other sources.
  uint8_t want;
  if (brake != B_NONE) want = 0;
  else if (vBatt <= DUMP_START_V) want = 0;
  else if (vBatt >= DUMP_FULL_V) want = 255;
  else want = (uint8_t)((vBatt - DUMP_START_V) / (DUMP_FULL_V - DUMP_START_V) * 255.0f);
  // Slew limit so the generator load changes smoothly.
  if (want > dumpPwm) dumpPwm = (want - dumpPwm > 25) ? dumpPwm + 25 : want;
  else if (want < dumpPwm) dumpPwm = (dumpPwm - want > 25) ? dumpPwm - 25 : want;
}

static void outputs() {
  digitalWrite(PIN_BRAKE, brake == B_NONE ? HIGH : LOW);
  analogWrite(PIN_DUMP, dumpPwm);
  unsigned long t = millis();
  bool led = brake != B_NONE ? (t / 200) & 1 : vBatt < LOW_BATT_V ? (t / 1000) & 1 : dumpPwm > 0;
  digitalWrite(PIN_LED, led);
}

static void report() {
  char a[8], b[8], line[17];
  dtostrf(windMs, 4, 1, a);
  snprintf(line, sizeof(line), "W%sm/s %4urpm ", a, rpm);
  lcd.setCursor(0, 0);
  lcd.print(line);
  dtostrf(vBatt, 5, 2, b);
  if (brake == B_NONE)
    snprintf(line, sizeof(line), "B%sV D%3u%%   ", b, (unsigned)(dumpPwm * 100UL / 255));
  else
    snprintf(line, sizeof(line), "BRK %-12s", BRAKE_NAME[brake]);
  lcd.setCursor(0, 1);
  lcd.print(line);

  // DATA,seconds,wind_ms,rpm,batt_v,gen_v,dump_pct,state
  Serial.print(F("DATA,"));
  Serial.print(millis() / 1000UL);
  Serial.print(','); Serial.print(windMs, 1);
  Serial.print(','); Serial.print(rpm);
  Serial.print(','); Serial.print(vBatt, 2);
  Serial.print(','); Serial.print(vGen, 1);
  Serial.print(','); Serial.print(dumpPwm * 100UL / 255);
  Serial.print(','); Serial.println(BRAKE_NAME[brake]);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_BRAKE, OUTPUT);
  digitalWrite(PIN_BRAKE, LOW);                 // braked until we know it's safe
  pinMode(PIN_DUMP, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_ANEMO, INPUT_PULLUP);
  pinMode(PIN_HALL, INPUT_PULLUP);
  pinMode(PIN_STOP, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ANEMO), onAnemo, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), onHall, FALLING);

  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Wind Turbine Ctl");
  logMsg("Wind turbine controller v1.0");
  Serial.println(F("DATA,seconds,wind_ms,rpm,batt_v,gen_v,dump_pct,state"));

  // Start braked; release after the first measurement if nothing is wrong.
  brake = B_MANUAL;
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  if (measure()) {
    control();
    report();
  }
  outputs();
}
