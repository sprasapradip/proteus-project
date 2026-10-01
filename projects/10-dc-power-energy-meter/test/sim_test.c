/*
 * DC power meter bench test (simavr), real-time build:
 *   tools/build_firmware.sh .../dc_power_meter.ino /tmp/m.hex
 *   gcc -I tools/sim projects/10-dc-power-energy-meter/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *   /tmp/t /tmp/m.elf
 */
#include <math.h>
#include "simtest.h"

enum { VSENSE = 0, ISENSE = 1 };
enum { BUTTON = 7, RELAY = 8 };

#define DIVIDER (22.0 / 122.0)

static void supply(double v) { adc_set_mv(VSENSE, (int)(v * DIVIDER * 1000.0 + 0.5)); }
static void load_amps(double a) {
  int mv = (int)(2500 + a * 185.0 + 0.5);
  adc_set_mv(ISENSE, mv > 5000 ? 5000 : mv);
}

static void press_ms(int ms) {
  pin_drive(BUTTON, 0); sim_run_ms(ms);
  pin_drive(BUTTON, 1); sim_run_ms(150);
}

typedef struct { double v, a, w, wh; char state[24]; } Row;

/* Reads the newest "DATA,<sec>,..." line from the serial log. */
static Row last_row(void) {
  Row r = { 0, 0, 0, 0, "" };
  const char *p = sim_uart, *found = NULL;
  while ((p = strstr(p, "DATA,")) != NULL) {
    if (p[5] >= '0' && p[5] <= '9') found = p;
    p += 5;
  }
  if (found) {
    unsigned long sec;
    sscanf(found, "DATA,%lu,%lf,%lf,%lf,%lf,%23[^\r\n]", &sec, &r.v, &r.a, &r.w, &r.wh, r.state);
  }
  return r;
}

static int near(double x, double want, double tol) { return fabs(x - want) <= tol; }

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s dc_power_meter.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  supply(12.0);
  load_amps(0);
  pin_drive(BUTTON, 1);
  Row r;
  size_t m;

  section("power on");
  sim_run_ms(2000);
  expect(pin_read(RELAY), "load connected at 12 V");
  r = last_row();
  expect(near(r.v, 12.0, 0.1), "reads 12.0 V (got %.2f)", r.v);
  expect(near(r.a, 0.0, 0.03), "reads 0 A (got %.3f)", r.a);

  section("2 A load for 10 s");
  load_amps(2.0);
  sim_run_ms(10000);
  r = last_row();
  expect(near(r.a, 2.0, 0.03), "reads 2.00 A (got %.3f)", r.a);
  expect(near(r.w, 24.0, 0.5), "reads 24 W (got %.2f)", r.w);
  expect(near(r.wh, 24.0 * 9.5 / 3600.0, 0.02), "about 0.065 Wh after ~10 s (got %.3f)", r.wh);

  section("over-current");
  m = sim_uart_mark();
  load_amps(4.8);
  sim_run_ms(100);
  expect(pin_read(RELAY), "short overload under 0.2 s tolerated");
  sim_run_ms(600);
  expect(!pin_read(RELAY), "load disconnected");
  expect(uart_since(m, "TRIP OVER-I"), "TRIP OVER-I reported");
  load_amps(0);
  sim_run_ms(2000);
  expect(!pin_read(RELAY), "trip is latched");
  press_ms(200);
  sim_run_ms(300);
  expect(pin_read(RELAY), "button reconnects the load");

  section("short circuit");
  m = sim_uart_mark();
  load_amps(30);
  sim_run_ms(500);
  expect(!pin_read(RELAY), "sensor at full scale trips at once");
  expect(uart_since(m, "TRIP SHORT"), "TRIP SHORT reported");
  load_amps(0);
  sim_run_ms(500);
  press_ms(200);
  sim_run_ms(300);

  section("over-voltage");
  m = sim_uart_mark();
  supply(27.0);
  sim_run_ms(1500);
  expect(!pin_read(RELAY), "disconnects above 26 V");
  expect(uart_since(m, "TRIP OVER-V"), "TRIP OVER-V reported");
  m = sim_uart_mark();
  press_ms(200);
  expect(uart_since(m, "reset refused"), "reset refused while still 27 V");
  supply(12.0);
  sim_run_ms(1000);
  press_ms(200);
  sim_run_ms(300);
  expect(pin_read(RELAY), "reset works once voltage is normal");

  section("flat battery");
  m = sim_uart_mark();
  supply(10.0);
  sim_run_ms(2000);
  expect(pin_read(RELAY), "brief dip under 3 s tolerated");
  sim_run_ms(2000);
  expect(!pin_read(RELAY), "low-voltage disconnect");
  supply(11.5);
  sim_run_ms(12000);
  expect(!pin_read(RELAY), "stays off between 10.5 and 12 V (hysteresis)");
  supply(12.6);
  sim_run_ms(11000);
  expect(pin_read(RELAY), "reconnects after 10 s above 12 V");

  section("clear energy counter");
  m = sim_uart_mark();
  press_ms(3300);
  sim_run_ms(1200);
  r = last_row();
  expect(uart_since(m, "energy counter cleared"), "3 s hold clears Wh");
  expect(r.wh < 0.01, "Wh back to zero (got %.3f)", r.wh);

  return sim_finish();
}
