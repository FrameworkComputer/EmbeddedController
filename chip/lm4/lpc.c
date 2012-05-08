/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "board.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i8042.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "port80.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)

#define LPC_SYSJUMP_TAG 0x4c50  /* "LP" */

static uint32_t host_events;     /* Currently pending SCI/SMI events */
static uint32_t event_mask[3];   /* Event masks for each type */


/* Configures GPIOs for module. */
static void configure_gpio(void)
{
	/* Set digital alternate function 15 for PL0:5, PM0:2, PM4:5 pins. */
	/* I/O: PL0:3 = command/address/data
	 * inp: PL4 (frame), PL5 (reset), PM0 (powerdown), PM5 (clock)
	 * out: PM1 (sci), PM4 (serirq) */
	gpio_set_alternate_function(LM4_GPIO_L, 0x3f, 0x0f);
	gpio_set_alternate_function(LM4_GPIO_M, 0x33, 0x0f);

#ifdef BOARD_bds
	/* Set the drive strength to 8mA for serirq only */
	/* TODO: (crosbug.com/p/7495) Only necessary on BDS because the cabling
	 * to the x86 is long and flaky; remove this for Link.  Setting this
	 * for all I/O lines seems to hang the x86 during boot. */
	LM4_GPIO_DR8R(LM4_GPIO_M) |= 0x00000010;
#endif
}


static void wait_send_serirq(uint32_t lpcirqctl) {
	LM4_LPC_LPCIRQCTL = lpcirqctl;

	/* TODO: udelay() is not graceful. Since the SIRQRIS is almost not
	 *       cleared in continuous mode and EC has problem to file
	 *       more than 1 frame in the quiet mode, this is the best way
	 *       we can do right now. */
	udelay(4);  /* 4 us is the time of 2 SERIRQ frames, which is long
	             * enough to guarantee the IRQ has been sent out. */
}

/* Manually generates an IRQ to host (edge-trigger).
 *
 * For SERIRQ quite mode, we need to set LM4_LPC_LPCIRQCTL twice.
 * The first one is to assert IRQ (pull low), and then the second one is
 * to de-assert it. This generates a pulse (high-low-high) for an IRQ.
 *
 * Note that the irq_num == 0 would set the AH bit (Active High).
 */
void lpc_manual_irq(int irq_num) {
	uint32_t common_bits =
	    0x00000004 |  /* PULSE */
	    0x00000002 |  /* ONCHG - for quiet mode */
	    0x00000001;   /* SND - send immediately */

	/* send out the IRQ first. */
	wait_send_serirq((1 << (irq_num + 16)) | common_bits);

	/* generate a all-high frame to simulate a rising edge. */
	wait_send_serirq(common_bits);
}


/* Generate SMI pulse to the host chipset via GPIO.
 *
 * If the x86 is in S0, SMI# is sampled at 33MHz, so minimum
 * pulse length is 60ns.  If the x86 is in S3, SMI# is sampled
 * at 32.768KHz, so we need pulse length >61us.  Both are short
 * enough and events are infrequent, so just delay for 65us.
 */
static void lpc_generate_smi(void)
{
	gpio_set_level(GPIO_PCH_SMIn, 0);
	udelay(65);
	gpio_set_level(GPIO_PCH_SMIn, 1);

	if (host_events & event_mask[LPC_HOST_EVENT_SMI])
		CPRINTF("[%T smi 0x%08x]\n",
			host_events & event_mask[LPC_HOST_EVENT_SMI]);
}


/* Generate SCI pulse to the host chipset via LPC0SCI */
static void lpc_generate_sci(void)
{
	LM4_LPC_LPCCTL |= LM4_LPC_SCI_START;

	if (host_events & event_mask[LPC_HOST_EVENT_SCI])
		CPRINTF("[%T sci 0x%08x]\n",
			host_events & event_mask[LPC_HOST_EVENT_SCI]);
}


uint8_t *lpc_get_host_range(int slot)
{
	return (uint8_t *)LPC_POOL_CMD_DATA + EC_LPC_PARAM_SIZE * slot;
}


uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)LPC_POOL_CMD_DATA + EC_LPC_PARAM_SIZE * 2;
}


void lpc_send_host_response(int slot, int result)
{
	int ch = slot ? LPC_CH_USER : LPC_CH_KERNEL;

	/* Write result to the data byte.  This sets the TOH bit in the
	 * status byte and triggers an IRQ on the host so the host can read
	 * the result. */
	/* TODO: (crosbug.com/p/7496) or it would, if we actually set up host
	 * IRQs */
	if (slot)
		LPC_POOL_USER[1] = result;
	else
		LPC_POOL_KERNEL[1] = result;

	/* Clear the busy bit */
	task_disable_irq(LM4_IRQ_LPC);
	LM4_LPC_ST(ch) &= ~(1 << 12);
	task_enable_irq(LM4_IRQ_LPC);

	/* ACPI 5.0-12.6.1: Generate SCI for Output Buffer Full
	 * condition on the kernel channel. */
	if (ch == LPC_CH_KERNEL)
		lpc_generate_sci();
}


