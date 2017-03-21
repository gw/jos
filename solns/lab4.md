# Lab 4: Pre-emptive Multitasking

[Lab page](https://pdos.csail.mit.edu/6.828/2016/labs/lab4/)

## Part A

> Compare kern/mpentry.S side by side with boot/boot.S. Bearing in mind that kern/mpentry.S is compiled and linked to run above KERNBASE just like everything else in the kernel, what is the purpose of macro MPBOOTPHYS? Why is it necessary in kern/mpentry.S but not in boot/boot.S? In other words, what could go wrong if it were omitted in kern/mpentry.S?

boot.S doesn't need it because BIOSes always load it at `0x7c00` AND it's linked at `0x7c00`:
```shell
$ objdump -h obj/boot/boot.out
obj/boot/boot.out:     file format elf32-i386

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         0000017c  00007c00  00007c00  00000074  2**2
                  CONTENTS, ALLOC, LOAD, CODE
  1 .eh_frame     000000b0  00007d7c  00007d7c  000001f0  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         000007b0  00000000  00000000  000002a0  2**2
                  CONTENTS, READONLY, DEBUGGING
  3 .stabstr      00000846  00000000  00000000  00000a50  2**0
                  CONTENTS, READONLY, DEBUGGING
  4 .comment      0000002b  00000000  00000000  00001296  2**0
                  CONTENTS, READONLY
```
Also see: `boot/Makefrag`.

Thus, no relocations are required b/c boot.S runs from the same physical memory into which it is initially loaded.

However, `mpentry.S` is compiled into the *kernel* image--thus, it is *loaded* at `0x100000` (physical) but *runs* from `0xf0100000` (virtual). It's common for OS kernels to run from high addresses like that b/c it leaves the bottom portion of virtual memory free for user procs. However, the machine might not have that much physical memory, so the kernel gets loaded at 1mb in physmem, just above BIOS ROM, hence the indirection. (see [Lab 1](https://pdos.csail.mit.edu/6.828/2016/labs/lab1/)).

This means that the linker binds address references in `mpentry.S` to high addresses relative to `0xf0100000` just like everything else in the kernel image. This would be fine once paging is enabled, but `mpentry.S` *is* the code that sets up paging for APs. Indeed, APs start in real mode, and thus can only run code below 640kb (in Low Memory).

This means we have to copy the `mpentry.S` code to `MPENTRY_PADDR` (0x7000), but any unused page-aligned physical address below 640kb would work. Thus, its "load address" is `MPENTRY_PADDR`, but it's link address remains unchanged from when the kernel image was linked: `0xf0100000`. This means we have to do some manual relocation--we achieve this with the `MPBOOTPHYS` macro which simply re-calculates absolute memory references in `mpentry.S` to be based off `MPENTRY_PADDR` instead of `0xf0100000`.

We could maybe achieve the same effect by putting `mpentry.S` in a different section of the kernel image so we can give it a different link address.


>It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock.

Even if one kernel thread holds the lock it can still be interrupted. When that happens, the x86 hardware pushes a trapframe onto whatever kernel stack is in the TSS. If we only had one big kernel stack shared b/w all CPUs, then if one thread is in the kernel, other threads could be simultaneously pushing trap frames onto the stack and clobbering it.

>In your implementation of env_run() you should have called lcr3(). Before and after the call to lcr3(), your code makes references (at least it should) to the variable e, the argument to env_run. Upon loading the %cr3 register, the addressing context used by the MMU is instantly changed. But a virtual address (namely e) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer e be dereferenced both before and after the addressing switch?

Because the JOS kernel maps its own VM mappings (linear->physical mappings for high-address kernel code) into the address space of EVERY newly allocated environment. Thus, switching to a new env's page dir won't change how kernel variables are mapped.

>Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen?

It's gotta save execution thread state (register state, stack) so the environment can be re-run later. JOS gets some help from the x86 hardware, which pushes register state into the trapframe on the kernel stack on every trap before giving control to the kernel. In `trap()`, `curenv->env_tf = *tf;` takes this kernel stack state and saves it on the Env struct.

## Notable Bugs
### Double-Acquiring Kernel Lock on multi-CPU startup
Running `make qemu-nox CPUS=2` gave this output:
```shell
***
*** Use Ctrl-a x to exit qemu
***
qemu-system-i386 -nographic -hda obj/kern/kernel.img -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 2
6828 decimal is 15254 octal!
Physical memory: 66556K available, base = 640K, extended = 65532K
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 2 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 0 starting
kernel panic on CPU 0 at kern/spinlock.c:65: CPU 0 cannot acquire &kernel_lock: already holding
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K>
```
`mp_main`, which should only run on APs, was running on the BSP, for some reason. Thus CPU0 would try to acquire the kernel lock which it already held from its acquisition in `i386_init`.


Took a while to figure out, but eventually traced it to trap.c:126. It was previously
```c
ltr(GD_TSS0);
```

but SHOULD be:
```c
ltr(GD_TSS0 + (cpu_id * sizeof(struct Segdesc)));
```

The `ltr` x86 instruction loads a new TSS Selector into the Task Register. This selector indexes into the GDT--the entry there points to the actual TSS to use. It sets a "busy" bit on said TSS to prevent another CPU from loading it simultaneously, but does not induce an actual task switch.

`GD_TSS0` is the TSS Selector for CPU0's TSS (we allocate one per CPU in the GDT in env.c). Thus, while booting on an AP, we were loading the TSS of the BSP. Stepping across this instruction on an AP in GDB caused a thread switch back to the BSP, at which point it would be executing `mp_main`, for some reason, instead of spinning at the bottom of `boot_aps`. Still don't understand why.

[more](https://pdos.csail.mit.edu/6.828/2010/readings/i386/s07_01.htm) on the TSS, TSS Selectors, TSS Descriptors.
