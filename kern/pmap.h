/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PMAP_H
#define JOS_KERN_PMAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/memlayout.h>
#include <inc/assert.h>
struct Env;

extern char bootstacktop[], bootstack[];

extern struct PageInfo *pages;
extern size_t npages;

extern pde_t *kern_pgdir;


/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 *
 * Just subtracts KERNBASE from the given kva.
 */
#define PADDR(kva) _paddr(__FILE__, __LINE__, kva)

static inline physaddr_t
_paddr(const char *file, int line, void *kva)
{
	if ((uint32_t)kva < KERNBASE)
		_panic(file, line, "PADDR called with invalid kva %08lx", kva);
	return (physaddr_t)kva - KERNBASE;
}

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It panics if you pass an invalid physical address.
 *
 * Just adds KERNBASE to the given pa and casts to void *.
 */
#define KADDR(pa) _kaddr(__FILE__, __LINE__, pa)

static inline void*
_kaddr(const char *file, int line, physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		_panic(file, line, "KADDR called with invalid pa %08lx", pa);
	return (void *)(pa + KERNBASE);
}


enum {
	// For page_alloc, zero the returned physical page.
	ALLOC_ZERO = 1<<0,  // TODO what's the point of shifting this 0?
};

void	mem_init(void);

void	page_init(void);
struct PageInfo *page_alloc(int alloc_flags);
void	page_free(struct PageInfo *pp);
int	page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm);
void	page_remove(pde_t *pgdir, void *va);
struct PageInfo *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store);
void	page_decref(struct PageInfo *pp);

void	tlb_invalidate(pde_t *pgdir, void *va);

void *	mmio_map_region(physaddr_t pa, size_t size);

int	user_mem_check(struct Env *env, const void *va, size_t len, int perm);
void	user_mem_assert(struct Env *env, const void *va, size_t len, int perm);

/* PageInfo* -> PFA of the page it corresponds to
 *
 * Get pointer's offset from start of pages array and shift left 12, converting
 * the lowest 20 bits of the offset into the top 20 bits of the physical address
 * (aka the PFN), with 12 low-order zeros for the index into the page, resulting
 * in the PA of the start of the page frame in physical memory.
 */
static inline physaddr_t
page2pa(struct PageInfo *pp)
{
	// pp and pages are pointers of the same type. Thus, subraction scales; i.e.
	// (pp - pages) evaluates to the number of `PageInfo` structs between the two
	// addresses, exclusive.
	// (pp - pages) assembles to subtracting the addresses and then >>3. This
	// divides by 8, which gives you the number of PageInfo structs between
	// the two addresses. sizeof(PageInfo) == 8 because it needs 2 bytes of
	// trailing padding to be self-aligned.
	return (pp - pages) << PGSHIFT;
}

/* PA -> PageInfo*
 *
 * Get PFA from a given PA, and use that to index into the pages array. i.e.
 * the PageInfo struct for a given PFA is at pages[pa >> 12].
 */
static inline struct PageInfo*
pa2page(physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		panic("pa2page called with invalid pa");

	return &pages[PGNUM(pa)];
}

/* PageInfo* -> KVA
 *
 * Get PA, then add KERNBASE to get KVA
 */
static inline void*
page2kva(struct PageInfo *pp)
{
	return KADDR(page2pa(pp));
}

pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);

#endif /* !JOS_KERN_PMAP_H */