/* Return true if the TOH is still set */
int lpc_keyboard_has_char(void) {
	return (LM4_LPC_ST(LPC_CH_KEYBOARD) & (1 << 0 /* TOH */)) ? 1 : 0;
}


void lpc_keyboard_put_char(uint8_t chr, int send_irq) {
	LPC_POOL_KEYBOARD[1] = chr;
	if (send_irq) {
		lpc_manual_irq(1);  /* IRQ#1 */
	}
}


int lpc_comx_has_char(void)
{
	return LM4_LPC_ST(LPC_CH_COMX) & 0x02;
}


int lpc_comx_get_char(void)
{
	return LPC_POOL_COMX[0];
}


void lpc_comx_put_char(int c)
{
	LPC_POOL_COMX[1] = c;
	/* TODO: manually trigger IRQ, like we do for keyboard? */
}


/* Update the host event status.
 * Sends a pulse if masked event status becomes non-zero:
 *  SMI pulse via EC_SMIn GPIO
 *  SCI pulse via LPC0SCI
 */
static void update_host_event_status(void) {
	uint32_t *mapped_raw_events =
		(uint32_t*)(lpc_get_memmap_range() + EC_LPC_MEMMAP_HOST_EVENTS);

	int need_sci = 0;
	int need_smi = 0;

	/* Disable LPC interrupt while updating status register */
	task_disable_irq(LM4_IRQ_LPC);

	if (host_events & event_mask[LPC_HOST_EVENT_SMI]) {
		/* Only generate SMI for first event */
		if (!(LM4_LPC_ST(LPC_CH_USER) & (1 << 10)) ||
		    !(LM4_LPC_ST(LPC_CH_KERNEL) & (1 << 10)))
			need_smi = 1;
		LM4_LPC_ST(LPC_CH_USER) |= (1 << 10);
		LM4_LPC_ST(LPC_CH_KERNEL) |= (1 << 10);
	} else {
		LM4_LPC_ST(LPC_CH_USER) &= ~(1 << 10);
		LM4_LPC_ST(LPC_CH_KERNEL) &= ~(1 << 10);
	}

	if (host_events & event_mask[LPC_HOST_EVENT_SCI]) {
		/* Generate SCI for every event */
		need_sci = 1;
		LM4_LPC_ST(LPC_CH_USER) |= (1 << 9);
		LM4_LPC_ST(LPC_CH_KERNEL) |= (1 << 9);
	} else {
		LM4_LPC_ST(LPC_CH_USER) &= ~(1 << 9);
		LM4_LPC_ST(LPC_CH_KERNEL) &= ~(1 << 9);
	}

	/* Copy host events to mapped memory */
	*mapped_raw_events = host_events;

	task_enable_irq(LM4_IRQ_LPC);

	/* Update level-sensitive wake signal */
	if (host_events & event_mask[LPC_HOST_EVENT_WAKE])
		gpio_set_level(GPIO_PCH_WAKEn, 0);
	else
		gpio_set_level(GPIO_PCH_WAKEn, 1);

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}


void lpc_set_host_events(uint32_t mask)
{
	if ((host_events & mask) == mask)
		return;

	host_events |= mask;
	update_host_event_status();
}


void lpc_clear_host_events(uint32_t mask)
{
	if (!(host_events & mask))
		return;

	host_events &= ~mask;
	update_host_event_status();
}


uint32_t lpc_get_host_events(void)
{
	return host_events;
}


void lpc_set_host_event_mask(enum lpc_host_event_type type, uint32_t mask)
{
	event_mask[type] = mask;
	update_host_event_status();
}


uint32_t lpc_get_host_event_mask(enum lpc_host_event_type type)
{
	return event_mask[type];
}


