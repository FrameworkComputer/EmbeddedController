/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "board.h"
#include "host_command.h"
#include "i8042.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "port80.h"
#include "registers.h"
#include "task.h"
#include "uart.h"


/* Configures GPIOs for module. */
static void configure_gpio(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable clocks to GPIO modules L, M (p. 404) */
	LM4_SYSTEM_RCGCGPIO |= 0x0c00;
	scratch = LM4_SYSTEM_RCGCGPIO;

	/* Set digital alternate function 15 for PL0:5, PM0:2, PM4:5 pins. */
	/* I/O: PL0:3 = command/address/data
	 * inp: PL4 (frame), PL5 (reset), PM0 (powerdown), PM5 (clock)
	 * out: PM1 (sci), PM2 (clkrun), PM4 (serirq) */
	LM4_GPIO_AFSEL(L) |= 0x3f;
	LM4_GPIO_AFSEL(M) |= 0x37;
	LM4_GPIO_PCTL(L) |= 0x00ffffff;
	LM4_GPIO_PCTL(M) |= 0x00ff0fff;
	LM4_GPIO_DEN(L) |= 0x3f;
	LM4_GPIO_DEN(M) |= 0x37;

	/* Set the drive strength to 8mA for serirq only */
	/* TODO: Only necessary on BDS because the cabling to the x86
	 * is long and flaky; remove this for Link.  Setting this for all
	 * I/O lines seems to hang the x86 during boot. */
	LM4_GPIO_DR8R(M) |= 0x00000010;
}


int lpc_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable RGCGLPC then delay a few clocks. */
	LM4_SYSTEM_RCGCLPC = 1;
	scratch = LM4_SYSTEM_RCGCLPC;

	LM4_LPC_LPCIM = 0;
	LM4_LPC_LPCCTL = 0;

	/* Configure GPIOs */
	configure_gpio();

	/* Set LPC channel 0 to I/O address 0x62 (data) / 0x66 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd) */
	LM4_LPC_ADR(LPC_CH_KERNEL) = EC_LPC_ADDR_KERNEL_DATA;
	LM4_LPC_CTL(LPC_CH_KERNEL) = (LPC_POOL_OFFS_KERNEL << (5 - 1));
	/* Unmask interrupt for host command writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_KERNEL, 4);

	/* Set LPC channel 1 to I/O address 0x80 (data), single endpoint,
	 * pool bytes 4(data)/5(cmd). */
	LM4_LPC_ADR(LPC_CH_PORT80) = 0x80;
	LM4_LPC_CTL(LPC_CH_PORT80) = (LPC_POOL_OFFS_PORT80 << (5 - 1));
	/* Unmask interrupt for host data writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_PORT80, 2);


	/* Set LPC channel 2 to I/O address 0x800, range endpoint,
	 * arbitration disabled, pool bytes 512-1023.  To access this from
	 * x86, use the following commands to set GEN_LPC2 and GEN_LPC3:
	 *
	 *   pci_write32 0 0x1f 0 0x88 0x007c0801
	 *   pci_write32 0 0x1f 0 0x8c 0x007c0901
	 */
	LM4_LPC_ADR(LPC_CH_CMD_DATA) = EC_LPC_ADDR_KERNEL_PARAM;
	LM4_LPC_CTL(LPC_CH_CMD_DATA) = 0x801D |
		(LPC_POOL_OFFS_CMD_DATA << (5 - 1));

	/* Set LPC channel 3 to I/O address 0x60 (data) / 0x64 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd) */
	LM4_LPC_ADR(LPC_CH_KEYBOARD) = 0x60;
	LM4_LPC_CTL(LPC_CH_KEYBOARD) = (1 << 24/* IRQSEL1 */) |
		(1 << 18/* IRQEN1 */) | (LPC_POOL_OFFS_KEYBOARD << (5 - 1));
	LM4_LPC_ST(LPC_CH_KEYBOARD) = 0;
	/* Unmask interrupt for host command/data writes and data reads */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 7);

	/* Set LPC channel 4 to I/O address 0x200 (data) / 0x204 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd) */
	LM4_LPC_ADR(LPC_CH_USER) = EC_LPC_ADDR_USER_DATA;
	LM4_LPC_CTL(LPC_CH_USER) = (LPC_POOL_OFFS_USER << (5 - 1));
	/* Unmask interrupt for host command writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_USER, 4);

	/* Set LPC channel 7 to I/O address 0x2F8 (COM2), bytes 8-15.
	 * Channel 7 ignores the TYPE bit. */
	LM4_LPC_ADR(LPC_CH_COMX) = 0x2f8;
	/* TODO: could configure IRQSELs and set IRQEN2/CX, and then the host
	 * can enable IRQs on its own. */
	LM4_LPC_CTL(LPC_CH_COMX) = 0x0004 | (LPC_POOL_OFFS_COMX << (5 - 1));

