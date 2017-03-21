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

