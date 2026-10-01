/*
 * ============================================================================
 *  Smart Parking System (6 slots, entry/exit barriers, live slot display)
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  For a municipality, hospital or mall car park. Every slot has an IR
 *  sensor, so the count is always the real number of free spaces (not an
 *  entry/exit counter that drifts during the day).
 *
 *    - LCD shows free spaces and a map of the 6 slots
 *    - entry barrier opens only if a slot is free; when full, a beep and
 *      "FULL" on the display, the barrier stays down
 *    - exit barrier always opens for a car leaving
 *    - a barrier NEVER closes while its sensor sees a car under it
 *    - barrier closes 2 s after the car has passed; if a car sits at an open
 *      barrier for 20 s the buzzer warns the attendant
 *    - slot sensors are filtered for 1.5 s (people walking past, reflections)
 *    - entry and exit counts plus a CSV event log over USB
 *
 *  Pin map
 *    D2..D7  slot 1..6 IR sensors (LOW = car in slot)
 *    A0      entry sensor (LOW = car at the barrier)
 *    A1      exit sensor  (LOW = car at the barrier)
 *    D9      entry barrier servo     D10  exit barrier servo
 *    D8      buzzer                  D13  FULL lamp
 *    D12 D11 A2 A3 A4 A5  LCD RS EN D4 D5 D6 D7
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <Servo.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
const uint8_t  SLOTS          = 6;
const uint16_t SLOT_FILTER_MS = 1500;
const uint16_t CAR_FILTER_MS  = 300;
const uint16_t CLOSE_AFTER_MS = 2000;
const uint16_t STUCK_WARN_MS  = 20000;
const uint8_t  GATE_DOWN_DEG  = 0;
const uint8_t  GATE_UP_DEG    = 90;
const uint8_t  GATE_STEP_MS   = 12;     // 90 deg in about 1.1 s

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_SLOT[SLOTS] = { 2, 3, 4, 5, 6, 7 };
const uint8_t PIN_ENTRY_CAR = A0, PIN_EXIT_CAR = A1;
const uint8_t PIN_ENTRY_SERVO = 9, PIN_EXIT_SERVO = 10;
const uint8_t PIN_BUZZER = 8, PIN_FULL = 13;

LiquidCrystal lcd(12, 11, A2, A3, A4, A5);

// ============================================================================
//  TYPES
// ============================================================================
struct Filtered {
  uint8_t pin;
  uint16_t settle;
  bool stable, pending;
  unsigned long changedAt;

  bool update() {                       // true when the stable value changes
    bool raw = digitalRead(pin) == LOW;
    if (raw != pending) { pending = raw; changedAt = millis(); }
    if (pending != stable && millis() - changedAt >= settle) {
      stable = pending;
      return true;
    }
    return false;
  }
};

struct Gate {
  const char *name;
  Servo servo;
  uint8_t pin;
  Filtered car;
  bool open;
  uint8_t pos;
  unsigned long lastStep, clearSince, openSince;
  bool warned;
};

Filtered slot[SLOTS];
Gate entry = { "entry", Servo(), PIN_ENTRY_SERVO, { PIN_ENTRY_CAR, CAR_FILTER_MS, false, false, 0 },
               false, GATE_DOWN_DEG, 0, 0, 0, false };
Gate exitG = { "exit", Servo(), PIN_EXIT_SERVO, { PIN_EXIT_CAR, CAR_FILTER_MS, false, false, 0 },
               false, GATE_DOWN_DEG, 0, 0, 0, false };

uint8_t freeSlots = SLOTS;
uint16_t entries = 0, exitsCount = 0;
unsigned long beepUntil = 0;

// ============================================================================
//  HELPERS
// ============================================================================
static void logEvent(const char *what) {
  // EVENT,seconds,what,free
  Serial.print(F("EVENT,"));
  Serial.print(millis() / 1000UL);
  Serial.print(',');
  Serial.print(what);
  Serial.print(',');
  Serial.println(freeSlots);
}

static void beep(uint16_t ms) { beepUntil = millis() + ms; }

static void countFree() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SLOTS; i++) if (!slot[i].stable) n++;
  freeSlots = n;
}

// ============================================================================
//  GATES
// ============================================================================
static void gateTask(Gate &g, bool allowedToOpen) {
  bool changed = g.car.update();
  bool carHere = g.car.stable;

  if (!g.open && carHere && changed) {
    if (allowedToOpen) {
      g.open = true;
      g.openSince = millis();
      g.warned = false;
      char buf[24];
      snprintf(buf, sizeof(buf), "%s_open", g.name);
      logEvent(buf);
    } else {
      beep(600);
      logEvent("entry_refused_full");
    }
  }

  if (g.open) {
    if (carHere) {
      g.clearSince = millis();
      if (!g.warned && millis() - g.openSince > STUCK_WARN_MS) {
        g.warned = true;
        beep(2000);
        char buf[24];
        snprintf(buf, sizeof(buf), "%s_car_waiting", g.name);
        logEvent(buf);
      }
    } else if (millis() - g.clearSince >= CLOSE_AFTER_MS) {
      g.open = false;
      if (&g == &entry) entries++; else exitsCount++;
      char buf[24];
      snprintf(buf, sizeof(buf), "%s_closed", g.name);
      logEvent(buf);
    }
  }

  // Slow, smooth barrier movement. If a car appears under a barrier that is
  // still coming down, it goes back up, even when the car park is full.
  // Safety beats the slot count.
  if (!g.open && carHere && g.pos > GATE_DOWN_DEG) {
    g.open = true;
    g.openSince = millis();
    logEvent("safety_reopen");
  }
  uint8_t target = g.open ? GATE_UP_DEG : GATE_DOWN_DEG;
  if (g.pos != target && millis() - g.lastStep >= GATE_STEP_MS) {
    g.lastStep = millis();
    g.pos += g.pos < target ? 1 : -1;
    g.servo.write(g.pos);
  }
}

// ============================================================================
//  DISPLAY
// ============================================================================
static void display() {
  static unsigned long last = 0;
  if (millis() - last < 300) return;
  last = millis();

  char line[17];
  if (freeSlots == 0) snprintf(line, sizeof(line), "PARKING FULL    ");
  else snprintf(line, sizeof(line), "Free %u/%u  In%c Ot%c", freeSlots, SLOTS,
                entry.open ? '^' : '_', exitG.open ? '^' : '_');
  lcd.setCursor(0, 0);
  lcd.print(line);
  for (uint8_t i = 0; i < SLOTS; i++) {
    line[i * 2] = '1' + i;
    line[i * 2 + 1] = slot[i].stable ? 'X' : '-';
  }
  for (uint8_t i = SLOTS * 2; i < 16; i++) line[i] = ' ';
  line[16] = '\0';
  lcd.setCursor(0, 1);
  lcd.print(line);

  digitalWrite(PIN_FULL, freeSlots == 0);
  if ((long)(beepUntil - millis()) > 0) tone(PIN_BUZZER, 1800);
  else noTone(PIN_BUZZER);
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  Serial.begin(9600);
  for (uint8_t i = 0; i < SLOTS; i++) {
    pinMode(PIN_SLOT[i], INPUT_PULLUP);
    slot[i] = { PIN_SLOT[i], SLOT_FILTER_MS, false, false, 0 };
    slot[i].stable = slot[i].pending = digitalRead(PIN_SLOT[i]) == LOW;
  }
  pinMode(PIN_ENTRY_CAR, INPUT_PULLUP);
  pinMode(PIN_EXIT_CAR, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_FULL, OUTPUT);
  entry.servo.attach(PIN_ENTRY_SERVO);
  exitG.servo.attach(PIN_EXIT_SERVO);
  entry.servo.write(GATE_DOWN_DEG);
  exitG.servo.write(GATE_DOWN_DEG);
  countFree();

  lcd.begin(16, 2);
  Serial.println(F("# Smart parking v1.0"));
  Serial.println(F("EVENT,seconds,event,free_slots"));
  logEvent("start");
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  for (uint8_t i = 0; i < SLOTS; i++) {
    if (slot[i].update()) {
      char buf[20];
      snprintf(buf, sizeof(buf), "slot%u_%s", i + 1, slot[i].stable ? "taken" : "free");
      countFree();
      logEvent(buf);
    }
  }
  gateTask(entry, freeSlots > 0);
  gateTask(exitG, true);
  display();
}