#ifdef USE_LPC_COMx_DMA
	/* TODO: haven't been able to get this to work yet */
	/* COMx UART DMA mode */
	LM4_LPC_LPCDMACX = 0x00070000;

	/* TODO: set up DMA */
	LM4_SYSTEM_RCGCDMA = 1;
	/* Wait 3 clocks before accessing other DMA regs */
	LM4_SYSTEM_RCGCDMA = 1;
	LM4_SYSTEM_RCGCDMA = 1;
	LM4_SYSTEM_RCGCDMA = 1;
	/* Enable master */
	LM4_DMA_DMACFG = 1;
	/* TODO: hope we don't need the channel control structs; we're just
	 * throwing this somewhere in memory.  Shouldn't need it if we leave
	 * all the channel disabled, though. */
	LM4_DMA_DMACTLBASE = 0x20004000;
	/* Map UART and LPC DMA functions to channels */
	LM4_DMA_DMACHMAP0 = 0x00003000;  /* Channel 3 encoding 3 = LPC0 Ch3 */
	LM4_DMA_DMACHMAP1 = 0x00000011;  /* Channels 8,9 encoding 1 = UART1 */
#else
	/* Use our LPC interrupt handler to notify COMxIM on write-from-host */
	LM4_LPC_LPCDMACX = 0x00110000;
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_COMX, 2);
#endif

	/* Enable LPC channels */
	LM4_LPC_LPCCTL =
		(1 << LPC_CH_KERNEL) |
		(1 << LPC_CH_PORT80) |
		(1 << LPC_CH_CMD_DATA) |
		(1 << LPC_CH_KEYBOARD) |
		(1 << LPC_CH_USER) |
		(1 << LPC_CH_COMX);

	return EC_SUCCESS;
}


uint8_t *lpc_get_host_range(int slot)
{
	return (uint8_t *)LPC_POOL_CMD_DATA + 256 * slot;
}


void lpc_send_host_response(int slot, int status)
{
	int ch = slot ? LPC_CH_USER : LPC_CH_KERNEL;

	/* Set status nibble (bits 7:4 from host side) and clear the busy
	 * bit (0x1000) (bit 2 from host side) */
	LM4_LPC_ST(ch) = (LM4_LPC_ST(ch) & 0xffffe0ff) | ((status & 0xf) << 8);

	/* Write dummy value to data byte.  This sets the TOH bit in the
	 * status byte and triggers an IRQ on the host so the host can read
	 * the status. */
	/* TODO: or it would, if we actually set up host IRQs */
	if (slot)
		LPC_POOL_USER[1] = 0;
	else
		LPC_POOL_KERNEL[1] = 0;
}


/* LPC interrupt handler */
static void lpc_interrupt(void)
{
	uint32_t mis = LM4_LPC_LPCMIS;

	/* Clear the interrupt bits we're handling */
	LM4_LPC_LPCIC = mis;

	/* Handle host kernel/user command writes */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_KERNEL, 4)) {
		/* Set the busy bit and clear the status */
		LM4_LPC_ST(LPC_CH_KERNEL) = (LM4_LPC_ST(LPC_CH_KERNEL) &
					     0xffffe0ff) | 0x1000;

		/* Read the command byte and pass to the host command handler.
		 * This clears the FRMH bit in the status byte. */
		host_command_received(0, LPC_POOL_KERNEL[0]);
	}
	if (mis & LM4_LPC_INT_MASK(LPC_CH_USER, 4)) {
		/* Set the busy bit and clear the status */
		LM4_LPC_ST(LPC_CH_USER) = (LM4_LPC_ST(LPC_CH_USER) &
					   0xffffe0ff) | 0x1000;

		/* Read the command byte and pass to the host command handler.
		 * This clears the FRMH bit in the status byte. */
		host_command_received(1, LPC_POOL_USER[0]);
	}

	/* Handle port 80 writes (CH0MIS1) */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_PORT80, 2))
		port_80_write(LPC_POOL_PORT80[0]);

	/* Handle port 60 command (CH3MIS2) and data (CH3MIS1) */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 2)) {
		/* Read the data byte and pass to the i8042 handler.
		 * This clears the FRMH bit in the status byte. */
		i8042_receives_data(LPC_POOL_KEYBOARD[0]);
	}
	if (mis & LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 4)) {
		/* Read the command byte and pass to the i8042 handler.
		 * This clears the FRMH bit in the status byte. */
		i8042_receives_command(LPC_POOL_KEYBOARD[0]);
	}
	if (mis & LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 1)) {
		/* Host picks up the data, try to send remaining bytes */
		task_send_msg(TASK_ID_I8042CMD, TASK_ID_I8042CMD, 0);
	}

	/* Handle COMx */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_COMX, 2)) {
		uint32_t cis = LM4_LPC_LPCDMACX;
		/* Clear the interrupt reasons we're handling */
		LM4_LPC_LPCDMACX = cis;

		/* Handle host writes */
		if (LM4_LPC_ST(LPC_CH_COMX) & 0x02) {
			if (LM4_UART_FR(1) & 0x20) {
				/* FIFO is full, so enable transmit
				 * interrupt to let us know when it
				 * empties */
				LM4_UART_IM(1) |= 0x20;
			} else {
				/* Space in FIFO, so copy byte */
				LM4_UART_DR(1) = LPC_POOL_COMX[0];
			}
		}

		/* TODO: handle UART input to host - if host read the
		 * to-host data, see if there's another byte still
		 * waiting on UART1. */
	}
}

DECLARE_IRQ(108, lpc_interrupt, 2);
