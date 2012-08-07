/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i8042.h"
#include "lpc.h"
#include "port80.h"
#include "pwm.h"
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

static uint8_t acpi_cmd;         /* Last received ACPI command */
static uint8_t acpi_addr;        /* First byte of data after ACPI command */
static int acpi_data_count;      /* Number of data writes after command */
static uint8_t acpi_mem_test;    /* Test byte in ACPI memory space */

static uint32_t host_events;     /* Currently pending SCI/SMI events */
static uint32_t event_mask[3];   /* Event masks for each type */
static struct host_cmd_handler_args host_cmd_args;
static int init_done;

static uint8_t * const cmd_params = (uint8_t *)LPC_POOL_CMD_DATA +
	EC_LPC_ADDR_HOST_PARAM - EC_LPC_ADDR_HOST_ARGS;
static uint8_t * const old_params = (uint8_t *)LPC_POOL_CMD_DATA +
	EC_LPC_ADDR_OLD_PARAM - EC_LPC_ADDR_HOST_ARGS;
static struct ec_lpc_host_args * const lpc_host_args =
	(struct ec_lpc_host_args *)LPC_POOL_CMD_DATA;

/* Configure GPIOs for module */
static void configure_gpio(void)
{
	/* Set digital alternate function 15 for PL0:5, PM0:2, PM4:5 pins. */
	/* I/O: PL0:3 = command/address/data
	 * inp: PL4 (frame), PL5 (reset), PM0 (powerdown), PM5 (clock)
	 * out: PM1 (sci), PM4 (serirq) */
	gpio_set_alternate_function(LM4_GPIO_L, 0x3f, 0x0f);
	gpio_set_alternate_function(LM4_GPIO_M, 0x33, 0x0f);
}

static void wait_irq_sent(void)
{
	/* TODO: udelay() is not graceful. Since the SIRQRIS is almost not
	 *       cleared in continuous mode and EC has problem to file
	 *       more than 1 frame in the quiet mode, this is the best way
	 *       we can do right now. */
	udelay(4);  /* 4 us is the time of 2 SERIRQ frames, which is long
	             * enough to guarantee the IRQ has been sent out. */
}

static void wait_send_serirq(uint32_t lpcirqctl)
{
	LM4_LPC_LPCIRQCTL = lpcirqctl;
	wait_irq_sent();
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

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)LPC_POOL_MEMMAP;
}

static void lpc_send_response(struct host_cmd_handler_args *args)
{
	uint8_t *out;
	int size = args->response_size;
	int max_size;

	/* Handle negative size */
	if (size < 0) {
		args->result = EC_RES_INVALID_RESPONSE;
		size = 0;
	}

	/* TODO(sjg@chromium.org): put flags in args? */
	if (lpc_host_args->flags & EC_HOST_ARGS_FLAG_FROM_HOST) {
		/* New-style response */
		int csum;
		int i;

		lpc_host_args->flags =
			(lpc_host_args->flags & ~EC_HOST_ARGS_FLAG_FROM_HOST) |
			EC_HOST_ARGS_FLAG_TO_HOST;

		lpc_host_args->data_size = size;

		csum = args->command + lpc_host_args->flags +
			lpc_host_args->command_version +
			lpc_host_args->data_size;

		for (i = 0, out = (uint8_t *)args->response; i < size;
		     i++, out++)
			csum += *out;

		lpc_host_args->checksum = (uint8_t)csum;

		out = cmd_params;
		max_size = EC_HOST_PARAM_SIZE;
	} else {
		/* Old-style response */
		lpc_host_args->flags = 0;
		out = old_params;
		max_size = EC_OLD_PARAM_SIZE;
	}

	/* Fail if response doesn't fit in the param buffer */
	if (size > max_size)
		args->result = EC_RES_INVALID_RESPONSE;
	else if (host_cmd_args.response != out)
		memcpy(out, args->response, size);

	/*
	 * Write result to the data byte.  This sets the TOH bit in the
	 * status byte and triggers an IRQ on the host so the host can read
	 * the result.
	 *
	 * TODO: (crosbug.com/p/7496) or it would, if we actually set up host
	 * IRQs
	 */
	LPC_POOL_CMD[1] = args->result;

	/* Clear the busy bit */
	task_disable_irq(LM4_IRQ_LPC);
	LM4_LPC_ST(LPC_CH_CMD) &= ~LM4_LPC_ST_BUSY;
	task_enable_irq(LM4_IRQ_LPC);
}

