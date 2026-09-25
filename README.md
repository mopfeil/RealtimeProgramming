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
| `examples/` | Wokwi projects (`sketch.ino`, `diagram.json`, `libraries.txt`), included in the text via `\wokwiexample` |
| `lib/FreeRTOS/` | Patched Arduino FreeRTOS library used in the examples |

## Wokwi examples

Each example is an Arduino Uno project with a logic analyzer on D0/D1 (pins 10/11).
The patched library drives one trace pin per task: `traceTASK_SWITCHED_IN/OUT` (in `Arduino_FreeRTOS.h`) set the pin stored in the task tag (`vTaskSetApplicationTaskTag`) HIGH when the task is switched in and LOW when it is switched out. Untagged tasks (e.g. idle) use tag NULL = pin 0.
The patched library in `lib/FreeRTOS` must be added to the Wokwi project.
Reference project: <https://wokwi.com/projects/446797093390429185>

> Status: the examples have not yet been run in the Wokwi simulator.
