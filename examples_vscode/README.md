# Wokwi examples for VS Code

Copies of the Wokwi examples in `../examples/` (`ex01`–`ex04`, `ex07`–`ex09`) that run in the
**Wokwi for VS Code** extension instead of the browser simulator on wokwi.com.

The difference to the browser: Wokwi for VS Code does **not compile** anything.
You build the firmware locally with PlatformIO, and Wokwi simulates the resulting `.hex`/`.elf` file.

```
src/sketch.ino ──(PlatformIO: Build)──▶ .pio/build/uno/firmware.hex/.elf
                                                   │
diagram.json + wokwi.toml ──(Wokwi: Start Simulator)┘──▶ simulation, serial output, wokwi.vcd
```

## Folder layout

Each example folder is a self-contained PlatformIO project:

| File | Purpose |
|------|---------|
| `src/sketch.ino` | Copy of `../examples/<ex>/sketch.ino` |
| `diagram.json` | Copy of `../examples/<ex>/diagram.json` (circuit: Uno, logic analyzer, button, …) |
| `platformio.ini` | Build settings: board `uno`, Arduino framework, libraries |
| `wokwi.toml` | Tells Wokwi where the firmware is and where to write the logic analyzer capture |

Libraries replace `libraries.txt`:

- FreeRTOS examples (`ex01`–`ex04`, `ex09`) link the patched library directly from `../lib/FreeRTOS`
  (`lib_deps = symlink://../../lib/FreeRTOS`). Changes to the library take effect on the next build;
  no upload to wokwi.com and no library id are needed.
- `ex07_time_slicer` gets `TimerOne` from the PlatformIO registry.
- `ex08_avr_isr_nesting` needs no library.

> The sketches and diagrams are duplicates; `../examples/` is the master copy (the LaTeX script includes the files from there).
> After changing an example there, run `./sync.sh` to copy `sketch.ino` and `diagram.json` here.
> `./sync.sh --check` only lists differences (exit code 1 if any).

## One-time setup

