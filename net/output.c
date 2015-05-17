#include "ns.h"
#include <inc/lib.h>
#include <kern/e1000.h>


extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	int ret;
	envid_t envid;
	while(1) {
		if ((ret = ipc_recv(&envid, &nsipcbuf, NULL)) < 0) 
			panic("ipc_recv: %e", ret);
		if ((envid != ns_envid) || (ret != NSREQ_OUTPUT))
			panic("Invalid Request");

		while ((ret = sys_net_transmit(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len)) == -E_DESC_FULL) {
			sys_yield();
		}	
		if (ret < 0)
			panic("sys_net_transmit: %e", ret);
	}
}
