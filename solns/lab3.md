# Lab 3: User Environments

[Lab page](https://pdos.csail.mit.edu/6.828/2016/labs/lab3/)

## Part A: User Envs and Exception Handling
>What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)

Some OSes use one handler entry point and check the value of a register to determine what code to actually run. That's perfectly valid, but having one handler per exception/interrupt gives us one IDT entry per exception/interrupt and thus allows us to set a different DPL for each handler. x86 will compare the DPL with the CPL in %cs and issue an exception if CPL isn't <= DPL. Thus, one handler per exception/interrupt lets us leverage hardware-level protection of these kernel functions.

>Did you have to do anything to make the user/softint program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but softint's code says int $14. Why should this produce interrupt vector 13? What happens if the kernel actually allows softint's int $14 instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

int $14 is a privileged interrupt--the corresponding gate has a DPL of 0, therefore user code cannot invoke it (device/processor interrupts compare CPL with destination code segment DPL, which in JOS is always GD_KT with DPL 0; software interrupts additionally compare the CPL with the gate DPL). When the hardware discovers the insufficient privilege, it immediately raises 0xd (General Protection) with CPL 0, which is then handled normally. We obviously should not allow user code to raise Page Faults or other arbitrary interrupts as that's a gross violation of kernel/user isolation, and could allow a malicious user process to wreak all sorts of havoc.

[this post](http://duartes.org/gustavo/blog/post/cpu-rings-privilege-and-protection/) goes into some more detail.
