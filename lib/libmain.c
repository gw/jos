// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, uvpd, and uvpt.

#include <inc/lib.h>

extern void umain(int argc, char **argv);

const volatile struct Env *thisenv;
const char *binaryname = "<unknown>";

void
libmain(int argc, char **argv)
{
	// set thisenv to point at our Env structure in envs[].
	thisenv = envs + ENVX(sys_getenvid());

	// Below is what it used to look like. This would page fault
	// when running the `hello` binary, because `thisenv->env_id`
	// accesses the int that lives 48 bytes offset from thisenv,
	// which, when 0, results in a linear address of 48, which
	// the obj/user/hello binary does NOT map (as confirmed by
	// checking objdump, which shows that it does not alloc or
	// load anything that low, and by checking `info mem` and `info pg`
	// in the QEMU monitor, which show that the lowest mapped VA
	// is 0x200000, which is where the hello binary's symbol
	// table gets loaded.)
	// thisenv = 0;

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}
