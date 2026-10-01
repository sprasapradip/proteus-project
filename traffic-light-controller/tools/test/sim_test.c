/*
 * Bench test for the traffic controller firmware using simavr.
 * Runs the real compiled ELF, presses the inputs on a timeline and checks
 * the lamp outputs every simulated millisecond.
 *
 * Build & run (Ubuntu):
 *   sudo apt-get install simavr libsimavr-dev libelf-dev
 *   SCALE=20 tools/build_hex.sh
 *   gcc tools/test/sim_test.c -o /tmp/sim_test -lsimavr -lelf
 *   /tmp/sim_test firmware/build/traffic_controller_site01_sim_x5.elf
 *
 *   FAULT_TEST=1 tools/build_hex.sh
 *   /tmp/sim_test firmware/build/traffic_controller_site01_faulttest.elf fault
 *
 * Exit code 0 = all checks passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>

#define F_CPU 16000000UL
#define CYCLES_PER_MS (F_CPU / 1000UL)

/* ATmega328P data-space addresses */
#define REG_PINB 0x23
#define REG_PINC 0x26
#define REG_PIND 0x29

enum { N, E, S, W };
static const char *ARM = "NESW";

static avr_t *avr;
static int failures;

typedef struct { int r, y, g; } Head;

static int pin_level(int arduinoPin) {
  if (arduinoPin <= 7) return (avr->data[REG_PIND] >> arduinoPin) & 1;
  if (arduinoPin <= 13) return (avr->data[REG_PINB] >> (arduinoPin - 8)) & 1;
  return (avr->data[REG_PINC] >> (arduinoPin - 14)) & 1;
}

static void read_heads(Head h[4], int *walk, int *stop) {
  for (int a = 0; a < 4; a++) {
    h[a].r = pin_level(2 + a * 3);
    h[a].y = pin_level(3 + a * 3);
    h[a].g = pin_level(4 + a * 3);
  }
  *walk = pin_level(17); /* A3 */
  *stop = pin_level(18); /* A4 */
}

static uint8_t inputLevels = 0x07; /* A0..A2 released (pulled up) */

static void set_input(int analogPin, int pressed) {
  /* inputs are pulled up, active low */
  if (pressed) inputLevels &= ~(1 << analogPin);
  else inputLevels |= (1 << analogPin);
  avr_ioport_external_t ext = { .name = 'C', .mask = 0x07, .value = inputLevels };
  avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL('C'), &ext);
  avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), analogPin),
                pressed ? 0 : 1);
}

static void fail(unsigned ms, const char *msg) {
  if (failures < 20) printf("FAIL @%ums: %s\n", ms, msg);
  failures++;
}

static void run_ms(unsigned ms) {
  avr_cycle_count_t target = avr->cycle + (avr_cycle_count_t)ms * CYCLES_PER_MS;
  while (avr->cycle < target) {
    int st = avr_run(avr);
    if (st == cpu_Done || st == cpu_Crashed) {
      printf("CPU stopped (state %d)\n", st);
      exit(2);
    }
  }
}

/* Statistics collected over a window */
typedef struct {
  int greenSeen[4];
  int walkSeen;
  int allYellowFlash;
  int allRedMs;
  int anyGreenMs;
  int allDarkMs;
  int samples;
} Window;

static void sample(unsigned now, Window *w, int phaseSplit) {
  Head h[4];
  int walk, stop;
  read_heads(h, &walk, &stop);
  int moving = 0, greens = 0, allRed = 1, allYellow = 1, allDark = 1;

  for (int a = 0; a < 4; a++) {
    if (h[a].g && (h[a].r || h[a].y)) fail(now, "green lit together with red/yellow");
    if (h[a].g) { greens++; w->greenSeen[a] = 1; }
    if (h[a].g || h[a].y) moving |= 1 << a;
    if (!h[a].r || h[a].y || h[a].g) allRed = 0;
    if (!h[a].y) allYellow = 0;
    if (h[a].r || h[a].y || h[a].g) allDark = 0;
  }
  if (allDark) w->allDarkMs++;
  if (phaseSplit && greens > 1) fail(now, "two approaches green at once (split plan)");
  if (!phaseSplit) {
    int ns = (1 << N) | (1 << S), ew = (1 << E) | (1 << W);
    int greenMask = 0;
    for (int a = 0; a < 4; a++) if (h[a].g) greenMask |= 1 << a;
    if ((greenMask & ns) && (greenMask & ew)) fail(now, "crossing streams green");
  }
  if (walk && moving) fail(now, "pedestrian WALK while traffic moving");
  if (walk && stop) fail(now, "WALK and DON'T WALK both lit");
  if (walk) w->walkSeen = 1;
  if (allYellow) w->allYellowFlash = 1;
  if (allRed) w->allRedMs++;
  if (greens) w->anyGreenMs++;
  w->samples++;
}

