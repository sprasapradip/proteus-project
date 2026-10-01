/*
 * ============================================================================
 *  DC Power & Energy Meter with protection relay
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  For solar / battery / DC supply work: shows volts, amps, watts and
 *  watt-hours, logs CSV over USB, and disconnects the load on
 *  over-voltage, over-current or a flat battery.
 *
 *  Measurement
 *    - voltage: 100k / 22k divider into A0  (0 .. 27.7 V)
 *    - current: ACS712-05B hall sensor into A1 (185 mV/A, +-5 A)
 *    - current zero is measured at power-on with the load relay open
 *    - 64-sample averaging per reading, 5 readings per second
 *
 *  Protection (relay on D8 connects the load)
 *    - over-voltage  > 26.0 V for 0.5 s   -> trip, latched
 *    - over-current  > 4.5 A  for 0.2 s   -> trip, latched
 *    - sensor at full scale (short)       -> trip at once, latched
 *    - low voltage   < 10.5 V for 3 s     -> disconnect, reconnects by
 *                                            itself above 12.0 V for 10 s
 *    Latched trips clear with the button once the cause is gone.
 *    Holding the button 3 s clears the Wh counter.
 *
 *  Energy is saved to EEPROM every 10 minutes, rotated across 16 slots so
 *  the EEPROM lasts for decades instead of about a year.
 *
 *  Pin map
 *    A0 voltage divider   A1 ACS712 OUT
 *    D2 D3 D4 D5 D11 D12  LCD D7 D6 D5 D4 EN RS (classic Arduino wiring)
 *    D7 push button to GND (reset trip / hold 3 s = clear Wh)
 *    D8 load relay driver   D9 buzzer   D13 trip LED
 * ============================================================================
 */

#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <avr/wdt.h>

// ============================================================================
//  CALIBRATION / SETTINGS
// ============================================================================
const float VREF          = 5.00;     // measure your 5 V rail and put it here
const float DIVIDER_RATIO = 22.0 / (100.0 + 22.0);
const float ACS_MV_PER_A  = 185.0;    // 05B: 185, 20A: 100, 30A: 66

const float OV_TRIP_V      = 26.0;
const float OC_TRIP_A      = 4.5;
const float UV_CUT_V       = 10.5;
const float UV_RECONNECT_V = 12.0;

const uint16_t OV_MS = 500, OC_MS = 200, UV_MS = 3000, UV_RECOVER_MS = 10000;
const unsigned long SAVE_EVERY_MS = 600000UL;

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_VSENSE = A0;
const uint8_t PIN_ISENSE = A1;
const uint8_t PIN_BUTTON = 7;
const uint8_t PIN_RELAY  = 8;
const uint8_t PIN_BUZZER = 9;
const uint8_t PIN_LED    = 13;

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ============================================================================
//  STATE
// ============================================================================
enum State : uint8_t { ST_ON, ST_LOW_V, ST_TRIP_OV, ST_TRIP_OC, ST_TRIP_SHORT };
const char *const STATE_NAME[] = { "ON", "LOW VOLT", "TRIP OVER-V", "TRIP OVER-I", "TRIP SHORT" };

State state = ST_ON;
float volts = 0, amps = 0, watts = 0;
double wattHours = 0;
float zeroMv = 2500;
bool rawCurrentAtRail = false;

unsigned long lastMeasure = 0, lastLog = 0, lastSave = 0;
unsigned long ovSince = 0, ocSince = 0, uvSince = 0, recoverSince = 0;
bool ovActive = false, ocActive = false, uvActive = false, recoverActive = false;

static bool since(unsigned long t0, unsigned long d) { return (millis() - t0) >= d; }

