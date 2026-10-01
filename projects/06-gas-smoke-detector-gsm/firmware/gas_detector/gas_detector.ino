/*
 * ============================================================================
 *  Gas / Smoke Detector with SMS alert  (v2)
 * ============================================================================
 *  Author  : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board   : Arduino Uno (ATmega328P @ 16 MHz)
 *
 *  Sensors : MQ-2 (LPG / smoke) on A0, MQ-3 (alcohol / vapour) on A1
 *  Actions : buzzer + red LED, exhaust fan relay, gas valve shut-off servo,
 *            SMS through a SIM900 GSM module
 *
 *  What changed from v1 (2023):
 *    - 60 s sensor warm-up, no false alarms at power on
 *    - averaging + 2 s confirmation + hysteresis, so a single spike or a
 *      reading hovering at the threshold doesn't make the alarm chatter
 *    - pre-alarm WARNING level with a short beep
 *    - exhaust fan keeps running 60 s after the gas clears (purge)
 *    - gas valve closes on alarm and stays closed until a person presses
 *      RESET with the air clear. It never reopens by itself.
 *    - SMS to up to two numbers, one per event, reminder every 10 min
 *    - broken / unplugged sensor detection (reading stuck at 0 or 1023)
 *    - mute button, watchdog, readable serial log
 *
 *  Pin map
 *    A0  MQ-2 analog out           D6   servo signal (gas valve)
 *    A1  MQ-3 analog out           D8   relay driver (2N5551) -> fan
 *    D3  RESET / MUTE push button  D9   buzzer
 *    D10 SoftwareSerial RX  <- SIM900 TX
 *    D11 SoftwareSerial TX  -> SIM900 RX
 *    D12 green "power / OK" LED    D13  red alarm LED
 *    D0/D1 USB serial log (9600)
 * ============================================================================
 */

#include <Servo.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h>

// ============================================================================
//  SETTINGS
// ============================================================================
// Alert phone numbers in international format, e.g. "+97798XXXXXXXX".
// Leave empty to disable SMS.
#ifndef ALERT_PHONE_1
#define ALERT_PHONE_1 ""
#endif
#ifndef ALERT_PHONE_2
#define ALERT_PHONE_2 ""
#endif
#ifndef SITE_NAME
#define SITE_NAME "Kitchen"
#endif

// 100 = real time. 10 makes the long timers (warm-up, purge, SMS reminder,
// mute) 10x faster for Proteus demos. Alarm confirmation and fault
// detection always run at real speed so the demo behaves like the real thing.
#ifndef TIME_SCALE_PERCENT
#define TIME_SCALE_PERCENT 100
#endif

// ADC thresholds (0..1023). Calibrate on site: log the clean-air value for a
// day, then set WARN about 1.5x and ALARM about 2x that value.
const uint16_t MQ2_WARN = 250, MQ2_ALARM = 400, MQ2_CLEAR = 320;
const uint16_t MQ3_WARN = 300, MQ3_ALARM = 450, MQ3_CLEAR = 370;

const uint16_t WARMUP_SEC      = 60;
const uint16_t CONFIRM_MS      = 2000;   // level must stay high this long
const uint16_t PURGE_SEC       = 60;     // fan run-on after the gas clears
const uint16_t SMS_REPEAT_MIN  = 10;     // reminder while alarm continues
const uint16_t MUTE_MIN        = 5;
const uint16_t FAULT_MS        = 5000;   // stuck reading before FAULT

const uint8_t SERVO_OPEN_DEG   = 0;
const uint8_t SERVO_CLOSED_DEG = 90;

// ============================================================================
//  PINS
// ============================================================================
const uint8_t PIN_MQ2    = A0;
const uint8_t PIN_MQ3    = A1;
const uint8_t PIN_BUTTON = 3;
const uint8_t PIN_SERVO  = 6;
const uint8_t PIN_FAN    = 8;
const uint8_t PIN_BUZZER = 9;
const uint8_t PIN_GSM_RX = 10;
const uint8_t PIN_GSM_TX = 11;
const uint8_t PIN_LED_OK = 12;
const uint8_t PIN_LED_ALARM = 13;

// ============================================================================
//  TYPES / GLOBALS
// ============================================================================
enum State : uint8_t { ST_WARMUP, ST_NORMAL, ST_WARNING, ST_ALARM, ST_PURGE, ST_FAULT };
const char *const STATE_NAME[] = { "WARMUP", "NORMAL", "WARNING", "ALARM", "PURGE", "SENSOR FAULT" };

