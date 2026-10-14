; ---------------------------------------------------------------------------
; EdSim51 example 5: ISR work that takes longer than the interrupt period
;
; Timer 0 interrupts every 200 us (12 MHz clock = 1 us per machine cycle).
; The ISR calls work(), whose duration is selected with switch SW0 (P2.0):
;   P2.0 = 1  ->  N = 40   ->  ISR = 106 cycles (fits into the period)
;   P2.0 = 0  ->  N = 230  ->  ISR = 488 cycles (longer than two periods)
;
; Watch in EdSim51 (memory and SFR panels):
;   TICKS   (20h)  number of ISR executions
;   OVERRUN (21h)  ISR ended with TF0 already set again: next tick is late
;   MAINHI  (23h)  progress of the main loop, also shown on the LEDs (P1)
;
; The right-hand column gives the machine cycles of each instruction.
; It is the input for the static analysis in the lecture.
; ---------------------------------------------------------------------------

RELOAD  EQU 56          ; 256 - 200: Timer 0 overflows every 200 cycles
NSHORT  EQU 40          ; loop count of work() with SW0 = 1
NLONG   EQU 230         ; loop count of work() with SW0 = 0

TICKS   EQU 20H         ; variables (stack starts at 30h)
OVERRUN EQU 21H
MAINLO  EQU 22H
MAINHI  EQU 23H

        ORG 0000H
        JMP main                ; reset vector

        ORG 000BH
        JMP t0isr               ; Timer 0 interrupt vector         2

        ORG 0030H
main:   MOV SP,#2FH             ; stack 30h..7Fh, above bank 1 and variables
        MOV TICKS,#0
        MOV OVERRUN,#0
        MOV MAINLO,#0
        MOV MAINHI,#0
        MOV TMOD,#02H           ; Timer 0, mode 2: 8-bit auto-reload
        MOV TH0,#RELOAD
        MOV TL0,#RELOAD
        SETB ET0                ; enable Timer 0 interrupt
        SETB EA                 ; global interrupt enable
        SETB TR0                ; start Timer 0

loop:   INC MAINLO              ; background work: 16-bit counter  1
        MOV A,MAINLO            ;                                  1
        JNZ loop                ;                                  2
        INC MAINHI
        MOV A,MAINHI
        CPL A                   ; LEDs are active low
        MOV P1,A
        JMP loop

; --- Timer 0 ISR -----------------------------------------------------------
t0isr:  PUSH PSW                ; save flags and bank selection    2
        PUSH ACC                ;                                  2
        MOV PSW,#08H            ; ISR uses register bank 1         2
        INC TICKS               ;                                  1
        MOV R7,#NSHORT          ;                                  1
        JB P2.0,doWork          ; SW0 = 1: short work              2
        MOV R7,#NLONG           ;                                  1
doWork: CALL work               ;                                  2
        JNB TF0,isrEnd          ; timer already overflowed again?  2
        INC OVERRUN             ; yes: deadline of next tick missed 1
isrEnd: POP ACC                 ;                                  2
        POP PSW                 ; restores bank 0                  2
        RETI                    ; return, re-enable this level     2

; --- work(): busy loop, 2*R7 + 2 cycles ---------------------------------------
work:   DJNZ R7,work            ;                                  2 (x R7)
        RET                     ;                                  2

        END
