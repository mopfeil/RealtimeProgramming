# Wokwi examples for VS Code

Copies of the Wokwi examples in `../examples/` (`ex01`–`ex04`, `ex07`) that run in the
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

- FreeRTOS examples (`ex01`–`ex04`) link the patched library directly from `../lib/FreeRTOS`
  (`lib_deps = symlink://../../lib/FreeRTOS`). Changes to the library take effect on the next build;
  no upload to wokwi.com and no library id are needed.
- `ex07_time_slicer` gets `TimerOne` from the PlatformIO registry.

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

Every `wokwi.toml` already sets `gdbServerPort = 3333`. To step through the code:

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
