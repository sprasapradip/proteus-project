/*
 * Records a real run of the Site 1 firmware for the demo video.
 *
 * Uses the same two-board simavr harness as test/countdown_test.c: the
 * controller (_sim_x5 build) with its A5 countdown link wired to the
 * countdown display unit. Every 50 ms it writes one JSON line with the lamps
 * that are actually lit, the pedestrian lamps, the digits on the four
 * displays, and any new serial log lines. The video only draws this data.
 *
 *   gcc -O1 sim_capture.c -o sim_capture -lsimavr -lelf
 *   ./sim_capture controller_sim_x5.elf countdown_display.elf 40000 > sim.jsonl
 */
#define main countdown_test_main
#include "../../test/countdown_test.c"
#undef main

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s controller.elf display.elf [ms]\n", argv[0]); return 2; }
  unsigned long total = argc > 3 ? strtoul(argv[3], NULL, 10) : 40000;

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

  size_t log_done = 0;
  int last_frame = 0;
  while (now_ms < total) {
    step_ms();
    if (now_ms % 50) continue;
    printf("{\"t\":%lu,\"lamps\":\"%c%c%c%c\",\"walk\":%d,\"stop\":%d,\"disp\":[",
           now_ms, lamp(0), lamp(1), lamp(2), lamp(3), lvl(ctl, 17), lvl(ctl, 18));
    for (int a = 0; a < 4; a++) {
      char d[5];
      arm_digits(a, d);
      printf("%s\"%s\"", a ? "," : "", d);
    }
    printf("],\"frame\":\"%s\",\"log\":[", nframes ? frames[nframes - 1].text : "");
    (void)last_frame;
    int first = 1;
    while (1) {
      char *nl = memchr(log_buf + log_done, '\n', log_len - log_done);
      if (!nl) break;
      char line[160];
      size_t n = (size_t)(nl - (log_buf + log_done));
      if (n > sizeof(line) - 1) n = sizeof(line) - 1;
      memcpy(line, log_buf + log_done, n);
      line[n] = 0;
      if (n && line[n - 1] == '\r') line[n - 1] = 0;
      printf("%s\"", first ? "" : ",");
      for (char *c = line; *c; c++) { if (*c == '"' || *c == '\\') putchar('\\'); putchar(*c); }
      putchar('"');
      first = 0;
      log_done = (size_t)(nl - log_buf) + 1;
    }
    printf("]}\n");
  }
  return 0;
}
