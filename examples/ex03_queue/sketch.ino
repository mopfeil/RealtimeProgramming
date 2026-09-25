#include <Arduino_FreeRTOS.h>
#include <queue.h>

const uint8_t PIN_PRODUCER = 10;
const uint8_t PIN_CONSUMER = 11;

QueueHandle_t q;

void producer(void *pv) {
  int value = 0;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    digitalWrite(PIN_PRODUCER, HIGH);
    xQueueSend(q, &value, pdMS_TO_TICKS(10));   // copies value into the queue
    value++;
    digitalWrite(PIN_PRODUCER, LOW);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
  }
}

void consumer(void *pv) {
  int received;
  for (;;) {
    if (xQueueReceive(q, &received, portMAX_DELAY) == pdPASS) {
      digitalWrite(PIN_CONSUMER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(15));            // simulated processing
      digitalWrite(PIN_CONSUMER, LOW);
    }
  }
}

void setup() {
  pinMode(PIN_PRODUCER, OUTPUT);
  pinMode(PIN_CONSUMER, OUTPUT);
  q = xQueueCreate(4, sizeof(int));
  xTaskCreate(producer, "Prod", 128, NULL, 1, NULL);
  xTaskCreate(consumer, "Cons", 128, NULL, 2, NULL);
}

void loop() { }
