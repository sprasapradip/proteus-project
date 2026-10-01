/*
 * Speed detector bench test (simavr):
 *   tools/build_firmware.sh .../speed_detector.ino /tmp/sd.hex
 *   gcc -I tools/sim projects/15-vehicle-speed-detector/test/sim_test.c -o /tmp/t -lsimavr -lelf
 */
#include "simtest.h"

enum { BEAM_A = 2, BEAM_B = 3, RESET_BTN = 7, OK_LAMP = 8 };

/* A vehicle crosses: the second beam breaks gap_ms after the first, each beam
 * stays broken for block_ms (vehicle length / speed). */
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

/* Last "VEHICLE,..." record: returns speed, fills direction and over flag. */
static double last_vehicle(char *dir, int *over) {
  const char *p = sim_uart, *found = NULL;
  while ((p = strstr(p, "VEHICLE,")) != NULL) {
    if (p[8] >= '0' && p[8] <= '9') found = p;
    p += 8;
  }
  double kmh = -1;
  unsigned long s;
  if (found) sscanf(found, "VEHICLE,%lu,%3[^,],%lf,%d", &s, dir, &kmh, over);
  return kmh;
}

static int count(const char *needle) {
  int n = 0;
  for (const char *p = sim_uart; (p = strstr(p, needle)) != NULL; p++) n++;
  return n;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s speed_detector.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(BEAM_A, 1); pin_drive(BEAM_B, 1); pin_drive(RESET_BTN, 1);
  sim_run_ms(500);
  char dir[4] = "";
  int over = -1;
  double v;

  section("car at 36 km/h, A to B");
  vehicle(BEAM_A, BEAM_B, 100, 400);
  v = last_vehicle(dir, &over);
  expect(v > 35.5 && v < 36.5, "measured %.1f km/h", v);
  expect(strcmp(dir, "A>B") == 0, "direction A>B (got %s)", dir);
  expect(over == 0, "under the 40 km/h limit");
  expect(pin_read(OK_LAMP), "green OK lamp on");

  section("motorbike at 60 km/h, B to A");
  vehicle(BEAM_B, BEAM_A, 60, 120);
  v = last_vehicle(dir, &over);
  expect(v > 59 && v < 61, "measured %.1f km/h", v);
  expect(strcmp(dir, "B>A") == 0, "direction B>A (got %s)", dir);
  expect(over == 1, "flagged over the limit");
  expect(!pin_read(OK_LAMP), "SLOW DOWN warning active (OK lamp off)");
  sim_run_ms(5000);
  expect(pin_read(OK_LAMP), "warning ends after 5 s");

  section("long bus at 20 km/h counts once");
  int before = count("VEHICLE,") ;
  vehicle(BEAM_A, BEAM_B, 180, 2200);       /* 12 m bus blocks both beams */
  v = last_vehicle(dir, &over);
  expect(count("VEHICLE,") == before + 1, "one record for one bus");
  expect(v > 19.5 && v < 20.5, "measured %.1f km/h", v);

  section("pedestrian breaks only one beam");
  size_t m = sim_uart_mark();
  before = count("VEHICLE,");
  pin_drive(BEAM_A, 0); sim_run_ms(800); pin_drive(BEAM_A, 1);
  sim_run_ms(2500);
  expect(uart_since(m, "only one beam"), "ignored after the 2 s timeout");
  expect(count("VEHICLE,") == before, "no speed recorded");

  section("glitch on both beams at once");
  m = sim_uart_mark();
  pin_drive(BEAM_A, 0); pin_drive(BEAM_B, 0); sim_run_ms(5);
  pin_drive(BEAM_A, 1); pin_drive(BEAM_B, 1); sim_run_ms(1000);
  expect(uart_since(m, "rejected: impossible speed"), "impossible speed rejected");

  section("still works afterwards");
  vehicle(BEAM_A, BEAM_B, 72, 300);
  v = last_vehicle(dir, &over);
  expect(v > 49.5 && v < 50.5, "measured %.1f km/h", v);

  section("reset counters");
  m = sim_uart_mark();
  pin_drive(RESET_BTN, 0); sim_run_ms(200); pin_drive(RESET_BTN, 1); sim_run_ms(200);
  expect(uart_since(m, "counters reset"), "RESET button clears the counters");

  return sim_finish();
}
