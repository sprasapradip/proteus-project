/*
 * Level crossing bench test (simavr), 10x-speed build (stuck sensor = 60 s):
 *   tools/build_firmware.sh .../level_crossing.ino /tmp/lc.hex -DTIME_SCALE_PERCENT=10
 *   gcc -I tools/sim projects/14-railway-level-crossing/test/sim_test.c -o /tmp/t -lsimavr -lelf
 */
#include "simtest.h"

enum { WEST = 2, EAST = 3, ISLAND = 4, LIGHT_A = 5, LIGHT_B = 6, SERVO = 9, KEY = 12 };

static int island_on;
static unsigned worst_pulse_with_train;   /* barrier position while a train is on the road */
static int flashes;
static int last_a;

static void watch(unsigned long now) {
  (void)now;
  if (island_on && sim_pulse.last_us > worst_pulse_with_train) worst_pulse_with_train = sim_pulse.last_us;
  int a = pin_read(LIGHT_A);
  if (a && !last_a) flashes++;
  last_a = a;
}

static int barrier_down(void) { return sim_pulse.last_us < 700; }
static int barrier_up(void) { return sim_pulse.last_us > 1400; }
static void sensor(int pin, int ms) { pin_drive(pin, 0); sim_run_ms(ms); pin_drive(pin, 1); }
static void train_over_road(int ms) {
  pin_drive(ISLAND, 0); island_on = 1; sim_run_ms(ms);
  pin_drive(ISLAND, 1); island_on = 0;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s level_crossing.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  pin_drive(WEST, 1); pin_drive(EAST, 1); pin_drive(ISLAND, 1); pin_drive(KEY, 1);
  pulse_watch(SERVO);
  sim_on_ms = watch;
  size_t m;

  section("road open");
  sim_run_ms(2000);
  expect(barrier_up(), "barrier up (pulse %u us)", sim_pulse.last_us);
  expect(flashes == 0, "red lights off");

  section("train from the west");
  sensor(WEST, 2000);
  sim_run_ms(3000);
  expect(flashes >= 3, "red lights flashing during warning (%d flashes)", flashes);
  expect(barrier_up(), "barrier waits 6 s of warning before moving");
  sim_run_ms(9000);
  expect(barrier_down(), "barrier fully down (pulse %u us)", sim_pulse.last_us);
  train_over_road(8000);
  sim_run_ms(500);
  sensor(EAST, 2000);
  sim_run_ms(1500);
  expect(barrier_down(), "still down 1.5 s after the train left (3 s delay)");
  sim_run_ms(9000);
  expect(barrier_up(), "barrier up again");
  expect(worst_pulse_with_train < 700, "barrier was down the whole time a train was on the road");

  section("two trains, opposite directions");
  m = sim_uart_mark();
  sensor(WEST, 1000);
  sim_run_ms(4000);
  sensor(EAST, 1000);                    /* second train, from the east */
  sim_run_ms(10000);
  train_over_road(5000);                 /* first train crosses */
  sensor(EAST, 1000);                    /* ...and leaves eastwards */
  sim_run_ms(8000);
  expect(barrier_down(), "stays closed: the second train hasn't passed yet");
  train_over_road(5000);                 /* second train crosses */
  sensor(WEST, 1000);
  sim_run_ms(12000);
  expect(barrier_up(), "opens after both trains have passed");
  expect(uart_since(m, "train approaching from east"), "second train was tracked");

  section("long train: far sensor reached while the tail is still on the road");
  sensor(EAST, 1000);
  sim_run_ms(14000);
  pin_drive(ISLAND, 0); island_on = 1; sim_run_ms(3000);
  sensor(WEST, 1000);                    /* front passes the far sensor */
  sim_run_ms(3000);
  expect(barrier_down(), "still closed while the tail is on the road");
  pin_drive(ISLAND, 1); island_on = 0;
  sim_run_ms(12000);
  expect(barrier_up(), "opens once the tail has cleared");
  expect(worst_pulse_with_train < 700, "barrier stayed down while the train was on the road");

  section("gateman key");
  pin_drive(KEY, 0);
  sim_run_ms(14000);
  expect(barrier_down(), "key forces the crossing closed");
  pin_drive(KEY, 1);
  sim_run_ms(12000);
  expect(barrier_up(), "opens again when the key is turned off");

  section("train that never arrives");
  sensor(WEST, 1000);
  sim_run_ms(40000);
  expect(barrier_down(), "stays closed while a train is expected (safe side)");
  m = sim_uart_mark();
  pin_drive(KEY, 0); sim_run_ms(500); pin_drive(KEY, 1);
  sim_run_ms(12000);
  expect(uart_since(m, "gateman cleared pending trains"), "gateman can clear it with the key");
  expect(barrier_up(), "road reopened");

  section("stuck island sensor");
  m = sim_uart_mark();
  pin_drive(ISLAND, 0);
  sim_run_ms(65000);
  expect(uart_since(m, "FAULT"), "FAULT after 10 min stuck (60 s at x10)");
  pin_drive(ISLAND, 1);
  sim_run_ms(10000);
  expect(barrier_down(), "stays closed in FAULT even after the sensor recovers");
  pin_drive(KEY, 0); sim_run_ms(500); pin_drive(KEY, 1);
  sim_run_ms(12000);
  expect(barrier_up(), "gateman reset opens the road");

  return sim_finish();
}
