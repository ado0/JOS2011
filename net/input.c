#include "ns.h"
#include <inc/lib.h>
#include <kern/e1000.h>

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int ret;
	int perm = PTE_P | PTE_W | PTE_U;
	uint8_t buf[BUFFER_SIZE];
	while(1) {
		if ((ret = sys_page_alloc(0, &nsipcbuf, perm)) < 0)
			panic("sys_pape_alloc: %e", ret);
		
		while ((ret = sys_net_receive(buf)) == -E_DESC_EMPTY) {
			sys_yield();
		}
		memmove(nsipcbuf.pkt.jp_data, buf, ret);
		nsipcbuf.pkt.jp_len = ret;
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, perm);
	}
}
