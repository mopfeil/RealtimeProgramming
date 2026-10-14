; Example 6 for https://schweigi.github.io/assembler-simulator/
; "resume later" with nested interrupts -> stack overflow
;
; Same emulated timer as example 5 (tick, period 10, NWORK at 08h).
; This time the ISR tries to let the next tick interrupt the running work,
; so that the old work can resume afterwards. As on the 8051, a running ISR
; blocks its own level until RETI. The ISR therefore executes RETI early
; (CALL allowNesting), which clears INPROG while the ISR continues.
;
;   DB 4  : each ISR finishes before the next tick, depth 1
;   DB 25 : every tick preempts the previous ISR, which can only resume
;           after the newer one has finished. That never happens.
; Run at 256 Hz (about 2 periods/s).
;
; Console: position 0 shows SETPOINT as a character ('S'), then one '>'
; per nesting level. Memory 02h..04h: TICKS, DEPTH, MAXDEPTH.
;
; HOW TO SPOT THE ERROR
; The stack has no visible border: it simply grows (orange cells in the
; memory view) over whatever lies below it. Watch these values:
;
;   check                     correct (DB 4)        wrong (DB 25)
;   SP (register panel)       E1h..E7h, returns     -4 bytes every period
;   DEPTH (03h)               0 or 1                +1 every period
;   MAXDEPTH (04h)            1                     = number of periods
;   console '>' bar           one '>'               one more per period
;   console position 0        'S'                   'S', later 'C'
;
; When does it show?
;   - from the 2nd period on: DEPTH 2, SP moves down and never comes back
;   - depth 18: the stack reaches SETPOINT and overwrites it with a byte
;     of a return address. Nothing reports this, the program continues.
;   - depth 19: the ISR outputs the corrupted SETPOINT, console 'S' -> 'C'
;     (in a real controller: a wrong value at the actuator)
;   - entry of level 21: SP < LIMIT, the guard stops the CPU, console '!'
; Without the guard the stack would next overwrite the code (reti), and
; the CPU would execute return addresses as instructions.

        JMP main
TICKS:  DB 0            ; 02h  ISR executions
DEPTH:  DB 0            ; 03h  current interrupt nesting depth
MAXDEPTH: DB 0          ; 04h
TIMER:  DB 10           ; 05h  timer count, overflows every 10 ticks
TF:     DB 0            ; 06h  timer overflow flag
INPROG: DB 0            ; 07h  "interrupt in progress" (blocks same level)
NWORK:  DB 4            ; 08h  <-- 4 = short work, 25 = long work

main:   CALL tick               ; idle loop
        JMP main

; --- Timer ISR -------------------------------------------------------------
isr:    PUSH A                  ; context: return address + 2 bytes
        PUSH B
        CMP SP, LIMIT           ; stack guard: the only check that turns
                                ; the silent overflow into a visible stop
        JC overflow
        MOV A, [TICKS]
        INC A
        MOV [TICKS], A
        MOV A, [DEPTH]
        INC A
        MOV [DEPTH], A
        CMP A, [MAXDEPTH]       ; MAXDEPTH = max(MAXDEPTH, DEPTH)
        JNA noMax
        MOV [MAXDEPTH], A
noMax:  ADD A, 232              ; depth bar on the console
        JC noBar
        MOV [A], '>'
noBar:  MOV A, [SETPOINT]       ; output setpoint to the "actuator";
                                ; shows garbage once the stack hit it
        MOV [232], A
        CALL allowNesting       ; RETI: next tick may now preempt this ISR
        MOV B, [NWORK]
        CALL work               ; level: tick ret, A, B, work ret = 4 bytes
        MOV A, [DEPTH]
        DEC A
        MOV [DEPTH], A
        POP B
        POP A
        RET                     ; RETI was already executed in allowNesting

allowNesting:
        JMP reti                ; clears INPROG, returns into the ISR

work:   CALL tick
        DEC B
        JNZ work
        RET

overflow:
        MOV [232], '!'          ; fatal: stop the CPU
        HLT

; --- emulated hardware: timer clock + interrupt logic ----------------------
; Real hardware also saves the flags. There is no PUSHF here, so tick is
; only called where the flags are not needed afterwards.
tick:   PUSH A
        MOV A, [TIMER]
        DEC A
        JNZ count
        MOV [TF], 1             ; overflow
        MOV A, 10               ; reload
count:  MOV [TIMER], A
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

ZONE:   DB "--------"           ; spare bytes: the last level overruns LIMIT
LIMIT:  DB "--------"           ; stack guard: SP below LIMIT -> halt
SETPOINT: DB 'S'                ; application data next to the stack,
                                ; overwritten at depth 18 (long work)
