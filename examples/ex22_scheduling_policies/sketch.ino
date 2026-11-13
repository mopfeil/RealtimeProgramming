// Scheduling policies compared on the same task set: FreeRTOS time slicing,
// rate monotonic (RM), shortest remaining time first (SRTF) and earliest
// deadline first (EDF).
//
// Three periodic tasks, deadline = end of the period (1 tick = 16 ms):
//   A  period  4 ticks ( 64 ms)  36 ms CPU time per job   U = 56 %
//   B  period  8 ticks (128 ms)  20 ms                    U = 16 %
//   C  period 16 ticks (256 ms)  24 ms                    U =  9 %
// Total 81 %: more than the Liu & Layland bound for three tasks (78 %), but
// the response time test shows that RM meets all deadlines.
//
// FreeRTOS itself only knows fixed priorities and time slicing. The other
// policies are emulated by the task Sched (priority 3, the highest). It runs
// at every tick and whenever a job has finished, and it
//   - releases the jobs (task notification) at the start of their period,
//   - checks the deadlines: a job that is not finished when its next period
//     starts is counted as missed and dropped, the new job starts from zero,
//   - chooses the job that may run and gives its task priority 2; all other
//     workers have priority 1 and do not run as long as the chosen one does.
//
// DIP switches 1 and 2 select the policy (read every 16 ticks, when all
// three periods start together):
//   OFF OFF  time slicing: all workers have priority 1, Sched chooses
//            nothing. FreeRTOS switches to the next ready worker at every
//            tick (round robin). Fair, but blind for deadlines: A gets at
//            most every second or third tick and misses its deadline.
//   ON  OFF  RM: the ready job with the shortest period runs. Fixed
//            priorities, all deadlines are met.
//   OFF ON   SRTF: the ready job with the least remaining CPU time runs.
//            Short jobs finish early (best average response time), but at
//            ticks 16k the short jobs B and C run before the long but urgent
//            job A: A misses its deadline.
//   ON  ON   EDF: the ready job with the nearest deadline runs. All
//            deadlines are met.
//
// Serial (115200): every 64 ticks one line per task with the jobs done and
// missed and the average and longest response time (release to finish).
//
// Logic analyzer (HIGH while the task runs):
//   D0 = pin 10 A     D1 = pin 11 B     D2 = pin 12 C
//   D3 = pin 9  Sched D4 = pin 8  Idle
//   D5 = pin 13 deadline missed (HIGH for one tick, also the LED)

#include <Arduino_FreeRTOS.h>
#include <util/delay.h>

const uint8_t PIN_SW1   = 2;        // DIP switch 1, ON = LOW
const uint8_t PIN_SW2   = 3;        // DIP switch 2, ON = LOW
const uint8_t PIN_SCHED = 9;        // trace pins, driven by the scheduler
const uint8_t PIN_IDLE  = 8;
const uint8_t PIN_MISS  = 13;

enum Policy : uint8_t { SLICE, RM, SRTF, EDF };

const uint8_t    N = 3;
const TickType_t HYPERPERIOD  = 16;  // all periods start together
const TickType_t REPORT_TICKS = 64;

struct Stats {
  uint8_t  done, missed;
  uint16_t maxMs;                   // longest response time
  uint16_t sumMs;                   // sum of the response times of done jobs
};

struct Job {
  const char      name;
  const uint8_t   pin;              // trace pin
  const TickType_t period;          // in ticks, deadline = period
  const uint8_t   cost;             // CPU time per job in ms
  volatile uint8_t remaining;       // ms of CPU time left, 0 = no job
  TickType_t      deadline;         // absolute, in ticks
  uint32_t        releasedUs;
  Stats           stats;
  TaskHandle_t    task;
};

Job jobs[N] = {
  { 'A', 10,  4, 36 },
  { 'B', 11,  8, 20 },
  { 'C', 12, 16, 24 },
};

TaskHandle_t schedTask;
Policy  policy  = SLICE;
int8_t  chosen  = -1;               // index of the job with priority 2

// Copy of the statistics for loop(), which prints them in the idle time
Stats   report[N];
Policy  reportPolicy;
volatile bool reportReady = false;

// One periodic task: wait for the release, then burn the CPU time of the job
// in steps of 1 ms. This is CPU time: it does not advance while preempted.
void workerTask(void *pv) {
  Job &j = *(Job *)pv;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);    // released by Sched
    while (j.remaining > 0) {
      _delay_us(1000);
      taskENTER_CRITICAL();         // Sched may drop the job at any tick
      if (j.remaining > 0 && --j.remaining == 0) {
        uint16_t ms = (micros() - j.releasedUs + 500) / 1000;
        j.stats.done++;
        j.stats.sumMs += ms;
        if (ms > j.stats.maxMs) j.stats.maxMs = ms;
      }
      taskEXIT_CRITICAL();
    }
    xTaskNotifyGive(schedTask);     // finished: let Sched choose the next
  }
}

// Start of a period: check the deadline of the old job, release the new one
void release(Job &j, TickType_t now) {
  if (j.remaining > 0) {            // old job not finished: deadline missed
    j.stats.missed++;
    digitalWrite(PIN_MISS, HIGH);
  } else {
    xTaskNotifyGive(j.task);        // the worker waits for its next job
  }
  j.remaining  = j.cost;            // a missed job is dropped
  j.deadline   = now + j.period;
  j.releasedUs = micros();
}

