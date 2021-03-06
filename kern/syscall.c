/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not (user_mem_assert won't return in that case)
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	struct Env *e;
	int err;

	if (err = env_alloc(&e, curenv->env_id))
		return err;

	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = curenv->env_tf;  // Copy register state
	e->env_tf.tf_regs.reg_eax = 0;  // Return 0 in child

	return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	if (!(status == ENV_RUNNABLE || status == ENV_NOT_RUNNABLE))
		return -E_INVAL;

	// Get target env, checking if
	// curenv is allowed to modify
	struct Env *e;
	int err;
	if (err = envid2env(envid, &e, 1))
		return err;

	e->env_status = status;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// Get target env, checking if
	// curenv is allowed to modify
	struct Env *e;
	int err;
	if (err = envid2env(envid, &e, 1))
		return err;

	// Check if user env is allowed to access
	// the memory location of the requested
	// callback.
	user_mem_assert(e, func, sizeof(func), 0);

	e->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Check address
	if ((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0)
		return -E_INVAL;

	// Check permissions
	if (perm & ~PTE_SYSCALL || !(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;

	// Allocate a physical page
	struct PageInfo *p;
	if ((p = page_alloc(ALLOC_ZERO)) == NULL)
		return -E_NO_MEM;

	// Get the Env struct for given env_id,
	// checking if curenv is allowed to modify
	int err;
	struct Env *e;
	if (err = envid2env(envid, &e, 1))
		return err;

	// Map newly-allocated page to target page dir
	if (err = page_insert(e->env_pgdir, p, va, perm)) {
		page_free(p);
		return err;
	}

	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Allocates a new page in the dest env's pgdir if necessary (see pgdir_walk.)
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
// If `check` is true, makes sure current env is allowed to modify
// both the src and dst envs.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     			 envid_t dstenvid, void *dstva, int perm, bool check)
{
	// Check source and dest addresses
	if ((uint32_t)srcva >= UTOP || (uint32_t)srcva % PGSIZE != 0)
		return -E_INVAL;
	if ((uint32_t)dstva >= UTOP || (uint32_t)dstva % PGSIZE != 0)
		return -E_INVAL;

	// Check permissions
	if (perm & ~PTE_SYSCALL || !(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;

	// Get source and dest Env structs,
	// checking if curenv is allowed to modify
	int err;
	struct Env *src_e;
	struct Env *dest_e;
	if (err = envid2env(srcenvid, &src_e, check))
		return err;
	if (err = envid2env(dstenvid, &dest_e, check))
		return err;

	// Look up source page
	struct PageInfo *p;
	pte_t  *pte_p;
	p = page_lookup(src_e->env_pgdir, srcva, &pte_p);
	// cprintf("[sys_page_map] srcva: %x, dstva: %x, pte: %x\n", srcva, dstva, *pte_p);

	// If caller wants to make it writable,
	// ensure it's writable in the source mapping
	if (perm & PTE_W && !(*pte_p & PTE_W))
		return -E_INVAL;

	// Map it into dest env's address space
	// Note that we're not allocating a new page
	// (not counting a new page table page, if
  // required in the dest env pgdir), simply
	// mapping an already allocated one into a new env.

	// Also note that we don't want to free it
	// on failure as we did in sys_page_alloc;
	// this page may still be used by the src env.
	if (err = page_insert(dest_e->env_pgdir, p, dstva, perm))
		return err;

	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Check address
	if ((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0)
		return -E_INVAL;

	// Get target env, checking if curenv
	// is allowed to modify
	int err;
	struct Env *e;
	if (err = envid2env(envid, &e, 1))
		return err;

	page_remove(e->env_pgdir, va);

	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	int r;

	struct Env *e;
	if (r = envid2env(envid, &e, 0))
		return r;

	if (!e->env_ipc_recving)
		return -E_IPC_NOT_RECV;

	if ((uintptr_t)srcva < UTOP && (uintptr_t)e->env_ipc_dstva < UTOP) {
		// Sender has given a potentially valid address
		// and receiver is asking for a page mapping
		if (r = sys_page_map(curenv->env_id, srcva, envid, e->env_ipc_dstva, perm, false))
			return r;
		e->env_ipc_perm = perm;
	} else {
		e->env_ipc_perm = 0;
	}

	e->env_ipc_recving = false;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_value = value;

	e->env_status = ENV_RUNNABLE;

	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	if ((uintptr_t)dstva < UTOP) {
		// env wants to receive a page mapping
		if ((uintptr_t)dstva % PGSIZE != 0 || dstva < 0)
			// but given address is not valid
			return -E_INVAL;
	}
	// NB we just have 1 big kernel
	// lock at the moment, so no need
	// to grab any other locks.

	// We set dstva regardless b/c
	// if the env is not expecting
	// a page mapping we must set it
	// to something >UTOP
	curenv->env_ipc_dstva = dstva;
	curenv->env_ipc_recving = true;
	curenv->env_status = ENV_NOT_RUNNABLE;

	// Enter scheduler--does not return. Important to note
	// that calling sched_yield doesn't push register state--
	// when this env returns, it will return via the original
	// trap frame generated by this env's syscall to sys_ipc_recv.
	// Thus, we need to manually set the return value in eax
	// BEFORE entering the scheduler...
	curenv->env_tf.tf_regs.reg_eax = 0;
	sched_yield();
	// ...because this line will never get run:
	// return 0;
	// If we don't set the return value like this, you'll
	// get a panic in lib/syscall.c:syscall because `ret`
	// will be 12, the syscallno for sys_ipc_recv, which is
	// stored in eax as an argument to the syscall instruction
	// but must then be replaced with the syscall return value.
}

static const char *syscallname(int syscallno)
{
	static const char * const names[] = {
		"cputs",
		"cgetc",
		"getenvid",
		"env_destroy",
		"page_alloc",
		"page_map",
		"page_unmap",
		"exofork",
		"env_set_status",
		"env_set_pgfault_upcall",
		"yield",
		"ipc_try_send",
		"ipc_recv"
	};

	if (syscallno < sizeof(names)/sizeof(names[0]))
		return names[syscallno];
	return "(unknown syscall)";
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	cprintf("[syscall] %d - %s\n", syscallno, syscallname(syscallno));
	switch (syscallno) {
		case SYS_cputs:
			sys_cputs((char *)a1, a2);
			return 0;

		case SYS_exofork:
			return sys_exofork();

		case SYS_env_set_status:
			return sys_env_set_status(a1, a2);

		case SYS_env_set_pgfault_upcall:
			return sys_env_set_pgfault_upcall(a1, (void *)a2);

		case SYS_page_alloc:
			return sys_page_alloc(a1, (void *)a2, a3);

		case SYS_page_map:
			return sys_page_map(a1, (void *)a2, a3, (void *)a4, a5, true);

		case SYS_page_unmap:
			return sys_page_unmap(a1, (void *)a2);

		case SYS_ipc_recv:
			return sys_ipc_recv((void *)a1);

		case SYS_ipc_try_send:
			return sys_ipc_try_send(a1, a2, (void *)a3, a4);

		case SYS_yield:
			sys_yield();
			return 0;

		// case SYS_cgetc:
		// 	return sys_cgetc();
		//
		case SYS_getenvid:
			return sys_getenvid();

		case SYS_env_destroy:
			return sys_env_destroy(a1);

		default:
			return -E_NO_SYS;
	}
}
