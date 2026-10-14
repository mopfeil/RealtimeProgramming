#include <Arduino_FreeRTOS.h>
#include <task.h>

#if configGENERATE_RUN_TIME_STATS != 1
#error "Run-time stats are disabled: use the patched library from lib/FreeRTOS"
#endif

// Trace pins for the logic analyzer (D0 = pin 10, D1 = pin 11), driven by the
// patched scheduler via the task tag -- see traceTASK_SWITCHED_IN/OUT.
const uint8_t PIN_TASK_A = 10;
const uint8_t PIN_TASK_B = 11;

// One periodic task: period in ticks (1 tick = watchdog period, ~16 ms),
// execution time in ms of CPU time, and a counter for missed deadlines.
struct PeriodicTask {
  const char *name;
  uint8_t pin;
  UBaseType_t priority;
  TickType_t periodTicks;
  uint16_t execMs;
  volatile uint16_t misses;
};

// Rate monotonic: the shorter period gets the higher priority.
PeriodicTask taskA = { "A", PIN_TASK_A, 2,  5, 16, 0 };   // U_A = 16 / ( 5 * 16) = 20 %
PeriodicTask taskB = { "B", PIN_TASK_B, 1, 10, 40, 0 };   // U_B = 40 / (10 * 16) = 25 %

// Consume ms milliseconds of CPU time. delayMicroseconds() counts CPU cycles,
// so time during which the task is preempted does not count (unlike millis()).
static void cpuBurn(uint16_t ms) {
  while (ms--) delayMicroseconds(1000);
}

void periodicTask(void *pv) {
  PeriodicTask *t = (PeriodicTask *)pv;
  TickType_t release = xTaskGetTickCount();
  for (;;) {
    cpuBurn(t->execMs);
    // Implicit deadline = release + period
    if ((TickType_t)(xTaskGetTickCount() - release) >= t->periodTicks) t->misses++;
    vTaskDelayUntil(&release, t->periodTicks);
  }
}

// ---------------------------------------------------------------------------
// Monitor: reads the run-time counters of all tasks every 2 s
// ---------------------------------------------------------------------------
const UBaseType_t MAX_TASKS = 6;
static TaskStatus_t status[MAX_TASKS];
static uint32_t lastCounter[MAX_TASKS + 1];   // indexed by xTaskNumber (1..)
static uint32_t lastTotal;
static char buf[144];   // also holds the vTaskGetRunTimeStats() table (5 lines)

void taskMonitor(void *pv) {
  TickType_t release = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&release, pdMS_TO_TICKS(2000));

    // 1) Raw data: one TaskStatus_t per task plus the current counter value
    uint32_t total;
    UBaseType_t n = uxTaskGetSystemState(status, MAX_TASKS, &total);
    uint32_t window = total - lastTotal;
    lastTotal = total;

    Serial.print(F("\n--- window "));
    Serial.print(window);
    Serial.println(F(" us ---\nTask     Prio   run[us]  CPU[%]  stack"));
    for (UBaseType_t i = 0; i < n; i++) {
      UBaseType_t id = status[i].xTaskNumber;
      if (id > MAX_TASKS) continue;
      uint32_t run = status[i].ulRunTimeCounter - lastCounter[id];
      lastCounter[id] = status[i].ulRunTimeCounter;
      uint32_t permille = run * 1000UL / window;   // run <= window < 4.29 s: no overflow
      snprintf(buf, sizeof(buf), "%-8s %4u %9lu %5lu.%lu %6u",
               status[i].pcTaskName, (unsigned)status[i].uxCurrentPriority,
               run, permille / 10, permille % 10,
               (unsigned)status[i].usStackHighWaterMark);
      Serial.println(buf);
    }
    Serial.print(F("Deadline misses: A="));
    Serial.print(taskA.misses);
    Serial.print(F(" B="));
    Serial.println(taskB.misses);

    // 2) Built-in formatting: cumulative counters since boot
    vTaskGetRunTimeStats(buf);
    Serial.print(F("vTaskGetRunTimeStats():\n"));
    Serial.print(buf);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TASK_A, OUTPUT);
  pinMode(PIN_TASK_B, OUTPUT);

  TaskHandle_t h;
  xTaskCreate(periodicTask, taskA.name, 128, &taskA, taskA.priority, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)taskA.pin);
  xTaskCreate(periodicTask, taskB.name, 128, &taskB, taskB.priority, &h);
  vTaskSetApplicationTaskTag(h, (TaskHookFunction_t)taskB.pin);
  xTaskCreate(taskMonitor, "Mon", 220, NULL, 3, NULL);
}

void loop() { }   // runs in the idle task