struct Channel {
  const char *name;
  uint8_t pin;
  uint16_t warn, alarm, clear;
  uint16_t ring[16];
  uint8_t idx;
  uint16_t avg;
  bool above;                 // average is at or over the alarm level
  unsigned long aboveSince;
  bool stuck;                 // raw reading pinned at 0 or 1023
  unsigned long stuckSince;
};

Channel ch[2] = {
  { "MQ-2", PIN_MQ2, MQ2_WARN, MQ2_ALARM, MQ2_CLEAR, {0}, 0, 0, false, 0, false, 0 },
  { "MQ-3", PIN_MQ3, MQ3_WARN, MQ3_ALARM, MQ3_CLEAR, {0}, 0, 0, false, 0, false, 0 },
};

Servo valve;
SoftwareSerial gsm(PIN_GSM_RX, PIN_GSM_TX);

State state = ST_WARMUP;
unsigned long stateSince = 0;
bool valveClosed = false;
unsigned long mutedUntil = 0;
unsigned long lastSmsAt = 0;
unsigned long lastSampleAt = 0;
unsigned long lastReportAt = 0;
uint8_t triggerChannel = 0;

// ============================================================================
//  HELPERS
// ============================================================================
static unsigned long ms(unsigned long realMs) {
  return realMs * TIME_SCALE_PERCENT / 100UL;
}

static bool since(unsigned long t0, unsigned long dur) {
  return (millis() - t0) >= dur;
}

static void logLine(const __FlashStringHelper *msg) {
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.println(msg);
}

static void setState(State s) {
  state = s;
  stateSince = millis();
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] state -> "));
  Serial.println(STATE_NAME[s]);
}

static void setValve(bool closed) {
  if (closed == valveClosed) return;
  valveClosed = closed;
  valve.write(closed ? SERVO_CLOSED_DEG : SERVO_OPEN_DEG);
  logLine(closed ? F("gas valve CLOSED") : F("gas valve opened"));
}

// ============================================================================
//  SMS (non-blocking, one message at a time)
// ============================================================================
enum SmsStep : uint8_t { SMS_IDLE, SMS_MODE, SMS_NUMBER, SMS_BODY, SMS_WAIT_SENT, SMS_GAP };

struct {
  SmsStep step = SMS_IDLE;
  char text[100];
  uint8_t phone = 0;           // index into PHONES
  unsigned long at = 0;
  char reply[24];
  uint8_t replyLen = 0;
} sms;

const char *const PHONES[] = { ALERT_PHONE_1, ALERT_PHONE_2 };

static bool smsEnabled() {
  return PHONES[0][0] != '\0' || PHONES[1][0] != '\0';
}

static void smsQueue(const char *text) {
  if (!smsEnabled()) {
    logLine(F("SMS skipped: no phone number set"));
    return;
  }
  if (sms.step != SMS_IDLE) {
    logLine(F("SMS busy, newest message replaces the queued one"));
  }
  strncpy(sms.text, text, sizeof(sms.text) - 1);
  sms.text[sizeof(sms.text) - 1] = '\0';
  sms.phone = 0;
  sms.step = SMS_MODE;
  sms.at = 0;
  logLine(F("SMS queued"));
}

static void smsReadReply() {
  while (gsm.available()) {
    char c = gsm.read();
    if (sms.replyLen < sizeof(sms.reply) - 1) {
      sms.reply[sms.replyLen++] = c;
      sms.reply[sms.replyLen] = '\0';
    }
  }
}

static bool smsReplyHas(const char *s) { return strstr(sms.reply, s) != NULL; }

static void smsClearReply() { sms.replyLen = 0; sms.reply[0] = '\0'; }

static void smsNextPhone() {
  do { sms.phone++; } while (sms.phone < 2 && PHONES[sms.phone][0] == '\0');
  sms.step = sms.phone < 2 ? SMS_GAP : SMS_IDLE;
  sms.at = millis();
}

