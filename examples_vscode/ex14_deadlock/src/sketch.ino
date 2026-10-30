// Deadlock: two tasks, two mutexes, and three ways to lock them.
//
// Two resources, each protected by a mutex: the sensor bus (I2C) and the
// display. Periods in scheduler ticks (16 ms):
//   Meas (prio 1) every 10 ticks, at 10k  : I2C, 20 ms work, then Display
//   Disp (prio 2) every 10 ticks, at 10k+1: Display, 2 ms, then I2C
// Meas still holds I2C (its 20 ms are longer than one tick) when Disp wakes.
//
// DIP switches (read by Disp at the start of each cycle):
//   1 OFF       : fixed lock order. Disp also takes I2C first, then Display.
//                 Disp waits for I2C, Meas finishes, no deadlock.
//   1 ON, 2 OFF : opposite order, wait forever. Disp holds Display and waits
//                 for I2C, Meas holds I2C and waits for Display: DEADLOCK.
//                 Both stop for good; only a reset of the simulation helps.
//   1 ON, 2 ON  : opposite order, but Disp waits at most 3 ticks for I2C.
//                 On a timeout it gives Display back, waits one tick and
//                 tries again ("retries"): the cycle breaks.
//
// Watch (prio 3) checks every 500 ms that both tasks made progress and
// prints the counters, or which task holds and waits for what.
//
// Logic analyzer: D0 = pin 10 (Meas), D1 = pin 11 (Disp).
// LEDs: pin 4 toggles per Meas cycle, pin 5 per Disp cycle,
//       pin 13 on while deadlocked.

#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <util/delay.h>

const uint8_t PIN_OPPOSITE = 2;     // DIP switch 1, ON = LOW
const uint8_t PIN_TIMEOUT  = 3;     // DIP switch 2, ON = LOW
const uint8_t PIN_MEAS     = 10;    // trace pins, driven by the scheduler
const uint8_t PIN_DISP     = 11;
const uint8_t PIN_LED_MEAS = 4;
const uint8_t PIN_LED_DISP = 5;
const uint8_t PIN_DEADLOCK = 13;

SemaphoreHandle_t i2cMutex, dispMutex;

// Bookkeeping for the watchdog: what each task holds and waits for
const char * volatile measHolds = "-";
const char * volatile measWaits = "-";
const char * volatile dispHolds = "-";
const char * volatile dispWaits = "-";
volatile uint16_t measCycles = 0, dispCycles = 0, retries = 0;

// CPU time, not wall-clock time: does not advance while preempted
void work(uint8_t ms) {
  while (ms--) _delay_us(1000);
}

void measTask(void *pv) {
  TickType_t last = 0;
  for (;;) {
    vTaskDelayUntil(&last, 10);     // ticks 10, 20, 30, ...
    measWaits = "I2C";
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    measWaits = "-"; measHolds = "I2C";
    work(20);                       // read the sensor
    measWaits = "Display";
    xSemaphoreTake(dispMutex, portMAX_DELAY);
    measWaits = "-"; measHolds = "I2C+Display";
    work(2);                        // show the value
    xSemaphoreGive(dispMutex);
    xSemaphoreGive(i2cMutex);
    measHolds = "-";
    measCycles++;
    digitalWrite(PIN_LED_MEAS, !digitalRead(PIN_LED_MEAS));
  }
}

void dispTask(void *pv) {
  TickType_t last = 0;
  vTaskDelayUntil(&last, 1);        // phase: ticks 11, 21, 31, ...
  for (;;) {
    vTaskDelayUntil(&last, 10);
    if (digitalRead(PIN_OPPOSITE) == HIGH) {
      // Fixed global order: I2C before Display, like Meas
      dispWaits = "I2C";
      xSemaphoreTake(i2cMutex, portMAX_DELAY);
      dispWaits = "Display"; dispHolds = "I2C";
      xSemaphoreTake(dispMutex, portMAX_DELAY);
      dispWaits = "-"; dispHolds = "I2C+Display";
      work(2);                      // draw the screen with a status icon
    } else {
      // Opposite order: Display first, then I2C
      bool useTimeout = digitalRead(PIN_TIMEOUT) == LOW;
      for (;;) {
        dispWaits = "Display";
        xSemaphoreTake(dispMutex, portMAX_DELAY);
        dispWaits = "-"; dispHolds = "Display";
        work(2);                    // draw the screen
        dispWaits = "I2C";
        if (xSemaphoreTake(i2cMutex, useTimeout ? 3 : portMAX_DELAY)) break;
        // Timeout: give back what we hold, back off, try again
        xSemaphoreGive(dispMutex);
        dispHolds = "-";
        retries++;
        vTaskDelay(1);
      }
      dispWaits = "-"; dispHolds = "Display+I2C";
      work(1);                      // read the sensor for the status icon
    }
    xSemaphoreGive(i2cMutex);
    xSemaphoreGive(dispMutex);
    dispHolds = "-";
    dispCycles++;
    digitalWrite(PIN_LED_DISP, !digitalRead(PIN_LED_DISP));
  }
}

void watchTask(void *pv) {
  uint16_t lastMeas = 0, lastDisp = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    bool stuck = measCycles == lastMeas && dispCycles == lastDisp;
    digitalWrite(PIN_DEADLOCK, stuck);
    if (stuck) {
      Serial.print(F("DEADLOCK? Meas holds "));
      Serial.print(measHolds);
      Serial.print(F(", waits for "));
      Serial.print(measWaits);
      Serial.print(F(" | Disp holds "));
      Serial.print(dispHolds);
      Serial.print(F(", waits for "));
      Serial.println(dispWaits);
    } else {
      Serial.print(F("cycles Meas "));
      Serial.print(measCycles);
      Serial.print(F(", Disp "));
      Serial.print(dispCycles);
      Serial.print(F(", retries "));
      Serial.println(retries);
    }
    lastMeas = measCycles;
    lastDisp = dispCycles;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_OPPOSITE, INPUT_PULLUP);
  pinMode(PIN_TIMEOUT, INPUT_PULLUP);
  pinMode(PIN_MEAS, OUTPUT);
  pinMode(PIN_DISP, OUTPUT);
  pinMode(PIN_LED_MEAS, OUTPUT);
  pinMode(PIN_LED_DISP, OUTPUT);
  pinMode(PIN_DEADLOCK, OUTPUT);

  i2cMutex  = xSemaphoreCreateMutex();
  dispMutex = xSemaphoreCreateMutex();

  TaskHandle_t h;
  xTaskCreate(measTask, "Meas", 128, NULL, 1, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_MEAS);
  xTaskCreate(dispTask, "Disp", 128, NULL, 2, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_DISP);
  xTaskCreate(watchTask, "Watch", 192, NULL, 3, NULL);
}

void loop() { }