/* Return true if the TOH is still set */
int lpc_keyboard_has_char(void)
{
	return (LM4_LPC_ST(LPC_CH_KEYBOARD) & LM4_LPC_ST_TOH) ? 1 : 0;
}


/* Put a char to host buffer and send IRQ if specified. */
void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	LPC_POOL_KEYBOARD[1] = chr;
	if (send_irq) {
		lpc_manual_irq(1);  /* IRQ#1 */
	}
}


/* Clear the keyboard buffer. */
void lpc_keyboard_clear_buffer(void)
{
	/* Make sure the previous TOH and IRQ has been sent out. */
	wait_irq_sent();

	LM4_LPC_ST(LPC_CH_KEYBOARD) &= ~LM4_LPC_ST_TOH;

	/* Ensure there is no TOH set in this period. */
	wait_irq_sent();
}

/* Send an IRQ to host if there is a byte in buffer already. */
void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char()) {
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


/**
 * Update the host event status.  Sends a pulse if masked event status becomes
 * non-zero:
 *   - SMI pulse via EC_SMIn GPIO
 *   - SCI pulse via LPC0SCI
 */
static void update_host_event_status(void) {
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable LPC interrupt while updating status register */
	task_disable_irq(LM4_IRQ_LPC);

	if (host_events & event_mask[LPC_HOST_EVENT_SMI]) {
		/* Only generate SMI for first event */
		if (!(LM4_LPC_ST(LPC_CH_ACPI) & LM4_LPC_ST_SMI))
			need_smi = 1;
		LM4_LPC_ST(LPC_CH_ACPI) |= LM4_LPC_ST_SMI;
	} else
		LM4_LPC_ST(LPC_CH_ACPI) &= ~LM4_LPC_ST_SMI;

	if (host_events & event_mask[LPC_HOST_EVENT_SCI]) {
		/* Generate SCI for every event */
		need_sci = 1;
		LM4_LPC_ST(LPC_CH_ACPI) |= LM4_LPC_ST_SCI;
	} else
		LM4_LPC_ST(LPC_CH_ACPI) &= ~LM4_LPC_ST_SCI;

	/* Copy host events to mapped memory */
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = host_events;

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

void lpc_set_host_event_state(uint32_t mask)
{
	if (mask != host_events) {
		host_events = mask;
		update_host_event_status();
	}
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

/**
 * Handle command (is_cmd=1) or data (is_cmd=0) writes to ACPI I/O ports.
 */
static void handle_acpi_write(int is_cmd)
{
	int data = 0;

	/* Set the busy bit */
	LM4_LPC_ST(LPC_CH_ACPI) |= LM4_LPC_ST_BUSY;

	/* Read command/data; this clears the FRMH status bit. */
	if (is_cmd) {
		acpi_cmd = LPC_POOL_ACPI[0];
		acpi_data_count = 0;
	} else {
		data = LPC_POOL_ACPI[0];
		/*
		 * The first data byte is the ACPI memory address for
		 * read/write commands.
		 */
		if (!acpi_data_count++)
			acpi_addr = data;
	}

	/* Process complete commands */
	if (acpi_cmd == EC_CMD_ACPI_READ && acpi_data_count == 1) {
		/* ACPI read cmd + addr */
		int result = 0;

		switch (acpi_addr) {
		case EC_ACPI_MEM_VERSION:
			result = EC_ACPI_MEM_VERSION_CURRENT;
			break;
		case EC_ACPI_MEM_TEST:
			result = acpi_mem_test;
			break;
		case EC_ACPI_MEM_TEST_COMPLIMENT:
			result = 0xff - acpi_mem_test;
			break;
#ifdef CONFIG_TASK_PWM
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * TODO: not very satisfying that LPC knows directly
			 * about the keyboard backlight, but for now this is
			 * good enough and less code than defining a new
			 * console command interface just for ACPI read/write.
			 */
			result = pwm_get_keyboard_backlight();
			break;
#endif
		default:
			break;
		}

		/* Send the result byte */
		CPRINTF("[%T ACPI read 0x%02x = 0x%02x]\n", acpi_addr, result);
		LPC_POOL_ACPI[1] = result;

	} else if (acpi_cmd == EC_CMD_ACPI_WRITE && acpi_data_count == 2) {
		/* ACPI write cmd + addr + data */
		CPRINTF("[%T ACPI write 0x%02x = 0x%02x]\n", acpi_addr, data);
		switch (acpi_addr) {
		case EC_ACPI_MEM_TEST:
			acpi_mem_test = data;
			break;
#ifdef CONFIG_TASK_PWM
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			pwm_set_keyboard_backlight(data);
			break;
#endif
		default:
			break;
		}

	} else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = 0;
		int i;

		for (i = 0; i < 32; i++) {
			if (host_events & (1 << i)) {
				host_clear_events(1 << i);
				evt_index = i + 1; /* Events are 1-based */
				break;
			}
		}

		CPRINTF("[%T ACPI query = %d]\n", evt_index);
		LPC_POOL_ACPI[1] = evt_index;
	}

	/* Clear the busy bit */
	LM4_LPC_ST(LPC_CH_ACPI) &= ~LM4_LPC_ST_BUSY;

	/*
	 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty / Output Buffer
	 * Full condition on the kernel channel.
	 */
	lpc_generate_sci();
}

/**
 * Handle unexpected ACPI query request on the normal command channel from an
 * old API firmware/kernel.  No need to handle other ACPI commands on the
 * normal command channel, because old firmware/kernel only supported query.
 */
/* TODO: remove when link EVT is deprecated. */
static int acpi_on_bad_channel(struct host_cmd_handler_args *args)
{
	int i;
	int result = 0;

	for (i = 0; i < 32; i++) {
		if (host_events & (1 << i)) {
			host_clear_events(1 << i);
			result = i + 1;  /* Events are 1-based */
			break;
		}
	}

	return result;
}
DECLARE_HOST_COMMAND(EC_CMD_ACPI_QUERY_EVENT,
		     acpi_on_bad_channel,
		     EC_VER_MASK(0));


/* Handle an incoming host command */
static void handle_host_command(int cmd)
{
	host_cmd_args.command = cmd;
	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_args.send_response = lpc_send_response;

	/* See if we have an old or new style command */
	if (lpc_host_args->flags & EC_HOST_ARGS_FLAG_FROM_HOST) {
		/* New style command */
		int size = lpc_host_args->data_size;
		int csum, i;

		host_cmd_args.version = lpc_host_args->command_version;
		host_cmd_args.params = cmd_params;
		host_cmd_args.params_size = size;
		host_cmd_args.response = cmd_params;
		host_cmd_args.response_max = EC_HOST_PARAM_SIZE;
		host_cmd_args.response_size = 0;

		/* Verify params size */
		if (size > EC_HOST_PARAM_SIZE) {
			host_cmd_args.result = EC_RES_INVALID_PARAM;
		} else {
			/* Verify checksum */
			csum = host_cmd_args.command +
				lpc_host_args->flags +
				lpc_host_args->command_version +
				lpc_host_args->data_size;

			for (i = 0; i < size; i++)
				csum += cmd_params[i];

			if ((uint8_t)csum != lpc_host_args->checksum)
				host_cmd_args.result = EC_RES_INVALID_CHECKSUM;
		}
	} else {
		/* Old style command */
		host_cmd_args.version = 0;
		host_cmd_args.params = old_params;
		host_cmd_args.params_size = EC_OLD_PARAM_SIZE;
		host_cmd_args.response = old_params;
		host_cmd_args.response_max = EC_OLD_PARAM_SIZE;
		host_cmd_args.response_size = 0;
	}

	/* Hand off to host command handler */
	host_command_received(&host_cmd_args);
}

/* LPC interrupt handler */
static void lpc_interrupt(void)
{
	uint32_t mis = LM4_LPC_LPCMIS;

	/* Clear the interrupt bits we're handling */
	LM4_LPC_LPCIC = mis;

#ifdef CONFIG_TASK_HOSTCMD
	/* Handle ACPI command and data writes */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_ACPI, 4))
		handle_acpi_write(1);
	if (mis & LM4_LPC_INT_MASK(LPC_CH_ACPI, 2))
		handle_acpi_write(0);

	/* Handle user command writes */
	if (mis & LM4_LPC_INT_MASK(LPC_CH_CMD, 4)) {
		/* Set the busy bit */
		LM4_LPC_ST(LPC_CH_CMD) |= LM4_LPC_ST_BUSY;

		/*
		 * Read the command byte.  This clears the FRMH bit in the
		 * status byte.
		 */
		handle_host_command(LPC_POOL_CMD[0]);
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

	/*
	 * Set LPC channel 0 to I/O address 0x62 (data) / 0x66 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd)
	 */
	LM4_LPC_ADR(LPC_CH_ACPI) = EC_LPC_ADDR_ACPI_DATA;
	LM4_LPC_CTL(LPC_CH_ACPI) = (LPC_POOL_OFFS_ACPI << (5 - 1));
	LM4_LPC_ST(LPC_CH_ACPI) = 0;
	/* Unmask interrupt for host command and data writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_ACPI, 6);

	/*
	 * Set LPC channel 1 to I/O address 0x80 (data), single endpoint,
	 * pool bytes 4(data)/5(cmd).
	 */
	LM4_LPC_ADR(LPC_CH_PORT80) = 0x80;
	LM4_LPC_CTL(LPC_CH_PORT80) = (LPC_POOL_OFFS_PORT80 << (5 - 1));
	/* Unmask interrupt for host data writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_PORT80, 2);

	/*
	 * Set LPC channel 2 to I/O address 0x880, range endpoint,
	 * arbitration disabled, pool bytes 512-639.  To access this from
	 * x86, use the following command to set GEN_LPC2:
	 *
	 *   pci_write32 0 0x1f 0 0x88 0x007c0801
	 */
	LM4_LPC_ADR(LPC_CH_CMD_DATA) = EC_LPC_ADDR_HOST_ARGS;
	LM4_LPC_CTL(LPC_CH_CMD_DATA) = 0x8019 |
		(LPC_POOL_OFFS_CMD_DATA << (5 - 1));

	/*
	 * Set LPC channel 3 to I/O address 0x60 (data) / 0x64 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd)
	 */
	LM4_LPC_ADR(LPC_CH_KEYBOARD) = 0x60;
	LM4_LPC_CTL(LPC_CH_KEYBOARD) = (1 << 24/* IRQSEL1 */) |
		(0 << 18/* IRQEN1 */) | (LPC_POOL_OFFS_KEYBOARD << (5 - 1));
	LM4_LPC_ST(LPC_CH_KEYBOARD) = 0;
	/* Unmask interrupt for host command/data writes and data reads */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 7);

	/*
	 * Set LPC channel 4 to I/O address 0x200 (data) / 0x204 (command),
	 * single endpoint, offset 0 for host command/writes and 1 for EC
	 * data writes, pool bytes 0(data)/1(cmd)
	 */
	LM4_LPC_ADR(LPC_CH_CMD) = EC_LPC_ADDR_HOST_DATA;
	LM4_LPC_CTL(LPC_CH_CMD) = (LPC_POOL_OFFS_CMD << (5 - 1));
	LM4_LPC_ST(LPC_CH_CMD) = 0;
	/* Unmask interrupt for host command writes */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_CMD, 4);

	/*
	 * Set LPC channel 5 to I/O address 0x900, range endpoint,
	 * arbitration enabled, pool bytes 768-1023.  To access this from
	 * x86, use the following command to set GEN_LPC3:
	 *
	 *   pci_write32 0 0x1f 0 0x8c 0x007c0901
	 */
	LM4_LPC_ADR(LPC_CH_MEMMAP) = EC_LPC_ADDR_MEMMAP;
	LM4_LPC_CTL(LPC_CH_MEMMAP) = 0x0019 | (LPC_POOL_OFFS_MEMMAP << (5 - 1));

	/*
	 * Set LPC channel 7 to COM port I/O address.  Note that channel 7
	 * ignores the TYPE bit and is always an 8-byte range.
	 */
	LM4_LPC_ADR(LPC_CH_COMX) = LPC_COMX_ADDR;
	/*
	 * TODO: could configure IRQSELs and set IRQEN2/CX, and then the host
	 * can enable IRQs on its own.
	 */
	LM4_LPC_CTL(LPC_CH_COMX) = 0x0004 | (LPC_POOL_OFFS_COMX << (5 - 1));
	/* Enable COMx emulation for reads and writes. */
	LM4_LPC_LPCDMACX = 0x00310000;
	/*
	 * Unmask interrupt for host data writes.  We don't need interrupts for
	 * reads, because there's no flow control in that direction; LPC is
	 * much faster than the UART, and the UART doesn't have anywhere
	 * sensible to buffer input anyway.
	 */
	LM4_LPC_LPCIM |= LM4_LPC_INT_MASK(LPC_CH_COMX, 2);

	/*
	 * Unmaksk LPC bus reset interrupt.  This lets us monitor the PCH
	 * PLTRST# signal for debugging.
	 */
	LM4_LPC_LPCIM |= (1 << 31);

	/* Enable LPC channels */
	LM4_LPC_LPCCTL = LM4_LPC_SCI_CLK_1 |
		(1 << LPC_CH_ACPI) |
		(1 << LPC_CH_PORT80) |
		(1 << LPC_CH_CMD_DATA) |
		(1 << LPC_CH_KEYBOARD) |
		(1 << LPC_CH_CMD) |
		(1 << LPC_CH_MEMMAP) |
		(1 << LPC_CH_COMX);

	/*
	 * Ensure the EC (slave) has control of the memory-mapped I/O space.
	 * Once the EC has won arbtration for the memory-mapped space, it will
	 * keep control of it until it writes the last byte in the space.
	 * (That never happens; we can't use the last byte in the space because
	 * ACPI can't see it anyway.)
	 */
	while (!(LM4_LPC_ST(LPC_CH_MEMMAP) & 0x10)) {
		/* Clear HW1ST */
		LM4_LPC_ST(LPC_CH_MEMMAP) &= ~0x40;
		/* Do a dummy slave write; this should cause SW1ST to be set */
		*LPC_POOL_MEMMAP = *LPC_POOL_MEMMAP;
	}

	/* Initialize host args and memory map to all zero */
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	/* We support LPC args */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED;

	/* Enable LPC interrupt */
	task_enable_irq(LM4_IRQ_LPC);

	/* Enable COMx UART */
	uart_comx_enable();

	/* Restore event masks if needed */
	lpc_post_sysjump();

	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */
	update_host_event_status();

	return EC_SUCCESS;
}
/*
 * Set prio to higher than default so other inits can initialize their
 * memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);


static int lpc_resume(void)
{
	/* Mask all host events until the host unmasks them itself.  */
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);

	/* Store port 80 event so we know where resume happened */
	port_80_write(PORT_80_EVENT_RESUME);

	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, lpc_resume, HOOK_PRIO_DEFAULT);
