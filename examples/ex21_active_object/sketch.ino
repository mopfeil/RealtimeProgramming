// Realtime patterns: ISR -> lock-free ring buffer -> active object.
//
// A pedestrian crossing. Button 1 (pin 2) requests the walk phase,
// button 2 (pin 3) is an emergency stop: all lights red within one tick.
// Both ISRs only time-stamp the event and put it into a single-producer/
// single-consumer ring buffer (no lock needed), then notify the controller.
//
//   DIP switch 1 OFF (pin 4 HIGH): active object. The controller task waits
//     for an event OR the next timeout, handles it completely and returns
//     to waiting (run to completion). States and timeouts are data, the task
//     never blocks inside a state. The ISR wakes it directly (higher
//     priority than the interrupted idle task): emergency response < 1 ms.
//   DIP switch 1 ON  (pin 4 LOW): anti-pattern, sequential code with
//     vTaskDelay() in the middle of the sequence ("yellow, wait 1 s, red,
//     wait 4 s"). An emergency during the sequence waits until it is over:
//     response up to 5 s. The ring buffer still keeps the event: no event is
//     lost, it is only late.
//
// Serial output (115200), e.g.
//   AO      : walk request -> yellow
//   AO      : EMERGENCY, all red after 0 ms
//   blocking: EMERGENCY, all red after 3354 ms
//
// LEDs: car red/yellow/green = pins 5/6/7, pedestrian green = pin 8.
// Logic analyzer: D0 = pin 10 (controller task, driven by the scheduler),
// D1 = pin 3 (emergency button, LOW = pressed), D2 = pin 5 (car red).

#include <Arduino_FreeRTOS.h>
#include <task.h>

const uint8_t PIN_WALK_BTN = 2, PIN_EMERGENCY_BTN = 3, PIN_BLOCKING = 4;
const uint8_t PIN_RED = 5, PIN_YELLOW = 6, PIN_GREEN = 7, PIN_WALK = 8;
const uint8_t PIN_CONTROLLER = 10;          // trace pin of the controller task

// ---------------------------------------------------------------- events
enum EventType : uint8_t { EV_WALK, EV_EMERGENCY, EV_TIMEOUT };
struct Event { EventType type; uint32_t t; };   // t = millis() in the ISR
enum State : uint8_t { CAR_GREEN, CAR_YELLOW, PED_WALK, EMERGENCY };

// Single-producer/single-consumer ring buffer. The ISRs are the producer
// (on the AVR they cannot interrupt each other), the controller task the
// consumer. Only the producer writes head, only the consumer writes tail;
// both are single bytes, so every read and write is atomic: no lock.
const uint8_t RING = 8;                     // power of two
Event ring[RING];
volatile uint8_t head = 0, tail = 0;
volatile uint8_t dropped = 0;

bool ringPush(Event e) {                    // ISR only
  uint8_t h = head;
  if ((uint8_t)(h - tail) == RING) { dropped++; return false; }  // full
  ring[h % RING] = e;                       // 1. write the element
  head = h + 1;                             // 2. then publish it
  return true;
}

bool ringPop(Event &e) {                    // controller task only
  uint8_t t = tail;
  if (t == head) return false;              // empty
  e = ring[t % RING];                       // 1. read the element
  tail = t + 1;                             // 2. then free the slot
  return true;
}

TaskHandle_t controller;

void post(EventType type) {                 // common part of both ISRs
  static uint32_t lastEdge[2];
  uint32_t now = millis();
  if (now - lastEdge[type] < 50) return;    // debounce: ignore bounces
  lastEdge[type] = now;
  ringPush({type, now});
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(controller, &woken);
  if (woken) taskYIELD();
}
void walkIsr()      { post(EV_WALK); }
void emergencyIsr() { post(EV_EMERGENCY); }

// ---------------------------------------------------------------- outputs
void lights(bool r, bool y, bool g, bool walk) {
  digitalWrite(PIN_RED, r);  digitalWrite(PIN_YELLOW, y);
  digitalWrite(PIN_GREEN, g); digitalWrite(PIN_WALK, walk);
}

