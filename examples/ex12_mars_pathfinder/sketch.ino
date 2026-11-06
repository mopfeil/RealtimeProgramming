// Priority inversion: the Mars Pathfinder bug (1997), rebuilt with FreeRTOS.
//
// Tasks (names as on Pathfinder), all periods in scheduler ticks (16 ms):
//   Bus   "bc_dist"  prio 3  every  8 ticks: takes the bus lock, 2 ms work
//   Watch "bc_sched" prio 3  every  8 ticks: checks that Bus finished its
//                            cycle; if not: "system reset" (here only a
//                            message, LED 13 and a counter)
//   Comm  (comms)    prio 2  every 64 ticks: 300 ms of work, no lock
//   Met   "ASI/MET"  prio 1  every 16 ticks: takes the bus lock, 20 ms work
//
// The phases are fixed, so the collision happens on purpose:
//   tick 16k   : Met takes the lock and works 20 ms (longer than one tick)
//   tick 16k+1 : Bus wakes, finds the lock taken and blocks
//   tick 64k+1 : Comm wakes at the same time as Bus
//
// DIP switch 1 selects the lock, read by Met at the start of each cycle:
//   OFF (pin 2 HIGH): binary semaphore, no priority inheritance (the
//     Pathfinder configuration). At tick 64k+1 Comm (prio 2) preempts Met
//     (prio 1), which still holds the lock: Bus waits for Comm's 300 ms
//     although Comm does not use the lock at all. Watch finds the cycle
//     unfinished: RESET. Unbounded priority inversion.
//   ON  (pin 2 LOW) : mutex with priority inheritance (the fix that JPL
//     uploaded to Mars). While Bus waits, Met runs with priority 3, Comm
//     cannot preempt it, Bus waits at most the rest of Met's 20 ms. Bounded
//     priority inversion.
//
// Serial (115200): one line per 64 ticks with the longest wait of Bus for
// the lock, and one line per reset.
//
// Logic analyzer (channel names in diagram.json; HIGH while the task runs):
//   D0 = pin 10 Bus     D1 = pin 11 Comm    D2 = pin 12 Met
//   D3 = pin 9  HIGH while Met holds the lock
//   D4 = pin 8  Watch   D5 = pin 7  Idle    D6 = pin 13 reset LED
// LED pin 13: on from a reset until the end of the next Comm period
// without one.

#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <util/delay.h>

const uint8_t PIN_INHERIT = 2;      // DIP switch 1, ON = LOW = mutex
const uint8_t PIN_BUS     = 10;     // trace pins, driven by the scheduler
const uint8_t PIN_COMM    = 11;
const uint8_t PIN_MET     = 12;
const uint8_t PIN_HOLDS   = 9;
const uint8_t PIN_WATCH   = 8;
const uint8_t PIN_IDLE    = 7;
const uint8_t PIN_RESET   = 13;

const TickType_t BUS_PERIOD  = 8;
const TickType_t MET_PERIOD  = 16;
const TickType_t COMM_PERIOD = 64;

SemaphoreHandle_t busSem;           // binary semaphore: no inheritance
SemaphoreHandle_t busMutex;         // mutex: priority inheritance
SemaphoreHandle_t volatile busLock; // the one in use, chosen by Met

volatile bool     cycleDone = true; // set by Bus, cleared by Watch
volatile uint16_t maxWaitMs = 0;    // longest wait of Bus for the lock
uint16_t resets = 0;
bool     resetSeen = false;     // reset in the current Comm period

// CPU time, not wall-clock time: does not advance while preempted
void work(uint16_t ms) {
  while (ms--) _delay_us(1000);
}

// High priority: distributes the data on the information bus
void busTask(void *pv) {
  TickType_t last = 0;
  vTaskDelayUntil(&last, 1);        // phase: ticks 1, 9, 17, ...
  for (;;) {
    uint32_t t0 = micros();
    xSemaphoreTake(busLock, portMAX_DELAY);
    uint16_t waited = (micros() - t0) / 1000;
    if (waited > maxWaitMs) maxWaitMs = waited;
    work(2);
    xSemaphoreGive(busLock);
    cycleDone = true;
    vTaskDelayUntil(&last, BUS_PERIOD);
  }
}

// High priority: the watchdog of the bus schedule
void watchTask(void *pv) {
  TickType_t last = 0;              // phase: ticks 8, 16, 24, ...
  for (;;) {
    vTaskDelayUntil(&last, BUS_PERIOD);
    if (!cycleDone) {               // Bus did not finish since last check
      resets++;
      resetSeen = true;
      digitalWrite(PIN_RESET, HIGH);
      Serial.print(F("t="));
      Serial.print(last * 1000UL / configTICK_RATE_HZ);
      Serial.print(F(" ms: bus cycle not finished -> SYSTEM RESET #"));
      Serial.println(resets);
    }
    cycleDone = false;
    if (last % COMM_PERIOD == 0) {  // report once per Comm period
      Serial.print(busLock == busMutex ? F("mutex    ") : F("semaphore"));
      Serial.print(F(": longest wait of Bus "));
      Serial.print(maxWaitMs);
      Serial.println(F(" ms"));
      maxWaitMs = 0;
      digitalWrite(PIN_RESET, resetSeen);
      resetSeen = false;
    }
  }
}

// Medium priority: long-running, does not use the bus
void commTask(void *pv) {
  TickType_t last = 0;
  vTaskDelayUntil(&last, 1);        // phase: ticks 65, 129, ...
  for (;;) {
    vTaskDelayUntil(&last, COMM_PERIOD);
    work(300);
  }
}

// Low priority: publishes the meteorological data on the bus
void metTask(void *pv) {
  TickType_t last = 0;              // phase: ticks 16, 32, 48, ...
  for (;;) {
    vTaskDelayUntil(&last, MET_PERIOD);
    busLock = digitalRead(PIN_INHERIT) == LOW ? busMutex : busSem;
    xSemaphoreTake(busLock, portMAX_DELAY);
    digitalWrite(PIN_HOLDS, HIGH);
    work(20);
    digitalWrite(PIN_HOLDS, LOW);
    xSemaphoreGive(busLock);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_INHERIT, INPUT_PULLUP);
  pinMode(PIN_BUS, OUTPUT);
  pinMode(PIN_COMM, OUTPUT);
  pinMode(PIN_MET, OUTPUT);
  pinMode(PIN_HOLDS, OUTPUT);
  pinMode(PIN_WATCH, OUTPUT);
  pinMode(PIN_IDLE, OUTPUT);
  pinMode(PIN_RESET, OUTPUT);

  busSem = xSemaphoreCreateBinary();
  xSemaphoreGive(busSem);           // a binary semaphore starts empty
  busMutex = xSemaphoreCreateMutex();
  busLock = busSem;

  TaskHandle_t h;
  xTaskCreate(busTask, "Bus", 128, NULL, 3, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_BUS);
  xTaskCreate(watchTask, "Watch", 192, NULL, 3, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_WATCH);
  xTaskCreate(commTask, "Comm", 128, NULL, 2, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_COMM);
  xTaskCreate(metTask, "Met", 128, NULL, 1, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)PIN_MET);
}

// loop() runs in the idle task: tag it on the first call, so that its
// trace pin follows the scheduler from now on like those of the others
void loop() {
  static bool tagged = false;
  if (!tagged) {
    vTaskSetApplicationTaskTag(NULL, (TaskHookFunction_t)PIN_IDLE);
    digitalWrite(PIN_IDLE, HIGH);   // already switched in: set by hand once
    tagged = true;
  }
}
