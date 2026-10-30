// Queues between an ISR and tasks: priorities, blocking, timeouts, losses.
//
//   Timer 1 ISR, every 20 ms: reads the potentiometer (A0) and sends the
//     sample with xQueueSendFromISR() to sampleQ (length 4). An ISR must
//     not block: if the queue is full, the sample is lost (counted).
//   Filter task (prio 2): receives from sampleQ with a timeout of 100 ms,
//     averages 4 samples and sends the mean to logQ (length 2). If logQ is
//     full it blocks (at most 1 s) until Log has made room.
//   Log task (prio 1): receives from logQ and prints one line, about 47 ms
//     at 9600 baud, one mean every 80 ms: Log keeps up.
//
// Normal operation: the Filter has the higher priority and takes each
// sample as soon as the ISR sends it: sampleQ never holds more than one
// sample ("fill=1"), the Filter never waits for logQ ("wait=0").
//
// DIP switch 1 ON (pin 2 LOW): the Filter needs 30 ms of CPU per sample,
//   longer than the 20 ms sample period. sampleQ fills up, the ISR loses
//   samples ("lost" grows, LED 13). logQ fills up too: the lower priority
//   Log task gets the CPU only while the Filter waits for room in logQ.
// DIP switch 2 ON (pin 3 LOW): the sensor is silent (the ISR sends
//   nothing). The Filter's receive times out every 100 ms ("timeouts").
//
// Exercise: set LOG_BAUD to 4800. A line now takes 94 ms, longer than the
// 80 ms between two means: on average Log is too slow. logQ stays full, the
// Filter blocks on it ("wait"), sampleQ fills up ("fill") and finally the
// ISR loses samples. A queue absorbs bursts, not a permanent overload.
//
// Logic analyzer: D0 = pin 10 (ISR, set by hand), D1 = pin 11 (Filter),
//                 D2 = pin 12 (Log).

#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <util/delay.h>

const uint8_t PIN_SLOW    = 2;      // DIP switch 1, ON = LOW = slow Filter
const uint8_t PIN_SILENT  = 3;      // DIP switch 2, ON = LOW = no samples
const uint8_t PIN_DBG_ISR = 10;
const uint8_t PIN_FILTER  = 11;     // trace pins, driven by the scheduler
const uint8_t PIN_LOG     = 12;
const uint8_t PIN_LOST    = 13;     // LED: the last sample was lost
const uint16_t LOG_BAUD   = 9600;

struct LogItem {                    // copied into the queue as a whole
  uint16_t mean;
  uint8_t  maxFill;                 // highest fill level of sampleQ seen
  uint8_t  waitMs;                  // previous wait of the Filter on logQ
};

QueueHandle_t sampleQ, logQ;
volatile uint16_t lost = 0;
uint16_t timeouts = 0;

// CPU time, not wall-clock time: does not advance while preempted
void work(uint8_t ms) {
  while (ms--) _delay_us(1000);
}

ISR(TIMER1_COMPA_vect) {
  static uint8_t ms = 0;
  if (++ms < 20) return;            // one sample every 20 ms
  ms = 0;
  if (digitalRead(PIN_SILENT) == LOW) return;
  digitalWrite(PIN_DBG_ISR, HIGH);
  uint16_t sample = analogRead(A0);
  BaseType_t woken = pdFALSE;
  bool full = xQueueSendFromISR(sampleQ, &sample, &woken) != pdTRUE;
  if (full) lost++;
  digitalWrite(PIN_LOST, full);
  digitalWrite(PIN_DBG_ISR, LOW);
  if (woken == pdTRUE) taskYIELD(); // Filter has a higher prio: run it now
}

void filterTask(void *pv) {
  uint16_t sum = 0;
  uint8_t n = 0, maxFill = 0, waitMs = 0;
  for (;;) {
    uint16_t sample;
    if (xQueueReceive(sampleQ, &sample, pdMS_TO_TICKS(100)) != pdTRUE) {
      timeouts++;                   // sensor silent for 100 ms
      continue;
    }
    uint8_t fill = uxQueueMessagesWaiting(sampleQ) + 1;  // incl. this one
    if (fill > maxFill) maxFill = fill;
    if (digitalRead(PIN_SLOW) == LOW) work(30);
    sum += sample;
    if (++n < 4) continue;

    LogItem item = { (uint16_t)(sum / 4), maxFill, waitMs };
    uint32_t t0 = micros();
    xQueueSend(logQ, &item, pdMS_TO_TICKS(1000));  // blocks if logQ full
    waitMs = (micros() - t0) / 1000;
    sum = 0; n = 0; maxFill = 0;
  }
}

void logTask(void *pv) {
  Serial.begin(LOG_BAUD);
  for (;;) {
    LogItem item;
    xQueueReceive(logQ, &item, portMAX_DELAY);
    Serial.print(F("mean="));
    Serial.print(item.mean);
    Serial.print(F(" fill="));
    Serial.print(item.maxFill);
    Serial.print(F(" wait="));
    Serial.print(item.waitMs);
    Serial.print(F(" ms lost="));
    Serial.print(lost);
    Serial.print(F(" timeouts="));
    Serial.println(timeouts);
  }
}

void setup() {
  pinMode(PIN_SLOW, INPUT_PULLUP);
  pinMode(PIN_SILENT, INPUT_PULLUP);
  pinMode(PIN_DBG_ISR, OUTPUT);
  pinMode(PIN_FILTER, OUTPUT);
  pinMode(PIN_LOG, OUTPUT);
  pinMode(PIN_LOST, OUTPUT);

  sampleQ = xQueueCreate(4, sizeof(uint16_t));
  logQ    = xQueueCreate(2, sizeof(LogItem));

  TaskHandle_t h;
  xTaskCreate(filterTask, "Filter", 160, NULL, 2, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_FILTER);
  xTaskCreate(logTask, "Log", 192, NULL, 1, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_LOG);

  // Timer 1: CTC, prescaler 64, 16 MHz / 64 / 250 = 1 kHz
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
  OCR1A  = 249;
  TIMSK1 = _BV(OCIE1A);
}

void loop() { }
