/*
 * Wind vane + yaw controller bench test (simavr), 10x-speed build:
 *   tools/build_firmware.sh .../wind_vane_yaw.ino /tmp/wv.hex -DTIME_SCALE_PERCENT=10
 *   gcc -I tools/sim projects/12-wind-vane-yaw-control/test/sim_test.c -o /tmp/t -lsimavr -lelf -lm
 *
 * The test plays the part of the turbine: while the controller drives the
 * motor, the nacelle turns at 12 deg/s and the position pot follows.
 */
#include <math.h>
#include "simtest.h"

enum { VANE = 0, NACELLE = 1 };
enum { PARK = 4, CW = 5, CCW = 6 };

static const double VANE_R[16] = {
  33000, 6570, 8200, 891, 1000, 688, 2200, 1410,
  3900, 3140, 16000, 14120, 120000, 42120, 64900, 21880
};

static double nacelle;          /* -270 .. +270 */
static double min_seen, max_seen;
static int motor_jammed;

static void set_pot(void) {
  adc_set_mv(NACELLE, (int)((nacelle + 270.0) / 540.0 * 5000.0 + 0.5));
}

static void turbine(unsigned long now) {
  (void)now;
  if (!motor_jammed) {
    if (pin_read(CW)) nacelle += 0.012;
    if (pin_read(CCW)) nacelle -= 0.012;
  }
  if (nacelle < min_seen) min_seen = nacelle;
  if (nacelle > max_seen) max_seen = nacelle;
  set_pot();
}

static void wind_from(int index) {   /* 0 = N, 4 = E, 8 = S, 12 = W */
  double r = VANE_R[index];
  adc_set_mv(VANE, (int)(5000.0 * r / (r + 10000.0) + 0.5));
}

static int near_deg(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s wind_vane_yaw.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(PARK, 1);
  nacelle = 0; set_pot();
  wind_from(0);
  sim_on_ms = turbine;
  size_t m;

  section("facing the wind already");
  sim_run_ms(5000);
  expect(!pin_read(CW) && !pin_read(CCW), "no movement when aligned");

  section("wind turns east");
  wind_from(4);
  sim_run_ms(20000);
  expect(near_deg(nacelle, 90, 12), "nacelle follows to 90 deg (now %.0f)", nacelle);

  section("gust from the west");
  double before = nacelle;
  wind_from(12); sim_run_ms(700); wind_from(4);
  sim_run_ms(6000);
  expect(near_deg(nacelle, before, 1), "a 0.7 s gust does not move the turbine");

  section("short way round: E -> WSW (247.5)");
  wind_from(11);
  sim_run_ms(25000);
  expect(near_deg(nacelle, 247.5, 12), "turned clockwise to 247.5 (now %.0f)", nacelle);

  section("cable limit: wind WNW (292.5) would pass +270");
  m = sim_uart_mark();
  wind_from(13);
  sim_run_ms(45000);
  expect(near_deg(nacelle, -67.5, 12), "went the long way to -67.5 (now %.0f)", nacelle);
  expect(max_seen <= 270.5 && min_seen >= -270.5, "never beyond +-270 deg (%.0f .. %.0f)",
         min_seen, max_seen);

  section("keep inside limits: SW, then ESE");
  wind_from(10); sim_run_ms(20000);
  expect(near_deg(nacelle, -135, 12), "SW reached at -135 (now %.0f)", nacelle);
  wind_from(5); sim_run_ms(25000);
  expect(near_deg(nacelle, -247.5, 12), "ESE reached at -247.5 (now %.0f)", nacelle);
  wind_from(3); sim_run_ms(40000);
  expect(near_deg(nacelle, 67.5, 12), "ENE: unwound the long way to +67.5 (now %.0f)", nacelle);
  expect(max_seen <= 270.5 && min_seen >= -270.5, "cable limit still respected");

  section("park / furl switch");
  pin_drive(PARK, 0);
  sim_run_ms(15000);
  expect(near_deg(nacelle, 157.5, 12), "rotor turned 90 deg out of the wind (now %.0f)", nacelle);
  pin_drive(PARK, 1);
  sim_run_ms(20000);
  expect(near_deg(nacelle, 67.5, 12), "back into the wind after PARK is off (now %.0f)", nacelle);

  section("jammed yaw gear");
  m = sim_uart_mark();
  motor_jammed = 1;
  wind_from(8);
  sim_run_ms(100000);
  expect(uart_since(m, "YAW STUCK"), "YAW STUCK after 90 s without reaching the target");
  expect(!pin_read(CW) && !pin_read(CCW), "motor switched off");

  return sim_finish();
}