// The policies: the ready job with the smallest key runs
uint16_t key(const Job &j, TickType_t now) {
  switch (policy) {
    case RM:   return j.period;                 // shortest period
    case SRTF: return j.remaining;              // least work left
    default:   return j.deadline - now;         // EDF: nearest deadline
  }
}

void dispatch(TickType_t now) {
  if (policy == SLICE) return;      // FreeRTOS shares priority 1 by itself
  int8_t best = -1;
  for (uint8_t i = 0; i < N; i++) {
    if (jobs[i].remaining == 0) continue;
    if (best < 0 || key(jobs[i], now) < key(jobs[best], now)) best = i;
  }
  if (best == chosen) return;
  if (chosen >= 0) vTaskPrioritySet(jobs[chosen].task, 1);
  if (best >= 0)   vTaskPrioritySet(jobs[best].task, 2);
  chosen = best;
}

void startPolicy() {
  if (chosen >= 0) vTaskPrioritySet(jobs[chosen].task, 1);
  chosen = -1;
  policy = (Policy)((digitalRead(PIN_SW1) == LOW ? 1 : 0) |
                    (digitalRead(PIN_SW2) == LOW ? 2 : 0));
  for (uint8_t i = 0; i < N; i++) jobs[i].stats = Stats();
}

void schedulerTask(void *pv) {
  TickType_t tick = 0;              // ticks handled so far
  startPolicy();
  for (uint8_t i = 0; i < N; i++) release(jobs[i], 0);
  for (;;) {
    dispatch(tick);
    ulTaskNotifyTake(pdTRUE, 1);    // until the next tick or a job finishes
    TickType_t now = xTaskGetTickCount();
    while (tick != now) {
      tick++;
      digitalWrite(PIN_MISS, LOW);
      if (tick % REPORT_TICKS == 0) {
        for (uint8_t i = 0; i < N; i++) report[i] = jobs[i].stats;
        reportPolicy = policy;
        reportReady = true;
      }
      if (tick % HYPERPERIOD == 0) {
        bool changed = policy != (Policy)((digitalRead(PIN_SW1) == LOW ? 1 : 0) |
                                          (digitalRead(PIN_SW2) == LOW ? 2 : 0));
        if (changed || tick % REPORT_TICKS == 0) startPolicy();
      }
      for (uint8_t i = 0; i < N; i++)
        if (tick % jobs[i].period == 0) release(jobs[i], tick);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SW1, INPUT_PULLUP);
  pinMode(PIN_SW2, INPUT_PULLUP);
  pinMode(PIN_SCHED, OUTPUT);
  pinMode(PIN_IDLE, OUTPUT);
  pinMode(PIN_MISS, OUTPUT);

  static const char *const names[N] = { "A", "B", "C" };
  for (uint8_t i = 0; i < N; i++) {
    pinMode(jobs[i].pin, OUTPUT);
    xTaskCreate(workerTask, names[i], 128, &jobs[i], 1, &jobs[i].task);
    vTaskSetApplicationTaskTag(jobs[i].task, (TaskHookFunction_t)jobs[i].pin);
  }
  xTaskCreate(schedulerTask, "Sched", 160, NULL, 3, &schedTask);
  vTaskSetApplicationTaskTag(schedTask, (TaskHookFunction_t)PIN_SCHED);
}

// loop() runs in the idle task: tag it on the first call, so that its trace
// pin follows the scheduler, and print the report when Sched has left one
void loop() {
  static bool tagged = false;
  if (!tagged) {
    vTaskSetApplicationTaskTag(NULL, (TaskHookFunction_t)PIN_IDLE);
    digitalWrite(PIN_IDLE, HIGH);   // already switched in: set by hand once
    tagged = true;
  }
  if (!reportReady) return;

  Stats  s[N];
  taskENTER_CRITICAL();
  memcpy(s, report, sizeof s);
  Policy p = reportPolicy;
  reportReady = false;
  taskEXIT_CRITICAL();

  Serial.println(p == SLICE ? F("--- time slicing") : p == RM ? F("--- RM")
               : p == SRTF  ? F("--- SRTF")         : F("--- EDF"));
  uint16_t sum = 0, done = 0, missed = 0;
  for (uint8_t i = 0; i < N; i++) {
    Serial.print(jobs[i].name);
    Serial.print(F(": done "));
    Serial.print(s[i].done);
    Serial.print(F(", missed "));
    Serial.print(s[i].missed);
    Serial.print(F(", response avg "));
    Serial.print(s[i].done ? s[i].sumMs / s[i].done : 0);
    Serial.print(F(" max "));
    Serial.print(s[i].maxMs);
    Serial.println(F(" ms"));
    sum += s[i].sumMs;
    done += s[i].done;
    missed += s[i].missed;
  }
  Serial.print(F("all: missed "));
  Serial.print(missed);
  Serial.print(F(", average response "));
  Serial.print(done ? sum / done : 0);
  Serial.println(F(" ms"));
}
