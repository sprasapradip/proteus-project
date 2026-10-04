/*
 * ============================================================================
 *  SITE 1  -  Countdown display unit
 *  Mahendra Highway junction at Eatwell Bakery Cafe, Bharatpur
 * ============================================================================
 *  Author    : Pradip Subedi (github.com/sprasapradip)
 *  Copyright (c) 2023-2026 Pradip Subedi. All rights reserved.
 *              Proprietary - no use, copying or modification without written
 *              permission. See LICENSE in the repository root.
 *  Board     : Arduino Uno / ATmega328P @ 16 MHz (a second board, not the
 *              traffic controller)
 *  Version   : 1.0.0-site01
 *
 *  Listens to the traffic controller's countdown link (controller pin A5)
 *  and shows the seconds left for every arm on four 4-digit displays:
 *
 *      G 35   green, 35 s left          Y  4   yellow, 4 s left
 *      r 42   red, green in 42 s        r111   red, green in 111 s
 *      ----   night flash, emergency or fault (no time to show)
 *      blank  no valid data for 3 s (cable cut, controller off)
 *
 *  This board only displays. It has no way to change the lamps, and the
 *  controller runs the junction the same with or without it.
 *
 *  Wiring
 *    D0 (RX)  <- controller A5 (countdown link), plus a common GND
 *    D1 (TX)  -> optional virtual terminal: link up / link lost messages
 *    D11      -> DIN  of both MAX7219
 *    D13      -> CLK  of both MAX7219
 *    D10      -> LOAD of U1 (North digits 0-3, East digits 4-7)
 *    D9       -> LOAD of U2 (South digits 0-3, West digits 4-7)
 *    D7       -> "link OK" LED (330 ohm to GND)
 *    MAX7219  SEG A..G, DP to the segments of both 4-digit displays it
 *             drives; DIG0..3 to the commons of the first, DIG4..7 to the
 *             second. ISET to +5 V through 10k.
 *    Displays 4-digit common cathode (7SEG-MPX4-CC in Proteus).
 * ============================================================================
 */

// Peripheral Configuration Code (do not edit)
//---CONFIG_BEGIN---
//---CONFIG_END---

#include <avr/wdt.h>

#define LINK_BAUD 9600

const uint8_t PIN_DIN   = 11;
const uint8_t PIN_CLK   = 13;
const uint8_t PIN_LOAD1 = 10;   // U1: North + East
const uint8_t PIN_LOAD2 = 9;    // U2: South + West
const uint8_t PIN_LINK_LED = 7;

const uint16_t LINK_TIMEOUT_MS = 3000;   // blank the displays after this
const uint16_t REINIT_MS       = 5000;   // rewrite the MAX7219 setup registers
const uint16_t LAMP_TEST_MS    = 1000;   // all segments on at power-up

// MAX7219 registers
#define REG_DIGIT0      0x01
#define REG_DECODE      0x09
#define REG_INTENSITY   0x0A
#define REG_SCAN_LIMIT  0x0B
#define REG_SHUTDOWN    0x0C
#define REG_TEST        0x0F

// Segment bits in no-decode mode: DP A B C D E F G
const uint8_t SEG_DIGIT[10] = {
  0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B
};
const uint8_t SEG_BLANK = 0x00;
const uint8_t SEG_DASH  = 0x01;
const uint8_t SEG_G     = 0x5E;   // a c d e f
const uint8_t SEG_Y     = 0x3B;   // b c d f g
const uint8_t SEG_R     = 0x05;   // e g (lower-case r)

// Frame "$CD,s,aaaa,bbbb,cccc,dddd*HH" is exactly 28 characters.
#define FRAME_LEN 28

char line[40];
uint8_t lineLen = 0;
uint8_t shown[4][4];              // segments per arm (N E S W) per digit
bool linkUp = false;
unsigned long lastFrameMs = 0;
unsigned long lastInitMs = 0;
uint32_t goodFrames = 0;
uint32_t badFrames = 0;

// ---------------------------------------------------------------- MAX7219
static void maxWrite(uint8_t loadPin, uint8_t reg, uint8_t value) {
  digitalWrite(loadPin, LOW);
  shiftOut(PIN_DIN, PIN_CLK, MSBFIRST, reg);
  shiftOut(PIN_DIN, PIN_CLK, MSBFIRST, value);
  digitalWrite(loadPin, HIGH);    // rising edge latches the 16 bits
}

static void maxBoth(uint8_t reg, uint8_t value) {
  maxWrite(PIN_LOAD1, reg, value);
  maxWrite(PIN_LOAD2, reg, value);
}

static void maxSetup() {
  maxBoth(REG_TEST, 0);
  maxBoth(REG_DECODE, 0);         // raw segments, letters are drawn by hand
  maxBoth(REG_SCAN_LIMIT, 7);     // all 8 digits
  maxBoth(REG_INTENSITY, 8);
  maxBoth(REG_SHUTDOWN, 1);
}

