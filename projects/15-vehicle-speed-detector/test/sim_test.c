/*
 * Speed detector v2 bench test (simavr):
 *   tools/build_firmware.sh .../speed_detector.ino /tmp/sd.hex
 *   gcc -I tools/sim projects/15-vehicle-speed-detector/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *
 * Besides the beams, the test reads the bits going to the MAX7219 display
 * (to check the number drivers see) and talks to the serial command port.
 */
#include "simtest.h"

enum { BEAM_A = 2, BEAM_B = 3, MAX_LOAD = 4, KEEP_DIST = 5, RESET_BTN = 7, OK_LAMP = 8 };
enum { MAX_DIN = 18, MAX_CLK = 19 };     /* A4, A5 */

/* ---- MAX7219 decoder ---------------------------------------------------- */
static uint16_t shift_reg;
static uint8_t digit[9];                 /* registers 1..8 */
static int display_on = 1, display_toggles;

static void max_clk(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; (void)param;
  if (value) shift_reg = (uint16_t)((shift_reg << 1) | pin_read(MAX_DIN));
}

static void max_load(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; (void)param;
  if (!value) return;
  uint8_t reg = shift_reg >> 8, val = shift_reg & 0xFF;
  if (reg >= 1 && reg <= 8) digit[reg] = val;
  if (reg == 0x0C) {
    if ((int)val != display_on) display_toggles++;
    display_on = val;
  }
}

/* Number on the 4-digit display, -1 if blank. */
static int shown(void) {
  int v = 0, any = 0;
  for (int d = 4; d >= 1; d--) {
    if (digit[d] == 0x0F) continue;
    v = v * 10 + (digit[d] & 0x0F);
    any = 1;
  }
  return any ? v : -1;
}

/* ---- beams ---------------------------------------------------------------- */
static void vehicle(int first, int second, int gap_ms, int block_ms) {
  pin_drive(first, 0);
  sim_run_ms(gap_ms);
  pin_drive(second, 0);
  sim_run_ms(block_ms - gap_ms);
  pin_drive(first, 1);
  sim_run_ms(gap_ms);
  pin_drive(second, 1);
  sim_run_ms(1000);
}

typedef struct {
  char time[16], dir[4], cls[8], headway[8];
  double kmh, len;
  int over, tail;
} Rec;

static Rec last_vehicle(void) {
  Rec r;
  memset(&r, 0, sizeof(r));
  r.kmh = -1;
  const char *p = sim_uart, *found = NULL;
  while ((p = strstr(p, "VEHICLE,")) != NULL) {
    if (strncmp(p, "VEHICLE,time", 12) != 0) found = p;
    p += 8;
  }
  if (found)
    sscanf(found, "VEHICLE,%15[^,],%3[^,],%lf,%7[^,],%lf,%7[^,],%d,%d",
           r.time, r.dir, &r.kmh, r.cls, &r.len, r.headway, &r.over, &r.tail);
  return r;
}

