/*
 * Wind turbine controller bench test (simavr), 10x-speed build:
 *   tools/build_firmware.sh .../wind_turbine_controller.ino /tmp/wt.hex -DTIME_SCALE_PERCENT=10
 *   gcc -I tools/sim projects/11-wind-turbine-controller/test/sim_test.c -o /tmp/t -lsimavr -lelf
 */
#include "simtest.h"

enum { ANEMO = 2, HALL = 3, STOP = 4, BRAKE = 7, DUMP = 9 };
enum { VBATT = 0, VGEN = 1 };

#define REG_TCCR1A 0x80
#define REG_OCR1AL 0x88

static double anemo_hz, hall_hz;
static double anemo_phase, hall_phase;

/* Called every simulated ms: square-ish pulses (3 ms low) at the set rates. */
static void pulses(unsigned long now) {
  (void)now;
  anemo_phase += anemo_hz / 1000.0;
  hall_phase += hall_hz / 1000.0;
  static int a_low, h_low;
  if (anemo_phase >= 1.0) { anemo_phase -= 1.0; pin_drive(ANEMO, 0); a_low = 3; }
  else if (a_low && --a_low == 0) pin_drive(ANEMO, 1);
  if (hall_phase >= 1.0) { hall_phase -= 1.0; pin_drive(HALL, 0); h_low = 1; }
  else if (h_low && --h_low == 0) pin_drive(HALL, 1);
}

static void wind(double ms) { anemo_hz = ms / 0.667; }
static void rotor(double rpm) { hall_hz = rpm / 60.0; }
static void battery(double v) { adc_set_mv(VBATT, (int)(v * 22.0 / 122.0 * 1000.0)); }

static int duty(void) {
  if (sim->data[REG_TCCR1A] & 0x80) return sim->data[REG_OCR1AL];
  return pin_read(DUMP) ? 255 : 0;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s wind_turbine_controller.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(ANEMO, 1); pin_drive(HALL, 1); pin_drive(STOP, 1);
  adc_set_mv(VGEN, 1500);
  battery(12.5);
  wind(5); rotor(200);
  sim_on_ms = pulses;
  size_t m;

  section("start-up");
  expect(!pin_read(BRAKE), "braked at power-on");
  sim_run_ms(3000);
  expect(pin_read(BRAKE), "brake released with normal wind");
  expect(duty() == 0, "no dump load while charging (duty %d)", duty());

  section("battery full -> dump load");
  battery(14.6);
  sim_run_ms(8000);
  expect(duty() > 100 && duty() < 160, "about half dump load at 14.6 V (duty %d)", duty());
  battery(15.0);
  sim_run_ms(8000);           /* load ramps about 10 % per second, never jumps */
  expect(duty() == 255, "full dump load above 14.8 V (duty %d)", duty());
  expect(pin_read(BRAKE), "turbine stays loaded, never disconnected");
  battery(13.0);
  sim_run_ms(15000);
  expect(duty() == 0, "dump load off when the battery drops (duty %d)", duty());

  section("storm");
  m = sim_uart_mark();
  wind(27);
  sim_run_ms(3000);
  expect(pin_read(BRAKE), "short gust tolerated");
  sim_run_ms(5000);
  expect(!pin_read(BRAKE), "brake on in a 27 m/s storm");
  expect(uart_since(m, "BRAKE: STORM"), "STORM reported");
  wind(10); rotor(0);
  sim_run_ms(20000);
  expect(!pin_read(BRAKE), "still braked: must be calm for 5 min (30 s at x10)");
  sim_run_ms(15000);
  expect(pin_read(BRAKE), "brake released after the calm period");

  section("over-speed");
  m = sim_uart_mark();
  wind(12); rotor(700);
  sim_run_ms(5000);
  expect(!pin_read(BRAKE), "brake on above 600 rpm");
  expect(uart_since(m, "BRAKE: OVERSPEED"), "OVERSPEED reported");
  rotor(0);
  sim_run_ms(35000);
  expect(pin_read(BRAKE), "released after calm period");

  section("manual stop switch");
  rotor(200);
  pin_drive(STOP, 0);
  sim_run_ms(1500);
  expect(!pin_read(BRAKE), "STOP switch brakes at once");
  sim_run_ms(5000);
  expect(!pin_read(BRAKE), "stays braked while the switch is on");
  pin_drive(STOP, 1);
  sim_run_ms(1500);
  expect(pin_read(BRAKE), "released when the switch is turned off");

  section("dump load can't keep up");
  m = sim_uart_mark();
  battery(15.6);
  sim_run_ms(13000);
  expect(!pin_read(BRAKE), "brake on after 10 s above 15.2 V");
  expect(uart_since(m, "BRAKE: BATT OVERVOLT"), "BATT OVERVOLT reported");

  return sim_finish();
}