static void pushDigits() {
  for (uint8_t a = 0; a < 4; a++) {
    uint8_t loadPin = a < 2 ? PIN_LOAD1 : PIN_LOAD2;
    uint8_t first = (a % 2) * 4;
    for (uint8_t d = 0; d < 4; d++) {
      maxWrite(loadPin, REG_DIGIT0 + first + d, shown[a][d]);
    }
  }
}

static void showAll(uint8_t seg) {
  memset(shown, seg, sizeof(shown));
  pushDigits();
}

// ---------------------------------------------------------------- frames
static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool isDigit3(const char *f) {
  return f[1] >= '0' && f[1] <= '9' && f[2] >= '0' && f[2] <= '9' &&
         f[3] >= '0' && f[3] <= '9';
}

static bool frameValid(const char *s, uint8_t len) {
  if (len != FRAME_LEN || s[0] != '$' || s[1] != 'C' || s[2] != 'D' ||
      s[3] != ',' || s[5] != ',' || s[25] != '*') {
    return false;
  }
  for (uint8_t a = 0; a < 3; a++) {
    if (s[10 + a * 5] != ',') return false;
  }
  uint8_t x = 0;
  for (uint8_t i = 1; i < 25; i++) x ^= (uint8_t)s[i];
  int hi = hexValue(s[26]), lo = hexValue(s[27]);
  if (hi < 0 || lo < 0) return false;
  return x == (uint8_t)((hi << 4) | lo);
}

// One arm field ("G035", "R---", ...) to four digits.
static void renderArm(const char *f, uint8_t *dig) {
  bool timed = isDigit3(f);
  uint8_t letter;
  switch (f[0]) {
    case 'G': letter = SEG_G; break;
    case 'Y': letter = SEG_Y; break;
    case 'R': letter = SEG_R; break;
    default:  timed = false; letter = SEG_DASH; break;   // F, X
  }
  if (!timed) {
    dig[0] = dig[1] = dig[2] = dig[3] = SEG_DASH;
    return;
  }
  uint16_t v = (f[1] - '0') * 100 + (f[2] - '0') * 10 + (f[3] - '0');
  dig[0] = letter;
  dig[1] = v >= 100 ? SEG_DIGIT[v / 100] : SEG_BLANK;
  dig[2] = v >= 10 ? SEG_DIGIT[(v / 10) % 10] : SEG_BLANK;
  dig[3] = SEG_DIGIT[v % 10];
}

static void applyFrame(const char *s) {
  uint8_t next[4][4];
  for (uint8_t a = 0; a < 4; a++) renderArm(s + 6 + a * 5, next[a]);
  if (memcmp(next, shown, sizeof(shown)) != 0) {
    memcpy(shown, next, sizeof(shown));
    pushDigits();
  }
}

static void handleLine() {
  if (!frameValid(line, lineLen)) {
    badFrames++;
    return;
  }
  goodFrames++;
  lastFrameMs = millis();
  if (!linkUp) {
    linkUp = true;
    digitalWrite(PIN_LINK_LED, HIGH);
    Serial.println(F("countdown: link up"));
  }
  applyFrame(line);
}

static void readLink() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '$') {               // a new frame always starts here
      lineLen = 0;
      line[lineLen++] = c;
    } else if (c == '\n' || c == '\r') {
      if (lineLen) {
        line[lineLen] = '\0';
        handleLine();
      }
      lineLen = 0;
    } else if (lineLen > 0 && lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    } else {
      lineLen = 0;                // noise or an over-long line
    }
  }
}

// ---------------------------------------------------------------- Arduino
void setup() {
  MCUSR = 0;
  wdt_disable();

  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_LOAD1, OUTPUT);
  pinMode(PIN_LOAD2, OUTPUT);
  pinMode(PIN_LINK_LED, OUTPUT);
  digitalWrite(PIN_LOAD1, HIGH);
  digitalWrite(PIN_LOAD2, HIGH);
  digitalWrite(PIN_LINK_LED, LOW);

  Serial.begin(LINK_BAUD);
  Serial.println(F("Site 1 countdown display v1.0.0"));

  maxSetup();
  showAll(SEG_BLANK);
  maxBoth(REG_TEST, 1);           // lamp test: every segment on
  delay(LAMP_TEST_MS);
  maxBoth(REG_TEST, 0);
  lastInitMs = millis();

  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();
  readLink();

  unsigned long now = millis();
  if (linkUp && now - lastFrameMs >= LINK_TIMEOUT_MS) {
    linkUp = false;
    digitalWrite(PIN_LINK_LED, LOW);
    showAll(SEG_BLANK);           // stale numbers are worse than none
    Serial.println(F("countdown: link lost, display blank"));
  }

  // Interference on long cables can upset the MAX7219, so its setup and the
  // digits are rewritten every few seconds.
  if (now - lastInitMs >= REINIT_MS) {
    lastInitMs = now;
    maxSetup();
    pushDigits();
  }
}
