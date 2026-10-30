// Binary vs. counting semaphore for events from an ISR.
//
// Timer 1 interrupts every 1 ms. Every 500 ms the ISR "receives a burst"
// of BURST events, 1 ms apart, and gives the event semaphore once per event.
// The Handler task (priority 2) takes the semaphore and needs 20 ms of CPU
// time per event: the burst arrives much faster than it is processed.
//
//   DIP switch 1 OFF (pin 2 HIGH): binary semaphore. The first give wakes
//     the Handler, the second sets the semaphore to 1, every further give
//     fails (the semaphore is already "full"): events are LOST.
//   DIP switch 1 ON  (pin 2 LOW) : counting semaphore (max. 10). The count
//     stores the pending events; the Handler processes all of them.
//
// The Report task (priority 1) prints one line per burst:
//   binary  : given 5, failed 3, handled 2
//   counting: given 5, failed 0, handled 5
//
// Logic analyzer: D0 = pin 10 (ISR gives, set by hand),
//                 D1 = pin 11 (Handler), D2 = pin 12 (Report).
// LED pin 13: on while the last burst lost events.

#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <util/delay.h>

const uint8_t PIN_COUNTING = 2;     // DIP switch 1, ON = LOW = counting
const uint8_t PIN_DBG_ISR  = 10;
const uint8_t PIN_HANDLER  = 11;    // trace pins, driven by the scheduler
const uint8_t PIN_REPORT   = 12;
const uint8_t PIN_LOST     = 13;

const uint8_t  BURST       = 5;     // events per burst
const uint16_t BURST_MS    = 500;   // burst period
const uint8_t  WORK_MS     = 20;    // CPU time per event

SemaphoreHandle_t binSem, cntSem;
SemaphoreHandle_t reportSem;        // ISR -> Report: "burst complete"
volatile SemaphoreHandle_t eventSem; // the one the ISR uses for this burst

volatile uint8_t given, failed, handled;
uint8_t lastGiven, lastFailed, lastHandled;   // copied by the ISR
bool lastCounting;

// CPU time, not wall-clock time: does not advance while preempted
void work(uint8_t ms) {
  while (ms--) _delay_us(1000);
}

ISR(TIMER1_COMPA_vect) {
  static uint16_t ms = 0;
  BaseType_t woken = pdFALSE;
  if (ms == 0) {                    // new burst: report the last one
    lastGiven = given; lastFailed = failed; lastHandled = handled;
    lastCounting = (eventSem == cntSem);
    given = failed = handled = 0;
    // binary or counting for this burst; drop events still pending
    eventSem = digitalRead(PIN_COUNTING) == LOW ? cntSem : binSem;
    xSemaphoreGiveFromISR(reportSem, &woken);
  }
  if (ms < BURST) {                 // one event per ms
    digitalWrite(PIN_DBG_ISR, HIGH);
    if (xSemaphoreGiveFromISR(eventSem, &woken) == pdTRUE) given++;
    else failed++;                  // semaphore full: the event is lost
    digitalWrite(PIN_DBG_ISR, LOW);
  }
  if (++ms == BURST_MS) ms = 0;
  if (woken == pdTRUE) taskYIELD(); // Handler has a higher prio than Report
}

void handlerTask(void *pv) {
  for (;;) {
    SemaphoreHandle_t s = eventSem;
    // Timeout 1 tick: the ISR may switch to the other semaphore
    if (xSemaphoreTake(s, 1) == pdTRUE) {
      work(WORK_MS);                // process one event
      handled++;
    }
  }
}

void reportTask(void *pv) {
  Serial.begin(115200);
  for (;;) {
    xSemaphoreTake(reportSem, portMAX_DELAY);
    if (lastGiven + lastFailed == 0) continue;   // first burst not yet over
    digitalWrite(PIN_LOST, lastHandled < BURST);
    Serial.print(lastCounting ? F("counting: given ") : F("binary  : given "));
    Serial.print(lastGiven);
    Serial.print(F(", failed "));
    Serial.print(lastFailed);
    Serial.print(F(", handled "));
    Serial.println(lastHandled);
  }
}

void setup() {
  pinMode(PIN_COUNTING, INPUT_PULLUP);
  pinMode(PIN_DBG_ISR, OUTPUT);
  pinMode(PIN_HANDLER, OUTPUT);
  pinMode(PIN_REPORT, OUTPUT);
  pinMode(PIN_LOST, OUTPUT);

  binSem    = xSemaphoreCreateBinary();          // created empty
  cntSem    = xSemaphoreCreateCounting(10, 0);   // max 10, initially 0
  reportSem = xSemaphoreCreateBinary();
  eventSem  = binSem;

  TaskHandle_t h;
  xTaskCreate(handlerTask, "Handler", 160, NULL, 2, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_HANDLER);
  xTaskCreate(reportTask, "Report", 192, NULL, 1, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_REPORT);

  // Timer 1: CTC, prescaler 64, 16 MHz / 64 / 250 = 1 kHz
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
  OCR1A  = 249;
  TIMSK1 = _BV(OCIE1A);
}

void loop() { }
