#include <Arduino_FreeRTOS.h>

// Debug pins for the logic analyzer (D0 = pin 10, D1 = pin 11)
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
    digitalWrite(PIN_TASK_A, HIGH);
    burn(20);
    digitalWrite(PIN_TASK_A, LOW);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
  }
}

// Low priority: period 60 ms, execution ~30 ms (gets preempted by A)
void taskB(void *pv) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    digitalWrite(PIN_TASK_B, HIGH);
    burn(30);
    digitalWrite(PIN_TASK_B, LOW);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(60));
  }
}

void setup() {
  pinMode(PIN_TASK_A, OUTPUT);
  pinMode(PIN_TASK_B, OUTPUT);
  xTaskCreate(taskA, "A", 128, NULL, 2, NULL);
  xTaskCreate(taskB, "B", 128, NULL, 1, NULL);
}

void loop() { }
