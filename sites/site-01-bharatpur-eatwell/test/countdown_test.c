/*
 * Site 1 countdown test (simavr).
 *
 * Runs two boards side by side, the way they are wired in Proteus:
 *   controller (traffic_controller_site01, 5x build)  A5 --->  RX  display unit
 *
 *   countdown_test <controller.elf> <display.elf>          normal operation
 *   countdown_test <controller-fault.elf> <display.elf> fault
 *
 * Checks
 *   - every countdown frame on A5 is well formed with a good checksum
 *   - the letters always match the lamps that are actually lit
 *   - a red arm's number is the real time until its green starts, and a
 *     green arm's number is the real time until its yellow (within 1 s)
 *   - the serial log times match the timing plan (a 35 s green reads 35 s)
 *   - the display unit shows exactly what the last frame said, shows
 *     dashes in night / emergency / fault, ignores corrupt frames and
 *     blanks itself when the link goes quiet
 *
 * Exit code 0 = all checks passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>

#define F_CPU 16000000UL
#define CYC_MS (F_CPU / 1000UL)
#define BIT_CYC 1664UL               /* Timer2: 16 MHz / 8 / 208 = 9615 baud */
#define SCALE_MS 200                 /* 5x build: one controller second */

#define REG_PINB 0x23
#define REG_PINC 0x26
#define REG_PIND 0x29

enum { N, E, S, W };
static const char ARM[] = "NESW";

static avr_t *ctl, *dsp;
static unsigned long now_ms;
static int failures;
static int linked = 1;               /* forward A5 bytes to the display */

/* ------------------------------------------------------------ helpers */
static void expect(int cond, const char *msg) {
  printf("  %-60s %s\n", msg, cond ? "ok" : "FAILED");
  if (!cond) failures++;
}