void report(const __FlashStringHelper *who, const __FlashStringHelper *what, int32_t ms = -1) {
  Serial.print(who); Serial.print(what);
  if (ms >= 0) { Serial.print(F(" after ")); Serial.print(ms); Serial.print(F(" ms")); }
  Serial.println();
}

// ------------------------------------------------- pattern: active object
State state = CAR_GREEN;
uint32_t deadline = 0;                      // 0 = no timeout pending

void enter(State s, uint32_t timeoutMs) {   // state change + timeout
  state = s;
  deadline = timeoutMs ? millis() + timeoutMs : 0;
  switch (s) {
    case CAR_GREEN:  lights(0, 0, 1, 0); break;
    case CAR_YELLOW: lights(0, 1, 0, 0); break;
    case PED_WALK:   lights(1, 0, 0, 1); break;
    case EMERGENCY:  lights(1, 0, 0, 0); break;
  }
}

void dispatch(const Event &e) {             // run to completion, never blocks
  if (e.type == EV_EMERGENCY) {
    enter(EMERGENCY, 3000);
    report(F("AO      : "), F("EMERGENCY, all red"), millis() - e.t);
    return;
  }
  switch (state) {
    case CAR_GREEN:
      if (e.type == EV_WALK) { enter(CAR_YELLOW, 1000); report(F("AO      : "), F("walk request -> yellow")); }
      break;
    case CAR_YELLOW:
      if (e.type == EV_TIMEOUT) enter(PED_WALK, 4000);
      break;                                // a second request is ignored
    case PED_WALK:
    case EMERGENCY:
      if (e.type == EV_TIMEOUT) { enter(CAR_GREEN, 0); report(F("AO      : "), F("cars green")); }
      break;
  }
}

void activeObjectStep() {
  TickType_t wait = portMAX_DELAY;
  if (deadline) {
    int32_t left = (int32_t)(deadline - millis());
    wait = left > 0 ? pdMS_TO_TICKS(left) + 1 : 0;
  }
  ulTaskNotifyTake(pdTRUE, wait);           // event or timeout
  Event e;
  while (ringPop(e)) dispatch(e);
  if (deadline && (int32_t)(millis() - deadline) >= 0) {
    deadline = 0;
    dispatch({EV_TIMEOUT, millis()});
  }
}

// ------------------------------------------- anti-pattern: blocking code
void blockingStep() {
  Event e;
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  while (ringPop(e)) {
    if (e.type == EV_EMERGENCY) {
      lights(1, 0, 0, 0);
      report(F("blocking: "), F("EMERGENCY, all red"), millis() - e.t);
      vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
      report(F("blocking: "), F("walk request -> yellow"));
      lights(0, 1, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(1000));      // nobody looks at the events now
      lights(1, 0, 0, 1);
      vTaskDelay(pdMS_TO_TICKS(4000));
    }
    lights(0, 0, 1, 0);
    report(F("blocking: "), F("cars green"));
  }
}

void controllerTask(void *pv) {
  enter(CAR_GREEN, 0);
  for (;;) {
    if (digitalRead(PIN_BLOCKING) == LOW) blockingStep();
    else activeObjectStep();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_WALK_BTN, INPUT_PULLUP);
  pinMode(PIN_EMERGENCY_BTN, INPUT_PULLUP);
  pinMode(PIN_BLOCKING, INPUT_PULLUP);
  const uint8_t outputs[] = {PIN_RED, PIN_YELLOW, PIN_GREEN, PIN_WALK, PIN_CONTROLLER};
  for (uint8_t p : outputs) pinMode(p, OUTPUT);

  xTaskCreate(controllerTask, "Ctrl", 192, NULL, 2, &controller);
  vTaskSetApplicationTaskTag(controller, (TaskHookFunction_t)PIN_CONTROLLER);
  attachInterrupt(digitalPinToInterrupt(PIN_WALK_BTN), walkIsr, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_EMERGENCY_BTN), emergencyIsr, FALLING);
  Serial.println(F("ex21: active object vs. blocking sequence"));
}

void loop() {}
