// Nested timer interrupts on the AVR without an RTOS: Arduino C version of
// the 8051 example ex06 (stack_overflow.asm).
//
// Timer 1 interrupts every T = 1 ms. The ISR calls work(), whose duration is
// selected with DIP switch 1:
//   switch 1 OFF (pin 2 HIGH):  300 us -> each ISR ends before the next tick
//   switch 1 ON  (pin 2 LOW) : 2500 us -> the next tick arrives during work()
// After its bookkeeping the ISR re-enables interrupts (sei), so that the next
// tick may preempt the running work, which resumes afterwards. This is the
// allowNesting (early RETI) of the 8051 version.
//
//   short work: depth stays 1, SP comes back to the same value every tick.
//   long work : every tick preempts the previous ISR, which can only resume
//               after the newer one has finished. That never happens: each
//               level leaves one ISR frame on the stack. The stack grows down
//               from RAMEND (0x8FF) into the application data at the top of
//               the static variables:
//               - app.setpoint is overwritten: the actuator LEDs show garbage
//               - SP < STACK_LIMIT: the guard stops the program, pin 13 on
//
// When does it show? (long work, one level per ms, 22 bytes per level)
//   - 2 ms : depth 2, SP moves down and never comes back up
//   - 79 ms: level 79 pushes its registers over app.setpoint (0x4E instead
//            of 0xF0). Nothing reports it, the ISR outputs the wrong value.
//   - 83 ms: SP < STACK_LIMIT, the guard stops the program
//
// Actuator: LEDs on pins 4..7 show the high nibble of app.setpoint
//           (0xF0 = all four on).
// Watch in the debugger: depth, maxDepth, levelSP[], app, SP, SREG (I bit).
//
// Debugging with Wokwi for VS Code: the dbg...() functions do nothing. They
// are breakpoint targets for debug.gdb (see README.md in examples_vscode).

#include <util/delay.h>

const uint8_t PIN_LONG  = 2;        // DIP switch 1, ON = LOW = long work
const uint8_t PIN_GUARD = 13;       // stack guard tripped

const uint8_t  SHORT_WORK = 3;      // work() in units of 100 us
const uint8_t  LONG_WORK  = 25;
const uint8_t  SETPOINT   = 0xF0;   // value for the actuator
const uint8_t  MAX_LEVELS = 100;

// Application data. Section .noinit comes right after .bss, so these are
// the static variables with the highest addresses: the first ones the
// stack runs into. setpoint is the last byte of the block.
struct AppData {
  uint8_t buffer[96];               // e.g. measured values
  uint8_t setpoint;
};
AppData app __attribute__((section(".noinit")));

// The guard needs room for one more ISR frame below SP, otherwise the
// frame that trips it has already overwritten the variables below app.
const uint8_t GUARD_RESERVE = 24;
#define STACK_LIMIT ((uint16_t)&app + GUARD_RESERVE)

volatile uint16_t ticks = 0;
volatile uint8_t  depth = 0;        // current nesting depth
volatile uint8_t  maxDepth = 0;
volatile uint16_t levelSP[MAX_LEVELS + 1];  // SP after entering level n
volatile bool     corruptionSeen = false;

// --- breakpoint targets (see debug.gdb) -------------------------------------
#define DBG_HOOK __attribute__((noinline))
DBG_HOOK void dbgTick(uint8_t level, uint16_t sp)    { asm volatile(""); }
DBG_HOOK void dbgNested(uint8_t level, uint16_t sp)  { asm volatile(""); }
DBG_HOOK void dbgSetpointCorrupted(uint8_t level, uint8_t value) { asm volatile(""); }
DBG_HOOK void dbgStackOverflow(uint8_t level, uint16_t sp) { asm volatile(""); }

__attribute__((noinline)) void work(uint8_t n) {
  for (; n > 0; n--) _delay_us(100);
}

void stackOverflow(uint8_t level, uint16_t sp) {
  cli();                            // fatal: stop all interrupts
  digitalWrite(PIN_GUARD, HIGH);
  dbgStackOverflow(level, sp);
  for (;;) { }
}

ISR(TIMER1_COMPA_vect) {
  // Interrupts are still disabled here: the bookkeeping is atomic.
  uint8_t level = ++depth;
  ticks++;
  if (level > maxDepth) maxDepth = level;
  uint16_t sp = SP;
  if (level <= MAX_LEVELS) levelSP[level] = sp;
  dbgTick(level, sp);
  if (sp < STACK_LIMIT) stackOverflow(level, sp);   // stack guard

  uint8_t value = app.setpoint;     // output setpoint to the actuator,
  PORTD = (PORTD & 0x0F) | (value & 0xF0);          // garbage once the
  if (value != SETPOINT && !corruptionSeen) {       // stack got there
    corruptionSeen = true;
    dbgSetpointCorrupted(level, value);
  }
  if (level == 2) dbgNested(level, sp);

  sei();                            // allowNesting: next tick may preempt
  work(digitalRead(PIN_LONG) == LOW ? LONG_WORK : SHORT_WORK);
  cli();
  depth--;
}

void setup() {
  pinMode(PIN_LONG, INPUT_PULLUP);
  pinMode(PIN_GUARD, OUTPUT);
  for (uint8_t pin = 4; pin <= 7; pin++) pinMode(pin, OUTPUT);
  memset(app.buffer, 0, sizeof(app.buffer));
  app.setpoint = SETPOINT;

  // Timer 1, CTC mode, prescaler 8: 2 MHz, compare match every 2000 counts
  cli();
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);
  OCR1A  = 2000 - 1;
  TCNT1  = 0;
  TIMSK1 = _BV(OCIE1A);
  sei();
}

// Background: runs only while no ISR is active.
void loop() { }
