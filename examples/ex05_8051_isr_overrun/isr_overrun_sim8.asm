; Example 5 for https://schweigi.github.io/assembler-simulator/
; ISR work that takes longer than the interrupt period
;
; The simulated CPU has no timer and no interrupts. Subroutine tick plays
; the hardware: one clock of a timer with period 10, plus the interrupt
; logic of the 8051. Every loop calls tick once per pass, so time in this
; model = number of tick calls. On overflow tick sets TF (a flag, not a
; counter). If TF is set and no ISR is in progress, tick vectors to isr:
; TF is cleared and INPROG is set. reti clears INPROG again.
;
; NWORK (address 08h) selects the duration of work() in ticks:
;   DB 4   ->  ISR takes 4 of 10 ticks (fits into the period)
;   DB 25  ->  ISR takes 25 ticks (longer than two periods)
; Edit the value, then Assemble and Run (256 Hz: about 1 period/s).
;
; Console = timeline, one character per tick (24 ticks, then it wraps):
;   .  main loop runs      #  ISR runs
;   |  timer overflow      X  overflow while TF still set: tick lost
; Memory 02h..05h: TICKS, PERIODS, OVERRUN, MAINCNT (hex).
;
; HOW TO SPOT THE ERROR
; Nothing crashes and nothing stops. The program runs on, just wrong.
; Compare what you see with what a correct system must show:
;
;   check                     correct (DB 4)        wrong (DB 25)
;   TICKS (02h) = PERIODS?    equal (TICKS at most  falls behind, e.g.
;                             1 behind)             5 ISRs in 13 periods
;   OVERRUN (04h)             stays 0               +1 after every ISR
;   timeline                  |####.....|####.....  |#########|#########X
;                                                   #####.###|#########X
;   dots between ISRs         5 per period          exactly one, then the
;                                                   ISR starts without '|'
;   MAINCNT (05h)             +6 per period         +1 per ISR (frozen)
;
; When does it show? The first ISR starts at tick 10. With DB 25 it is
; still running at tick 20 ('|' inside the '#' block: TF pending) and at
; tick 30 the next overflow finds TF still set: the first 'X' (about
; step 980). From then on every ISR loses one or two ticks.
; The timeline wraps after 24 ticks: stop the run (or use Step) to read
; it, and compare the counters in the memory view, not the console.

        JMP main
TICKS:  DB 0            ; 02h  ISR executions
PERIODS: DB 0           ; 03h  timer overflows (what TICKS should be)
OVERRUN: DB 0           ; 04h  ISR ended with TF already set again
MAINCNT: DB 0           ; 05h  progress of the main loop
TIMER:  DB 10           ; 06h  timer count, overflows every 10 ticks
TF:     DB 0            ; 07h  timer overflow flag
NWORK:  DB 4            ; 08h  <-- 4 = short work, 25 = long work
INPROG: DB 0            ; 09h  "interrupt in progress" (blocks same level)
POS:    DB 232          ; 0Ah  next console position of the timeline

main:   CALL tick               ; one timer clock per pass
        MOV A, [MAINCNT]        ; background work; with DB 25 only one
                                ; pass between two ISRs: main starves
        INC A
        MOV [MAINCNT], A
        JMP main

; --- Timer ISR -------------------------------------------------------------
isr:    PUSH A                  ; save the registers the ISR uses
        PUSH B
        MOV A, [TICKS]
        INC A
        MOV [TICKS], A
        MOV B, [NWORK]
        CALL work
        MOV A, [TF]             ; timer already overflowed again?
        CMP A, 0                ; the only place where the program itself
        JZ isrEnd               ; can notice the error: TF set at ISR end
        MOV A, [OVERRUN]        ; yes: deadline of next tick missed
        INC A
        MOV [OVERRUN], A
isrEnd: POP B
        POP A
        JMP reti

; --- work(): busy loop, one tick per pass, B passes ------------------------
work:   CALL tick
        DEC B
        JNZ work
        RET

; --- emulated hardware: timer clock, timeline, interrupt logic -------------
; Real hardware also saves the flags. There is no PUSHF here, so tick is
; only called where the flags are not needed afterwards.
tick:   PUSH A
        PUSH B
        MOV B, '.'              ; timeline: main runs ...
        MOV A, [INPROG]
        CMP A, 0
        JZ timer
        MOV B, '#'              ; ... or the ISR runs
timer:  MOV A, [TIMER]
        DEC A
        JNZ count
        MOV B, '|'              ; overflow
        MOV A, [TF]
        CMP A, 0
        JZ setTF
        MOV B, 'X'              ; TF still set: this tick is lost; real
                                ; hardware does this silently, no counter
setTF:  MOV [TF], 1
        MOV A, [PERIODS]
        INC A
        MOV [PERIODS], A
        MOV A, 10               ; reload
count:  MOV [TIMER], A
        MOV A, [POS]            ; write timeline character
        MOV [A], B
        INC A
        JNC nowrap              ; 255 + 1 sets carry (not zero)
        MOV A, 232
nowrap: MOV [POS], A
        POP B
        MOV A, [TF]             ; interrupt pending ...
        CMP A, 0
        JZ noIrq
        MOV A, [INPROG]         ; ... and no ISR in progress?
        CMP A, 0
        JNZ noIrq
        MOV [TF], 0             ; vectoring clears TF
        MOV [INPROG], 1
        POP A
        JMP isr                 ; isr returns to the caller of tick
noIrq:  POP A
        RET

reti:   MOV [INPROG], 0         ; RETI = RET + end of "in progress"
        RET
