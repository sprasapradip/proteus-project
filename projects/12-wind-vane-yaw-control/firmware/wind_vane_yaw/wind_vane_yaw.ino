/*
 * ============================================================================
 *  Wind Direction Vane + Turbine Yaw Controller
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  Reads a 16-position wind vane, works out where the wind is really coming
 *  from (not every gust), and turns the turbine nacelle to face it with a
 *  small geared motor.
 *
 *    - wind direction from a resistor-network vane (SparkFun / Davis style)
 *    - direction is a vector average (about 30 s), so 350 deg and 10 deg
 *      average to 0 deg, not 180 deg
 *    - no yaw when the wind is too variable (averaged vector too short)
 *    - starts yawing only after the error has stayed above 15 deg for 10 s,
 *      stops within 5 deg: no hunting back and forth
 *    - CABLE TWIST PROTECTION: the nacelle may turn only between -270 and
 *      +270 deg (1.5 turns). The controller picks the direction that keeps
 *      it inside that range, even if it means the long way round.
 *    - PARK / furl switch turns the rotor 90 deg out of the wind (storms,
 *      maintenance)
 *    - motor timeout: if the target isn't reached in 90 s the motor stops
 *      and YAW STUCK is reported (jammed gear, motor fault)
 *
 *  Pin map
 *    A0  wind vane (vane between A0 and GND, 10k from A0 to 5 V)
 *    A1  nacelle position: 10-turn pot (or encoder interface), 0..5 V =
 *        -270 .. +270 deg, 2.5 V = facing north
 *    D4  PARK switch to GND
 *    D5  motor driver IN1 (clockwise)   D6  IN2 (counter-clockwise)
 *    D9  motor driver ENABLE (PWM, soft start)
 *    D13 status LED
 *    D12 D11 D10 D8 D3 D2  LCD RS EN D4 D5 D6 D7
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <math.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100
#endif

const float AVG_TIME_S       = 30.0f;  // direction averaging time constant
const float MIN_STEADINESS   = 0.35f;  // 0 = random wind, 1 = perfectly steady
const float START_ERROR_DEG  = 15.0f;
const float STOP_ERROR_DEG   = 5.0f;
const uint16_t START_DELAY_S = 10;
const uint16_t YAW_TIMEOUT_S = 90;     // real seconds, never scaled
const float LIMIT_DEG        = 270.0f; // cable twist limit each way
const float PARK_OFFSET_DEG  = 90.0f;

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_VANE = A0, PIN_NACELLE = A1, PIN_PARK = 4;
const uint8_t PIN_CW = 5, PIN_CCW = 6, PIN_EN = 9, PIN_LED = 13;

LiquidCrystal lcd(12, 11, 10, 8, 3, 2);

// SparkFun weather meter vane: resistance for each of the 16 directions.
const float VANE_R[16] = {
  33000, 6570, 8200, 891, 1000, 688, 2200, 1410,
  3900, 3140, 16000, 14120, 120000, 42120, 64900, 21880
};
const char *const DIR_NAME[16] = {
  "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
};
uint16_t vaneAdc[16];

// ============================================================================
//  STATE
// ============================================================================
enum Mode : uint8_t { M_TRACK, M_YAW_CW, M_YAW_CCW, M_FAULT };
const char *const MODE_NAME[] = { "TRACK", "YAW CW", "YAW CCW", "YAW STUCK" };

float avgX = 1, avgY = 0;         // averaged wind unit vector
float windDeg = 0, steadiness = 0;
float nacelleDeg = 0;             // -270 .. +270
float targetDeg = 0;
Mode mode = M_TRACK;
unsigned long errorSince = 0, yawStarted = 0;
bool errorActive = false;
uint8_t enPwm = 0;
int8_t lastDirIndex = 0;

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long secs(float s) { return (unsigned long)(s * 1000.0f * TIME_SCALE_PERCENT / 100.0f); }
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }

static void logMsg(const char *msg) {
  Serial.print(F("["));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

static float wrap360(float a) {
  while (a < 0) a += 360.0f;
  while (a >= 360.0f) a -= 360.0f;
  return a;
}

// Of the angles equal to `heading` (mod 360) that lie inside the cable
// limit, return the one closest to the current nacelle angle.
static float bestTarget(float heading) {
  float best = 0, bestDist = 1e9;
  for (int k = -2; k <= 2; k++) {
    float cand = wrap360(heading) + 360.0f * k;
    if (cand < -LIMIT_DEG || cand > LIMIT_DEG) continue;
    float d = fabs(cand - nacelleDeg);
    if (d < bestDist) { bestDist = d; best = cand; }
  }
  return best;
}

// ============================================================================
//  SENSING
// ============================================================================
static int8_t readVaneIndex() {
  uint16_t adc = analogRead(PIN_VANE);
  int8_t best = -1;
  uint16_t bestDiff = 0xFFFF;
  for (uint8_t i = 0; i < 16; i++) {
    uint16_t d = adc > vaneAdc[i] ? adc - vaneAdc[i] : vaneAdc[i] - adc;
    if (d < bestDiff) { bestDiff = d; best = i; }
  }
  // Reading far from every position = open or shorted vane: ignore it.
  return bestDiff > 25 ? -1 : best;
}

static void sense() {
  static unsigned long last = 0;
  if (!since(last, 100)) return;
  float dt = (millis() - last) / 1000.0f;
  last = millis();

  int8_t idx = readVaneIndex();
  if (idx >= 0) {
    lastDirIndex = idx;
    float a = idx * 22.5f * DEG_TO_RAD;
    float k = dt * 1000.0f / (float)secs(AVG_TIME_S);
    if (k > 1) k = 1;
    avgX += k * (cos(a) - avgX);
    avgY += k * (sin(a) - avgY);
  }
  windDeg = wrap360(atan2(avgY, avgX) * RAD_TO_DEG);
  steadiness = sqrt(avgX * avgX + avgY * avgY);

  nacelleDeg = (analogRead(PIN_NACELLE) / 1023.0f) * (2 * LIMIT_DEG) - LIMIT_DEG;
}

// ============================================================================
//  CONTROL
// ============================================================================
static void setMode(Mode m) {
  if (m == mode) return;
  mode = m;
  char buf[48], w[8], t[8];
  dtostrf(windDeg, 1, 0, w);
  dtostrf(targetDeg, 1, 0, t);
  snprintf(buf, sizeof(buf), "%s (wind %s, target %s)", MODE_NAME[m], w, t);
  logMsg(buf);
  if (m == M_YAW_CW || m == M_YAW_CCW) yawStarted = millis();
}

static void control() {
  if (mode == M_FAULT) return;              // latched until reset / power cycle

  bool park = digitalRead(PIN_PARK) == LOW;
  float heading = park ? windDeg + PARK_OFFSET_DEG : windDeg;
  bool windUsable = steadiness >= MIN_STEADINESS || park;

  float err;
  if (mode == M_TRACK) {
    targetDeg = bestTarget(heading);
    err = targetDeg - nacelleDeg;
    bool big = windUsable && fabs(err) > START_ERROR_DEG;
    if (big && !errorActive) errorSince = millis();
    errorActive = big;
    // PARK reacts at once; normal tracking waits so gusts don't move it.
    if (big && (park || since(errorSince, secs(START_DELAY_S)))) {
      setMode(err > 0 ? M_YAW_CW : M_YAW_CCW);
    }
  } else {
    // Keep following the average while turning (it is still settling),
    // but stay on the same turn: pick the equivalent angle nearest the
    // current target, inside the cable limit.
    float best = targetDeg, bestDist = 1e9;
    for (int k = -2; k <= 2; k++) {
      float cand = wrap360(heading) + 360.0f * k;
      if (cand < -LIMIT_DEG || cand > LIMIT_DEG) continue;
      float d = fabs(cand - targetDeg);
      if (d < bestDist) { bestDist = d; best = cand; }
    }
    if (windUsable) targetDeg = best;
    err = targetDeg - nacelleDeg;
    bool done = fabs(err) < STOP_ERROR_DEG ||
                (mode == M_YAW_CW && err < 0) || (mode == M_YAW_CCW && err > 0);
    bool atLimit = (mode == M_YAW_CW && nacelleDeg >= LIMIT_DEG - 2) ||
                   (mode == M_YAW_CCW && nacelleDeg <= -LIMIT_DEG + 2);
    if (done || atLimit) {
      errorActive = false;
      setMode(M_TRACK);
    } else if (since(yawStarted, YAW_TIMEOUT_S * 1000UL)) {
      setMode(M_FAULT);
    }
  }
}

static void drive() {
  static unsigned long lastRamp = 0;
  bool moving = mode == M_YAW_CW || mode == M_YAW_CCW;
  if (!moving) enPwm = 0;
  else if (since(lastRamp, 20) && enPwm < 255) {   // ~1 s soft start
    lastRamp = millis();
    enPwm = enPwm > 250 ? 255 : enPwm + 5;
  }
  digitalWrite(PIN_CW, mode == M_YAW_CW);
  digitalWrite(PIN_CCW, mode == M_YAW_CCW);
  analogWrite(PIN_EN, enPwm);
  unsigned long t = millis();
  digitalWrite(PIN_LED, mode == M_FAULT ? (t / 200) & 1 : moving);
}

static void report() {
  static unsigned long last = 0;
  if (!since(last, 1000)) return;
  last = millis();
  char line[17], w[6], n[6];
  dtostrf(windDeg, 3, 0, w);
  snprintf(line, sizeof(line), "Wind %-3s %sdeg  ", DIR_NAME[(int)((windDeg + 11.25f) / 22.5f) % 16], w);
  lcd.setCursor(0, 0);
  lcd.print(line);
  dtostrf(nacelleDeg, 4, 0, n);
  snprintf(line, sizeof(line), "Nac%s %-9s", n, MODE_NAME[mode]);
  lcd.setCursor(0, 1);
  lcd.print(line);

  // DATA,seconds,wind_deg,steadiness,nacelle_deg,target_deg,mode
  Serial.print(F("DATA,"));
  Serial.print(millis() / 1000UL);
  Serial.print(','); Serial.print(windDeg, 0);
  Serial.print(','); Serial.print(steadiness, 2);
  Serial.print(','); Serial.print(nacelleDeg, 0);
  Serial.print(','); Serial.print(targetDeg, 0);
  Serial.print(','); Serial.println(MODE_NAME[mode]);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_CW, OUTPUT);
  pinMode(PIN_CCW, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PARK, INPUT_PULLUP);
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Wind Vane + Yaw");

  for (uint8_t i = 0; i < 16; i++) {
    vaneAdc[i] = (uint16_t)(1023.0f * VANE_R[i] / (VANE_R[i] + 10000.0f) + 0.5f);
  }
  // Start the average at the current reading.
  int8_t idx = readVaneIndex();
  if (idx < 0) idx = 0;
  avgX = cos(idx * 22.5f * DEG_TO_RAD);
  avgY = sin(idx * 22.5f * DEG_TO_RAD);
  logMsg("Wind vane + yaw controller v1.0");
  Serial.println(F("DATA,seconds,wind_deg,steadiness,nacelle_deg,target_deg,mode"));
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  sense();
  control();
  drive();
  report();
}
