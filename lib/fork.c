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
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if(!(err & FEC_WR))
		panic("pgfault: not writable");
	if(!(vpt[(uint32_t)addr>>PGSHIFT] & PTE_COW))
		panic("pgfault: not copy-on-write");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);

	memmove(PFTEMP, (void *)PTE_ADDR(addr), PGSIZE);

	if ((r = sys_page_map(0, PFTEMP, 0, (void *)PTE_ADDR(addr),
					PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
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
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	if(vpt[pn] & (PTE_W|PTE_COW)){
		if((r = sys_page_map(0, (void *)(pn<<PGSHIFT),
					envid, (void *)(pn<<PGSHIFT),
					PTE_P|PTE_U|PTE_COW)) < 0)
			return r;
		if((r = sys_page_map(0, (void *)(pn<<PGSHIFT),
					0, (void *)(pn<<PGSHIFT),
					PTE_P|PTE_U|PTE_COW)) < 0)
			return r;
	} else {    // page is readonly
		if((r = sys_page_map(0, (void *)(pn<<PGSHIFT),
					envid, (void *)(pn<<PGSHIFT),
					PTE_P|PTE_U)) < 0)
			return r;
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
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t envid;
	uint8_t *addr;
	int r;
	extern unsigned char end[];
	int i, j, pn;

	set_pgfault_handler(pgfault);

	envid = sys_exofork();
	if(envid < 0)
		panic("sys_exofork: %e", envid);
	if(envid == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

#if 1
	for(i=0; i<PDX(UTOP); i++){
		if(!(vpd[i] & PTE_P))
			continue;

		for(j=0; j<NPTENTRIES; j++){
			pn = i*NPTENTRIES+j;
			if(pn != PGNUM(UXSTACKTOP-PGSIZE))
				duppage(envid, pn);
		}
	}
#else
	// We are the parent
	for (addr = (uint8_t*) UTEXT; addr < end; addr += PGSIZE)
		duppage(envid, (uint32_t)addr>>PGSHIFT);
	duppage(envid, ((uint32_t)&addr>>PGSHIFT));
#endif

	// process the exception stack
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);

	// setup page fault handler
	if( (r = sys_env_set_pgfault_upcall(envid,
				thisenv->env_pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: %e", r);

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