static void smsTask() {
  smsReadReply();
  switch (sms.step) {
    case SMS_IDLE:
      break;

    case SMS_MODE:
      if (PHONES[sms.phone][0] == '\0') { smsNextPhone(); break; }
      smsClearReply();
      gsm.print(F("AT+CMGF=1\r"));
      sms.step = SMS_NUMBER;
      sms.at = millis();
      break;

    case SMS_NUMBER:
      if (smsReplyHas("OK") || since(sms.at, 1000)) {
        smsClearReply();
        gsm.print(F("AT+CMGS=\""));
        gsm.print(PHONES[sms.phone]);
        gsm.print(F("\"\r"));
        sms.step = SMS_BODY;
        sms.at = millis();
      }
      break;

    case SMS_BODY:
      if (smsReplyHas(">")) {
        smsClearReply();
        gsm.print(sms.text);
        gsm.write(26);                 // Ctrl+Z sends the message
        sms.step = SMS_WAIT_SENT;
        sms.at = millis();
      } else if (since(sms.at, 5000)) {
        logLine(F("GSM: no '>' prompt, SMS failed (check SIM900 power / SIM card)"));
        smsNextPhone();
      }
      break;

    case SMS_WAIT_SENT:
      if (smsReplyHas("+CMGS") || smsReplyHas("OK")) {
        logLine(F("SMS sent"));
        smsNextPhone();
      } else if (smsReplyHas("ERROR") || since(sms.at, 15000)) {
        logLine(F("GSM: SMS not confirmed"));
        smsNextPhone();
      }
      break;

    case SMS_GAP:
      if (since(sms.at, 3000)) sms.step = SMS_MODE;
      break;
  }
}

static void smsAlarm(const char *what) {
  char buf[100];
  snprintf(buf, sizeof(buf), "ALERT %s: %s detected (%s=%u). Valve closed, fan on.",
           SITE_NAME, what, ch[triggerChannel].name, ch[triggerChannel].avg);
  smsQueue(buf);
  lastSmsAt = millis();
}

// ============================================================================
//  SENSORS
// ============================================================================
static void sampleSensors() {
  if (!since(lastSampleAt, 50)) return;
  lastSampleAt = millis();

  for (uint8_t i = 0; i < 2; i++) {
    Channel &c = ch[i];
    uint16_t raw = analogRead(c.pin);
    c.ring[c.idx] = raw;
    c.idx = (c.idx + 1) & 15;
    uint32_t sum = 0;
    for (uint8_t k = 0; k < 16; k++) sum += c.ring[k];
    c.avg = sum / 16;

    // A disconnected sensor reads ~0, a shorted one ~1023.
    bool stuck = raw < 20 || raw > 1015;
    if (stuck && !c.stuck) c.stuckSince = millis();
    c.stuck = stuck;

    bool above = c.avg >= c.alarm;
    if (above && !c.above) c.aboveSince = millis();
    c.above = above;
  }
}

static bool anyConfirmedAlarm() {
  for (uint8_t i = 0; i < 2; i++) {
    if (ch[i].above && since(ch[i].aboveSince, CONFIRM_MS)) {
      triggerChannel = i;
      return true;
    }
  }
  return false;
}

static bool allClear() {
  return ch[0].avg < ch[0].clear && ch[1].avg < ch[1].clear;
}

static bool anyWarn() {
  return ch[0].avg >= ch[0].warn || ch[1].avg >= ch[1].warn;
}

static bool anyStuck() {
  for (uint8_t i = 0; i < 2; i++) {
    if (ch[i].stuck && since(ch[i].stuckSince, FAULT_MS)) return true;
  }
  return false;
}

// ============================================================================
//  BUTTON
// ============================================================================
static bool buttonPressed() {
  static bool last = false, stable = false;
  static unsigned long changed = 0;
  bool raw = digitalRead(PIN_BUTTON) == LOW;
  if (raw != last) { last = raw; changed = millis(); }
  if (raw != stable && since(changed, 50)) {
    stable = raw;
    return stable;
  }
  return false;
}

static void handleButton() {
  if (!buttonPressed()) return;
  if (state == ST_ALARM) {
    mutedUntil = millis() + ms(MUTE_MIN * 60000UL);
    logLine(F("buzzer muted"));
  } else if (valveClosed && allClear()) {
    setValve(false);
    logLine(F("reset by user"));
  } else if (valveClosed) {
    logLine(F("reset refused: gas level still high"));
  }
}

