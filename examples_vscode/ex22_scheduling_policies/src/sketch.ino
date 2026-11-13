// Scheduling policies compared on the same task set: FreeRTOS time slicing,
// rate monotonic (RM), shortest remaining time first (SRTF) and earliest
// deadline first (EDF).
//
// Periodic tasks, deadline = end of the period (1 tick = 16 ms).
// Task set 1 (DIP switch 3 OFF):
//   A  period  4 ticks ( 64 ms)  36 ms CPU time per job   U = 56 %
//   B  period  8 ticks (128 ms)  20 ms                    U = 16 %
//   C  period 16 ticks (256 ms)  24 ms                    U =  9 %
// Total 81 %: more than the Liu & Layland bound for three tasks (78 %), but
// the response time test shows that RM meets all deadlines.
// Task set 2 (DIP switch 3 ON): where RM fails and EDF does not
//   A  period  5 ticks ( 80 ms)  30 ms                    U = 38 %
//   B  period  7 ticks (112 ms)  60 ms                    U = 54 %
// Total 91 %: more than the bound for two tasks (83 %), and here RM does
// miss: A has the shorter period and always runs first, so B gets only
// 52 ms before its first deadline. EDF lets B finish first whenever the
// deadline of B is the nearer one, and meets all deadlines (U <= 100 %).
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
// DIP switches 1 and 2 select the policy (switches are read when all
// periods start together: every 16 ticks in set 1, every 35 ticks in set 2).
// The results below are those of task set 1:
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
// Serial (115200): every 64 ticks (set 2: 70 ticks) one line per task with
// the jobs done and missed and the average and longest response time
// (release to finish).
//
// Logic analyzer (HIGH while the task runs):
//   D0 = pin 10 A     D1 = pin 11 B     D2 = pin 12 C
//   D3 = pin 9  Sched D4 = pin 8  Idle
//   D5 = pin 13 deadline missed (HIGH for one tick, also the LED)

#include <Arduino_FreeRTOS.h>
#include <util/delay.h>

const uint8_t PIN_SW1   = 2;        // DIP switch 1, ON = LOW
const uint8_t PIN_SW2   = 3;        // DIP switch 2, ON = LOW
const uint8_t PIN_SW3   = 4;        // DIP switch 3, ON = LOW: task set 2
const uint8_t PIN_SCHED = 9;        // trace pins, driven by the scheduler
const uint8_t PIN_IDLE  = 8;
const uint8_t PIN_MISS  = 13;

enum Policy : uint8_t { SLICE, RM, SRTF, EDF };

const uint8_t N = 3;

struct TaskSet {
  uint8_t tasks;                    // number of tasks used
  uint8_t hyperperiod;              // ticks until all periods start together
  uint8_t reportEvery;              // hyperperiods per report
  struct { uint8_t period, cost; } task[N];   // ticks, ms of CPU time
};

const TaskSet SETS[2] = {
  { 3, 16, 4, { { 4, 36 }, { 8, 20 }, { 16, 24 } } },
  { 2, 35, 2, { { 5, 30 }, { 7, 60 } } },
};

struct Stats {
  uint8_t  done, missed;
  uint16_t maxMs;                   // longest response time
  uint16_t sumMs;                   // sum of the response times of done jobs
};

struct Job {
  const char      name;
  const uint8_t   pin;              // trace pin
  uint8_t         period;           // in ticks, deadline = period
  uint8_t         cost;             // CPU time per job in ms
  volatile uint8_t remaining;       // ms of CPU time left, 0 = no job
  TickType_t      deadline;         // absolute, in ticks = next release
  uint32_t        releasedUs;
  Stats           stats;
  TaskHandle_t    task;
};

Job jobs[N] = { { 'A', 10 }, { 'B', 11 }, { 'C', 12 } };

TaskHandle_t schedTask;
Policy  policy  = SLICE;
uint8_t setNo   = 0;                // index into SETS
int8_t  chosen  = -1;               // index of the job with priority 2

// Copy of the statistics for loop(), which prints them in the idle time
Stats   report[N];
Policy  reportPolicy;
uint8_t reportSet;
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

Policy readPolicy() {
  return (Policy)((digitalRead(PIN_SW1) == LOW ? 1 : 0) |
                  (digitalRead(PIN_SW2) == LOW ? 2 : 0));
}

uint8_t readSet() {
  return digitalRead(PIN_SW3) == LOW ? 1 : 0;
}

// Switch to the task set chosen with switch 3 and start a job of each task
void loadSet(TickType_t now) {
  setNo = readSet();
  const TaskSet &set = SETS[setNo];
  for (uint8_t i = 0; i < N; i++) {
    Job &j = jobs[i];
    if (i >= set.tasks) {           // task not used in this set
      j.remaining = 0;
      continue;
    }
    j.period = set.task[i].period;
    j.cost   = set.task[i].cost;
    if (j.remaining == 0) xTaskNotifyGive(j.task);
    j.remaining  = j.cost;
    j.deadline   = now + j.period;
    j.releasedUs = micros();
  }
}

// Start of a report interval: read the policy, clear the statistics
void startPolicy() {
  if (chosen >= 0) vTaskPrioritySet(jobs[chosen].task, 1);
  chosen = -1;
  policy = readPolicy();
  for (uint8_t i = 0; i < N; i++) jobs[i].stats = Stats();
}

void schedulerTask(void *pv) {
  TickType_t tick = 0;              // ticks handled so far
  uint8_t inHyper = 0;              // ticks since all periods started together
  uint8_t hypers  = 0;              // hyperperiods since the last report
  startPolicy();
  loadSet(0);
  for (;;) {
    dispatch(tick);
    ulTaskNotifyTake(pdTRUE, 1);    // until the next tick or a job finishes
    if (xTaskGetTickCount() == tick) continue;  // a job finished: dispatch

    tick++;
    digitalWrite(PIN_MISS, LOW);
    for (uint8_t i = 0; i < SETS[setNo].tasks; i++)
      if (jobs[i].deadline == tick) release(jobs[i], tick);

    if (++inHyper < SETS[setNo].hyperperiod) continue;
    inHyper = 0;                    // all periods have just started together
    bool reportNow = ++hypers >= SETS[setNo].reportEvery;
    if (reportNow) {
      for (uint8_t i = 0; i < N; i++) report[i] = jobs[i].stats;
      reportPolicy = policy;
      reportSet = setNo;
      reportReady = true;
    }
    if (reportNow || policy != readPolicy() || setNo != readSet()) {
      hypers = 0;
      startPolicy();
      if (setNo != readSet()) loadSet(tick);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SW1, INPUT_PULLUP);
  pinMode(PIN_SW2, INPUT_PULLUP);
  pinMode(PIN_SW3, INPUT_PULLUP);
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
  uint8_t n = SETS[reportSet].tasks;
  uint8_t setShown = reportSet + 1;
  reportReady = false;
  taskEXIT_CRITICAL();

  Serial.print(p == SLICE ? F("--- time slicing") : p == RM ? F("--- RM")
             : p == SRTF  ? F("--- SRTF")         : F("--- EDF"));
  Serial.print(F(", task set "));
  Serial.println(setShown);
  uint16_t sum = 0, done = 0, missed = 0;
  for (uint8_t i = 0; i < n; i++) {
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
