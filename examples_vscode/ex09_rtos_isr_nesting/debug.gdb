# GDB script for ex09_rtos_isr_nesting.
# VS Code loads it after connecting to Wokwi (.vscode/launch.json,
# postRemoteConnectCommands). Without VS Code:
#   avr-gdb .pio/build/uno/firmware.elf -ex "target remote :3334" -x debug.gdb
#
# Views: type in the VS Code Debug Console with the prefix -exec
#   -exec sv        stack view: SP, SREG, running task, top of the stack
#   -exec levels    one line per nesting level: SP, task, interrupted code
#   -exec tasks     all tasks: stack start, SP, bytes left
#   -exec heap      FreeRTOS heap: task stacks, TCBs and app in address order
#
# Breakpoints (numbers as shown by "-exec info breakpoints"):
#   dbgNested             depth reaches 2: the first nested ISR
#   dbgSetpointCorrupted  a task stack has overwritten app->setpoint
#   dbgRtosStackOverflow  FreeRTOS has noticed the overflow (at a task switch)
#   dbgOverrun            deferred mode: the Worker missed a tick
#   dbgTick               every ISR entry; disabled, "-exec enable <n>"

set confirm off
set pagination off

# Bytes pushed by the prologue of the Timer 1 ISR (__vector_11): the
# hardware return address lies right above them. "levels" warns when the
# value no longer fits the code; count the push instructions with
#   -exec disassemble __vector_11
set $isrPushes = 23

define sv
  printf "SP = 0x%03x   SREG = 0x%02x (I = %d)   depth = %d   ticks = %u\n", (unsigned)$sp, $SREG, ($SREG >> 7) & 1, depth, ticks
  printf "running task: %s, its stack starts at 0x%03x: %d bytes left\n", pxCurrentTCB->pcTaskName, (unsigned)pxCurrentTCB->pxStack, (int)((unsigned)$sp - (unsigned)pxCurrentTCB->pxStack)
  printf "top of the stack, SP+1 upwards:\n"
  x/48xb $sp + 1
end
document sv
Stack view: SP, SREG, the running task and the 48 bytes on top of the stack.
end

define levels
  if depth >= 2 && levelSP[1] - levelSP[2] - 4 != $isrPushes
    printf "warning: frame is %d bytes, set $isrPushes to %d\n", levelSP[1] - levelSP[2], levelSP[1] - levelSP[2] - 4
  end
  printf "level   SP    bytes  stack of  interrupted code (return address)\n"
  set $n = 1
  while $n <= depth && $n <= 40
    if depth > 8 && $n == 4
      printf "  ...\n"
      set $n = depth - 2
    end
    set $s = levelSP[$n]
    if $n == 1
      printf "%5d  0x%03x    -   %-8s  ", $n, $s, levelTask[$n]
    else
      printf "%5d  0x%03x  %3d   %-8s  ", $n, $s, levelSP[$n - 1] - $s, levelTask[$n]
    end
    set $ra = $s + $isrPushes + 1
    set $ret = ((*(unsigned char *)$ra << 8) | *(unsigned char *)($ra + 1)) * 2
    info symbol $ret
    set $n = $n + 1
  end
end
document levels
One line per active nesting level: SP after the ISR prologue, stack bytes
the level added, the task whose stack it uses and the code it interrupted.
Above 8 levels only the first and the last three.
end

define showtask
  set $t = (TCB_t *)$arg0
  if $t == pxCurrentTCB
    set $tsp = (unsigned)$sp
    printf "* "
  else
    set $tsp = (unsigned)$t->pxTopOfStack
    printf "  "
  end
  printf "%-7s  %d     0x%03x      0x%03x  %5d\n", $t->pcTaskName, $t->uxPriority, (unsigned)$t->pxStack, $tsp, (int)($tsp - (unsigned)$t->pxStack)
end

define tasks
  printf "  task     prio  stack start  SP     bytes left (< 0: overflow)\n"
  showtask hWorker
  showtask hCtrl
  showtask xIdleTaskHandle
  showtask xTimerTaskHandle
  printf "* running, SP = CPU register SP; others: SP saved in the TCB\n"
  printf "depth = %d   ticks = %u   overruns = %u   tick count = %u\n", depth, ticks, overruns, xTickCount
end
document tasks
All tasks with priority, start of the stack, SP and the bytes left before
the stack overflows.
end

define heap
  printf "FreeRTOS heap, ascending addresses (= order of allocation).\n"
  printf "Each stack grows down towards the block below it.\n"
  printf "0x%03x  Worker stack (128 bytes)\n", (unsigned)((TCB_t *)hWorker)->pxStack
  printf "0x%03x  Worker TCB\n", (unsigned)hWorker
  printf "0x%03x  Ctrl stack (128 bytes)\n", (unsigned)((TCB_t *)hCtrl)->pxStack
  printf "0x%03x  Ctrl TCB\n", (unsigned)hCtrl
  printf "0x%03x  app->buffer[384]\n", (unsigned)app
  printf "0x%03x  app->setpoint = 0x%02x (0xf0 = intact)\n", (unsigned)&app->setpoint, app->setpoint
  printf "0x%03x  Idle stack (192 bytes)\n", (unsigned)((TCB_t *)xIdleTaskHandle)->pxStack
  printf "0x%03x  Idle TCB\n", (unsigned)xIdleTaskHandle
  printf "SP = 0x%03x (task %s)\n", (unsigned)$sp, pxCurrentTCB->pcTaskName
end
document heap
FreeRTOS heap layout: task stacks, TCBs and the application data.
end

break dbgNested
commands
  levels
end
break dbgSetpointCorrupted
commands
  heap
  levels
end
break dbgRtosStackOverflow
commands
  tasks
  heap
end
break dbgOverrun
commands
  tasks
end
break dbgTick
disable $bpnum