// ============================================================================
//  STATE MACHINE
// ============================================================================
static void runStateMachine() {
  switch (state) {
    case ST_WARMUP:
      if (since(stateSince, ms(WARMUP_SEC * 1000UL))) setState(ST_NORMAL);
      return;

    case ST_FAULT:
      if (!anyStuck()) {
        logLine(F("sensor reading back to normal"));
        setState(ST_NORMAL);
      }
      return;

    default:
      break;
  }

  if (anyStuck()) {
    setState(ST_FAULT);
    smsQueue("FAULT " SITE_NAME ": gas sensor reading stuck. Check wiring.");
    return;
  }

  switch (state) {
    case ST_NORMAL:
    case ST_WARNING:
    case ST_PURGE:
      if (anyConfirmedAlarm()) {
        setState(ST_ALARM);
        setValve(true);
        mutedUntil = 0;
        smsAlarm(triggerChannel == 0 ? "Gas/smoke" : "Alcohol/vapour");
      } else if (state == ST_PURGE) {
        if (since(stateSince, ms(PURGE_SEC * 1000UL))) setState(ST_NORMAL);
      } else if (state == ST_NORMAL && anyWarn()) {
        setState(ST_WARNING);
      } else if (state == ST_WARNING && !anyWarn()) {
        setState(ST_NORMAL);
      }
      break;

    case ST_ALARM:
      if (allClear()) {
        setState(ST_PURGE);
        smsQueue("CLEAR " SITE_NAME ": gas level normal. Valve stays closed until reset.");
      } else if (since(lastSmsAt, ms(SMS_REPEAT_MIN * 60000UL))) {
        smsAlarm("Gas still");
      }
      break;

    default:
      break;
  }
}

static void driveOutputs() {
  unsigned long t = millis();
  bool blink = (t / 500) & 1;
  bool alarm = state == ST_ALARM;
  bool muted = mutedUntil && (long)(mutedUntil - t) > 0;

  digitalWrite(PIN_FAN, (alarm || state == ST_PURGE) ? HIGH : LOW);
  digitalWrite(PIN_LED_ALARM, alarm ? blink : (state == ST_WARNING || valveClosed));
  digitalWrite(PIN_LED_OK, state == ST_WARMUP ? blink : state != ST_FAULT);

  // Buzzer patterns: alarm = 0.5 s on/off, warning = short chirp every 5 s,
  // fault = double chirp every 10 s.
  static bool toneOn = false;
  bool want = false;
  if (alarm && !muted) want = blink;
  else if (state == ST_WARNING) want = (t % 5000) < 120;
  else if (state == ST_FAULT) want = (t % 10000) < 100 || ((t % 10000) > 250 && (t % 10000) < 350);

  if (want && !toneOn) tone(PIN_BUZZER, alarm ? 2500 : 1800);
  if (!want && toneOn) noTone(PIN_BUZZER);
  toneOn = want;
}

static void report() {
  if (!since(lastReportAt, 5000)) return;
  lastReportAt = millis();
  Serial.print('[');
  Serial.print(millis() / 1000UL);
  Serial.print(F("s] "));
  Serial.print(STATE_NAME[state]);
  Serial.print(F("  MQ-2="));
  Serial.print(ch[0].avg);
  Serial.print(F("  MQ-3="));
  Serial.print(ch[1].avg);
  Serial.print(F("  fan="));
  Serial.print(digitalRead(PIN_FAN) ? F("ON") : F("off"));
  Serial.print(F("  valve="));
  Serial.println(valveClosed ? F("CLOSED") : F("open"));
}

// ============================================================================
//  ARDUINO
// ============================================================================
void setup() {
  MCUSR = 0;
  wdt_disable();

  pinMode(PIN_FAN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_FAN, LOW);

  Serial.begin(9600);
  gsm.begin(9600);

  valve.attach(PIN_SERVO);
  valve.write(SERVO_OPEN_DEG);

  logLine(F("Gas/Smoke detector v2 - " SITE_NAME));
  if (!smsEnabled()) logLine(F("note: no alert phone number set, SMS disabled"));

  gsm.print(F("AT\r"));
  gsm.print(F("ATE0\r"));

  // Fill the averaging buffers so the first average is meaningful.
  for (uint8_t i = 0; i < 2; i++) {
    uint16_t v = analogRead(ch[i].pin);
    for (uint8_t k = 0; k < 16; k++) ch[i].ring[k] = v;
    ch[i].avg = v;
  }

  setState(ST_WARMUP);
  wdt_enable(WDTO_4S);
}

void loop() {
  wdt_reset();
  sampleSensors();
  handleButton();
  runStateMachine();
  driveOutputs();
  smsTask();
  report();
}
