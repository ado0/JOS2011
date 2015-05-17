#include <inc/memlayout.h>
#include <inc/string.h>

#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here
extern pde_t *kern_pgdir;
struct tx_desc tx_fifo[TX_SIZE]__attribute__((aligned(16)));
struct rx_desc rx_fifo[RX_SIZE]__attribute__((aligned(16)));
uint8_t *tx_buffer[TX_SIZE * MAX_PACKET_SIZE];
uint8_t *rx_buffer[RX_SIZE * BUFFER_SIZE]; // 2048 bytes buffer for per rx descriptor
int attach_e1000(struct pci_func *pcif)
{
	uintptr_t addr;
	size_t size;
	int perm, i;
	
	// Enable PCI device
	pci_func_enable(pcif);
	addr = pcif->reg_base[0];
	size = pcif->reg_size[0];
	size = ROUNDUP(size, PGSIZE);
	perm = PTE_P | PTE_W | PTE_PCD | PTE_PWT;
	
	// Memory map I/O for PCI device
	boot_map_region(kern_pgdir, KSTACKTOP, size, addr, perm);
	e100 = (uint32_t *)KSTACKTOP;
	cprintf("e1000 status register: %x\n", e100[E1000_STATUS/sizeof(uint32_t)]);

	// transmit initialization
	// Program the Transmit Descriptor Base Address Registers
	e100[E1000_TDBAL/sizeof(uint32_t)] = PADDR((void*)tx_fifo);
	e100[E1000_TDBAH/sizeof(uint32_t)] = 0x0;
	
	// Set the Transmit Descriptor Length Register
	e100[E1000_TDLEN/sizeof(uint32_t)] = TX_SIZE * sizeof(struct tx_desc);
	
	// Set the Transmit Descriptor Head and Tail Registers
	e100[E1000_TDH/sizeof(uint32_t)] = 0;
    e100[E1000_TDT/sizeof(uint32_t)] = 0;
	
	// Initialize the Transmit Control Register 
	// Transmit Enable
	e100[E1000_TCTL/sizeof(uint32_t)] |= E1000_TCTL_EN;
	//Padding short packets, makes the packet 64 bytes long.
	e100[E1000_TCTL/sizeof(uint32_t)] |= E1000_TCTL_PSP;
	//Configure the Collision Threshold (TCTL.CT) to the desired value. 
	//Ethernet standard is 10h.
	//This setting only has meaning in half duplex mode.--by 8254x manual
	e100[E1000_TCTL/sizeof(uint32_t)] |= 0x10 << E1000_TCTL_CT_SHIFT;
	//Configure the Collision Distance (TCTL.COLD) to its expected value. 
	//For full duplex operation, this value should be set to 40h. 
	//For gigabit half duplex, this value should be set to 200h. 
	//For 10/100 half duplex, this value should be set to 40h.--by 8254 manual
	e100[E1000_TCTL/sizeof(uint32_t)] |= 0x40 << E1000_TCTL_COLD_SHIFT;
	
	// Program the Transmit IPG Register
	e100[E1000_TIPG/sizeof(uint32_t)] |= 10 << E1000_IPGT_SHIFT; // IPGR
	e100[E1000_TIPG/sizeof(uint32_t)] |= 4 << E1000_IPGR1_SHIFT; //IPGR1
	e100[E1000_TIPG/sizeof(uint32_t)] |= 6 << E1000_IPGR2_SHIFT; //IPGR2
	e100[E1000_TIPG/sizeof(uint32_t)] |= 0 << E1000_RS_SHIFT;
	
	// Initialize tx_fifo and packet buffer array
	memset((void*)tx_fifo, 0x0, TX_SIZE * sizeof(struct tx_desc));
	memset(tx_buffer, 0x0, TX_SIZE * MAX_PACKET_SIZE);
	for ( i = 0; i < TX_SIZE; i++) {
		tx_fifo[i].status |= E1000_TXD_STAT_DD;
		tx_fifo[i].addr = PADDR(tx_buffer + i * MAX_PACKET_SIZE);
	}

	// receive initialization
	
	// Program the Receive Descriptor Base Address Registers
	e100[E1000_RDBAL / sizeof(uint32_t)] = PADDR((void*)rx_fifo);
	e100[E1000_RDBAH / sizeof(uint32_t)] = 0x0;
	
	// Set the Receive Descriptor Length Register
	e100[E1000_RDLEN / sizeof(uint32_t)] = RX_SIZE * sizeof(struct rx_desc);
	
	// Set the Receive Descriptor Head and Tail Registers
	e100[E1000_RDH / sizeof(uint32_t)] = 0x0;
	e100[E1000_RDT / sizeof(uint32_t)] = 0x0;
	
	// configure NIC MAC Address 52:54:00:12:34:56
	// 52:54:00:12:34:56 is from lowest-order byte to highest-order
	// so 52:54:00:12 are the low-order 32 bits of the MAC address 
	// and 34:56 are the high-order 16 bits
//	e100[E1000_RAL/sizeof(uint32_t)] = (0x12 << 24) | (0x0 << 16) | (0x54 << 8) | (0x52 << 0);
//	e100[E1000_RAH/sizeof(uint32_t)] = (0x56 << 8) | (0x34 << 0);
	// Challenge. read mac address from EEPROM
	// first 16-bit
	e100[E1000_EERD/sizeof(uint32_t)] = 0x0;
	e100[E1000_EERD/sizeof(uint32_t)] |= E1000_EEPROM_RW_REG_START;
	while(!(e100[E1000_EERD/sizeof(uint32_t)] & E1000_EEPROM_RW_REG_DONE));
	e100[E1000_RAL/sizeof(uint32_t)] = 
		e100[E1000_EERD/sizeof(uint32_t)] >> E1000_EEPROM_RW_REG_DATA;
	// second 16-bit
	e100[E1000_EERD/sizeof(uint32_t)] = 0x1 << E1000_EEPROM_RW_ADDR_SHIFT;
	e100[E1000_EERD/sizeof(uint32_t)] |= E1000_EEPROM_RW_REG_START;
	while(!(e100[E1000_EERD/sizeof(uint32_t)] & E1000_EEPROM_RW_REG_DONE));
	e100[E1000_RAL/sizeof(uint32_t)] |= e100[E1000_EERD/sizeof(uint32_t)] & 0xffff0000;
	// third 16-bit
	e100[E1000_EERD/sizeof(uint32_t)] = 0x2 << E1000_EEPROM_RW_ADDR_SHIFT;
	e100[E1000_EERD/sizeof(uint32_t)] |= E1000_EEPROM_RW_REG_START;
	while(!(e100[E1000_EERD/sizeof(uint32_t)] & E1000_EEPROM_RW_REG_DONE));
	e100[E1000_RAH/sizeof(uint32_t)] = e100[E1000_EERD/sizeof(uint32_t)] >> E1000_EEPROM_RW_REG_DATA;
	//end challenge
	


	//address valid
	e100[E1000_RAH/sizeof(uint32_t)] |= E1000_RAH_AV;	
	
	//initialize Multicast Table Array to 0b, total 4096 bit in MTA.
	memset(((uint8_t*)e100) + E1000_MTA, 0, 4096/8);
	
	//mask all interrupts 
	//e100[E1000_IMC/sizeof(uint32_t)] |= 0xFFFFFFFF;
	
	
	// Buffer Size 2048, because largest possible standard Ethernet 
	// packet (1518 bytes), we use one descriptor  for one packet. 
	e100[E1000_RCTL/sizeof(uint32_t)] = 0;
	e100[E1000_RCTL/sizeof(uint32_t)] |= E1000_RCTL_SECRC; /* Strip Ethernet CRC */
	e100[E1000_RCTL/sizeof(uint32_t)] |= E1000_RCTL_SZ_2048;
	
	// Initialize rx_fifo and packet buffer array
	memset((void*)rx_fifo, 0x0, RX_SIZE * sizeof(struct tx_desc));
	memset(rx_buffer, 0x0, RX_SIZE * BUFFER_SIZE);
	for (i = 0; i < RX_SIZE; i++) {
		rx_fifo[i].addr = PADDR(rx_buffer + i * BUFFER_SIZE);
	}	
	
	//Initialize the Receive Control Register
	e100[E1000_RCTL/sizeof(uint32_t)] &= ~E1000_RCTL_LPE; // Long Packet Reception disable
	e100[E1000_RCTL/sizeof(uint32_t)] |= E1000_RCTL_BAM; // broadcast enable.
	//Loopback Mode (RCTL.LBM) should be set to 00b for normal operation.
	e100[E1000_RCTL/sizeof(uint32_t)] &= ~E1000_RCTL_LBM_NO;
	e100[E1000_RCTL/sizeof(uint32_t)] &= ~E1000_RCTL_MPE; // multicast promiscuous disable
	e100[E1000_RCTL/sizeof(uint32_t)] &= ~E1000_RCTL_BSEX; // Buffer size extension
	/* broadcast enable */
	//it is best to leave the Ethernet controller receive logic disabled (RCTL.EN = 0b) 
	//until after the receive descriptor ring has been initialized and software is ready 
	//to process received packets --by 8254x manual
	e100[E1000_RCTL/sizeof(uint32_t)] |= E1000_RCTL_EN;  //enable receiver
	return 0;
}

