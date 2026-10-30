// Mutex: two tasks share one resource, the serial port.
//
// Status (priority 1) prints a long status line again and again.
// Alarm (priority 2) prints a short alarm line every 300 ms.
// At 9600 baud one character takes about 1 ms, the status line about 80 ms:
// the TX buffer (64 bytes) fills, and Status is still inside print() when
// Alarm preempts it.
//
//   DIP switch 1 OFF (pin 2 HIGH): no protection. The alarm line lands in
//     the middle of the status line:
//       [status] temperature=21.5 C pressure=10[ALARM 7] door open
//       13 hPa humidity=45 % battery=12.6 V
//   DIP switch 1 ON  (pin 2 LOW) : both tasks take the mutex around their
//     print(). Every line is complete. Alarm waits until Status gives the
//     mutex back; while it waits, Status inherits priority 2.
//
// Alarm waits at most 200 ms for the mutex. If it gets no access in time it
// counts a timeout (Status holds the mutex for about 80 ms, so this should
// not happen; make the timeout shorter than the line to provoke it).
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

void statusTask(void *pv) {
  for (;;) {
    bool locked = useMutex() && xSemaphoreTake(serialMutex, portMAX_DELAY);
    digitalWrite(PIN_HOLDS, locked);
    Serial.println(F("[status] temperature=21.5 C pressure=1013 hPa humidity=45 % battery=12.6 V"));
    Serial.flush();                 // wait until the line is on the wire
    digitalWrite(PIN_HOLDS, LOW);
    if (locked) xSemaphoreGive(serialMutex);   // the taker gives it back
    vTaskDelay(1);                  // pause one tick
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
