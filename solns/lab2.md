# Lab 2: Memory Management
## Part 1: Physical Page Management
###
>5. In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen? `cprintf("x=%d y=%d", 3);`

Whatever is 4 bytes (the size of an int) after wherever 3 was placed on the stack. Garbage.