static avr_t *load(const char *elf) {
  static elf_firmware_t fw[2];
  static int n;
  memset(&fw[n], 0, sizeof(fw[n]));
  if (elf_read_firmware(elf, &fw[n])) { fprintf(stderr, "cannot read %s\n", elf); exit(2); }
  avr_t *a = avr_make_mcu_by_name("atmega328p");
  avr_init(a);
  a->frequency = F_CPU;
  avr_load_firmware(a, &fw[n]);
  uint32_t flags = 0;                /* keep simavr from echoing the UART */
  avr_ioctl(a, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
  flags &= ~AVR_UART_FLAG_STDIO;
  avr_ioctl(a, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
  n++;
  return a;
}

static int lvl(avr_t *a, int pin) {
  if (pin <= 7) return (a->data[REG_PIND] >> pin) & 1;
  if (pin <= 13) return (a->data[REG_PINB] >> (pin - 8)) & 1;
  return (a->data[REG_PINC] >> (pin - 14)) & 1;
}

static uint8_t inLevels = 0x07;
static void set_input(int analogPin, int pressed) {
  if (pressed) inLevels &= ~(1 << analogPin); else inLevels |= (1 << analogPin);
  avr_ioport_external_t ext = { .name = 'C', .mask = 0x07, .value = inLevels };
  avr_ioctl(ctl, AVR_IOCTL_IOPORT_SET_EXTERNAL('C'), &ext);
  avr_raise_irq(avr_io_getirq(ctl, AVR_IOCTL_IOPORT_GETIRQ('C'), analogPin), pressed ? 0 : 1);
}

/* lamp letter actually lit on an arm: G, Y, R or '.' (dark) */
static char lamp(int a) {
  if (lvl(ctl, 4 + a * 3)) return 'G';
  if (lvl(ctl, 3 + a * 3)) return 'Y';
  if (lvl(ctl, 2 + a * 3)) return 'R';
  return '.';
}

/* ------------------------------------------------------------ controller log */
static char log_buf[1 << 17];
static size_t log_len;
static void on_log(struct avr_irq_t *irq, uint32_t v, void *p) {
  (void)irq; (void)p;
  if (log_len < sizeof(log_buf) - 1) { log_buf[log_len++] = (char)v; log_buf[log_len] = 0; }
}

/* display unit's own serial output (link up / lost messages) */
static char dlog[4096];
static size_t dlog_len;
static void on_dlog(struct avr_irq_t *irq, uint32_t v, void *p) {
  (void)irq; (void)p;
  if (dlog_len < sizeof(dlog) - 1) { dlog[dlog_len++] = (char)v; dlog[dlog_len] = 0; }
}

/* ------------------------------------------------------------ A5 decoder */
typedef struct {
  unsigned long t_ms;                /* start of the first byte */
  char text[40];
  char lamps[4];                     /* lamps lit when the frame started */
  int valid;
} Frame;

#define MAX_FRAMES 4000
static Frame frames[MAX_FRAMES];
static int nframes, badframes;

static int rx_level = 1, in_byte;
static avr_cycle_count_t byte_start, edge_cyc[16];
static int edge_lvl[16], nedges;
static char cur[64];
static int cur_len;
static unsigned long cur_t;
static char cur_lamps[4], byte_lamps[4];

static int level_at(avr_cycle_count_t c) {
  int l = 0;                         /* the start edge drove it low */
  for (int i = 0; i < nedges; i++) if (edge_cyc[i] <= c) l = edge_lvl[i];
  return l;
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static int frame_ok(const char *s) {
  if (strlen(s) != 28 || s[0] != '$' || strncmp(s, "$CD,", 4) || s[25] != '*') return 0;
  unsigned x = 0;
  for (int i = 1; i < 25; i++) x ^= (unsigned char)s[i];
  return hexv(s[26]) >= 0 && hexv(s[27]) >= 0 && x == (unsigned)(hexv(s[26]) * 16 + hexv(s[27]));
}

static void got_byte(uint8_t b) {
  if (linked) avr_raise_irq(avr_io_getirq(dsp, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT), b);
  if (b == '$') {
    cur_len = 0;
    cur_t = now_ms;
    memcpy(cur_lamps, byte_lamps, 4);   /* lamps at the start bit */
  }
  if (b == '\n') {
    if (cur_len && cur[cur_len - 1] == '\r') cur_len--;
    cur[cur_len] = 0;
    if (nframes < MAX_FRAMES) {
      Frame *f = &frames[nframes++];
      f->t_ms = cur_t;
      snprintf(f->text, sizeof(f->text), "%.39s", cur);
      memcpy(f->lamps, cur_lamps, 4);
      f->valid = frame_ok(cur);
      if (!f->valid) badframes++;
    }
    cur_len = 0;
  } else if (cur_len < (int)sizeof(cur) - 1) {
    cur[cur_len++] = (char)b;
  }
}

static void finish_byte_if_due(void) {
  if (!in_byte || ctl->cycle < byte_start + BIT_CYC * 19 / 2) return;
  uint8_t b = 0;
  for (int i = 0; i < 8; i++)
    if (level_at(byte_start + BIT_CYC * (3 + 2 * i) / 2)) b |= 1 << i;
  in_byte = 0;
  nedges = 0;
  got_byte(b);
}

static void on_a5(struct avr_irq_t *irq, uint32_t v, void *p) {
  (void)irq; (void)p;
  finish_byte_if_due();
  if (!in_byte && rx_level == 1 && v == 0) {
    in_byte = 1;
    byte_start = ctl->cycle;
    nedges = 0;
    for (int a = 0; a < 4; a++) byte_lamps[a] = lamp(a);
  } else if (in_byte && nedges < 16) {
    edge_cyc[nedges] = ctl->cycle;
    edge_lvl[nedges++] = (int)v;
  }
  rx_level = (int)v;
}

/* ------------------------------------------------------------ MAX7219 model */
static uint8_t chip[2][16];
static uint16_t sr;
static int din, clk, load1 = 1, load2 = 1;

static void on_din(struct avr_irq_t *i, uint32_t v, void *p) { (void)i; (void)p; din = (int)v; }
static void on_clk(struct avr_irq_t *i, uint32_t v, void *p) {
  (void)i; (void)p;
  if (v && !clk) sr = (uint16_t)((sr << 1) | (din & 1));
  clk = (int)v;
}
static void latch(int c) { chip[c][(sr >> 8) & 0x0F] = (uint8_t)(sr & 0xFF); }
static void on_load1(struct avr_irq_t *i, uint32_t v, void *p) { (void)i; (void)p; if (v && !load1) latch(0); load1 = (int)v; }
static void on_load2(struct avr_irq_t *i, uint32_t v, void *p) { (void)i; (void)p; if (v && !load2) latch(1); load2 = (int)v; }

/* the four digits of an arm as text, decoded from the segment patterns */
static void arm_digits(int a, char out[5]) {
  static const struct { uint8_t seg; char ch; } MAP[] = {
    {0x7E,'0'},{0x30,'1'},{0x6D,'2'},{0x79,'3'},{0x33,'4'},{0x5B,'5'},{0x5F,'6'},
    {0x70,'7'},{0x7F,'8'},{0x7B,'9'},{0x00,' '},{0x01,'-'},{0x5E,'G'},{0x3B,'Y'},{0x05,'r'}};
  int c = a < 2 ? 0 : 1, first = (a % 2) * 4;
  for (int d = 0; d < 4; d++) {
    uint8_t s = chip[c][1 + first + d];
    out[d] = '?';
    for (unsigned k = 0; k < sizeof(MAP) / sizeof(MAP[0]); k++) if (MAP[k].seg == s) out[d] = MAP[k].ch;
  }
  out[4] = 0;
}

/* what the display should show for one frame field */
static void expected_digits(const char *f, char out[5]) {
  int timed = f[1] >= '0' && f[1] <= '9';
  char letter = f[0] == 'G' ? 'G' : f[0] == 'Y' ? 'Y' : f[0] == 'R' ? 'r' : 0;
  if (!timed || !letter) { strcpy(out, "----"); return; }
  int v = (f[1] - '0') * 100 + (f[2] - '0') * 10 + (f[3] - '0');
  snprintf(out, 5, "%c%c%c%c", letter, v >= 100 ? '0' + v / 100 : ' ',
           v >= 10 ? '0' + (v / 10) % 10 : ' ', '0' + v % 10);
}

/* ------------------------------------------------------------ timeline */
#define MAX_EVT 64
static unsigned long onset[4][MAX_EVT], ending[4][MAX_EVT];
static int nonset[4], nending[4];
static char prev_lamp[4] = { '.', '.', '.', '.' };
static unsigned long input_events[16];
static int ninput;

static int display_checks, display_mismatch;
static char display_mismatch_msg[160];

static void step_ms(void) {
  avr_cycle_count_t t1 = ctl->cycle + CYC_MS, t2 = dsp->cycle + CYC_MS;
  while (ctl->cycle < t1 || dsp->cycle < t2) {
    if (ctl->cycle <= dsp->cycle && ctl->cycle < t1) {
      int st = avr_run(ctl);
      if (st == cpu_Done || st == cpu_Crashed) { printf("controller stopped\n"); exit(2); }
      finish_byte_if_due();
    } else {
      int st = avr_run(dsp);
      if (st == cpu_Done || st == cpu_Crashed) { printf("display stopped\n"); exit(2); }
    }
  }
  now_ms++;
  for (int a = 0; a < 4; a++) {
    char l = lamp(a);
    if (l == 'G' && prev_lamp[a] != 'G' && nonset[a] < MAX_EVT) onset[a][nonset[a]++] = now_ms;
    if (l != 'G' && prev_lamp[a] == 'G' && nending[a] < MAX_EVT) ending[a][nending[a]++] = now_ms;
    prev_lamp[a] = l;
  }
  /* 45 ms after the latest frame started (it takes ~31 ms to send, the
   * next one can't start before 50 ms), the display must show it */
  /* (the display runs a 1 s lamp test at power-up and misses those frames) */
  if (linked && nframes && frames[nframes - 1].valid && frames[nframes - 1].t_ms > 1300 &&
      now_ms == frames[nframes - 1].t_ms + 45) {
    const Frame *f = &frames[nframes - 1];
    for (int a = 0; a < 4; a++) {
      char want[5], got[5];
      expected_digits(f->text + 6 + a * 5, want);
      arm_digits(a, got);
      display_checks++;
      if (strcmp(want, got)) {
        if (!display_mismatch)
          snprintf(display_mismatch_msg, sizeof(display_mismatch_msg),
                   "@%lums arm %c frame %s shows '%s' want '%s'", now_ms, ARM[a], f->text, got, want);
        display_mismatch++;
      }
    }
  }
}

static void run(unsigned long ms) { while (ms--) step_ms(); }
static void input(int pin, int on) { set_input(pin, on); if (ninput < 16) input_events[ninput++] = now_ms; }

static int any_input_between(unsigned long a, unsigned long b) {
  for (int i = 0; i < ninput; i++) if (input_events[i] > a && input_events[i] <= b) return 1;
  return 0;
}

static long first_after(unsigned long *list, int n, unsigned long t) {
  for (int i = 0; i < n; i++) if (list[i] > t) return (long)list[i];
  return -1;
}

static int field_num(const char *f) {
  if (f[1] < '0' || f[1] > '9') return -1;
  return (f[1] - '0') * 100 + (f[2] - '0') * 10 + (f[3] - '0');
}

/* log: seconds between "GREEN N for 35s" and the following "YELLOW N" */
static int log_green_seconds(void) {
  const char *g = strstr(log_buf, "] GREEN N for 35s");
  if (!g) return -1;
  const char *y = strstr(g, "] YELLOW N");
  if (!y) return -1;
  const char *tg = g, *ty = y;
  while (tg > log_buf && strncmp(tg, " t=", 3)) tg--;
  while (ty > log_buf && strncmp(ty, " t=", 3)) ty--;
  return atoi(ty + 3) - atoi(tg + 3);
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s controller.elf display.elf [fault]\n", argv[0]); return 2; }
  int fault = argc > 3 && !strcmp(argv[3], "fault");

  ctl = load(argv[1]);
  dsp = load(argv[2]);
  avr_irq_register_notify(avr_io_getirq(ctl, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), on_log, NULL);
  avr_irq_register_notify(avr_io_getirq(dsp, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), on_dlog, NULL);
  avr_irq_register_notify(avr_io_getirq(ctl, AVR_IOCTL_IOPORT_GETIRQ('C'), 5), on_a5, NULL);
  avr_irq_register_notify(avr_io_getirq(dsp, AVR_IOCTL_IOPORT_GETIRQ('B'), 3), on_din, NULL);
  avr_irq_register_notify(avr_io_getirq(dsp, AVR_IOCTL_IOPORT_GETIRQ('B'), 5), on_clk, NULL);
  avr_irq_register_notify(avr_io_getirq(dsp, AVR_IOCTL_IOPORT_GETIRQ('B'), 2), on_load1, NULL);
  avr_irq_register_notify(avr_io_getirq(dsp, AVR_IOCTL_IOPORT_GETIRQ('B'), 1), on_load2, NULL);
  for (int p = 0; p < 3; p++) set_input(p, 0);

  if (fault) {
    printf("\n[F] forced conflict: countdown goes to dashes\n");
    run(16000);
    int last_ok = 0;
    for (int i = nframes - 1; i >= 0; i--) {
      if (frames[i].t_ms < 12000) break;
      last_ok = frames[i].valid && !strncmp(frames[i].text, "$CD,F,X---,X---,X---,X---", 25);
      if (!last_ok) break;
    }
    expect(last_ok, "frames say FAULT, every arm X---");
    char d[5]; int dashes = 1;
    for (int a = 0; a < 4; a++) { arm_digits(a, d); if (strcmp(d, "----")) dashes = 0; }
    expect(dashes, "display shows ---- on every arm");
    printf("\nResult: %s (%d problem%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
  }

  /* timeline: 2 normal cycles, a pedestrian request, night, emergency */
  run(60000);
  unsigned long ped_at = now_ms;
  input(2, 1); run(200); input(2, 0);
  run(40000);
  unsigned long night_at = now_ms;
  input(0, 1); run(14000);
  unsigned long night_frames_from = now_ms - 3000;
  input(0, 0); run(6000);
  unsigned long emerg_at = now_ms;
  input(1, 1); run(4000);
  unsigned long emerg_frames_from = now_ms - 2000;
  input(1, 0); run(8000);
  (void)ped_at; (void)night_at; (void)emerg_at;

  printf("\n[1] countdown link frames\n");
  int max_gap = 0;
  for (int i = 1; i < nframes; i++) {
    int gap = (int)(frames[i].t_ms - frames[i - 1].t_ms);
    if (gap > max_gap) max_gap = gap;
  }
  char msg[120];
  snprintf(msg, sizeof msg, "%d frames, all with a good checksum", nframes);
  expect(nframes > 300 && badframes == 0, msg);
  snprintf(msg, sizeof msg, "a frame at least once a second (longest gap %d ms)", max_gap);
  expect(max_gap <= 1100, msg);
  expect(nframes && !strncmp(frames[0].text, "$CD,U,R00", 9), "first frame during start-up all-red");

  const char *first_green = NULL;
  for (int i = 0; i < nframes; i++)
    if (!strncmp(frames[i].text, "$CD,G,", 6)) { first_green = frames[i].text; break; }
  snprintf(msg, sizeof msg, "first green: %.20s", first_green ? first_green + 6 : "none");
  expect(first_green && !strncmp(first_green + 6, "G035,R042,R069,R111", 19), msg);

  printf("[2] letters match the lamps\n");
  int letter_bad = 0, letter_checked = 0;
  for (int i = 0; i < nframes; i++) {
    Frame *f = &frames[i];
    if (!f->valid || f->text[4] == 'N' || f->text[4] == 'F') continue;
    for (int a = 0; a < 4; a++) {
      char want = f->text[6 + a * 5];
      letter_checked++;
      if (want != f->lamps[a]) {
        if (!letter_bad) printf("    first mismatch @%lums %s arm %c lamp %c\n", f->t_ms, f->text, ARM[a], f->lamps[a]);
        letter_bad++;
      }
    }
  }
  snprintf(msg, sizeof msg, "%d arm readings, %d wrong", letter_checked, letter_bad);
  expect(letter_checked > 1000 && letter_bad == 0, msg);

  printf("[3] numbers match real time\n");
  int red_checked = 0, red_bad = 0, green_checked = 0, green_bad = 0;
  for (int i = 0; i < nframes; i++) {
    Frame *f = &frames[i];
    if (!f->valid) continue;
    for (int a = 0; a < 4; a++) {
      const char *fld = f->text + 6 + a * 5;
      int v = field_num(fld);
      if (v < 0 || (fld[0] != 'R' && fld[0] != 'G')) continue;
      long actual = fld[0] == 'R' ? first_after(onset[a], nonset[a], f->t_ms)
                                  : first_after(ending[a], nending[a], f->t_ms);
      if (actual < 0) continue;
      if (any_input_between(f->t_ms - 300, (unsigned long)actual)) continue;
      long left = actual - (long)f->t_ms;              /* real ms */
      /* number shown is the rounded-up seconds: v-1 < left/SCALE <= v */
      int ok = left > (long)(v - 1) * SCALE_MS - 30 && left <= (long)v * SCALE_MS + 30;
      if (fld[0] == 'R') { red_checked++; if (!ok) red_bad++; }
      else { green_checked++; if (!ok) green_bad++; }
      if (!ok && red_bad + green_bad <= 3)
        printf("    @%lums %s arm %c says %d s, real %.2f s\n", f->t_ms, f->text, ARM[a], v, left / (double)SCALE_MS);
    }
  }
  snprintf(msg, sizeof msg, "red: time to green right in %d of %d", red_checked - red_bad, red_checked);
  expect(red_checked > 500 && red_bad == 0, msg);
  snprintf(msg, sizeof msg, "green: time to yellow right in %d of %d", green_checked - green_bad, green_checked);
  expect(green_checked > 100 && green_bad == 0, msg);

  int ped_seen = 0;
  for (int i = 0; i < nframes; i++) if (frames[i].text[4] == 'W') ped_seen = 1;
  expect(ped_seen, "pedestrian phase sent (state W), countdown includes it");

  printf("[4] night and emergency\n");
  int night_ok = 0, night_n = 0, em_ok = 0, em_n = 0;
  for (int i = 0; i < nframes; i++) {
    Frame *f = &frames[i];
    if (f->t_ms >= night_frames_from && f->t_ms < night_frames_from + 2500) {
      night_n++; night_ok += !strncmp(f->text, "$CD,N,F---,F---,F---,F---", 25);
    }
    if (f->t_ms >= emerg_frames_from && f->t_ms < emerg_frames_from + 1500) {
      em_n++; em_ok += !strncmp(f->text, "$CD,E,R---,R---,R---,R---", 25);
    }
  }
  expect(night_n > 0 && night_ok == night_n, "night: every arm F--- (no number)");
  expect(em_n > 0 && em_ok == em_n, "emergency: every arm R--- (no number)");

  printf("[5] serial log uses the plan's seconds\n");
  int gs = log_green_seconds();
  snprintf(msg, sizeof msg, "GREEN N for 35s lasts %d s in the log", gs);
  expect(gs == 35, msg);
  expect(strstr(log_buf, "countdown link on A5") != NULL, "boot log announces the countdown link");

  printf("[6] display unit\n");
  snprintf(msg, sizeof msg, "shows every frame (%d arm checks, %d wrong)", display_checks, display_mismatch);
  expect(display_checks > 1000 && display_mismatch == 0, msg);
  if (display_mismatch) printf("    %s\n", display_mismatch_msg);
  expect(chip[0][0x09] == 0 && chip[0][0x0B] == 7 && chip[0][0x0C] == 1 &&
         chip[1][0x09] == 0 && chip[1][0x0B] == 7 && chip[1][0x0C] == 1, "MAX7219 set up (raw, 8 digits, on)");
  expect(lvl(dsp, 7) == 1, "link LED on while frames arrive");

  /* a corrupt frame must not change the digits */
  linked = 0;
  char before[4][5];
  for (int a = 0; a < 4; a++) arm_digits(a, before[a]);
  const char *bad = "$CD,G,G099,R099,R099,R099*00\r\n";
  for (const char *c = bad; *c; c++) {
    avr_raise_irq(avr_io_getirq(dsp, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT), (uint8_t)*c);
    run(2);
  }
  run(50);
  int same = 1;
  for (int a = 0; a < 4; a++) { char d[5]; arm_digits(a, d); if (strcmp(d, before[a])) same = 0; }
  expect(same, "corrupt frame (bad checksum) ignored");

  /* cable cut: blank after 3 s */
  run(3500);
  int blank = 1;
  for (int a = 0; a < 4; a++) { char d[5]; arm_digits(a, d); if (strcmp(d, "    ")) blank = 0; }
  expect(blank && lvl(dsp, 7) == 0, "link quiet 3 s: display blank, LED off");
  expect(strstr(dlog, "link lost") != NULL, "display reports link lost");

  printf("\nResult: %s (%d problem%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
  if (failures && getenv("SHOW_LOG")) printf("--- log ---\n%s\n", log_buf);
  return failures ? 1 : 0;
}
