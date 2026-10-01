/*
 * Smart parking bench test (simavr):
 *   tools/build_firmware.sh .../smart_parking.ino /tmp/p.hex
 *   gcc -I tools/sim projects/13-smart-parking-system/test/sim_test.c -o /tmp/t -lsimavr -lelf
 */
#include "simtest.h"

enum { ENTRY_CAR = 14, EXIT_CAR = 15 };          /* A0, A1 */
enum { ENTRY_SERVO = 9, EXIT_SERVO = 10, FULL_LAMP = 13 };

static Pulse *gate_in, *gate_out;

static int is_up(Pulse *p) { return p->last_us > 1300; }     /* ~90 deg  */
static int is_down(Pulse *p) { return p->last_us < 700; }    /* ~0 deg   */

static void car(int pin, int present) { pin_drive(pin, !present); }

/* A car drives through the entry barrier and parks in a slot. */
static void park(int slot_pin) {
  car(ENTRY_CAR, 1); sim_run_ms(2500);
  car(ENTRY_CAR, 0); sim_run_ms(4000);
  car(slot_pin, 1);  sim_run_ms(2000);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s smart_parking.elf\n", argv[0]); return 2; }
  sim_load(argv[1]);
  for (int p = 2; p <= 7; p++) car(p, 0);
  car(ENTRY_CAR, 0); car(EXIT_CAR, 0);
  gate_in = pulse_watch_extra(ENTRY_SERVO);
  gate_out = pulse_watch_extra(EXIT_SERVO);
  size_t m;

  section("start");
  sim_run_ms(1000);
  expect(is_down(gate_in) && is_down(gate_out), "both barriers down");
  expect(uart_since(0, "EVENT,0,start,6"), "6 free slots reported");

  section("first car");
  car(ENTRY_CAR, 1);
  sim_run_ms(2000);
  expect(is_up(gate_in), "entry barrier opens (pulse %u us)", gate_in->last_us);
  sim_run_ms(5000);
  expect(is_up(gate_in), "stays open while the car is under it");
  car(ENTRY_CAR, 0);
  sim_run_ms(1000);
  expect(is_up(gate_in), "waits 2 s after the car has passed");
  sim_run_ms(3000);
  expect(is_down(gate_in), "closes after the car has passed");
  m = sim_uart_mark();
  car(2, 1);
  sim_run_ms(500);
  expect(!uart_since(m, "slot1_taken"), "slot sensor filtered (no change after 0.5 s)");
  sim_run_ms(1500);
  expect(uart_since(m, "slot1_taken,5"), "slot 1 taken, 5 free");

  section("car park fills up");
  for (int p = 3; p <= 7; p++) park(p);
  sim_run_ms(500);
  expect(pin_read(FULL_LAMP), "FULL lamp on with 6/6 taken");
  m = sim_uart_mark();
  car(ENTRY_CAR, 1);
  sim_run_ms(3000);
  expect(uart_since(m, "entry_refused_full"), "next car refused");
  expect(is_down(gate_in), "entry barrier stays down when full");
  car(ENTRY_CAR, 0);
  sim_run_ms(1000);

  section("a car leaves");
  car(4, 0);
  car(EXIT_CAR, 1);
  sim_run_ms(2000);
  expect(is_up(gate_out), "exit barrier opens");
  car(EXIT_CAR, 0);
  sim_run_ms(4000);
  expect(is_down(gate_out), "exit barrier closes");
  expect(!pin_read(FULL_LAMP), "FULL lamp off with a free slot");

  section("safety: tailgater under a closing barrier, car park now full");
  m = sim_uart_mark();
  car(ENTRY_CAR, 1); sim_run_ms(2500);           /* takes the last free slot */
  car(ENTRY_CAR, 0);
  car(4, 1);                                     /* ... and parks in slot 3 */
  sim_run_ms(2600);                              /* barrier starts coming down */
  unsigned before = gate_in->last_us;
  car(ENTRY_CAR, 1); sim_run_ms(1500);           /* second car right behind */
  expect(uart_since(m, "entry_refused_full"), "the tailgater is refused (car park full)");
  expect(uart_since(m, "safety_reopen"), "but the barrier goes back up over the car");
  expect(is_up(gate_in) && before < 1450, "fully raised again (was %u us while closing)", before);

  section("car waiting at the barrier");
  m = sim_uart_mark();
  sim_run_ms(21000);
  expect(uart_since(m, "entry_car_waiting"), "attendant warning after 20 s");
  car(ENTRY_CAR, 0);
  sim_run_ms(4000);
  expect(is_down(gate_in), "closes once the car has gone");

  return sim_finish();
}
