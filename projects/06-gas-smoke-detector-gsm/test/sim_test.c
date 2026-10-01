/*
 * Gas detector bench test (simavr). Uses the 10x-speed build:
 *   tools/build_firmware.sh projects/06-gas-smoke-detector-gsm/firmware/gas_detector/gas_detector.ino \
 *     /tmp/gas.hex -DTIME_SCALE_PERCENT=10 '-DALERT_PHONE_1="+9779800000000"'
 *   gcc -I tools/sim projects/06-gas-smoke-detector-gsm/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *   /tmp/t /tmp/gas.elf
 */
#include "simtest.h"

enum { MQ2 = 0, MQ3 = 1 };
enum { BUTTON = 3, SERVO = 6, FAN = 8, LED_OK = 12, LED_ALARM = 13 };

static void press(void) {
  pin_drive(BUTTON, 0); sim_run_ms(150);
  pin_drive(BUTTON, 1); sim_run_ms(150);
}

/* Servo library: 0 deg = 544 us, 90 deg = ~1470 us */
static int valve_closed(void) { return sim_pulse.last_us > 1200 && sim_pulse.last_us < 1800; }
static int valve_open(void) { return sim_pulse.last_us > 400 && sim_pulse.last_us < 800; }

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s gas_detector.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(BUTTON, 1);
  adc_set_mv(MQ2, 1000);   /* clean air ~205 counts */
  adc_set_mv(MQ3, 1000);
  pulse_watch(SERVO);
  size_t m;

  section("warm-up");
  sim_run_ms(3000);
  expect(!pin_read(FAN), "fan off while warming up");
  adc_set_mv(MQ2, 3000);
  sim_run_ms(2000);
  expect(!pin_read(FAN), "no alarm during warm-up even with gas present");
  adc_set_mv(MQ2, 1000);
  m = sim_uart_mark();
  sim_run_ms(3000);
  expect(uart_since(m, "state -> NORMAL"), "NORMAL after 60 s (6 s at x10)");
  expect(valve_open(), "valve open (pulse %u us)", sim_pulse.last_us);

  section("warning level");
  m = sim_uart_mark();
  adc_set_mv(MQ2, 1500);   /* ~307 counts, above WARN 250, below ALARM 400 */
  sim_run_ms(1500);
  expect(uart_since(m, "state -> WARNING"), "WARNING reported");
  expect(!pin_read(FAN), "fan still off on warning");

  section("short spike is ignored");
  m = sim_uart_mark();
  adc_set_mv(MQ2, 4000); sim_run_ms(150);
  adc_set_mv(MQ2, 1500); sim_run_ms(1500);
  expect(!uart_since(m, "state -> ALARM"), "150 ms spike did not trigger ALARM");

  section("alarm");
  m = sim_uart_mark();
  adc_set_mv(MQ2, 2600);   /* ~532 counts */
  sim_run_ms(1500);
  expect(!uart_since(m, "state -> ALARM"), "not yet: level must hold for 2 s");
  sim_run_ms(2000);
  expect(uart_since(m, "state -> ALARM"), "ALARM after confirmation");
  expect(pin_read(FAN), "exhaust fan ON");
  expect(valve_closed(), "gas valve closed (pulse %u us)", sim_pulse.last_us);
  expect(uart_since(m, "SMS queued"), "SMS queued");
  m = sim_uart_mark();
  press();
  expect(uart_since(m, "buzzer muted"), "button mutes the buzzer during alarm");

  section("gas clears");
  m = sim_uart_mark();
  adc_set_mv(MQ2, 1000);
  sim_run_ms(2000);
  expect(uart_since(m, "state -> PURGE"), "PURGE state");
  expect(pin_read(FAN), "fan keeps running to purge the room");
  sim_run_ms(6000);
  expect(!pin_read(FAN), "fan stops after the 60 s purge (6 s at x10)");
  expect(valve_closed(), "valve stays closed after the gas clears");

  section("manual reset");
  m = sim_uart_mark();
  press();
  sim_run_ms(200);
  expect(uart_since(m, "reset by user"), "reset accepted with clean air");
  expect(valve_open(), "valve reopened (pulse %u us)", sim_pulse.last_us);

  section("unplugged sensor");
  m = sim_uart_mark();
  adc_set_mv(MQ3, 0);
  sim_run_ms(3000);
  expect(!uart_since(m, "SENSOR FAULT"), "not reported before 5 s");
  sim_run_ms(3000);
  expect(uart_since(m, "state -> SENSOR FAULT"), "stuck reading reported as SENSOR FAULT");
  expect(!pin_read(FAN), "no false alarm from a dead sensor");
  m = sim_uart_mark();
  adc_set_mv(MQ3, 1000);
  sim_run_ms(1500);
  expect(uart_since(m, "state -> NORMAL"), "back to NORMAL when the reading recovers");

  return sim_finish();
}
