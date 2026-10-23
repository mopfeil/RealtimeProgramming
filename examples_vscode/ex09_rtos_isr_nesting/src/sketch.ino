// Nested timer interrupts under FreeRTOS: the RTOS counterpart of
// ex08_avr_isr_nesting (and of the 8051 example ex06).
//
// Timer 1 interrupts every T = 1 ms, work() as in ex08:
//   DIP switch 1 OFF (pin 2 HIGH):  300 us of work per tick
//   DIP switch 1 ON  (pin 2 LOW) : 2500 us of work per tick
// DIP switch 2 selects where the work runs:
//   OFF (pin 3 HIGH): in the ISR, with interrupts re-enabled (nesting, as ex08)
//   ON  (pin 3 LOW) : in the Worker task; the ISR only sends a notification
//                     ("deferred interrupt handling")
//
// On the AVR an ISR has no stack of its own: it pushes its frame onto the
// stack of whatever task it interrupted. A task stack is a block on the
// FreeRTOS heap, of fixed size, with other heap blocks right below it.
//
//   nested, long work: the levels pile up on the stack of the interrupted
//     task (Idle, 192 bytes). The overflow runs into the heap block below:
//     app, then the TCB and stack of Ctrl. FreeRTOS checks the stack only
//     when it switches the task out (configCHECK_FOR_STACK_OVERFLOW 1),
//     i.e. at the next tick (16 ms) at the earliest. Times from the tick
//     at which Ctrl sees switch 1 ON:
//     - 2 ms : depth 2, both levels on the Idle stack, 27 bytes per level
//     - 8 ms : level 8 (or 7) overwrites app->setpoint: the LEDs go dark.
//              FreeRTOS does not notice.
//     - 16 ms: next tick, switch from Idle to Ctrl. Idle's saved SP lies
//              far below the start of its stack: stack overflow hook.
//     Without the large app block the overflow would destroy the TCB of
//     Ctrl before that tick, and the check could come much later or never.
//   deferred, long work: the stacks stay flat. The Worker cannot keep up:
//     ulTaskNotifyTake() returns more than one pending tick (overruns), and
//     the lower priority Ctrl task starves. A timing error, but no memory
//     corruption, and the program can see it.
//
// Actuator: Ctrl outputs the high nibble of app->setpoint to the LEDs on
// pins 4..7 every tick (0xF0 = all on). Pin 13: stack overflow hook.
// Watch in the debugger: depth, levelSP[], levelTask[], overruns,
// pxCurrentTCB, *app, SP, SREG (I bit).
//
// Debugging with Wokwi for VS Code: the dbg...() functions do nothing. They
// are breakpoint targets for debug.gdb (see README.md in examples_vscode).

#include <Arduino_FreeRTOS.h>
#include <task.h>
#include <util/delay.h>

const uint8_t PIN_LONG  = 2;        // DIP switch 1, ON = LOW = long work
const uint8_t PIN_DEFER = 3;        // DIP switch 2, ON = LOW = Worker task
const uint8_t PIN_ALARM = 13;       // stack overflow hook

const uint8_t SHORT_WORK = 3;       // work() in units of 100 us
const uint8_t LONG_WORK  = 25;
const uint8_t SETPOINT   = 0xF0;
const uint8_t MAX_LEVELS = 40;

// Application data on the FreeRTOS heap, allocated after the Ctrl task and
// right before the scheduler creates the Idle task: the heap block directly
// below the Idle stack. setpoint is its last byte.
struct AppData {
  uint8_t buffer[384];              // e.g. a history of measured values
  uint8_t setpoint;
};
AppData *app;

TaskHandle_t hWorker, hCtrl;

volatile uint16_t ticks = 0;
volatile uint8_t  depth = 0;
volatile uint8_t  maxDepth = 0;
volatile uint16_t levelSP[MAX_LEVELS + 1];      // SP after entering level n
const char * volatile levelTask[MAX_LEVELS + 1]; // task whose stack it uses
volatile uint16_t overruns = 0;     // ticks the Worker did not keep up with
// Operating mode from the DIP switches, read by Ctrl right after each
// scheduler tick: a long-work phase always starts at the same point of the
// tick period, so the timeline below does not depend on when you flip the
// switch.
volatile bool longWork = false;
volatile bool deferred = false;
volatile bool     corruptionSeen = false;
volatile bool     overrunSeen = false;

