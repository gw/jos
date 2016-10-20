# Lab 1

[Lab page](https://pdos.csail.mit.edu/6.828/2016/labs/lab1/)

## Part 3: The Kernel
### Formatted Printing to the Console
>5. In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen? `cprintf("x=%d y=%d", 3);`

Whatever is 4 bytes (the size of an int) after wherever 3 was placed on the stack. Garbage.


### The Stack
  >Exercise 9. Determine where the kernel initializes its stack, and exactly where in memory its stack is located. How does the kernel reserve space for its stack? And at which "end" of this reserved area is the stack pointer initialized to point to?

- entry.S:77 inits the stack pointer.
- `bootstacktop` is calculated as being KSTKSIZE bytes (PGSIZEx8 aka 4096x8 aka 0x8000) off of the beginning of the data segment, aligned on PGSHIFT (12 byte aka log2(4096)) boundaries. The link address (the address from which the kernel code expects to be executed from) of the data segment according to `i586-pc-linux-objdump -h obj/kern/kernel` is 0xf0108000 (KERNBASE, defined in memlayout.h, is f0000000); thus, `bootstacktop` ends up being 0xf0110000 (0xf0108000 + 0x8000). This is proven in the `kernel.asm` disassembly:
```
	# Set the stack pointer
	movl	$(bootstacktop),%esp
f0100034:	bc 00 00 11 f0       	mov    $0xf0110000,%esp
```
x86 stacks grow downward (towards smaller mem addresses) so the stack pointer is initialized to the highest memory address in the stack, hence the writeable data segment in entry.S:
```
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:
```

>Exercise 10. To become familiar with the C calling conventions on the x86, find the address of the test_backtrace function in obj/kern/kernel.asm, set a breakpoint there, and examine what happens each time it gets called after the kernel starts. How many 32-bit words does each recursive nesting level of test_backtrace push on the stack, and what are those words?

- x86 passes all function arguments on the stack. test_backtrace's prologue increments the stack pointer by 14 bytes. To call:
```
cprintf("entering test_backtrace %d\n", x);
```
..it pushes 8(ebp) (the rightmost param) and a pointer to the string constant in the data segment. Calling cprintf implicitly pushes the next instruction address in the frame as well.
- The call to:
```
mon_backtrace(0, 0, 0);
```
...zeroes out the top three 4-byte words on the stack, and then pushes the implicit return address, resulting in 16 bytes of used stack, the most for this function. Not sure why the prologue only increments by 14.
