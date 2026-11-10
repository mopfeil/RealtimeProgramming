# Periodic job in MicroPython: interpreter speed and garbage collection.
# The same job and the same output as ex15 (C) and ex17 (Rust).
#
# Every 2000 us the job takes a new sample (pseudo random), runs a 16-tap
# FIR filter over the last 16 samples and builds a log record. Every 500
# periods the program prints statistics (outside of the measurement):
#
#   heap=0 rel=0 gc=0 period min/avg/max 1977/2000/2023 us job min/max 338/373 us overruns 0
#
#   DIP switch 1 ON (GP10 LOW): the job allocates like normal Python code:
#     a new list for the window, new strings for the log record.
#     Every allocation fills the heap; when it is full, the garbage
#     collector stops the program until it has swept the whole heap.
#     OFF: no allocation in the loop: preallocated array, integer math.
#   DIP switch 2 ON (GP11 LOW): relative timing, "work, then wait 2000 us".
#     OFF: absolute timing, "wait until the next multiple of 2000 us".
#   DIP switch 3 ON (GP12 LOW): gc.collect() in the slack after every job:
#     each collection is short because little garbage has accumulated, and
#     it happens when it does no harm (pattern "GC in idle time").
#
# Logic analyzer: D0 = GP2 (HIGH during the job), D1 = GP3 (HIGH during
# the explicit gc.collect()). Output on the USB serial port (REPL).
import gc
import time
from array import array
from machine import Pin

PERIOD_US = 2000
REPORT_EVERY = 500
TAPS = 16

job_pin = Pin(2, Pin.OUT)
gc_pin = Pin(3, Pin.OUT)
sw_heap = Pin(10, Pin.IN, Pin.PULL_UP)
sw_rel = Pin(11, Pin.IN, Pin.PULL_UP)
sw_gc = Pin(12, Pin.IN, Pin.PULL_UP)

coeff = array("h", [1, 3, 7, 12, 18, 24, 28, 31, 31, 28, 24, 18, 12, 7, 3, 1])
samples = array("h", [0] * TAPS)    # ring buffer, allocated once
state = array("i", [12345, 0])      # [rng, head]: no new int objects per job
records = [None] * 8                # last 8 log records (switch 1 ON)


def next_sample():
    # LCG as in the C version. Its values stay below 2**30: a MicroPython
    # "small int", which needs no heap (a bigger product would allocate)
    r = (state[0] * 75 + 74) % 65537
    state[0] = r
    return (r & 0x0FFF) - 2048


def job_static():
    h = state[1]
    samples[h] = next_sample()
    h = (h + 1) % TAPS
    state[1] = h
    acc = 0
    for i in range(TAPS):
        acc += coeff[i] * samples[(h + i) % TAPS]
    return acc


def job_heap(n):
    h = state[1]
    samples[h] = next_sample()
    h = (h + 1) % TAPS
    state[1] = h
    window = [samples[(h + i) % TAPS] for i in range(TAPS)]   # new list
    acc = 0
    for i in range(TAPS):
        acc += coeff[i] * window[i]
    records[n % 8] = "y=" + str(acc)                           # new strings
    return acc


def run(use_heap, relative, collect):
    p_min, p_max, j_min, j_max, overruns = 1 << 30, 0, 1 << 30, 0, 0
    ticks_us, ticks_diff, ticks_add = time.ticks_us, time.ticks_diff, time.ticks_add
    nxt = ticks_add(ticks_us(), PERIOD_US)
    while ticks_diff(ticks_us(), nxt) < 0:      # start on a period boundary
        pass
    first = last = ticks_us()
    for n in range(REPORT_EVERY + 1):
        start = ticks_us()
        if n > 0:
            p = ticks_diff(start, last)
            if p < p_min:
                p_min = p
            if p > p_max:
                p_max = p
            if p > PERIOD_US + PERIOD_US // 10:
                overruns += 1
        last = start
        job_pin(1)
        if use_heap:
            job_heap(n)
        else:
            job_static()
        job_pin(0)
        d = ticks_diff(ticks_us(), start)
        if d < j_min:
            j_min = d
        if d > j_max:
            j_max = d
        if collect:
            gc_pin(1)
            gc.collect()                        # in the slack of this period
            gc_pin(0)
        if relative:
            time.sleep_us(PERIOD_US)            # work, then wait
        else:
            nxt = ticks_add(nxt, PERIOD_US)     # wait until the deadline
            while ticks_diff(ticks_us(), nxt) < 0:
                pass
    avg = ticks_diff(last, first) // REPORT_EVERY
    return p_min, avg, p_max, j_min, j_max, overruns


print("ex16 MicroPython: periodic job, 2000 us")
while True:
    h, r, c = sw_heap() == 0, sw_rel() == 0, sw_gc() == 0
    res = run(h, r, c)
    print("heap=%d rel=%d gc=%d period min/avg/max %d/%d/%d us job min/max %d/%d us overruns %d"
          % ((h, r, c) + res))
