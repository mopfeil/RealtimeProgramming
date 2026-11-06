// Mutex: two tasks share one resource, the serial port.
//
// Status (priority 1) prints a long status line again and again.
// Alarm (priority 2) prints a short alarm line every 300 ms.
// At 9600 baud one character takes about 1 ms, the status line about 80 ms:
// the TX buffer (64 bytes) fills, and Status is still in the middle of its
// line when Alarm preempts it.
//
//   DIP switch 1 OFF (pin 2 HIGH): no protection. The alarm line lands in
//     the middle of the status line:
//       [status] temperature=21.5 C pressure=10[ALARM 7] door open
//       13 hPa humidity=45 % battery=12.6 V
//   DIP switch 1 ON  (pin 2 LOW) : both tasks take the mutex around their
//     output. Every line is complete. Alarm waits until Status gives the
//     mutex back; while it waits, Status inherits priority 2.
//
// Alarm waits at most 200 ms for the mutex. If it gets no access in time it
// counts a timeout (Status holds the mutex for about 80 ms, so this should
// not happen; make the timeout shorter than the line to provoke it).
// The switch is read on every pass, so it can be flipped while running.
//
// Logic analyzer: D0 = pin 10 (Status), D1 = pin 11 (Alarm),
//                 D2 = pin 12 (HIGH while Status holds the mutex).

#include <Arduino_FreeRTOS.h>
#include <semphr.h>

const uint8_t PIN_USE_MUTEX = 2;    // DIP switch 1, ON = LOW = mutex
const uint8_t PIN_STATUS    = 10;   // trace pins, driven by the scheduler
const uint8_t PIN_ALARM     = 11;
const uint8_t PIN_HOLDS     = 12;

SemaphoreHandle_t serialMutex;
uint16_t alarmCount = 0, timeouts = 0;

bool useMutex() { return digitalRead(PIN_USE_MUTEX) == LOW; }

// The Arduino serial driver is not reentrant: Serial.write() computes the
// next buffer index before it waits for space. A task preempted in this
// wait writes its character with a stale index later and throws away what
// the other task put into the buffer meanwhile. So Status sends its line one
// character at a time: it waits for space outside the driver and writes
// each character with interrupts disabled. Every character is protected,
// the line as a whole is not; that is the job of the mutex.
void statusPrint(const __FlashStringHelper *text) {
  const char *p = (const char *)text;
  char c;
  while ((c = pgm_read_byte(p++)) != 0) {
    while (Serial.availableForWrite() == 0) { }   // wait for buffer space
    taskENTER_CRITICAL();
    Serial.write(c);                // there is space: returns at once
    taskEXIT_CRITICAL();
  }
}

void statusTask(void *pv) {
  for (;;) {
    bool locked = useMutex() && xSemaphoreTake(serialMutex, portMAX_DELAY);
    digitalWrite(PIN_HOLDS, locked);
    statusPrint(F("[status] temperature=21.5 C pressure=1013 hPa humidity=45 % battery=12.6 V\r\n"));
    digitalWrite(PIN_HOLDS, LOW);
    if (locked) xSemaphoreGive(serialMutex);   // the taker gives it back
    // No delay and no Serial.flush(): the TX buffer stays full, so Status
    // is almost always waiting for buffer space and the alarm (which wakes on
    // a tick) catches it at a different point of the line every time.
    // With a pause aligned to the tick, the alarm would always arrive while
    // the line was already complete in the buffer and nothing would mix.
  }
}

void alarmTask(void *pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(300));
    alarmCount++;
    bool locked = false;
    if (useMutex()) {
      locked = xSemaphoreTake(serialMutex, pdMS_TO_TICKS(200));
      if (!locked) { timeouts++; continue; }   // give up, try next period
    }
    Serial.print(F("[ALARM "));
    Serial.print(alarmCount);
    Serial.print(F("] door open, timeouts "));
    Serial.println(timeouts);
    if (locked) xSemaphoreGive(serialMutex);
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_USE_MUTEX, INPUT_PULLUP);
  pinMode(PIN_STATUS, OUTPUT);
  pinMode(PIN_ALARM, OUTPUT);
  pinMode(PIN_HOLDS, OUTPUT);

  serialMutex = xSemaphoreCreateMutex();     // created "available"

  TaskHandle_t h;
  xTaskCreate(statusTask, "Status", 160, NULL, 1, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_STATUS);
  xTaskCreate(alarmTask, "Alarm", 160, NULL, 2, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_ALARM);
}

void loop() { }
