# Interrupt and shared data in MicroPython: hard and soft IRQs, allocation.
# The same experiment as ex18 (C) and ex20 (Rust).
#
# PWM slice 2 drives GP5 with 1 kHz; a wire takes the signal back to GP6.
# Each rising edge on GP6 raises an interrupt. The PWM counter runs at
# 1 MHz and starts at 0 on the edge, so the counter value that the handler
# reads is its latency in microseconds (machine.mem32 reads the register).
#
# The handler writes a sample of 8 words, all with the same sequence number.
# The main loop copies the sample word by word (the interpreter needs about
# 10 us per word, as the "processing" in the C version). A copy whose words
# differ is torn.
#
#   DIP switch 1 OFF (GP10 HIGH): no protection, torn copies.
#   DIP switch 1 ON  (GP10 LOW) : interrupts disabled while copying.
#     DIP switch 2 OFF: long critical section, the word-by-word copy.
#     DIP switch 2 ON : short critical section, one slice assignment
#       (snap[:] = shared: a single C function, no allocation).
#   DIP switch 3 ON (GP12 LOW): soft IRQ (hard=False). The handler does not
#     run in the interrupt, but is scheduled and runs between two bytecodes
#     of the main program: no restrictions, but a much higher latency.
#     OFF: hard IRQ, the handler runs inside the interrupt.
#   DIP switch 4 ON (GP13 LOW): the handler allocates (it builds a list).
#     A hard IRQ must not allocate: MemoryError, the handler is switched off.
#     A soft IRQ may.
#
# Output every second on the USB serial port (REPL), e.g.
#   protect=0 short=0 soft=0 alloc=0 irqs 1000 copies 2200 torn 700 latency min/max 30/40 us
#
# Logic analyzer: D0 = GP5 (PWM, the interrupt source), D1 = GP3 (HIGH
# during the handler), D2 = GP2 (HIGH while main copies).
import time
import machine
import micropython
from array import array
from machine import PWM, Pin

micropython.alloc_emergency_exception_buf(200)   # traceback from a hard IRQ

WORDS = 8
PWM_BASE = 0x40050000
SLICE = 2                                         # GP5 = PWM slice 2, channel B
CSR, DIV, CTR, CC, TOP = (PWM_BASE + SLICE * 0x14 + o for o in (0, 4, 8, 12, 16))

isr_pin = Pin(3, Pin.OUT)
main_pin = Pin(2, Pin.OUT)
sw_protect = Pin(10, Pin.IN, Pin.PULL_UP)
sw_short = Pin(11, Pin.IN, Pin.PULL_UP)
sw_soft = Pin(12, Pin.IN, Pin.PULL_UP)
sw_alloc = Pin(13, Pin.IN, Pin.PULL_UP)

shared = array("I", [0] * WORDS)    # written by the handler
snap = array("I", [0] * WORDS)      # main's copy, allocated once
stat = array("I", [0, 0xFFFF, 0])   # [irqs, latency min, latency max]
mode = array("I", [0])              # handler allocates? (read in the handler)
mem32 = machine.mem32


def on_edge(pin):
    isr_pin(1)
    lat = mem32[CTR]                # us since the edge
    if lat < stat[1]:
        stat[1] = lat
    if lat > stat[2]:
        stat[2] = lat
    seq = stat[0] + 1
    for i in range(WORDS):
        shared[i] = seq
    stat[0] = seq
    if mode[0]:
        _ = [seq] * 4               # allocation: not allowed in a hard IRQ
    isr_pin(0)


# PWM: MicroPython sets up the pin and the slice, then the registers are
# changed to a 1 MHz counter (125 MHz / 125), period 1000 us, HIGH for 500 us
pwm = PWM(Pin(5), freq=1000, duty_u16=32768)
mem32[DIV] = 125 << 4               # integer divider 125 (8.4 fixed point)
mem32[TOP] = 999
mem32[CC] = (500 << 16) | (mem32[CC] & 0xFFFF)   # channel B in bits 31..16
irq_in = Pin(6, Pin.IN)


def attach(soft):
    irq_in.irq(on_edge, Pin.IRQ_RISING, hard=not soft)


print("ex19 MicroPython: IRQ and shared data")
soft = sw_soft() == 0
attach(soft)
copies = torn = 0
last_report = time.ticks_ms()
while True:
    protect, short_cs = sw_protect() == 0, sw_short() == 0
    mode[0] = sw_alloc() == 0
    if (sw_soft() == 0) != soft:
        soft = not soft
        attach(soft)

    main_pin(1)
    if not protect:
        for i in range(WORDS):
            snap[i] = shared[i]
    elif not short_cs:
        state = machine.disable_irq()   # long critical section
        for i in range(WORDS):
            snap[i] = shared[i]
        machine.enable_irq(state)
    else:
        state = machine.disable_irq()   # short critical section
        snap[:] = shared
        machine.enable_irq(state)
    main_pin(0)

    copies += 1
    first = snap[0]
    for i in range(1, WORDS):
        if snap[i] != first:
            torn += 1
            break

    if time.ticks_diff(time.ticks_ms(), last_report) >= 1000:
        last_report = time.ticks_ms()
        state = machine.disable_irq()   # read and reset together
        n, lo, hi = stat[0], stat[1], stat[2]
        stat[0], stat[1], stat[2] = 0, 0xFFFF, 0
        machine.enable_irq(state)
        print("protect=%d short=%d soft=%d alloc=%d irqs %d copies %d torn %d latency min/max %d/%d us"
              % (protect, short_cs, soft, mode[0], n, copies, torn, lo, hi))
        copies = torn = 0
