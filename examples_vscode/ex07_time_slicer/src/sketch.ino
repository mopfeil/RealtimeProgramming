#include <TimerOne.h>

// Binary time slicer: a timer interrupt every T = 500 us calls exactly ONE
// slot function. The slot is the number of trailing zero bits of the tick
// counter:
//   tick 1, 3, 5, 7, ...    -> slot 0, every 2 ticks (1 ms)
//   tick 2, 6, 10, 14, ...  -> slot 1, every 4 ticks (2 ms)
//   tick 4, 12, 20, ...     -> slot 2, every 8 ticks (4 ms)
//   tick 8, 16, 24, ...     -> slot 3, every 8 ticks (4 ms, all the rest)
// Two slot functions never run in the same tick, and none interrupts another.
// The one condition: every slot function must be finished before the next
// tick (C + ISR overhead <= T).
//
// Logic analyzer: D0..D2 = pins 13, 12, 11, toggled by slot 0, 1, 2
//                 D3     = pin 10, HIGH while the timer ISR runs
// DIP switch 1 ON (pin 2 LOW): slot 2 needs 700 us > T -> overrun

const uint8_t PIN_SLOT0 = 13;
const uint8_t PIN_SLOT1 = 12;
const uint8_t PIN_SLOT2 = 11;
const uint8_t PIN_ISR   = 10;
const uint8_t PIN_SLOW  = 2;

const unsigned long TICK_US = 500;  // time slice T
const unsigned int  SLOW_US = 700;  // work of slot 2 with the switch ON

volatile uint32_t ticks = 0;        // executed time slices
volatile uint16_t overruns = 0;     // next tick already due at ISR end

static void toggle(uint8_t pin) {
  digitalWrite(pin, !digitalRead(pin));
}

// --- slot functions: run inside the ISR, must be short and bounded --------
void fastTask()   { toggle(PIN_SLOT0); }             // every 1 ms
void mediumTask() { toggle(PIN_SLOT1); }             // every 2 ms
void slowTask() {                                    // every 4 ms
  toggle(PIN_SLOT2);
  if (digitalRead(PIN_SLOW) == LOW) {
    delayMicroseconds(SLOW_US);     // too much work for one time slice
  }
}
void spareTask()  { }                                // every 4 ms, free

typedef void (*SlotFunction)(void);
const SlotFunction slots[] = { fastTask, mediumTask, slowTask, spareTask };
const uint8_t NSLOTS = sizeof(slots) / sizeof(slots[0]);

void timeSlicerISR() {
  static uint8_t tick = 0;          // unsigned: wraps from 255 to 0 cleanly
  digitalWrite(PIN_ISR, HIGH);

  uint8_t t = ++tick;
  uint8_t slot = 0;
  while (slot < NSLOTS - 1 && (t & 1) == 0) {  // count trailing zeros
    t >>= 1;
    slot++;
  }
  slots[slot]();

  ticks++;
  if (TIFR1 & _BV(TOV1)) {          // timer overflowed again during the ISR:
    overruns++;                     // the next slice starts late
  }
  digitalWrite(PIN_ISR, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SLOT0, OUTPUT);
  pinMode(PIN_SLOT1, OUTPUT);
  pinMode(PIN_SLOT2, OUTPUT);
  pinMode(PIN_ISR, OUTPUT);
  pinMode(PIN_SLOW, INPUT_PULLUP);
  Timer1.initialize(TICK_US);
  Timer1.attachInterrupt(timeSlicerISR);
}

// Background: everything without a deadline. Runs in the gaps between ticks.
void loop() {
  static uint32_t lastTicks = 0;
  delay(1000);
  noInterrupts();                   // 32-bit value: copy atomically
  uint32_t t = ticks;
  uint16_t o = overruns;
  interrupts();
  Serial.print("ticks/s: ");
  Serial.print(t - lastTicks);      // expected 1000000 / TICK_US = 2000
  Serial.print("  overruns: ");
  Serial.print(o);
  Serial.println(digitalRead(PIN_SLOW) == LOW ? "  (slot 2 slow)" : "");
  lastTicks = t;
}