1. Install [VS Code](https://code.visualstudio.com/).
2. Install the extensions **PlatformIO IDE** (`platformio.platformio-ide`) and
   **Wokwi Simulator** (`wokwi.wokwi-vscode`).
   PlatformIO downloads the AVR compiler and the Arduino core on the first build (takes a few minutes).
3. Activate the Wokwi license (free, needs a Wokwi account):
   `F1` → **Wokwi: Request a New License** → confirm in the browser (*Get your license*) →
   confirm again in VS Code. VS Code shows *License activated for …*.
   The free license has to be renewed from time to time; VS Code tells you when.

## Workflow

### 1. Open an example

Either open one example folder directly (*File → Open Folder… → `ex01_tasks_priorities`*),
or open all of them at once with *File → Open Workspace from File… → `examples_vscode.code-workspace`*.

Wokwi and PlatformIO both look for their config files (`wokwi.toml`, `platformio.ini`) in the root of a workspace folder.
In the multi-folder workspace:

- PlatformIO: choose the project in the status bar (folder icon, *Switch PlatformIO Project Environment*)
  or build it from the PlatformIO sidebar (*Project Tasks → ex0x_… → uno → General → Build*).
- Wokwi: the first `F1` → **Wokwi: Start Simulator** asks which `wokwi.toml` to use.
  To switch to another example later: `F1` → **Wokwi: Select Config File**.

### 2. Build

Click the ✓ (*PlatformIO: Build*) in the status bar, or run `pio run` in the example folder.
Successful output ends with `[SUCCESS]` and creates `.pio/build/uno/firmware.hex` and `firmware.elf`.

The warning `implicit declaration of function 'analogWrite'` comes from the trace macro in the
patched FreeRTOS library and is harmless.

### 3. Simulate

`F1` → **Wokwi: Start Simulator**. A tab with the circuit opens and the simulation starts.

- Click parts to interact: push button in `ex02`, DIP switch 1 in `ex07`.
- Serial output (`ex04`, `ex07`) appears in the VS Code terminal panel; you can also type input there.
- The simulation runs only while the simulator tab is visible. Keep it open, e.g. in a split editor next to the code.
- Pause/resume with the button in the simulator tab.

### 4. Edit, rebuild, restart

Change `src/sketch.ino`, build again (✓), then restart the simulation
(`F1` → **Wokwi: Start Simulator** again).
Wokwi always simulates the last built firmware: if the code change seems to have no effect, check that the build succeeded.

### 5. Look at the waveforms (logic analyzer)

The logic analyzer records while the simulation runs. When you **stop** the simulation
(close the simulator tab or `F1` → **Wokwi: Stop Simulator**), Wokwi writes `wokwi.vcd` into the example folder
(path set by `vcdFile` in `wokwi.toml`).
Open it in [Surfer](https://app.surfer-project.org/) (drag and drop the file), or in GTKWave or the
*Surfer* VS Code extension.

Channels as in the browser version: D0 = pin 10, D1 = pin 11 (one trace pin per task, HIGH while the task is running);
in `ex07`: D0–D2 = pins 13/12/11 (slot jobs), D3 = pin 10 (ISR active).

Keep the run short (a few seconds of simulated time): the logic analyzer has a limited sample buffer.

### 6. Optional: debugging with GDB

Every `wokwi.toml` already sets `gdbServerPort = 3333` (`ex09`: 3334). `ex08` and `ex09` come with a
complete debug setup, see [Stack examples with the debugger](#stack-examples-with-the-debugger-ex08-ex09).
For the other examples:

1. Install the **C/C++** extension (`ms-vscode.cpptools`) and a recent `avr-gdb`
   (the one bundled with the Arduino IDE is too old; see <https://docs.wokwi.com/vscode/debugging>).
2. Create `.vscode/launch.json` in the example folder:

   ```json
   {
     "version": "0.2.0",
     "configurations": [
       {
         "name": "Wokwi GDB",
         "type": "cppdbg",
         "request": "launch",
         "program": "${workspaceFolder}/.pio/build/uno/firmware.elf",
         "cwd": "${workspaceFolder}",
         "MIMode": "gdb",
         "miDebuggerPath": "avr-gdb",
         "miDebuggerServerAddress": "localhost:3333"
       }
     ]
   }
   ```

3. `F1` → **Wokwi: Start Simulator and Wait for Debugger**, then `F5`. Always start Wokwi first.

Note: breakpoints stop the whole simulated CPU, including the FreeRTOS tick, so the timing on the logic analyzer
is not meaningful while debugging.

## Stack examples with the debugger (ex08, ex09)

AVR versions of the 8051 example `ex06` (nested ISR, stack overflow), made to be watched in the debugger:

| Folder | Content |
|--------|---------|
| `ex08_avr_isr_nesting` | Arduino C, no RTOS. Timer 1 ISR every 1 ms re-enables interrupts (`sei`, the `allowNesting` of the 8051 version); with long work each tick nests one level deeper. The stack grows down from `RAMEND` (0x8FF) into the application data `app` at the top of the static RAM; a stack guard stops the program. |
| `ex09_rtos_isr_nesting` | The same ISR under FreeRTOS. The ISR frames land on the stack of the interrupted task (Idle), which lies on the FreeRTOS heap. DIP switch 2 switches to deferred interrupt handling (ISR only notifies a Worker task). |

Circuit: DIP switch 1 = long work (pin 2), DIP switch 2 = deferred mode (pin 3, `ex09` only),
LEDs 4..7 = actuator output of `setpoint` (all on = correct), pin 13 = stack guard / FreeRTOS stack overflow hook.

Extra files in these two folders:

| File | Purpose |
|------|---------|
| `.vscode/launch.json` | Debug configuration *Wokwi GDB + debug.gdb* (C/C++ extension, `avr-gdb`, connects to Wokwi) |
| `debug.gdb` | Breakpoints and view commands, loaded automatically after connecting |
| `platformio.ini` | `build_type = debug` (`-Og -g`): arguments, locals and FreeRTOS TCBs are visible |

### Prerequisites

- Extension **C/C++** (`ms-vscode.cpptools`).
- A recent `avr-gdb` in the `PATH` (Linux: `sudo apt install gdb-avr`; others: <https://docs.wokwi.com/vscode/debugging>).
  The `avr-gdb` in `~/.platformio/packages/toolchain-atmelavr` is too old and needs `libncurses5`.
  If `avr-gdb` is not in the `PATH`, put its full path into `miDebuggerPath` in `.vscode/launch.json`.

### Run a debug session

1. Build (✓).
2. `F1` → **Wokwi: Start Simulator and Wait for Debugger**.
3. `F5` (configuration *Wokwi GDB + debug.gdb*). The program runs normally (short work).
4. In the simulator tab, switch DIP switch 1 ON (long work); for `ex09` first choose switch 2.
   The program runs to the first breakpoint.
5. `F5` continues to the next breakpoint.

The times in the table below count from the moment the long work starts: in `ex08` right after flipping the switch,
in `ex09` at the next scheduler tick (the Ctrl task reads the switches after each tick).

To run both examples side by side, open each folder in its own VS Code window
(*File → Open Folder…*); `ex08` uses GDB port 3333, `ex09` port 3334.

PlatformIO adds its own *PIO Debug* entries to `launch.json` when it opens the project. They do not work with
Wokwi; keep *Wokwi GDB + debug.gdb* selected in the *Run and Debug* view.

### Breakpoints

The `dbg…()` functions in the sketches are empty; `debug.gdb` sets breakpoints on them. The breakpoints come from GDB,
not from the editor, so VS Code shows a hit like an exception (*Hit breakpoint N at 0x…*) and does not list them in
the *Breakpoints* view. Each hit prints a view in the **Debug Console**.

| Breakpoint | ex08 (Arduino C), long work | ex09 (FreeRTOS), long work, switch 2 OFF | Printed |
|------------|------|------|---------|
| `dbgNested` | 2 ms: first nested ISR, depth 2 | 2 ms: both levels on the Idle stack | `levels` |
| `dbgSetpointCorrupted` | 79 ms: level 79 overwrites `app.setpoint` | 7–8 ms: level 7 or 8 overwrites `app->setpoint` | `memmap`/`heap`, `levels` |
| `dbgStackOverflow` | 83 ms: own stack guard trips | – | `memmap` |
| `dbgRtosStackOverflow` | – | 16 ms, level 16: FreeRTOS notices at the next task switch, Idle SP about 250 bytes below its stack | `tasks`, `heap` |
| `dbgOverrun` | – | switch 2 ON: 3 ms, the Worker missed a tick (stacks stay flat) | `tasks` |
| `dbgTick` | every ISR entry, disabled | same | – |

Enable/disable: `-exec enable 6`, `-exec disable 3` (numbers from `-exec info breakpoints`).
You can of course also set your own breakpoints in the editor.

With the short work (switch 1 OFF) no breakpoint is hit: the depth stays 1. Enable `dbgTick` to look at single ISR entries.

### Views

**Run and Debug sidebar**

- *Variables → Locals*: the arguments of the `dbg…()` function, e.g. `level`, `sp`.
- *Variables → Registers → CPU*: `r0`–`r31`, `SREG` (bit 7 = I: interrupts enabled), `SP`, `PC`.
- *Call Stack*: `dbgNested` ← `__vector_11` (the Timer 1 ISR). GDB cannot unwind further through an interrupt; use `levels`.
- *Watch* (add with `+`), useful expressions:

  | Expression | Shows |
  |------------|-------|
  | `depth`, `maxDepth`, `ticks` | nesting depth, deepest depth, ISR entries |
  | `levelSP` | SP of each nesting level (difference = bytes per level) |
  | `levelTask` (ex09) | task whose stack each level uses |
  | `app` (ex08), `*app` (ex09) | application data; `setpoint` = 0xf0 when intact |
  | `*(unsigned char(*)[32])($sp + 1)` | the 32 bytes on top of the stack |
  | `$SREG >> 7` | I flag |
  | `*pxCurrentTCB` (ex09) | TCB of the running task: `pxStack` = start (lowest address) of its stack |

**Debug Console** (commands with the prefix `-exec`):

| Command | Shows |
|---------|-------|
| `-exec sv` | SP, SREG, depth and the 48 bytes on top of the stack (ex09: also the running task and its bytes left) |
| `-exec levels` | one line per nesting level: SP, bytes added, (ex09: task) and the code it interrupted, decoded from the return address on the stack |
| `-exec memmap` (ex08) | RAM layout: `RAMEND`, SP, `app.setpoint`, guard limit, `.bss` |
| `-exec tasks` (ex09) | all tasks: priority, stack start, SP, bytes left (negative = overflow) |
| `-exec heap` (ex09) | FreeRTOS heap in address order: stacks, TCBs, `app` |
| `-exec info registers` | all registers |
| `-exec x/64xb $sp+1` | raw stack memory |

GDB shows RAM addresses with an offset of 0x800000 (`0x8008d2` = RAM address 0x8d2):
on the AVR, flash and RAM both start at address 0, GDB needs the offset to tell them apart.

`levels` finds the return address of each level above the registers pushed by the ISR prologue.
Their number is set in `debug.gdb` (`$isrPushes`). After changing the ISR, `levels` warns if the value no
longer fits.

While the program is stopped, the whole simulated CPU stops, including the timers: when you continue,
the program carries on as if there had been no pause.

## Command line (without VS Code)

```
cd ex01_tasks_priorities
pio run                     # build
wokwi-cli --timeout 5000 .  # simulate 5 s (needs WOKWI_CLI_TOKEN from https://wokwi.com/dashboard/ci)
```

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| *Firmware not found* when starting Wokwi | Build first; check the path in `wokwi.toml` |
| Wokwi asks for a license | `F1` → **Wokwi: Request a New License** |
| Simulation stalls | Simulator tab hidden: bring it to the front |
| No `wokwi.vcd` | The file is written only when the simulation is stopped |
| `Arduino_FreeRTOS.h: No such file` | The example was moved: `lib_deps` in `platformio.ini` is a path relative to the example folder |
| Wrong example simulated in the multi-folder workspace | `F1` → **Wokwi: Select Config File** |
| `F5` fails: *Unable to start debugging* / connection refused | Start Wokwi first with **Start Simulator and Wait for Debugger**; check that the port in `launch.json` matches `gdbServerPort` in `wokwi.toml` |
| `F5` fails: `avr-gdb` not found | Install `avr-gdb` or set the full path in `miDebuggerPath` |
| No breakpoint is hit (ex08/ex09) | DIP switch 1 is OFF (short work): nothing goes wrong. Switch it ON |
