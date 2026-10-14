# Realtime Programming

Lecture on realtime programming, based on the script *RTP-Master24* (Prof. Dr. Markus Pfeil).
One LaTeX source builds either the **script** or the **slides**.

## Build

    make script   # build/script.pdf
    make slides   # build/slides.pdf

Or set `\providecommand{\lecturemode}{script|slides}` at the top of `main.tex`.
Requires `pdflatex`, `latexmk`, `beamer`, and the `metropolis` beamer theme.

## Layout

| Path | Content |
|------|---------|
| `main.tex`, `preamble.tex` | Mode switch, common setup |
| `chapters/` | Lecture content; `\scriptonly{}` / `\slidesonly{}` mark mode-specific text |
| `examples/` | Wokwi projects (`sketch.ino`, `diagram.json`, `libraries.txt`), included in the text via `\wokwiexample`; `ex05_8051_*`, `ex06_8051_*`: EdSim51 assembler programs (`.asm`), included via `\edsimexample` |
| `lib/FreeRTOS/` | Patched Arduino FreeRTOS library used in the examples |

## Wokwi examples

Each example is an Arduino Uno project with a logic analyzer on D0/D1 (pins 10/11).
The patched library drives one trace pin per task: `traceTASK_SWITCHED_IN/OUT` (in `Arduino_FreeRTOS.h`) set the pin stored in the task tag (`vTaskSetApplicationTaskTag`) HIGH when the task is switched in and LOW when it is switched out. Untagged tasks (e.g. idle) use tag NULL = pin 0.
The patched library in `lib/FreeRTOS` is uploaded to Wokwi and referenced in each `libraries.txt` as `FreeRTOS@wokwi:<id>`. After changing `lib/FreeRTOS`, upload it again (Library Manager → "+" → Upload a Library) and update the id in all `libraries.txt` files.
Waveforms: Wokwi exports the logic analyzer capture automatically; open it in <https://app.surfer-project.org/>.
Reference project: <https://wokwi.com/projects/446797093390429185>

Examples:
- `ex01`–`ex04`: FreeRTOS (tasks, ISR + semaphore, queue, run-time statistics)
- `ex07_time_slicer`: binary time slicer without RTOS (TimerOne library), chapter `03-time-slicer.tex`. D0–D2 = pins 13/12/11 (slot jobs), D3 = pin 10 (ISR active); DIP switch 1 makes slot 2 longer than a time slice. Based on <https://wokwi.com/projects/414976138190111745>.

## EdSim51 examples (8051 assembler)

The machine-level part (`chapters/02-machine-level.tex`: execution, interrupts, static analysis) uses the 8051 simulator EdSim51 (<https://www.edsim51.com/>) instead of Wokwi, because the effects are visible on the level of instructions, stack bytes and machine cycles.
Load the `.asm` file, press *Assm*, then *Step* or *Run*. Switch SW0 (P2.0) selects short or long ISR work.

- `ex05_8051_isr_overrun`: ISR longer than the timer period → lost ticks, starved main loop
- `ex06_8051_stack_overflow`: re-enabling the interrupt inside the ISR (nesting) → stack overflow

Both folders also contain a version for the browser simulator <https://schweigi.github.io/assembler-simulator/> (`*_sim8.asm`), which needs no installation.
That CPU has no timer, interrupts or input pins, so the subroutine `tick` emulates timer and interrupt logic (called once per loop pass, period 10 ticks).
Paste the file into the editor, edit `NWORK` (4 = short work, 25 = long work), press *Assemble* and *Run* (speed 256 Hz or more).
The console shows a tick timeline (example 5) or SETPOINT plus a nesting-depth bar (example 6).

> Status: the Wokwi examples have not yet been run in the Wokwi simulator. The EdSim51 examples were checked with a cycle-accurate model of the used instructions, not yet in EdSim51 itself. The `*_sim8.asm` versions were run with the simulator's original assembler and CPU code (`asm.js`, `cpu.js`) under Node.js, not yet in the browser UI.
