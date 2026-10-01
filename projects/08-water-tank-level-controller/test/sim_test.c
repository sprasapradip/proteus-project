/*
 * Water level controller bench test (simavr), 10x-speed build:
 *   tools/build_firmware.sh .../water_level_controller.ino /tmp/w.hex -DTIME_SCALE_PERCENT=10
 *   gcc -I tools/sim projects/08-water-tank-level-controller/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *   /tmp/t /tmp/w.elf
 */
#include "simtest.h"

enum { P25 = 2, P50 = 3, P75 = 4, P100 = 5, SUMP = 6, MODE = 7, PUMP = 8, BUTTON = 10 };

/* wet = 1 means water touches the probe (pin pulled LOW) */
static void probe(int pin, int wet) { pin_drive(pin, !wet); }

static void set_level(int n) {
  probe(P25, n >= 1); probe(P50, n >= 2); probe(P75, n >= 3); probe(P100, n >= 4);
}

static void press(void) {
  pin_drive(BUTTON, 0); sim_run_ms(150);
  pin_drive(BUTTON, 1); sim_run_ms(150);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s water_level_controller.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  set_level(0);
  probe(SUMP, 1);
  pin_drive(MODE, 1);      /* AUTO */
  pin_drive(BUTTON, 1);
  size_t m;

  section("empty tank, sump has water");
  sim_run_ms(2500);
  expect(pin_read(PUMP), "pump starts");

  section("tank fills");
  for (int n = 1; n <= 3; n++) { set_level(n); sim_run_ms(3000); }
  expect(pin_read(PUMP), "still pumping at 75%%");
  m = sim_uart_mark();
  set_level(4);
  sim_run_ms(1000);
  expect(pin_read(PUMP), "splash shorter than 2 s is ignored");
  sim_run_ms(1500);
  expect(!pin_read(PUMP), "pump stops at 100%%");
  expect(uart_since(m, "state -> TANK FULL"), "TANK FULL reported");

  section("water used, level falls");
  set_level(3); sim_run_ms(3000);
  set_level(2); sim_run_ms(3000);
  expect(!pin_read(PUMP), "no restart at 50%% (hysteresis)");
  set_level(1); sim_run_ms(3000);
  expect(pin_read(PUMP), "restarts below 50%%");

  section("sump runs dry");
  m = sim_uart_mark();
  probe(SUMP, 0);
  sim_run_ms(2500);
  expect(!pin_read(PUMP), "pump stopped on dry sump");
  expect(uart_since(m, "state -> SUMP DRY"), "SUMP DRY reported");
  probe(SUMP, 1);
  sim_run_ms(15000);
  expect(!pin_read(PUMP), "waits after water returns (5 min, 30 s at x10)");
  sim_run_ms(20000);
  expect(pin_read(PUMP), "restarts after the wait");

  section("pump runs but level never rises");
  m = sim_uart_mark();
  sim_run_ms(65000);
  expect(!pin_read(PUMP), "pump stopped after 10 min with no rise (60 s at x10)");
  expect(uart_since(m, "FAULT: NO RISE"), "NO RISE fault latched");
  sim_run_ms(10000);
  expect(!pin_read(PUMP), "fault stays latched");
  m = sim_uart_mark();
  press();
  sim_run_ms(7000);
  expect(uart_since(m, "fault reset by user"), "button resets the fault");
  expect(pin_read(PUMP), "pump restarts after reset");

  section("probe wiring fault");
  m = sim_uart_mark();
  probe(P75, 1);           /* 25%% and 75%% wet, 50%% dry: impossible */
  sim_run_ms(2500);
  expect(uart_since(m, "FAULT: PROBE ERROR"), "impossible probe pattern detected");
  expect(!pin_read(PUMP), "pump stopped on probe fault");
  probe(P75, 0);
  sim_run_ms(2500);
  press();
  sim_run_ms(500);

  section("manual mode");
  pin_drive(MODE, 0);
  sim_run_ms(7000);
  expect(!pin_read(PUMP), "manual mode: pump waits for the button");
  press();
  sim_run_ms(300);
  expect(pin_read(PUMP), "button starts the pump");
  press();
  sim_run_ms(300);
  expect(!pin_read(PUMP), "button stops the pump");
  m = sim_uart_mark();
  sim_run_ms(7000);
  press();
  sim_run_ms(300);
  probe(SUMP, 0);
  sim_run_ms(2500);
  expect(!pin_read(PUMP), "dry-run protection also works in manual");

  return sim_finish();
}
