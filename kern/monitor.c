// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include<kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "backtrace the stack", mon_backtrace},
	{ "show_map", "show mapping between lva and uva", mon_showmappings},
	{ "set_perm", "change pte perm at specific address", mon_chperm},
	{ "mem_dump", "dump the memory content", mon_dump},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uintptr_t *ebp, *eip;
	int i;
	struct Eipdebuginfo info;
	uint32_t args[5];
	cprintf("Stack backtrace:\n");
	ebp =(uintptr_t*) read_ebp();
	while(ebp != 0)
	{
		eip = ebp+1;
		for(i = 0; i < 5; i++)
			args[i] = *(ebp+2+i);
		
		cprintf(" ebp %08x eip %08x args %08x %08x %08x %08x %08x\n", ebp, *eip, 
			args[0], args[1], args[2], args[3], args[4]);
		debuginfo_eip(*eip, &info);
		cprintf("     %s:%d: %.*s+%d\n",info.eip_file, info.eip_line,
			info.eip_fn_namelen, info.eip_fn_name, (*eip)-info.eip_fn_addr);
		ebp = (uintptr_t*)(*ebp);
	}
	return 0;
}

//@author: Daochen Liu(dcliu@mail.ustc.edu.cn)
//2014.9.29
//implement showmapping
void showmappings(uintptr_t lva, uintptr_t uva);
int mon_showmappings(int argc, char** argv, struct Trapframe *tf)
{
	if(argc != 3) {
		cprintf("Usage: showmappings LOWADDR, UPPERADDR\n");
		cprintf("Both address must be aligned in 4KB\n");
		return 0;
	}
	uintptr_t lva = strtol(argv[1], 0, 0);
	uintptr_t uva = strtol(argv[2], 0, 0);
	if(lva > uva)	{
		cprintf("LOWADDR must be less than UPPERADDR\n");
		return 0;
	}
	showmappings(lva, uva);
	return 0;
}
void showmappings(uintptr_t lva, uintptr_t uva)
{
	pte_t *pte;
	while(lva <= uva) {
		cprintf("  0x%08x  ", lva);
		pte = pgdir_walk(kern_pgdir, (void*)lva, 0);
		if(pte == NULL || !(*pte & PTE_P)) {
			cprintf("not mapped\n");
			lva += PGSIZE;
			continue;
		}
		lva += PGSIZE;
		cprintf("  0x%08x", PTE_ADDR(*pte));
	
		if(*pte & PTE_G) 
			cprintf("  G");
		else
			cprintf("  -");

		if(*pte & PTE_PS) 
			cprintf("S");
		else 
			cprintf("-");
		
		if(*pte & PTE_D) 
			cprintf("D");
		else 
			cprintf("-");

		if(*pte & PTE_A) 
			cprintf("A");
		else 
			cprintf("-");

		if(*pte & PTE_PCD) 
			cprintf("C");
		else 
			cprintf("-");

		if(*pte & PTE_PWT) 
			cprintf("T");
		else 
			cprintf("-");

		if(*pte & PTE_U) 
			cprintf("U");
		else 
			cprintf("-");

			
		if(*pte & PTE_W) 
			cprintf("W");
		else 
			cprintf("-");
		if(*pte & PTE_P)	
			cprintf("P\n");
		else
			cprintf("-\n");
	}
}

//@author: Daochen Liu(dcliu@mail.ustc.edu.cn)
//2014.9.29
//set, clear, or change the permissions of any mapping in the current address space
// Usage: chperm +/-perm addr. 
// example chperm +w addr // KERNEL W/R
// the addr should be mapped and aligned at 4kb.
int mon_chperm(int argc, char **argv, struct Trapframe *tf)
{
	pte_t *pte;
	uint32_t flag;
	char *option = argv[1];
	uintptr_t va = strtol(argv[2], 0, 0);
#define CLEAR 0
#define SET 1
	if(argc != 3) {
		cprintf("Usage: chperm +/-perm addr\n");
		return 0;
	}
	if(option[0] == '+') 
		flag = SET;
	else if(option[0] == '-')
		flag = CLEAR;
	else {
		cprintf("Usage: chperm +/-perm addr\n");
		return 0;
	}
		
	option++;
	if(PGOFF(va) != 0) {
		cprintf("addr shoud be aligned at 4KB\n");
		return 0;
	}
	pte = pgdir_walk(kern_pgdir, (void*)va, 0);
	if(pte == NULL) 
		return 0;

	while(*option != '\0') {
		switch(*option) {
		case 'k':
			if(flag == SET)
				*pte &= ~PTE_U;
			else // CLEAR k
				*pte |= PTE_U;
			break;
		case 'u': 
			if(flag == SET)
				*pte |= PTE_U;
			else // CLEAR
				*pte &= ~PTE_U;
			break;
		case 'p':
			if(flag == SET)
				*pte |= PTE_P;
			else 
				*pte &= ~PTE_P;
			break;
		case 'w':
			if(flag == SET)
				*pte |= PTE_W;
			else
				*pte &= ~PTE_W;
			break;
		case 'r':
			if(flag == SET)
				*pte &= ~PTE_W;
			else //CLEAR
				*pte |= PTE_W;
			break;
		default:
			break;
		}
		option++;
	}
	return 0;
}

//@author: Daochen Liu(dcliu@mail.ustc.edu.cn)
//2014.9.30
// dump the content of memory 
// la,va can be virtual address or physical address
// dump -p/v la size
int mon_dump(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t la;
	uint32_t size, i;
	char flag;
	if(argc != 4)
	{
		cprintf("Usage: dump -p/v la size\n");
		cprintf("-p specify la  physical address\n");
		cprintf("-v specify la  virtual address\n");
		cprintf("the range [la, la+size] shoud be mapped, otherwise crashed\n");
		return 0;
	}
	la = strtol(argv[2], 0, 0);
	size = strtol(argv[3], 0, 0);
	if(*argv[1] != '-')
	{
		cprintf("Usage: dump -p/v la size\n");
		cprintf("-p specify la  physical address\n");
		cprintf("-v specify la  virtual address\n");
		return 0;

	}
	
	flag = *(++argv[1]);
	if(flag == 'p') 
		la = (uintptr_t)KADDR(la);
	
	if(la + size < la) {
		cprintf("size too large\n");
		return 0; 
	}

	for(i = 0; i < size; i++) {
		if(i % 4 == 0) {	
			cprintf("\n0x");
			if(flag == 'p')
				cprintf("%08x:", PADDR((void*)la));
			else 
				cprintf("%08x:", la);
		}
		cprintf("  0x");
		cprintf("%08x", *(uintptr_t*)(la));
		la += 4;
	}
	cprintf("\n");
	return 0;
}


	





/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
