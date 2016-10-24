# Lab 3: User Environments

[Lab page](https://pdos.csail.mit.edu/6.828/2016/labs/lab3/)

## Part A: User Envs and Exception Handling
### Notes
- Like a Unix process, JOS environments couple the concepts of "thread" and "address space". A thread is primarily defined by its saved registers (`env_tf`) and an address space is defined by the page directory (`env_pgdir`). To run an environment, JOS needs to set up the CPU with BOTH the saved registers and the saved address space.