// return 0 on success
//  < 0 on error
int 
transmit_e1000(void* src, size_t len)
{
	int next = e100[E1000_TDT/sizeof(uint32_t)];
	if (len > MAX_PACKET_SIZE)
		return -E_OUT_BUFF;
	// Check if next tx desc is free
	if (tx_fifo[next].status & E1000_TXD_STAT_DD) {
		memmove(tx_buffer + MAX_PACKET_SIZE * next, src, len);
		tx_fifo[next].length = len; 
		
		tx_fifo[next].status &= ~E1000_TXD_STAT_DD;
		tx_fifo[next].cmd |=  E1000_TXD_CMD_RS;
		tx_fifo[next].cmd |= E1000_TXD_CMD_EOP;
		
		e100[E1000_TDT/sizeof(uint32_t)] = (next + 1) % TX_SIZE;
		return 0;
	} else {
		return -E_DESC_FULL;
	}	
}
int 
receive_e1000(void* dst)
{
	int len;
	int tail = e100[E1000_RDT/sizeof(uint32_t)];
	if (rx_fifo[tail].status & E1000_RXD_STAT_DD) {
			len = rx_fifo[tail].length;
			memmove(dst, rx_buffer + BUFFER_SIZE * tail, len);
			rx_fifo[tail].status &= ~E1000_RXD_STAT_DD;
			rx_fifo[tail].status &= ~E1000_RXD_STAT_EOP;
			e100[E1000_RDT/sizeof(uint32_t)] = (tail + 1) % RX_SIZE;
			return len;
	} else {
		return -E_DESC_EMPTY; 
	}
}
