/*
 * ============================================================================
 *  Smart Street Light (LDR + PIR, dimming LED driver)
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  A street light that is off in the day, dims itself at night and goes to
 *  full brightness only when someone is actually on the road. On a quiet
 *  street this saves well over half the energy of an always-on lamp.
 *
 *  Behaviour
 *    - day/night from an LDR, with confirmation time and hysteresis so car
 *      headlights or a passing cloud don't switch it
 *    - evening (first 4 h after dusk): 40 % idle
 *    - late night: 20 % idle
 *    - motion (PIR): 100 % for 60 s, fast fade up, slow fade down
 *    - TEST button: full brightness for 30 s, day or night (for maintenance)
 *
 *  Pin map
 *    A0  LDR divider (LDR to 5 V, 10k to GND: more light = higher voltage)
 *    D2  PIR sensor output (HIGH = motion)
 *    D4  TEST push button to GND
 *    D9  PWM to the LED driver / logic-level MOSFET gate
 *    D13 status LED (blinks at night)
 * ============================================================================
 */

#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100      // 10 = timers 10x faster for demos
#endif

const uint16_t DARK_BELOW     = 300;   // ADC counts: goes dark under this
const uint16_t LIGHT_ABOVE    = 450;   // and back to day above this
const uint16_t DUSK_CONFIRM_S = 30;
const uint16_t DAWN_CONFIRM_S = 60;
#ifndef EVENING_SEC
#define EVENING_SEC (4UL * 3600UL)
#endif
const uint32_t EVENING_S      = EVENING_SEC;
const uint16_t MOTION_HOLD_S  = 60;
const uint16_t TEST_HOLD_S    = 30;

const uint8_t LEVEL_EVENING = 40;     // percent
const uint8_t LEVEL_LATE    = 20;
const uint8_t LEVEL_MOTION  = 100;

const uint16_t FADE_UP_MS   = 500;    // 0 -> 100 %
const uint16_t FADE_DOWN_MS = 3000;   // 100 -> 0 %

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_LDR    = A0;
const uint8_t PIN_PIR    = 2;
const uint8_t PIN_TEST   = 4;
const uint8_t PIN_LAMP   = 9;
const uint8_t PIN_STATUS = 13;

// ============================================================================
//  STATE
// ============================================================================
bool night = false;
unsigned long nightSince = 0;
unsigned long candidateSince = 0;
bool candidate = false;             // what the LDR currently suggests
unsigned long motionUntil = 0;
unsigned long testUntil = 0;
uint16_t lightAvg = 0;
float brightness = 0;               // 0..100 %, what the lamp shows now
uint8_t target = 0;
unsigned long lastStep = 0;

static unsigned long secs(unsigned long s) { return s * 1000UL * TIME_SCALE_PERCENT / 100UL; }
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }
static bool before(unsigned long until) { return (long)(until - millis()) > 0; }

static void logMsg(const char *msg) {
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

// ============================================================================
//  SENSING
// ============================================================================
static void readLight() {
  static unsigned long last = 0;
  if (!since(last, 100)) return;
  last = millis();
  // Exponential average, roughly the last 1.5 s.
  lightAvg = (lightAvg * 15 + analogRead(PIN_LDR)) / 16;

  bool darkNow = night ? lightAvg < LIGHT_ABOVE : lightAvg < DARK_BELOW;
  if (darkNow != candidate) {
    candidate = darkNow;
    candidateSince = millis();
  }
  if (candidate != night) {
    unsigned long need = secs(candidate ? DUSK_CONFIRM_S : DAWN_CONFIRM_S);
    if (since(candidateSince, need)) {
      night = candidate;
      nightSince = millis();
      logMsg(night ? "dusk: lamp on" : "dawn: lamp off");
    }
  }
}

static void readMotionAndButton() {
  static bool lastPir = false;
  bool pir = digitalRead(PIN_PIR) == HIGH;
  if (pir) {
    if (!lastPir && night) logMsg("motion");
    motionUntil = millis() + secs(MOTION_HOLD_S);
  }
  lastPir = pir;

  static bool lastBtn = false;
  static unsigned long changed = 0;
  bool btn = digitalRead(PIN_TEST) == LOW;
  if (btn != lastBtn && since(changed, 50)) {
    changed = millis();
    lastBtn = btn;
    if (btn) {
      testUntil = millis() + secs(TEST_HOLD_S);
      logMsg("test: full brightness");
    }
  }
}

// ============================================================================
//  LAMP
// ============================================================================
static void decideTarget() {
  uint8_t t = 0;
  if (before(testUntil)) {
    t = 100;
  } else if (night) {
    t = since(nightSince, secs(EVENING_S)) ? LEVEL_LATE : LEVEL_EVENING;
    if (before(motionUntil)) t = LEVEL_MOTION;
  }
  if (t != target) {
    target = t;
    char buf[32];
    snprintf(buf, sizeof(buf), "target %u%%", target);
    logMsg(buf);
  }
}

static void fade() {
  unsigned long now = millis();
  unsigned long dt = now - lastStep;
  if (dt < 10) return;
  lastStep = now;
  float up = 100.0f * dt / FADE_UP_MS;
  float down = 100.0f * dt / FADE_DOWN_MS;
  if (brightness < target) brightness = min((float)target, brightness + up);
  else if (brightness > target) brightness = max((float)target, brightness - down);

  // LEDs look linear to the eye with a roughly squared curve.
  float x = brightness / 100.0f;
  uint8_t pwm = (uint8_t)(x * x * 255.0f + 0.5f);
  if (brightness > 0 && pwm == 0) pwm = 1;
  analogWrite(PIN_LAMP, pwm);

  digitalWrite(PIN_STATUS, night ? (now / 1000) & 1 : LOW);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_LAMP, OUTPUT);
  analogWrite(PIN_LAMP, 0);
  pinMode(PIN_STATUS, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TEST, INPUT_PULLUP);
  Serial.begin(9600);

  // Start from the real light level so a reset at night doesn't wait 30 s.
  lightAvg = analogRead(PIN_LDR);
  night = candidate = lightAvg < DARK_BELOW;
  nightSince = millis();
  logMsg(night ? "Smart street light v1.0, starting at NIGHT" : "Smart street light v1.0, starting at DAY");
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  readLight();
  readMotionAndButton();
  decideTarget();
  fade();
}
