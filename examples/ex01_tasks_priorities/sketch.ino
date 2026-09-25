#include <Arduino_FreeRTOS.h>

// Trace pins for the logic analyzer (D0 = pin 10, D1 = pin 11).
// The patched library sets the pin stored in the task's application tag
// HIGH when the scheduler switches the task in (Running) and LOW when it
// switches it out (Ready/Blocked) -- see traceTASK_SWITCHED_IN/OUT.
const uint8_t PIN_TASK_A = 10;
const uint8_t PIN_TASK_B = 11;

// Busy-wait for ms milliseconds (simulates computation time)
static void burn(uint16_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) { }
}

// High priority: period 100 ms, execution ~20 ms
void taskA(void *pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    burn(20);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
  }
}

// Low priority: period 60 ms, execution ~30 ms (gets preempted by A)
void taskB(void *pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    burn(30);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(60));
  }
}

void setup() {
  TaskHandle_t hA, hB;
  pinMode(PIN_TASK_A, OUTPUT);
  pinMode(PIN_TASK_B, OUTPUT);
  xTaskCreate(taskA, "A", 128, NULL, 2, &hA);
  xTaskCreate(taskB, "B", 128, NULL, 1, &hB);
  vTaskSetApplicationTaskTag(hA, (TaskHookFunction_t)PIN_TASK_A);
  vTaskSetApplicationTaskTag(hB, (TaskHookFunction_t)PIN_TASK_B);
}

void loop() { }
