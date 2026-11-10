// Periodic job in C: period, jitter and the price of the heap.
// The same job and the same output as ex16 (MicroPython) and ex17 (Rust).
//
// Every 2000 us the job takes a new sample (pseudo random), runs a 16-tap
// FIR filter over the last 16 samples and builds a log record. Every 500
// periods the program prints statistics (outside of the measurement):
//
//   heap=0 rel=0 gc=- period min/avg/max 2000/2000/2000 us job min/max 13/16 us overruns 0
//
//   DIP switch 1 ON (GP10 LOW): the log record is malloc'ed with a varying
//     size, eight records stay alive in a ring, the oldest is freed.
//     The heap gets fragmented, malloc/free take varying time.
//     OFF: the record is a static buffer.
//   DIP switch 2 ON (GP11 LOW): relative timing, "work, then wait 2000 us".
//     The period becomes 2000 us + job time, and drifts with the job.
//     OFF: absolute timing, "wait until the next multiple of 2000 us".
//   (DIP switch 3 is used by the MicroPython version only.)
//
// Logic analyzer: D0 = GP2 (HIGH during the job), D1 = GP3 (HIGH during
// malloc/free). Output on Serial1 (GP0/GP1).

#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/timer.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const uint PIN_JOB = 2, PIN_HEAP_OP = 3;
const uint PIN_SW_HEAP = 10, PIN_SW_REL = 11;
const uint32_t PERIOD_US = 2000;
const int REPORT_EVERY = 500;        // periods between two reports
const int TAPS = 16;

const int16_t coeff[TAPS] = {1, 3, 7, 12, 18, 24, 28, 31, 31, 28, 24, 18, 12, 7, 3, 1};
int16_t samples[TAPS];                // ring buffer of the last 16 samples
int head = 0;
uint32_t rng = 12345;

char staticRecord[512];               // record without heap
char *records[8];                     // records on the heap (switch 1 ON)
int recIdx = 0;

int16_t nextSample() {                // linear congruential generator
  rng = (rng * 75 + 74) % 65537;
  return (int16_t)(rng & 0x0FFF) - 2048;
}

int32_t job(bool useHeap) {
  samples[head] = nextSample();
  head = (head + 1) % TAPS;
  int32_t acc = 0;
  for (int i = 0; i < TAPS; i++)      // FIR: 16 multiply-accumulates
    acc += (int32_t)coeff[i] * samples[(head + i) % TAPS];

  char *rec;
  if (useHeap) {
    gpio_put(PIN_HEAP_OP, 1);
    size_t len = 16 + rng % 400;      // varying size: fragments the heap
    free(records[recIdx]);                // free the oldest record
    rec = records[recIdx] = (char *)malloc(len);
    recIdx = (recIdx + 1) % 8;
    gpio_put(PIN_HEAP_OP, 0);
    if (rec == nullptr) return acc;       // heap exhausted: no record
  } else {
    rec = staticRecord;
  }
  snprintf(rec, 16, "y=%ld", (long)acc);   // the log record
  return acc;
}

void setup() {
  Serial1.begin(115200);
  gpio_init(PIN_JOB);     gpio_set_dir(PIN_JOB, GPIO_OUT);
  gpio_init(PIN_HEAP_OP); gpio_set_dir(PIN_HEAP_OP, GPIO_OUT);
  pinMode(PIN_SW_HEAP, INPUT_PULLUP);
  pinMode(PIN_SW_REL, INPUT_PULLUP);
  Serial1.println("ex15 C: periodic job, 2000 us");
}

void loop() {
  bool useHeap = digitalRead(PIN_SW_HEAP) == LOW;
  bool relative = digitalRead(PIN_SW_REL) == LOW;
  uint32_t pMin = UINT32_MAX, pMax = 0, jMin = UINT32_MAX, jMax = 0, overruns = 0;
  volatile int32_t sink = 0;

  uint32_t next = time_us_32() + PERIOD_US;
  while ((int32_t)(time_us_32() - next) < 0) { }  // start on a period boundary
  uint32_t first = time_us_32(), last = first;

  for (int n = 0; n <= REPORT_EVERY; n++) {
    uint32_t start = time_us_32();
    if (n > 0) {                                    // period since last start
      uint32_t p = start - last;
      if (p < pMin) pMin = p;
      if (p > pMax) pMax = p;
      if (p > PERIOD_US + PERIOD_US / 10) overruns++;
    }
    last = start;
    gpio_put(PIN_JOB, 1);
    sink = job(useHeap);
    gpio_put(PIN_JOB, 0);
    uint32_t d = time_us_32() - start;
    if (d < jMin) jMin = d;
    if (d > jMax) jMax = d;

    if (relative) {
      busy_wait_us_32(PERIOD_US);                   // work, then wait
    } else {
      next += PERIOD_US;                            // wait until the deadline
      while ((int32_t)(time_us_32() - next) < 0) { }
    }
  }
  uint32_t avg = (last - first) / REPORT_EVERY;
  char line[120];
  snprintf(line, sizeof line,
           "heap=%d rel=%d gc=- period min/avg/max %lu/%lu/%lu us job min/max %lu/%lu us overruns %lu",
           useHeap, relative, (unsigned long)pMin, (unsigned long)avg, (unsigned long)pMax,
           (unsigned long)jMin, (unsigned long)jMax, (unsigned long)overruns);
  Serial1.println(line);
  (void)sink;
}
