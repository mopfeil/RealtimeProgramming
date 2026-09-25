#include <Arduino_FreeRTOS.h>
#include <queue.h>

// Trace pins: driven by the patched scheduler via the task application tag
const uint8_t PIN_PRODUCER = 10;
const uint8_t PIN_CONSUMER = 11;

QueueHandle_t q;

void producer(void *pv) {
  int value = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    xQueueSend(q, &value, pdMS_TO_TICKS(10));   // copies value into the queue
    value++;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
  }
}

void consumer(void *pv) {
  int received;
  for (;;) {
    if (xQueueReceive(q, &received, portMAX_DELAY) == pdPASS) {
      vTaskDelay(pdMS_TO_TICKS(15));            // simulated processing
    }
  }
}

void setup() {
  pinMode(PIN_PRODUCER, OUTPUT);
  pinMode(PIN_CONSUMER, OUTPUT);
  q = xQueueCreate(4, sizeof(int));
  TaskHandle_t hP, hC;
  xTaskCreate(producer, "Prod", 128, NULL, 1, &hP);
  xTaskCreate(consumer, "Cons", 128, NULL, 2, &hC);
  vTaskSetApplicationTaskTag(hP, (TaskHookFunction_t)PIN_PRODUCER);
  vTaskSetApplicationTaskTag(hC, (TaskHookFunction_t)PIN_CONSUMER);
}

void loop() { }
