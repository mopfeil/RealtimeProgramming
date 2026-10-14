#include <Arduino_FreeRTOS.h>
#include <semphr.h>

const uint8_t PIN_BUTTON   = 2;   // INT0, falling edge
const uint8_t PIN_LED      = 12;
const uint8_t PIN_DBG_ISR  = 10;  // high while the ISR runs (set by hand)
const uint8_t PIN_DBG_TASK = 11;  // task trace pin, driven by the patched scheduler

SemaphoreHandle_t buttonSem;

void buttonISR() {
  digitalWrite(PIN_DBG_ISR, HIGH);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(buttonSem, &woken);
  digitalWrite(PIN_DBG_ISR, LOW);
  if (woken == pdTRUE) taskYIELD();
}

void buttonTask(void *pv) {
  for (;;) {
    xSemaphoreTake(buttonSem, portMAX_DELAY);   // blocked until the ISR gives
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    vTaskDelay(pdMS_TO_TICKS(5));               // simulated work
  }
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_DBG_ISR, OUTPUT);
  pinMode(PIN_DBG_TASK, OUTPUT);

  buttonSem = xSemaphoreCreateBinary();
  TaskHandle_t hButton;
  xTaskCreate(buttonTask, "Button", 128, NULL, 2, &hButton);
  vTaskSetApplicationTaskTag(hButton, (TaskHookFunction_t)PIN_DBG_TASK);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonISR, FALLING);
}

void loop() { }
