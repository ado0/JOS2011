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
	if(!(err & FEC_WR) || !(vpt[(uintptr_t)addr >> PGSHIFT] & PTE_COW))
		panic("pgfault: not write or not cow");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	int ret;
	if((ret = sys_page_alloc(0, (void*)PFTEMP, PTE_U|PTE_W|PTE_P)) < 0)
		panic("sys_page_alloc: %e", ret);
	memmove((void*)PFTEMP, (void*)ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if((ret = sys_page_map(0, (void*)PFTEMP, 0, (void*)ROUNDDOWN(addr, PGSIZE), PTE_U|PTE_W|PTE_P)) < 0)
		panic("sys_page_map: %e", ret);
	if((ret = sys_page_unmap(0, (void*)PFTEMP)) < 0)
		panic("sys_page_unmap: %e", ret);

//	panic("pgfault not implemented");
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
	uintptr_t addr = pn * PGSIZE;
	int perm = 0;
	int ret;
	perm = PTE_P | PTE_U;
	if (vpt[pn] & PTE_SHARE) {
		if ((ret = sys_page_map(0, (void*)addr, envid, (void*)addr, vpt[pn] & PTE_SYSCALL)) < 0)
			panic("sys_page_map: %e", ret);
		return 0;
	}
	if ((vpt[pn] & PTE_COW) || (vpt[pn] & PTE_W)) {
		perm |= PTE_COW;
		if((ret = sys_page_map(0, (void*)addr, envid, (void*)addr, perm)) < 0)
			panic("sys_page_map: %e", ret);
		if((ret = sys_page_map(0, (void*)addr, 0, (void*)addr, perm)) < 0)
			panic("sys_page_map: %e", ret);
		return 0;
	} else {
		if((ret = sys_page_map(0, (void*)addr, envid, (void*)addr, perm)) < 0)
			panic("sys_page_map: %e", ret);
	}
//	panic("duppage not implemented");
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
	int pn, pd, i, ret;
	uintptr_t addr;
	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if(envid < 0) 
		panic("sys_exofork: %e", envid);
	if(envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	} 
/*	
	for(pd = 0; pd <= PDX(UTOP); pd++)
	{
		if(vpd[pd] & PTE_P) {
			for(i = 0; i < NPTENTRIES; i++) {
				addr = (pd << PTSHIFT) + (i << PTXSHIFT);
				pn = addr >> PGSHIFT;
				if(addr < UXSTACKTOP - PGSIZE) {
					if(vpt[pn] & PTE_P)	
						duppage(envid, pn);
				}
			}
		}
	} */
	for(addr = 0; addr < UTOP - PGSIZE; addr += PGSIZE) {
		pd = PDX(addr);
		if(vpd[pd] & PTE_P) {
			pn = addr >> PGSHIFT;
			if(vpt[pn] & PTE_P)
				duppage(envid, pn);
		}
	}
	if((ret = sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P)) < 0)
		panic("sys_page_alloc: %e", ret);
	if((ret = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall)) < 0)
		panic("sys_env_pgfault_upcall: %e", ret);
	if((ret = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) 
		panic("sys_env_set_status: %e", ret);
	return envid;
//	panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
