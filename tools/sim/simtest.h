/*
 * Tiny helper for bench-testing Arduino Uno firmware in simavr.
 * Each project's test (projects/<name>/test/sim_test.c) includes this file,
 * loads the real compiled ELF and drives pins / ADC inputs on a timeline.
 *
 *   sudo apt-get install simavr libsimavr-dev libelf-dev
 *   gcc -I tools/sim projects/<name>/test/sim_test.c -o /tmp/t -lsimavr -lelf
 *
 * Pin numbers are Arduino numbers: 0..13 = D0..D13, 14..19 = A0..A5.
 */
#ifndef SIMTEST_H
#define SIMTEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_uart.h>

#define SIM_F_CPU 16000000UL
#define SIM_CYCLES_PER_MS (SIM_F_CPU / 1000UL)

static avr_t *sim;
static int sim_failures;
static unsigned long sim_now_ms;

/* ---- serial output capture ------------------------------------------- */
static char sim_uart[1 << 16];
static size_t sim_uart_len;

static void sim_uart_byte(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; (void)param;
  if (sim_uart_len < sizeof(sim_uart) - 1) {
    sim_uart[sim_uart_len++] = (char)value;
    sim_uart[sim_uart_len] = '\0';
  }
  if (getenv("SIM_ECHO")) putchar((int)value);
}

/* Marks the current end of the log; uart_since() searches after it. */
__attribute__((unused)) static size_t sim_uart_mark(void) { return sim_uart_len; }

__attribute__((unused)) static int uart_since(size_t mark, const char *needle) {
  return strstr(sim_uart + mark, needle) != NULL;
}

/* ---- pins --------------------------------------------------------------- */
static char port_of(int pin) { return pin <= 7 ? 'D' : pin <= 13 ? 'B' : 'C'; }
static int bit_of(int pin) { return pin <= 7 ? pin : pin <= 13 ? pin - 8 : pin - 14; }

__attribute__((unused)) static int pin_read(int pin) {
  avr_ioport_state_t st;
  avr_ioctl(sim, AVR_IOCTL_IOPORT_GETSTATE(port_of(pin)), &st);
  return (st.pin >> bit_of(pin)) & 1;
}

static uint8_t ext_mask[3], ext_value[3];

/* Drives an input pin from outside, like a switch or sensor would. */
__attribute__((unused)) static void pin_drive(int pin, int level) {
  char p = port_of(pin);
  int i = p == 'B' ? 0 : p == 'C' ? 1 : 2, b = bit_of(pin);
  ext_mask[i] |= 1 << b;
  if (level) ext_value[i] |= 1 << b; else ext_value[i] &= ~(1 << b);
  avr_ioport_external_t ext = { .name = p, .mask = ext_mask[i], .value = ext_value[i] };
  avr_ioctl(sim, AVR_IOCTL_IOPORT_SET_EXTERNAL(p), &ext);
  avr_raise_irq(avr_io_getirq(sim, AVR_IOCTL_IOPORT_GETIRQ(p), b), level);
}

/* Sets the voltage on analog input A<ch> in millivolts. */
__attribute__((unused)) static void adc_set_mv(int ch, int mv) {
  avr_raise_irq(avr_io_getirq(sim, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + ch), mv);
}

/* ---- pulse width (servo) ------------------------------------------------ */
typedef struct { int pin; avr_cycle_count_t rise; unsigned last_us; unsigned count; } Pulse;
static Pulse sim_pulse;

__attribute__((unused)) static void pulse_edge(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq; Pulse *p = (Pulse *)param;
  if (value) {
    p->rise = sim->cycle;
  } else if (p->rise) {
    p->last_us = (unsigned)((sim->cycle - p->rise) / (SIM_F_CPU / 1000000UL));
    p->count++;
  }
}

__attribute__((unused)) static void pulse_watch(int pin) {
  sim_pulse.pin = pin;
  avr_irq_register_notify(avr_io_getirq(sim, AVR_IOCTL_IOPORT_GETIRQ(port_of(pin)), bit_of(pin)),
                          pulse_edge, &sim_pulse);
}

/* Extra watchers when a test needs more than one servo signal. */
__attribute__((unused)) static Pulse *pulse_watch_extra(int pin) {
  static Pulse pool[4];
  static int used;
  if (used >= 4) { fprintf(stderr, "too many pulse watchers\n"); exit(2); }
  Pulse *p = &pool[used++];
  p->pin = pin;
  avr_irq_register_notify(avr_io_getirq(sim, AVR_IOCTL_IOPORT_GETIRQ(port_of(pin)), bit_of(pin)),
                          pulse_edge, p);
  return p;
}

/* ---- run control -------------------------------------------------------- */
typedef void (*sim_sampler)(unsigned long now_ms);
static sim_sampler sim_on_ms;

static void sim_run_ms(unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) {
    avr_cycle_count_t target = sim->cycle + SIM_CYCLES_PER_MS;
    while (sim->cycle < target) {
      int st = avr_run(sim);
      if (st == cpu_Done || st == cpu_Crashed) {
        printf("CPU stopped (state %d) at %lu ms\n", st, sim_now_ms);
        exit(2);
      }
    }
    sim_now_ms++;
    if (sim_on_ms) sim_on_ms(sim_now_ms);
  }
}

static void sim_load(const char *elf) {
  static elf_firmware_t fw;
  memset(&fw, 0, sizeof(fw));
  if (elf_read_firmware(elf, &fw)) {
    fprintf(stderr, "cannot read %s\n", elf);
    exit(2);
  }
  sim = avr_make_mcu_by_name("atmega328p");
  if (!sim) { fprintf(stderr, "no atmega328p in simavr\n"); exit(2); }
  avr_init(sim);
  sim->frequency = SIM_F_CPU;
  sim->vcc = sim->avcc = sim->aref = 5000;
  avr_load_firmware(sim, &fw);
  sim->log = getenv("SIM_ECHO") ? LOG_OUTPUT : LOG_NONE;
  avr_irq_register_notify(avr_io_getirq(sim, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
                          sim_uart_byte, NULL);
}

/* ---- results ------------------------------------------------------------ */
__attribute__((unused)) static void section(const char *title) { printf("[%s]\n", title); }

static void expect(int cond, const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  printf("  %-60s %s\n", msg, cond ? "ok" : "FAILED");
  if (!cond) sim_failures++;
}

/* Types a line into the board's serial port, one byte per ~2 ms (9600 baud
 * is about 1 ms per byte, so the receive buffer never overflows). */
__attribute__((unused)) static void uart_send(const char *text) {
  avr_irq_t *rx = avr_io_getirq(sim, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
  for (const char *c = text; *c; c++) {
    avr_raise_irq(rx, (uint8_t)*c);
    sim_run_ms(2);
  }
}

static int sim_finish(void) {
  printf("\nResult: %s (%d problem%s)\n", sim_failures ? "FAIL" : "PASS",
         sim_failures, sim_failures == 1 ? "" : "s");
  if (sim_failures && !getenv("SIM_ECHO")) {
    printf("--- serial log ---\n%s\n", sim_uart);
  }
  return sim_failures ? 1 : 0;
}

#endif
