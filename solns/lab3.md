# Lab 3: User Environments

[Lab page](https://pdos.csail.mit.edu/6.828/2016/labs/lab3/)

## Part A: User Envs and Exception Handling
>What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)

Some OSes use one handler entry point and check the value of a register to determine what code to actually run. That's perfectly valid, but having one handler per exception/interrupt gives us one IDT entry per exception/interrupt and thus allows us to set a different DPL for each handler. x86 will compare the DPL with the CPL in %cs and issue an exception if CPL isn't <= DPL. Thus, one handler per exception/interrupt lets us leverage hardware-level protection of these kernel functions.

>Did you have to do anything to make the user/softint program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but softint's code says int $14. Why should this produce interrupt vector 13? What happens if the kernel actually allows softint's int $14 instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

int $14 is a privileged interrupt--the corresponding gate has a DPL of 0, therefore user code cannot invoke it (device/processor interrupts compare CPL with destination code segment DPL, which in JOS is always GD_KT with DPL 0; software interrupts additionally compare the CPL with the gate DPL). When the hardware discovers the insufficient privilege, it immediately raises 0xd (General Protection) with CPL 0, which is then handled normally. We obviously should not allow user code to raise Page Faults or other arbitrary interrupts as that's a gross violation of kernel/user isolation, and could allow a malicious user process to wreak all sorts of havoc.

[this post](http://duartes.org/gustavo/blog/post/cpu-rings-privilege-and-protection/) goes into some more detail.

>Exercise 9 backtrace page fault reason?
This is the output of backtrace after running user/breakpoint:

```
K> backtrace
Stack backtrace:
  ebp effffe90  eip f0100d5e  args 00000001 effffeb0 f01ba000 0000000d f0107d9c
       kern/monitor.c:142: runcmd+300
  ebp efffff00  eip f0100ddb  args f01979e9 f01ba000 efffff40 f01042ca f0104280
       kern/monitor.c:162: monitor+86
  ebp efffff30  eip f0104f98  args f01ba000 00000000 efffff70 f01042ed f010791c
       kern/trap.c:179: trap_dispatch+109
  ebp efffff70  eip f0105100  args f01ba000 efffffbc 00000003 00000000 00001000
       kern/trap.c:237: trap+208
  ebp efffffb0  eip f0105212  args f01ba000 00000000 00000000 eebfdfd0 efffffdc
       kern/trapentry.S:83: <unknown>+0
  ebp eebfdfd0  eip 00800083  args 00000000 00000000 00000000 00000000 00000000
       <unknown>:0: <unknown>+0
  ebp eebfdff0  eip 00800031  args 00000000 00000000Incoming TRAP frame at 0xeffffe1c, trapno 14
kernel panic at kern/trap.c:172: Kernel mode PGFault! Dying!
```
What's happening here?
- Backtrace is walking the call stack, starting with the monitor invocation of `backtrace` in kernel-land and going into the userland calls.
- We get to the first stack frame below USTACKTOP, and start reading off arguments, but there aren't enough, so when we get to the third arg we try to dereference a memory address above USTACKTOP that we haven't mapped, so the hardware issues a page fault.
