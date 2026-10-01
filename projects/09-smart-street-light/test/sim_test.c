/*
 * Smart street light bench test (simavr). Test build:
 *   tools/build_firmware.sh .../smart_street_light.ino /tmp/sl.hex \
 *     -DTIME_SCALE_PERCENT=10 -DEVENING_SEC=300
 *   gcc -I tools/sim projects/09-smart-street-light/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *   /tmp/t /tmp/sl.elf
 */
#include "simtest.h"

enum { LDR = 0 };              /* A0 */
enum { PIR = 2, TEST_BTN = 4, LAMP = 9 };

#define REG_TCCR1A 0x80
#define REG_OCR1AL 0x88

/* Lamp PWM duty 0..255. analogWrite(0/255) switches PWM off and drives the pin. */
static int duty(void) {
  if (sim->data[REG_TCCR1A] & 0x80) return sim->data[REG_OCR1AL];
  return pin_read(LAMP) ? 255 : 0;
}

static void ldr_mv(int mv) { adc_set_mv(LDR, mv); }

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s smart_street_light.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  ldr_mv(4000);                /* bright day */
  pin_drive(PIR, 0);
  pin_drive(TEST_BTN, 1);

  section("day");
  sim_run_ms(2000);
  expect(duty() == 0, "lamp off in daylight (duty %d)", duty());
  pin_drive(PIR, 1); sim_run_ms(200); pin_drive(PIR, 0);
  sim_run_ms(1000);
  expect(duty() == 0, "motion ignored during the day");
  ldr_mv(500); sim_run_ms(1500); ldr_mv(4000);
  sim_run_ms(2000);
  expect(duty() == 0, "1.5 s of shadow does not switch the lamp on");

  section("dusk");
  ldr_mv(500);
  sim_run_ms(6000);
  expect(duty() > 30 && duty() < 50, "evening idle about 40%% (duty %d)", duty());

  section("headlights at night");
  ldr_mv(4000); sim_run_ms(2000); ldr_mv(500);
  sim_run_ms(1500);
  expect(duty() > 30, "still on after a 2 s headlight flash (duty %d)", duty());

  section("motion");
  pin_drive(PIR, 1); sim_run_ms(200); pin_drive(PIR, 0);
  sim_run_ms(800);
  expect(duty() == 255, "full brightness within a second (duty %d)", duty());
  sim_run_ms(4000);
  expect(duty() == 255, "held while the motion timer runs");
  sim_run_ms(6000);
  expect(duty() > 30 && duty() < 50, "fades back to idle (duty %d)", duty());

  section("late night");
  sim_run_ms(20000);
  expect(duty() > 3 && duty() < 20, "late-night idle about 20%% (duty %d)", duty());

  section("dawn");
  ldr_mv(4000);
  sim_run_ms(4000);
  expect(duty() > 0, "dawn needs 60 s of light (6 s at x10)");
  sim_run_ms(7000);
  expect(duty() == 0, "lamp off after dawn (duty %d)", duty());

  section("maintenance test button");
  pin_drive(TEST_BTN, 0); sim_run_ms(200); pin_drive(TEST_BTN, 1);
  sim_run_ms(1000);
  expect(duty() == 255, "TEST gives full brightness in daylight");
  sim_run_ms(6000);
  expect(duty() == 0, "TEST ends by itself");

  return sim_finish();
}