static unsigned clock_ms;

static void run_window(unsigned ms, Window *w, int split) {
  memset(w, 0, sizeof(*w));
  for (unsigned i = 0; i < ms; i++) {
    run_ms(1);
    sample(++clock_ms, w, split);
  }
}

static void expect(int cond, const char *msg) {
  printf("  %-55s %s\n", msg, cond ? "ok" : "FAILED");
  if (!cond) failures++;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s firmware.elf [opposing]\n", argv[0]);
    return 2;
  }
  int split = !(argc > 2 && strcmp(argv[2], "opposing") == 0);
  int faultTest = argc > 2 && strcmp(argv[2], "fault") == 0;

  elf_firmware_t fw;
  memset(&fw, 0, sizeof(fw));
  if (elf_read_firmware(argv[1], &fw)) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  avr = avr_make_mcu_by_name("atmega328p");
  avr_init(avr);
  avr->frequency = F_CPU;
  avr_load_firmware(avr, &fw);

  for (int p = 0; p < 3; p++) set_input(p, 0);

  Window w;

  if (faultTest) {
    /* Build with FAULT_TEST=1: the firmware requests every arm green at
     * t = 8 s. The monitor must refuse it before it reaches the pins and
     * latch FAULT (all heads flashing red) for good. */
    printf("\n[F] injected conflict\n");
    run_window(12000, &w, split);
    run_window(4000, &w, split);
    expect(w.anyGreenMs == 0, "no green after the conflict");
    expect(w.allRedMs > 1000 && w.allDarkMs > 1000, "all heads flashing red");
    set_input(1, 1); run_window(2000, &w, split); set_input(1, 0);
    set_input(0, 1); run_window(2000, &w, split); set_input(0, 0);
    run_window(6000, &w, split);
    expect(w.anyGreenMs == 0 && w.allDarkMs > 1000, "stays latched after inputs toggle");
    printf("\nResult: %s (%d problem%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
  }

  /* The x5 sim build: 1 logical second = 200 ms. One full split cycle is
   * (30+25+30+25 + 4*5) s = 130 s logical = 26 s simulated. */
  printf("\n[1] normal cycle\n");
  run_window(30000, &w, split);
  expect(w.greenSeen[N] && w.greenSeen[E] && w.greenSeen[S] && w.greenSeen[W],
         "every approach received green");
  expect(!w.walkSeen, "no WALK without a request");

  printf("[2] pedestrian request\n");
  set_input(2, 1); run_window(200, &w, split); set_input(2, 0);
  run_window(12000, &w, split);
  expect(w.walkSeen, "WALK shown after request");

  printf("[3] night mode\n");
  set_input(0, 1);
  run_window(10000, &w, split);       /* let the running phase finish */
  run_window(3000, &w, split);
  expect(w.allYellowFlash && w.anyGreenMs == 0, "flashing yellow, no green");

  printf("[4] night mode off\n");
  set_input(0, 0);
  run_window(4000, &w, split);
  expect(w.anyGreenMs > 0, "returns to normal through all-red");

  printf("[5] emergency hold\n");
  set_input(1, 1);
  run_window(2500, &w, split);        /* yellow 0.6 s + all-red 0.4 s max */
  run_window(5000, &w, split);
  expect(w.allRedMs == w.samples, "all red for the whole hold");

  printf("[6] emergency released\n");
  set_input(1, 0);
  run_window(4000, &w, split);
  expect(w.anyGreenMs > 0, "traffic resumes after release");

  printf("\nResult: %s (%d problem%s)\n", failures ? "FAIL" : "PASS",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