// --- breakpoint targets (see debug.gdb) -------------------------------------
#define DBG_HOOK __attribute__((noinline))
DBG_HOOK void dbgTick(uint8_t level, uint16_t sp)    { asm volatile(""); }
DBG_HOOK void dbgNested(uint8_t level, uint16_t sp)  { asm volatile(""); }
DBG_HOOK void dbgSetpointCorrupted(uint8_t level, uint8_t value) { asm volatile(""); }
DBG_HOOK void dbgRtosStackOverflow(const char *task) { asm volatile(""); }
DBG_HOOK void dbgOverrun(uint16_t pending)           { asm volatile(""); }

__attribute__((noinline)) void work(uint8_t n) {
  for (; n > 0; n--) _delay_us(100);
}

uint8_t workUnits() {
  return longWork ? LONG_WORK : SHORT_WORK;
}

// Called by FreeRTOS when it switches out a task whose saved SP lies below
// the start of its stack. Replaces the weak default in the library.
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
  cli();
  digitalWrite(PIN_ALARM, HIGH);
  dbgRtosStackOverflow(name);
  for (;;) { }
}

ISR(TIMER1_COMPA_vect) {
  uint8_t level = ++depth;          // interrupts still disabled here
  ticks++;
  if (level > maxDepth) maxDepth = level;
  uint16_t sp = SP;
  if (level <= MAX_LEVELS) {
    levelSP[level] = sp;
    levelTask[level] = pcTaskGetName(NULL);   // the interrupted task
  }
  dbgTick(level, sp);
  if (app->setpoint != SETPOINT && !corruptionSeen) {
    corruptionSeen = true;
    dbgSetpointCorrupted(level, app->setpoint);
  }

  if (deferred) {                   // deferred: notify, return
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(hWorker, &woken);
    depth--;
    if (woken == pdTRUE) taskYIELD();
    return;
  }

  if (level == 2) dbgNested(level, sp);
  sei();                            // allowNesting: next tick may preempt
  work(workUnits());
  cli();
  depth--;
}

void workerTask(void *pv) {
  for (;;) {
    uint16_t pending = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (pending > 1) {              // ticks arrived while we were working
      overruns += pending - 1;
      if (!overrunSeen) {
        overrunSeen = true;
        dbgOverrun(pending);
      }
    }
    longWork = digitalRead(PIN_LONG) == LOW;  // Ctrl may be starved now
    work(workUnits());
  }
}

void ctrlTask(void *pv) {
  // Timer 1, CTC mode, prescaler 8: compare match every 1 ms. Started here,
  // not in setup(), so that the first tick already finds the scheduler running.
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);
  OCR1A  = 2000 - 1;
  TCNT1  = 0;
  TIMSK1 = _BV(OCIE1A);
  for (;;) {
    longWork = digitalRead(PIN_LONG) == LOW;
    deferred = digitalRead(PIN_DEFER) == LOW;
    uint8_t value = app->setpoint;  // output setpoint to the actuator
    PORTD = (PORTD & 0x0F) | (value & 0xF0);
    vTaskDelay(1);
  }
}

void setup() {
  pinMode(PIN_LONG, INPUT_PULLUP);
  pinMode(PIN_DEFER, INPUT_PULLUP);
  pinMode(PIN_ALARM, OUTPUT);
  for (uint8_t pin = 4; pin <= 7; pin++) pinMode(pin, OUTPUT);

  xTaskCreate(workerTask, "Worker", 128, NULL, 2, &hWorker);
  xTaskCreate(ctrlTask, "Ctrl", 128, NULL, 1, &hCtrl);
  app = (AppData *)pvPortMalloc(sizeof(AppData));
  memset(app->buffer, 0, sizeof(app->buffer));
  app->setpoint = SETPOINT;
  // The scheduler starts after setup() and creates Idle and the timer task.
}

void loop() { }                     // runs in the Idle task
