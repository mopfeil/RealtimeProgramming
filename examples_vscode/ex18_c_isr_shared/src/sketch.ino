// Interrupt and shared data in C: torn reads, critical sections, latency.
// The same experiment as ex19 (MicroPython) and ex20 (Rust).
//
// PWM slice 2 drives GP5 with 1 kHz; a wire takes the signal back to GP6.
// Each rising edge on GP6 raises an interrupt. The PWM counter runs at
// 1 MHz and starts at 0 on the edge, so the counter value that the ISR reads
// is its own latency in microseconds.
//
// The ISR writes a sample of 8 words, all with the same sequence number.
// The main loop copies the sample and "processes" every word (10 us each).
// A copy whose words differ is torn: the ISR came in the middle of it.
//
//   DIP switch 1 OFF (GP10 HIGH): no protection, torn copies.
//   DIP switch 1 ON  (GP10 LOW) : interrupts disabled while copying.
//     DIP switch 2 OFF: long critical section, copy and process inside.
//       No torn copies, but the ISR waits up to ~80 us: latency goes up.
//     DIP switch 2 ON : short critical section, only the copy is inside,
//       processing follows with interrupts enabled. Low latency, no tears.
//
// Output every second on Serial1 (GP0/GP1), e.g.
//   protect=0 short=0 irqs 1000 copies 12000 torn 950 latency min/max 2/2 us
//
// Logic analyzer: D0 = GP5 (PWM, the interrupt source), D1 = GP3 (HIGH
// during the ISR), D2 = GP2 (HIGH while main copies/processes).

#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/timer.h>

const uint PIN_PWM = 5, PIN_IRQ_IN = 6, PIN_ISR = 3, PIN_MAIN = 2;
const uint PIN_SW_PROTECT = 10, PIN_SW_SHORT = 11;
const int WORDS = 8;

struct Sample { uint32_t w[WORDS]; };
volatile Sample shared;               // written by the ISR, read by main
volatile uint32_t irqs = 0, latMin = UINT32_MAX, latMax = 0;
uint slice;

void onEdge() {                       // ISR
  gpio_put(PIN_ISR, 1);
  uint32_t lat = pwm_get_counter(slice);   // us since the edge
  if (lat < latMin) latMin = lat;
  if (lat > latMax) latMax = lat;
  uint32_t seq = irqs + 1;
  for (int i = 0; i < WORDS; i++) shared.w[i] = seq;
  irqs = seq;
  gpio_put(PIN_ISR, 0);
}

uint32_t process(uint32_t w) {        // stands for real work on each word
  busy_wait_us_32(10);
  return w;
}

void setup() {
  Serial1.begin(115200);
  gpio_init(PIN_ISR);  gpio_set_dir(PIN_ISR, GPIO_OUT);
  gpio_init(PIN_MAIN); gpio_set_dir(PIN_MAIN, GPIO_OUT);
  pinMode(PIN_SW_PROTECT, INPUT_PULLUP);
  pinMode(PIN_SW_SHORT, INPUT_PULLUP);

  // PWM: 1 MHz counter, period 1000 us, HIGH for the first 500 us
  gpio_set_function(PIN_PWM, GPIO_FUNC_PWM);
  slice = pwm_gpio_to_slice_num(PIN_PWM);
  pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);
  pwm_set_wrap(slice, 999);
  pwm_set_gpio_level(PIN_PWM, 500);
  pinMode(PIN_IRQ_IN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IRQ_IN), onEdge, RISING);
  pwm_set_enabled(slice, true);
  Serial1.println("ex18 C: ISR and shared data");
}

void loop() {
  static uint32_t lastReport = 0, copies = 0, torn = 0;
  bool protect = digitalRead(PIN_SW_PROTECT) == LOW;
  bool shortCs = digitalRead(PIN_SW_SHORT) == LOW;
  uint32_t copy[WORDS];

  gpio_put(PIN_MAIN, 1);
  if (!protect) {
    for (int i = 0; i < WORDS; i++) copy[i] = process(shared.w[i]);
  } else if (!shortCs) {
    uint32_t s = save_and_disable_interrupts();     // long critical section
    for (int i = 0; i < WORDS; i++) copy[i] = process(shared.w[i]);
    restore_interrupts(s);
  } else {
    uint32_t s = save_and_disable_interrupts();     // short critical section
    for (int i = 0; i < WORDS; i++) copy[i] = shared.w[i];
    restore_interrupts(s);
    for (int i = 0; i < WORDS; i++) copy[i] = process(copy[i]);
  }
  gpio_put(PIN_MAIN, 0);

  copies++;
  for (int i = 1; i < WORDS; i++)
    if (copy[i] != copy[0]) { torn++; break; }

  uint32_t now = millis();
  if (now - lastReport >= 1000) {
    lastReport = now;
    uint32_t s = save_and_disable_interrupts();    // read and reset together
    uint32_t n = irqs, lo = latMin, hi = latMax;
    irqs = 0; latMin = UINT32_MAX; latMax = 0;
    restore_interrupts(s);
    char line[100];
    snprintf(line, sizeof line, "protect=%d short=%d irqs %lu copies %lu torn %lu latency min/max %lu/%lu us",
             protect, shortCs, (unsigned long)n, (unsigned long)copies, (unsigned long)torn,
             (unsigned long)lo, (unsigned long)hi);
    Serial1.println(line);
    copies = torn = 0;
  }
}