/* LPC interrupt handler */
static void lpc_interrupt(void)
{
	uint32_t mis = LM4_LPC_LPCMIS;

	/* Clear the interrupt bits we're handling */
	LM4_LPC_LPCIC = mis;

#ifdef CONFIG_TASK_HOSTCMD
	/* Handle host kernel/user command writes */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_KERNEL, 4)) {
		/* Set the busy bit */
		LM4_LPC_ST(LPC_CH_KERNEL) |= (1 << 12);

		/* Read the command byte and pass to the host command handler.
		 * This clears the FRMH bit in the status byte. */
		host_command_received(0, LPC_POOL_KERNEL[0]);

		/* ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty
		 * condition on the kernel channel. */
		lpc_generate_sci();
	}
	if (mis & LM4_LPC_INT_MASK(LPC_CH_USER, 4)) {
		/* Set the busy bit */
		LM4_LPC_ST(LPC_CH_USER) |= (1 << 12);

		/* Read the command byte and pass to the host command handler.
		 * This clears the FRMH bit in the status byte. */
		host_command_received(1, LPC_POOL_USER[0]);
	}
#endif

	/* Handle port 80 writes (CH0MIS1) */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_PORT80, 2))
		port_80_write(LPC_POOL_PORT80[0]);

#ifdef CONFIG_TASK_I8042CMD
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
		task_wake(TASK_ID_I8042CMD);
	}
#endif

	/* Handle COMx */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_COMX, 2)) {
		/* Handle host writes */
		if (lpc_comx_has_char()) {
			/* Copy a character to the UART if there's space */
			if (uart_comx_putc_ok())
				uart_comx_putc(lpc_comx_get_char());
		}
	}

	/* Debugging: print changes to LPC0RESET */
	if (mis & (1 << 31)) {
		CPRINTF("[%T LPC RESET# %sasserted]\n",
			(LM4_LPC_LPCSTS & (1<<10)) ? "" : "de");
	}
}
DECLARE_IRQ(LM4_IRQ_LPC, lpc_interrupt, 2);


/* Preserve event masks across a sysjump */
static int lpc_sysjump(void)
{
	system_add_jump_tag(LPC_SYSJUMP_TAG, 1,
			    sizeof(event_mask), event_mask);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_SYSJUMP, lpc_sysjump, HOOK_PRIO_DEFAULT);


/* Restore event masks after a sysjump */
static void lpc_post_sysjump(void)
{
	const uint32_t *prev_mask;
	int size, version;

	prev_mask = (const uint32_t *)system_get_jump_tag(LPC_SYSJUMP_TAG,
							  &version, &size);
	if (!prev_mask || version != 1 || size != sizeof(event_mask))
		return;

	memcpy(event_mask, prev_mask, sizeof(event_mask));
	update_host_event_status();
}


static int lpc_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));

	/* Enable RGCGLPC then delay a few clocks. */
	LM4_SYSTEM_RCGCLPC = 1;
	scratch = LM4_SYSTEM_RCGCLPC;

	LM4_LPC_LPCIM = 0;
	LM4_LPC_LPCCTL = 0;
	LM4_LPC_LPCIRQCTL = 0;

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
		(0 << 18/* IRQEN1 */) | (LPC_POOL_OFFS_KEYBOARD << (5 - 1));
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

	/* Set LPC channel 7 to COM port I/O address.  Note that channel 7
	 * ignores the TYPE bit and is always an 8-byte range. */
	LM4_LPC_ADR(LPC_CH_COMX) = LPC_COMX_ADDR;
	/* TODO: could configure IRQSELs and set IRQEN2/CX, and then the host
	 * can enable IRQs on its own. */
	LM4_LPC_CTL(LPC_CH_COMX) = 0x0004 | (LPC_POOL_OFFS_COMX << (5 - 1));
	/* Enable COMx emulation for reads and writes. */
	LM4_LPC_LPCDMACX = 0x00310000;
	/* Unmask interrupt for host data writes.  We don't need interrupts for
	 * reads, because there's no flow control in that direction; LPC is
	 * much faster than the UART, and the UART doesn't have anywhere
	 * sensible to buffer input anyway. */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_COMX, 2);

	/* Unmaksk LPC bus reset interrupt.  This lets us monitor the PCH
	 * PLTRST# signal for debugging. */
	LM4_LPC_LPCIM |= (1 << 31);

	/* Enable LPC channels */
	LM4_LPC_LPCCTL = LM4_LPC_SCI_CLK_1 |
		(1 << LPC_CH_KERNEL) |
		(1 << LPC_CH_PORT80) |
		(1 << LPC_CH_CMD_DATA) |
		(1 << LPC_CH_KEYBOARD) |
		(1 << LPC_CH_USER) |
		(1 << LPC_CH_COMX);

	/* Enable LPC interrupt */
	task_enable_irq(LM4_IRQ_LPC);

	/* Enable COMx UART */
	uart_comx_enable();

	/* Restore event masks if needed */
	lpc_post_sysjump();

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_DEFAULT);


static int lpc_resume(void)
{
	/* Mask all host events until the host unmasks them itself.  */
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, lpc_resume, HOOK_PRIO_DEFAULT);