static void command(const char *cmd) {
  char line[64];
  snprintf(line, sizeof(line), "%s\n", cmd);
  uart_send(line);
  sim_run_ms(300);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s speed_detector.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(BEAM_A, 1); pin_drive(BEAM_B, 1); pin_drive(RESET_BTN, 1);
  avr_irq_register_notify(avr_io_getirq(sim, AVR_IOCTL_IOPORT_GETIRQ('C'), 5), max_clk, NULL);
  avr_irq_register_notify(avr_io_getirq(sim, AVR_IOCTL_IOPORT_GETIRQ('D'), 4), max_load, NULL);
  sim_run_ms(800);
  Rec r;
  size_t m;

  section("start-up");
  expect(uart_since(0, "limit=40 heavy_limit=30"), "default limits 40 / 30 km/h");
  expect(shown() == -1, "big display blank");

  section("car at 36 km/h");
  vehicle(BEAM_A, BEAM_B, 100, 400);
  r = last_vehicle();
  expect(r.kmh > 35.5 && r.kmh < 36.5, "speed %.1f km/h", r.kmh);
  expect(!strcmp(r.cls, "CAR") && r.len > 3.6 && r.len < 4.4, "class %s, length %.1f m", r.cls, r.len);
  expect(shown() == 36, "big display shows 36 (got %d)", shown());
  expect(r.over == 0 && pin_read(OK_LAMP), "within the limit, THANK YOU lamp on");

  section("motorbike at 60 km/h, B to A");
  int toggles = display_toggles;
  vehicle(BEAM_B, BEAM_A, 60, 120);
  r = last_vehicle();
  expect(r.kmh > 59 && r.kmh < 61 && !strcmp(r.dir, "B>A"), "%.1f km/h %s", r.kmh, r.dir);
  expect(!strcmp(r.cls, "BIKE"), "class %s (length %.1f m)", r.cls, r.len);
  expect(r.over == 1 && !pin_read(OK_LAMP), "over the limit: SLOW DOWN");
  expect(shown() == 60, "big display shows 60 (got %d)", shown());
  sim_run_ms(1200);
  expect(display_toggles > toggles, "big display flashes while warning");
  sim_run_ms(5000);
  expect(shown() == -1 && display_on, "display blanks again afterwards");

  section("buses and trucks");
  vehicle(BEAM_A, BEAM_B, 180, 2200);
  r = last_vehicle();
  expect(!strcmp(r.cls, "HEAVY") && r.len > 11 && r.len < 13, "bus at %.0f km/h: %s %.1f m", r.kmh, r.cls, r.len);
  expect(r.over == 0, "20 km/h is within the heavy limit");
  vehicle(BEAM_A, BEAM_B, 100, 1300);
  r = last_vehicle();
  expect(!strcmp(r.cls, "HEAVY") && r.over == 1,
         "truck at %.0f km/h: over the 30 km/h heavy limit (car limit is 40)", r.kmh);
  expect(!pin_read(OK_LAMP), "SLOW DOWN shown for the truck");
  sim_run_ms(5000);

  section("tailgating");
  vehicle(BEAM_A, BEAM_B, 100, 400);
  vehicle(BEAM_A, BEAM_B, 100, 400);     /* front-to-front 1.5 s later */
  r = last_vehicle();
  expect(r.tail == 1, "second car 1.5 s behind is flagged (headway %s s)", r.headway);
  int lamp = 0;
  for (int i = 0; i < 10; i++) { sim_run_ms(100); lamp |= pin_read(KEEP_DIST); }
  expect(lamp, "KEEP DISTANCE lamp flashing");
  sim_run_ms(5000);
  vehicle(BEAM_A, BEAM_B, 100, 400);     /* about 7 s after the last one */
  r = last_vehicle();
  expect(r.tail == 0, "safe gap not flagged (headway %s s)", r.headway);

  section("not vehicles");
  m = sim_uart_mark();
  pin_drive(BEAM_A, 0); sim_run_ms(800); pin_drive(BEAM_A, 1);
  sim_run_ms(2500);
  expect(uart_since(m, "only one beam"), "pedestrian ignored");
  m = sim_uart_mark();
  pin_drive(BEAM_A, 0); pin_drive(BEAM_B, 0); sim_run_ms(5);
  pin_drive(BEAM_A, 1); pin_drive(BEAM_B, 1); sim_run_ms(1000);
  expect(uart_since(m, "impossible speed"), "glitch on both beams rejected");

  section("settings are PIN protected");
  m = sim_uart_mark();
  command("SET LIMIT 30");
  expect(uart_since(m, "ERR locked"), "change refused without PIN");
  command("PIN 1111");
  expect(uart_since(m, "ERR wrong PIN"), "wrong PIN refused");
  command("PIN 1234");
  command("SET LIMIT 30");
  expect(uart_since(m, "OK limit saved"), "limit changed to 30 after PIN");
  command("set heavy 25");
  expect(uart_since(m, "OK heavy limit saved"), "lower-case commands work too");
  vehicle(BEAM_A, BEAM_B, 100, 400);
  r = last_vehicle();
  expect(r.over == 1, "a car at 36 km/h now breaks the new 30 km/h limit");

  section("clock");
  m = sim_uart_mark();
  command("TIME 08:30:00");
  expect(uart_since(m, "OK time D0 08:30:00"), "clock set");
  sim_run_ms(5000);
  vehicle(BEAM_A, BEAM_B, 100, 400);
  r = last_vehicle();
  expect(!strncmp(r.time, "D0 08:30:0", 10), "vehicle time-stamped %s", r.time);

  section("statistics");
  m = sim_uart_mark();
  command("STATS");
  sim_run_ms(300);
  expect(uart_since(m, "vehicles=9 bike=1 car=6 heavy=2"), "counts per class");
  expect(uart_since(m, "p85="), "85th percentile speed reported");
  expect(uart_since(m, "per_hour 8h=1"), "vehicles per hour of the day");

  section("violation log in EEPROM");
  m = sim_uart_mark();
  command("LOG");
  sim_run_ms(500);
  expect(uart_since(m, "LOG end, 5 records"), "5 violations / tailgaters stored");

  section("survives a power cut");
  avr_reset(sim);
  /* simavr forgets external pin levels on reset: drive them again (the
   * firmware hasn't run yet, so the toggle isn't seen as a beam break). */
  pin_drive(BEAM_A, 0); pin_drive(BEAM_A, 1);
  pin_drive(BEAM_B, 0); pin_drive(BEAM_B, 1);
  pin_drive(RESET_BTN, 0); pin_drive(RESET_BTN, 1);
  sim_run_ms(800);
  m = sim_uart_mark();
  command("STATUS");
  expect(uart_since(m, "limit=30 heavy_limit=25"), "settings kept after reset");
  m = sim_uart_mark();
  command("LOG");
  sim_run_ms(500);
  expect(uart_since(m, "LOG end, 5 records"), "violation log kept after reset");
  expect(uart_since(m, "D0 08:30:0"), "logged time kept with the record");

  section("beam fault");
  m = sim_uart_mark();
  pin_drive(BEAM_B, 0);
  sim_run_ms(31000);
  expect(uart_since(m, "BEAM FAULT: beam B"), "beam blocked 30 s reported");
  expect(!pin_read(OK_LAMP), "THANK YOU lamp off during the fault");
  pin_drive(BEAM_B, 1);
  sim_run_ms(2500);
  expect(uart_since(m, "beams OK again"), "recovers when the beam is clear");
  vehicle(BEAM_A, BEAM_B, 100, 400);
  r = last_vehicle();
  expect(r.kmh > 35.5 && r.kmh < 36.5, "measuring again after the fault (%.1f km/h)", r.kmh);

  section("clear log");
  m = sim_uart_mark();
  command("PIN 1234");
  command("CLEAR LOG");
  command("LOG");
  sim_run_ms(300);
  expect(uart_since(m, "LOG end, 0 records"), "log cleared with PIN");

  return sim_finish();
}
