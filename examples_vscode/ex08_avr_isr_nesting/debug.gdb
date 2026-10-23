# GDB script for ex08_avr_isr_nesting.
# VS Code loads it after connecting to Wokwi (.vscode/launch.json,
# postRemoteConnectCommands). Without VS Code:
#   avr-gdb .pio/build/uno/firmware.elf -ex "target remote :3333" -x debug.gdb
#
# Views: type in the VS Code Debug Console with the prefix -exec
#   -exec sv        stack view: SP, SREG, top of the stack
#   -exec levels    one line per nesting level: SP and interrupted code
#   -exec memmap    RAM layout: variables, guard, SP
#
# Breakpoints (numbers as shown by "-exec info breakpoints"):
#   dbgNested             depth reaches 2: the first nested ISR
#   dbgSetpointCorrupted  the stack has overwritten app.setpoint
#   dbgStackOverflow      the stack guard has tripped
#   dbgTick               every ISR entry; disabled, "-exec enable <n>"

set confirm off
set pagination off

# Bytes pushed by the prologue of the Timer 1 ISR (__vector_11): the
# hardware return address lies right above them. "levels" warns when the
# value no longer fits the code; count the push instructions with
#   -exec disassemble __vector_11
set $isrPushes = 18

define sv
  printf "SP = 0x%03x   SREG = 0x%02x (I = %d)   depth = %d   ticks = %u\n", (unsigned)$sp, $SREG, ($SREG >> 7) & 1, depth, ticks
  printf "stack used: %d bytes (RAMEND 0x8ff - SP)\n", 0x8ff - (unsigned)$sp
  printf "top of the stack, SP+1 upwards:\n"
  x/48xb $sp + 1
end
document sv
Stack view: SP, SREG, nesting depth and the 48 bytes on top of the stack.
end

define levels
  if depth >= 2 && levelSP[1] - levelSP[2] - 4 != $isrPushes
    printf "warning: frame is %d bytes, set $isrPushes to %d\n", levelSP[1] - levelSP[2], levelSP[1] - levelSP[2] - 4
  end
  printf "level   SP    bytes  interrupted code (return address on the stack)\n"
  set $n = 1
  while $n <= depth && $n <= 100
    if depth > 8 && $n == 4
      printf "  ...\n"
      set $n = depth - 2
    end
    set $s = levelSP[$n]
    if $n == 1
      printf "%5d  0x%03x    -   ", $n, $s
    else
      printf "%5d  0x%03x  %3d   ", $n, $s, levelSP[$n - 1] - $s
    end
    set $ra = $s + $isrPushes + 1
    set $ret = ((*(unsigned char *)$ra << 8) | *(unsigned char *)($ra + 1)) * 2
    info symbol $ret
    set $n = $n + 1
  end
end
document levels
One line per active nesting level: SP after the ISR prologue, stack bytes
the level added and the code it interrupted. Above 8 levels only the
first and the last three. All of them: levelsall
end

define levelsall
  set $n = 1
  while $n <= depth && $n <= 100
    printf "%5d  0x%03x\n", $n, levelSP[$n]
    set $n = $n + 1
  end
end

define memmap
  printf "0x8ff  RAMEND, the stack starts here and grows down\n"
  printf "0x%03x  SP (stack used: %d bytes)\n", (unsigned)$sp, 0x8ff - (unsigned)$sp
  printf "       free: %d bytes (negative: the stack has passed app.setpoint)\n", (int)((unsigned)$sp - (unsigned)&app.setpoint)
  printf "0x%03x  app.setpoint = 0x%02x (0xf0 = intact)\n", (unsigned)&app.setpoint, app.setpoint
  printf "0x%03x  STACK_LIMIT, guard trips when SP is below\n", (unsigned)&app + 24
  printf "0x%03x  app.buffer[96] (.noinit)\n", (unsigned)&app
  printf "0x%03x  .bss: depth, ticks, levelSP[], millis counters ...\n", (unsigned)&__bss_start
end
document memmap
RAM layout: stack, application data, guard limit.
end

break dbgNested
commands
  levels
end
break dbgSetpointCorrupted
commands
  memmap
  levels
end
break dbgStackOverflow
commands
  memmap
end
break dbgTick
disable $bpnum
