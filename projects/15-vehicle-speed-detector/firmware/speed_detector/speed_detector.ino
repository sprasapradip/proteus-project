/*
 * ============================================================================
 *  Road Vehicle Speed Detector with "SLOW DOWN" warning sign
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  Two infrared break-beams across the road, a known distance apart. The
 *  time between the vehicle breaking beam A and beam B gives its speed.
 *  For school zones, hospital roads and village bazaars, where a flashing
 *  sign with your own speed slows drivers down better than a fixed board.
 *
 *    - both directions (A then B, or B then A)
 *    - timing by hardware interrupts in microseconds: at 1 m spacing and
 *      60 km/h the error is far below 0.1 km/h
 *    - over the limit: SLOW DOWN sign + beep for 5 s, speed on the LCD
 *    - a vehicle must clear both beams before the next one is measured, so
 *      a long truck or bus counts once
 *    - only one beam broken (pedestrian, dog, a bird) times out after 2 s
 *      and is ignored; impossible speeds (> 200 km/h) are rejected
 *    - counts, average and top speed, violations; CSV line per vehicle
 *
 *  Pin map
 *    D2  beam A receiver (LOW = beam broken)   D3  beam B receiver
 *    D7  RESET counters button to GND
 *    D8  green "OK" lamp     D10 SLOW DOWN sign (relay / LED driver)
 *    D9  buzzer              D13 status LED
 *    D12 D11 A0 A1 A2 A3  LCD RS EN D4 D5 D6 D7
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
const float    BEAM_GAP_M      = 1.00f;    // measure it on site, to the cm
const float    SPEED_LIMIT_KMH = 40.0f;
const float    MAX_PLAUSIBLE   = 200.0f;
const uint16_t PASS_TIMEOUT_MS = 2000;
const uint16_t CLEAR_MS        = 300;      // both beams clear this long
const uint16_t WARN_MS         = 5000;

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_A = 2, PIN_B = 3, PIN_RESET = 7;
const uint8_t PIN_OK = 8, PIN_BUZZER = 9, PIN_SIGN = 10, PIN_LED = 13;

LiquidCrystal lcd(12, 11, A0, A1, A2, A3);

// ============================================================================
//  STATE
// ============================================================================
volatile unsigned long tA = 0, tB = 0;      // micros() of first break
volatile bool hitA = false, hitB = false;
volatile bool armed = true;

enum Phase : uint8_t { WAITING, MEASURING, PASSING };
Phase phase = WAITING;
unsigned long phaseSince = 0, clearSince = 0;
bool clearActive = false;

uint32_t vehicles = 0, violations = 0, rejected = 0;
float lastKmh = 0, topKmh = 0;
double sumKmh = 0;
char lastDir = '-';
unsigned long warnUntil = 0;

// ============================================================================
//  INTERRUPTS
// ============================================================================
void onBeamA() {
  if (armed && !hitA) { tA = micros(); hitA = true; }
}
void onBeamB() {
  if (armed && !hitB) { tB = micros(); hitB = true; }
}

// ============================================================================
//  HELPERS
// ============================================================================
static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }

static void logMsg(const char *msg) {
  Serial.print(F("# ["));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

static void rearm() {
  noInterrupts();
  hitA = hitB = false;
  armed = true;
  interrupts();
  phase = WAITING;
}

static void record(float kmh, char dir) {
  vehicles++;
  lastKmh = kmh;
  lastDir = dir;
  sumKmh += kmh;
  if (kmh > topKmh) topKmh = kmh;
  bool over = kmh > SPEED_LIMIT_KMH;
  if (over) {
    violations++;
    warnUntil = millis() + WARN_MS;
  }
  // VEHICLE,seconds,direction,kmh,over_limit
  Serial.print(F("VEHICLE,"));
  Serial.print(millis() / 1000UL);
  Serial.print(',');
  Serial.print(dir == '>' ? F("A>B") : F("B>A"));
  Serial.print(',');
  Serial.print(kmh, 1);
  Serial.print(',');
  Serial.println(over ? 1 : 0);
}

// ============================================================================
//  MEASUREMENT
// ============================================================================
static void measure() {
  noInterrupts();
  bool a = hitA, b = hitB;
  unsigned long ta = tA, tb = tB;
  interrupts();
  bool beamsClear = digitalRead(PIN_A) == HIGH && digitalRead(PIN_B) == HIGH;

  switch (phase) {
    case WAITING:
      if (a || b) { phase = MEASURING; phaseSince = millis(); }
      break;

    case MEASURING:
      if (a && b) {
        noInterrupts(); armed = false; interrupts();
        unsigned long dt = ta < tb ? tb - ta : ta - tb;
        char dir = ta < tb ? '>' : '<';
        float kmh = dt > 0 ? BEAM_GAP_M / (dt / 1e6f) * 3.6f : 9999;
        if (kmh > MAX_PLAUSIBLE) {
          rejected++;
          logMsg("rejected: impossible speed (beam noise?)");
        } else {
          record(kmh, dir);
        }
        phase = PASSING;
        clearActive = false;
      } else if (since(phaseSince, PASS_TIMEOUT_MS)) {
        rejected++;
        logMsg("ignored: only one beam broken (pedestrian / animal)");
        noInterrupts(); armed = false; interrupts();
        phase = PASSING;
        clearActive = false;
      }
      break;

    case PASSING:
      // Wait for the whole vehicle (or person) to leave both beams.
      if (beamsClear && !clearActive) clearSince = millis();
      clearActive = beamsClear;
      if (beamsClear && since(clearSince, CLEAR_MS)) rearm();
      break;
  }
}

// ============================================================================
//  BUTTON / OUTPUTS / DISPLAY
// ============================================================================
static void handleReset() {
  static bool last = false;
  static unsigned long changed = 0;
  bool now = digitalRead(PIN_RESET) == LOW;
  if (now != last && since(changed, 50)) {
    changed = millis();
    last = now;
    if (now) {
      vehicles = violations = rejected = 0;
      sumKmh = topKmh = lastKmh = 0;
      logMsg("counters reset");
    }
  }
}

static void outputs() {
  unsigned long t = millis();
  bool warn = (long)(warnUntil - t) > 0;
  digitalWrite(PIN_SIGN, warn && ((t / 400) & 1));
  digitalWrite(PIN_OK, !warn);
  digitalWrite(PIN_LED, phase != WAITING);
  static bool toneOn = false;
  bool want = warn && (t % 1000) < 150;
  if (want && !toneOn) tone(PIN_BUZZER, 2500);
  if (!want && toneOn) noTone(PIN_BUZZER);
  toneOn = want;
}

static void display() {
  static unsigned long last = 0;
  if (!since(last, 250)) return;
  last = millis();
  char line[17], s[8], avg[8];
  dtostrf(lastKmh, 5, 1, s);
  bool warn = (long)(warnUntil - millis()) > 0;
  snprintf(line, sizeof(line), "%skm/h %s", s, warn ? "SLOW! " : "      ");
  lcd.setCursor(0, 0);
  lcd.print(line);
  dtostrf(vehicles ? sumKmh / vehicles : 0, 4, 0, avg);
  snprintf(line, sizeof(line), "N%-4lu V%-3lu A%s", (unsigned long)vehicles,
           (unsigned long)violations, avg);
  lcd.setCursor(0, 1);
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
  pinMode(PIN_OK, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SIGN, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_A), onBeamA, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_B), onBeamB, FALLING);
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Speed Detector");
  logMsg("Vehicle speed detector v1.0");
  Serial.println(F("VEHICLE,seconds,direction,kmh,over_limit"));
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  measure();
  handleReset();
  outputs();
  display();
}
