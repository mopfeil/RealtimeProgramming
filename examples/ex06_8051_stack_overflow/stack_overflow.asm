; ---------------------------------------------------------------------------
; EdSim51 example 6: "resume later" with nested interrupts -> stack overflow
;
; Same timing as example 5: Timer 0 every 200 us, work() selected by SW0.
; This time the ISR tries to let the *next* tick interrupt the running work
; so that the old work can resume where it left off afterwards.
; The 8051 blocks interrupts of the same level until RETI. The ISR therefore
; executes RETI early (CALL allowNesting), which re-enables Timer 0
; interrupts while the ISR continues.
;
;   P2.0 = 1 (short work) : each ISR finishes before the next tick, depth 1
;   P2.0 = 0 (long work)  : every tick preempts the previous ISR, which can
;                           only resume after the newer one has finished.
;                           That never happens: each level pushes 7 bytes,
;                           the stack grows into SETPOINT (60h) and the LEDs
;                           show garbage. At SP >= 78h the stack guard stops
;                           the program with all LEDs on.
;
; Watch: SP, TICKS (20h), DEPTH (21h), MAXDEPTH (22h), RAM 30h..7Fh, LEDs.
; ---------------------------------------------------------------------------

RELOAD   EQU 56          ; 256 - 200: Timer 0 overflows every 200 cycles
NSHORT   EQU 40
NLONG    EQU 230
STACKLIM EQU 78H         ; stack guard: SP must stay below this address

TICKS    EQU 20H
DEPTH    EQU 21H         ; current interrupt nesting depth
MAXDEPTH EQU 22H
SETPOINT EQU 60H         ; application data located above the stack

        ORG 0000H
        JMP main

        ORG 000BH
        JMP t0isr

        ORG 0030H
main:   MOV SP,#2FH             ; stack grows upwards from 30h
        MOV TICKS,#0
        MOV DEPTH,#0
        MOV MAXDEPTH,#0
        MOV SETPOINT,#0F0H      ; value for the "actuator" (LEDs 4..7 on)
        MOV TMOD,#02H           ; Timer 0, mode 2: 8-bit auto-reload
        MOV TH0,#RELOAD
        MOV TL0,#RELOAD
        SETB ET0
        SETB EA
        SETB TR0
idle:   JMP idle

; --- Timer 0 ISR -----------------------------------------------------------
t0isr:  PUSH ACC                ; context: 2 bytes PC (hardware) + 3 bytes
        PUSH PSW
        PUSH 07H                ; R7 of bank 0, used by work()
        MOV A,SP                ; stack guard
        CLR C
        SUBB A,#STACKLIM
        JNC stackOverflow
        INC TICKS
        INC DEPTH
        MOV A,DEPTH             ; MAXDEPTH = max(MAXDEPTH, DEPTH)
        CLR C
        SUBB A,MAXDEPTH
        JC noMax
        MOV MAXDEPTH,DEPTH
noMax:  MOV A,SETPOINT          ; output setpoint to the actuator
        CPL A                   ; LEDs are active low
        MOV P1,A
        CALL allowNesting       ; RETI: next tick may now preempt this ISR
        MOV R7,#NSHORT
        JB P2.0,doWork
        MOV R7,#NLONG
doWork: CALL work               ; + 2 bytes return address = 7 bytes/level
        DEC DEPTH
        POP 07H
        POP PSW
        POP ACC
        RET                     ; RETI was already executed in allowNesting

allowNesting:
        RETI                    ; clears "interrupt in progress", returns

work:   DJNZ R7,work
        RET

stackOverflow:
        CLR EA                  ; fatal: stop all interrupts
        MOV P1,#00H             ; all LEDs on
halt:   JMP halt

        END