static void logMsg(const char *msg) {
  Serial.print(F("# ["));
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

// ============================================================================
//  EEPROM (16-slot wear levelling)
// ============================================================================
struct Record {
  uint32_t seq;
  float wh;
  uint8_t check;
};
const uint8_t SLOTS = 16;
uint32_t saveSeq = 0;

static uint8_t checksum(const Record &r) {
  const uint8_t *p = (const uint8_t *)&r;
  uint8_t c = 0xA5;
  for (uint8_t i = 0; i < offsetof(Record, check); i++) c = (c << 1 | c >> 7) ^ p[i];
  return c;
}

static void loadEnergy() {
  Record best = { 0, 0, 0 };
  bool found = false;
  for (uint8_t s = 0; s < SLOTS; s++) {
    Record r;
    EEPROM.get(s * sizeof(Record), r);
    if (r.check == checksum(r) && r.seq != 0xFFFFFFFFUL && (!found || r.seq > best.seq)) {
      best = r;
      found = true;
    }
  }
  if (found && best.wh >= 0 && best.wh < 1e7) {
    wattHours = best.wh;
    saveSeq = best.seq;
  }
}

static void saveEnergy() {
  Record r;
  r.seq = ++saveSeq;
  r.wh = (float)wattHours;
  r.check = checksum(r);
  EEPROM.put((r.seq % SLOTS) * sizeof(Record), r);
  logMsg("energy saved");
}

// ============================================================================
//  MEASUREMENT
// ============================================================================
static float readMv(uint8_t pin, bool *atRail) {
  uint32_t sum = 0;
  uint16_t hi = 0;
  for (uint8_t i = 0; i < 64; i++) {
    uint16_t v = analogRead(pin);
    sum += v;
    if (v > hi) hi = v;
  }
  if (atRail) *atRail = hi >= 1020;
  return (sum / 64.0f) * VREF * 1000.0f / 1023.0f;
}

static void measure() {
  unsigned long now = millis();
  unsigned long dt = now - lastMeasure;
  if (dt < 200) return;
  lastMeasure = now;

  volts = readMv(PIN_VSENSE, NULL) / 1000.0f / DIVIDER_RATIO;
  amps = (readMv(PIN_ISENSE, &rawCurrentAtRail) - zeroMv) / ACS_MV_PER_A;
  if (fabs(amps) < 0.02f) amps = 0;          // hide sensor noise around zero
  if (digitalRead(PIN_RELAY) == LOW) amps = 0;
  watts = volts * amps;
  if (watts > 0) wattHours += watts * dt / 3600000.0;
}

// ============================================================================
//  PROTECTION
// ============================================================================
static void setState(State s) {
  if (s == state) return;
  state = s;
  digitalWrite(PIN_RELAY, s == ST_ON ? HIGH : LOW);
  char buf[40];
  snprintf(buf, sizeof(buf), "state -> %s", STATE_NAME[s]);
  logMsg(buf);
}

// Tracks how long a condition has been true.
static bool heldFor(bool cond, bool &active, unsigned long &t0, unsigned long need) {
  if (cond && !active) t0 = millis();
  active = cond;
  return cond && since(t0, need);
}

static bool latched() { return state == ST_TRIP_OV || state == ST_TRIP_OC || state == ST_TRIP_SHORT; }

static void protect() {
  if (latched()) return;

  if (rawCurrentAtRail && state == ST_ON) { setState(ST_TRIP_SHORT); return; }
  if (heldFor(volts > OV_TRIP_V, ovActive, ovSince, OV_MS)) { setState(ST_TRIP_OV); return; }
  if (heldFor(fabs(amps) > OC_TRIP_A, ocActive, ocSince, OC_MS)) { setState(ST_TRIP_OC); return; }

  if (state == ST_ON) {
    if (heldFor(volts < UV_CUT_V, uvActive, uvSince, UV_MS)) setState(ST_LOW_V);
  } else if (state == ST_LOW_V) {
    if (heldFor(volts > UV_RECONNECT_V, recoverActive, recoverSince, UV_RECOVER_MS)) {
      recoverActive = false;
      uvActive = false;
      setState(ST_ON);
    }
  }
}

static void handleButton() {
  static bool last = false, stable = false;
  static unsigned long changed = 0, pressedAt = 0;
  static bool longDone = false;
  bool raw = digitalRead(PIN_BUTTON) == LOW;
  if (raw != last) { last = raw; changed = millis(); }
  if (raw != stable && since(changed, 50)) {
    stable = raw;
    if (stable) {
      pressedAt = millis();
      longDone = false;
    } else if (!longDone && latched()) {
      bool causeGone = volts <= OV_TRIP_V && volts >= UV_CUT_V;
      if (causeGone) {
        ovActive = ocActive = false;
        logMsg("trip reset by user");
        setState(ST_ON);
      } else {
        logMsg("reset refused: voltage still out of range");
      }
    }
  }
  if (stable && !longDone && since(pressedAt, 3000)) {
    longDone = true;
    wattHours = 0;
    saveEnergy();
    logMsg("energy counter cleared");
  }
}

// ============================================================================
//  OUTPUT
// ============================================================================
static void display() {
  static unsigned long last = 0;
  if (!since(last, 500)) return;
  last = millis();
  char a[8], b[8], line[17];

  dtostrf(volts, 5, 2, a);
  dtostrf(amps, 6, 3, b);
  snprintf(line, sizeof(line), "%sV %sA ", a, b);
  lcd.setCursor(0, 0);
  lcd.print(line);

  lcd.setCursor(0, 1);
  if (state == ST_ON) {
    dtostrf(watts, 5, 1, a);
    if (wattHours < 1000) {
      dtostrf(wattHours, 6, 1, b);
      snprintf(line, sizeof(line), "%sW %sWh", a, b);
    } else {
      dtostrf(wattHours / 1000.0, 5, 2, b);
      snprintf(line, sizeof(line), "%sW %skWh", a, b);
    }
  } else {
    snprintf(line, sizeof(line), "%-16s", STATE_NAME[state]);
  }
  lcd.print(line);
}

static void csvLog() {
  if (!since(lastLog, 1000)) return;
  lastLog = millis();
  // DATA,seconds,volts,amps,watts,watt_hours,state
  Serial.print(F("DATA,"));
  Serial.print(millis() / 1000UL);
  Serial.print(',');
  Serial.print(volts, 2);
  Serial.print(',');
  Serial.print(amps, 3);
  Serial.print(',');
  Serial.print(watts, 2);
  Serial.print(',');
  Serial.print(wattHours, 3);
  Serial.print(',');
  Serial.println(STATE_NAME[state]);
}

static void alerts() {
  unsigned long t = millis();
  bool trip = latched();
  digitalWrite(PIN_LED, trip ? (t / 250) & 1 : state == ST_LOW_V ? (t / 1000) & 1 : LOW);
  static bool toneOn = false;
  bool want = trip && (t % 1000) < 200;
  if (want && !toneOn) tone(PIN_BUZZER, 2000);
  if (!want && toneOn) noTone(PIN_BUZZER);
  toneOn = want;
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);          // load stays off until we've checked
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("DC Power Meter");
  lcd.setCursor(0, 1);
  lcd.print("calibrating...");

  // Relay is open, so whatever the ACS712 reads now is its zero.
  delay(500);
  zeroMv = readMv(PIN_ISENSE, NULL);
  loadEnergy();
  char buf[48];
  char z[8];
  dtostrf(zeroMv, 6, 1, z);
  snprintf(buf, sizeof(buf), "DC power meter v1.0, current zero %s mV", z);
  logMsg(buf);
  Serial.println(F("DATA,seconds,volts,amps,watts,watt_hours,state"));

  // First reading, then connect the load only if the voltage is sane.
  lastMeasure = millis() - 200;
  measure();
  state = ST_LOW_V;                       // forces setState() below to act
  if (volts > OV_TRIP_V)      setState(ST_TRIP_OV);
  else if (volts < UV_CUT_V)  logMsg("state -> LOW VOLT");
  else                        setState(ST_ON);

  lcd.clear();
  lastSave = millis();
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  measure();
  protect();
  handleButton();
  display();
  csvLog();
  alerts();
  if (since(lastSave, SAVE_EVERY_MS)) {
    lastSave = millis();
    saveEnergy();
  }
}
