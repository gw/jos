// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	// cprintf("page fault at %x, eip %x\n", utf->utf_fault_va, utf->utf_eip);
	uint32_t err = utf->utf_err;
	if (!(err & FEC_WR))
		panic("[fork] pgfault received fault that wasn't a write\n");

	void *flt_addr = (void *) utf->utf_fault_va;
	if (!(uvpt[PGNUM(flt_addr)] & PTE_COW))
		panic("[fork] pgfault received a write fault for non-COW page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	int r;
	// Allocate new page
	if (r = sys_page_alloc(0, PFTEMP, PTE_U|PTE_P|PTE_W))
		panic("[fork] pgfault:sys_page_alloc failed %x for addr: %x", r, flt_addr);

	// Copy COW page contents into newly-allocated page
	memcpy(PFTEMP, ROUNDDOWN(flt_addr, PGSIZE), PGSIZE);

	// Insert newly-allocated-and-populated page into the place of the COW page
	if (r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(flt_addr, PGSIZE), PTE_U|PTE_P|PTE_W))
		panic("[fork] pgfault:sys_page_map failed %x for addr: %x", r, flt_addr);

	// De-allocate
	if (r = sys_page_unmap(0, PFTEMP))
		panic("[fork] pgfault:sys_page_unmap failed %x for addr: %x", r, PFTEMP);
}

//
// Map our virtual address va into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, uintptr_t va)
{
	int r;
	uint32_t perm = uvpt[PGNUM(va)] & PTE_SYSCALL;
	if (perm & PTE_W || perm & PTE_COW) {
		// Writable
		// Mark COW in child
		if (r = sys_page_map(0, (void *)va, envid, (void *)va, PTE_U|PTE_P|PTE_COW))
			panic("duppage: sys_page_map failed for %x: %d\n", va, r);
		// Mark COW in parent (b/c it may have just been W)
		if (r = sys_page_map(0, (void *)va, 0, (void *)va, PTE_U|PTE_P|PTE_COW))
			panic("duppage: sys_page_map failed for %x: %d\n", va, r);
	} else {
		// Read-only
		if (r = sys_page_map(0, (void *)va, envid, (void *)va, PTE_U|PTE_P))
			panic("duppage: sys_page_map failed for %x: %d\n", va, r);
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	set_pgfault_handler(pgfault);

	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid_t envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e\n", envid);

	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.
	// Copy address space
	uintptr_t va;
	for (va = 0; va < USTACKTOP; va += PGSIZE) {
		// TODO: Could optimize so if the PDE is not present,
		// skip the whole thing instead of still looping through
		// all its PTEs.
		if (uvpd[PDX(va)] & PTE_P &&  // see memlayout.h for uvpd/uvpt explanation
				uvpd[PDX(va)] & PTE_U &&
		  	uvpt[PGNUM(va)] & PTE_P &&
		  	uvpt[PGNUM(va)] & PTE_U) {
			duppage(envid, va);
		}
	}

	// Allocate a new user exception stack for the child
	sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U|PTE_P|PTE_W);

	// Assembly language pgfault entrypoint defined in lib/pfentry.S.
	// Why can't this call to set_pgfault_upcall be executed in the child
	// code above, before fixing `thisenv`?
	// If you look at the assembly for lib.h:sys_exofork, you'll notice it
	// makes the `int` syscall and then immediately modifies a stack
	// variable.
	// When handling sys_exofork calls, the kernel allocates a new env
	// and copies the parent's register state to it (see syscall.c:sys_exofork).
	// The parent then returns from sys_exofork above and copies its address space
	// into the child and marks all writeable pages COW.
	// Thus, when the child finally runs (after the parent returns from `fork`)
	// it will try to return from sys_exofork (just as the parent did, except with
	// a 0 return value) and thus try to modify the aforementioned stack variable.
	// This stack page was just marked COW instead of W, which triggers a
	// userland page fault, but it hasn't gotten the chance to execute any of the
	// code after the `sys_exofork` call, and thus it cannot register its own
	// page fault handler--the parent must do it before the child can return
	// from `sys_exofork`.
	extern void _pgfault_upcall(void);
	int r;
	if (r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall))
		panic("[fork] sys_env_set_pgfault_upcall: %x", r);

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
