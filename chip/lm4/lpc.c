/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "port80.h"
#include "pwm.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* LPC channels */
#define LPC_CH_ACPI     0  /* ACPI commands */
#define LPC_CH_PORT80   1  /* Port 80 debug output */
#define LPC_CH_CMD_DATA 2  /* Data for host commands (args/params/response) */
#define LPC_CH_KEYBOARD 3  /* 8042 keyboard emulation */
#define LPC_CH_CMD      4  /* Host commands */
#define LPC_CH_MEMMAP   5  /* Memory-mapped data */
#define LPC_CH_COMX     7  /* UART emulation */
/* LPC pool offsets */
#define LPC_POOL_OFFS_ACPI       0  /* ACPI commands - 0=in, 1=out */
#define LPC_POOL_OFFS_PORT80     4  /* Port 80 - 4=in, 5=out */
#define LPC_POOL_OFFS_COMX       8  /* UART emulation range - 8-15 */
#define LPC_POOL_OFFS_KEYBOARD  16  /* Keyboard - 16=in, 17=out */
#define LPC_POOL_OFFS_CMD       20  /* Host commands - 20=in, 21=out */
#define LPC_POOL_OFFS_CMD_DATA 512  /* Data range for host commands - 512-767 */
#define LPC_POOL_OFFS_MEMMAP   768  /* Memory-mapped data - 768-1023 */
/* LPC pool data pointers */
#define LPC_POOL_ACPI     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_ACPI)
#define LPC_POOL_PORT80   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_PORT80)
#define LPC_POOL_COMX     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_COMX)
#define LPC_POOL_KEYBOARD (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_KEYBOARD)
#define LPC_POOL_CMD      (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_CMD)
#define LPC_POOL_CMD_DATA (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_CMD_DATA)
#define LPC_POOL_MEMMAP   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_MEMMAP)
/* LPC COMx I/O address (in x86 I/O address space) */
#define LPC_COMX_ADDR 0x3f8  /* COM1 */

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
static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags;   /* Flags from host command */

/* Params must be 32-bit aligned */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __attribute__((aligned(4)));
static int init_done;

static uint8_t * const cmd_params = (uint8_t *)LPC_POOL_CMD_DATA +
	EC_LPC_ADDR_HOST_PARAM - EC_LPC_ADDR_HOST_ARGS;
static struct ec_lpc_host_args * const lpc_host_args =
	(struct ec_lpc_host_args *)LPC_POOL_CMD_DATA;

/* Configure GPIOs for module */
static void configure_gpio(void)
{
	/*
	 * Set digital alternate function 15 for PL0:5, PM0:2, PM4:5 pins.
	 *
	 * I/O: PL0:3 = command/address/data
	 * inp: PL4 (frame), PL5 (reset), PM0 (powerdown), PM5 (clock)
	 * out: PM1 (sci), PM4 (serirq)
	 */
	gpio_set_alternate_function(LM4_GPIO_L, 0x3f, 0x0f);
	gpio_set_alternate_function(LM4_GPIO_M, 0x33, 0x0f);
}

static void wait_irq_sent(void)
{
	/*
	 * TODO(yjlou): udelay() is not graceful. Since the SIRQRIS is almost
	 * not cleared in continuous mode and EC has problem to file more than
	 * 1 frame in the quiet mode, this is the best way we can do right now.
	 *
	 * 4 us is the time of 2 SERIRQ frames, which is long enough to
	 * guarantee the IRQ has been sent out.
	 */
	udelay(4);
}

static void wait_send_serirq(uint32_t lpcirqctl)
{
	LM4_LPC_LPCIRQCTL = lpcirqctl;
	wait_irq_sent();
}

/**
 * Manually generate an IRQ to host (edge-trigger).
 *
 * @param irq_num	IRQ number to generate.  Pass 0 to set the AH
 *			(active high) bit.
 *
 * For SERIRQ quite mode, we need to set LM4_LPC_LPCIRQCTL twice.
 * The first one is to assert IRQ (pull low), and then the second one is
 * to de-assert it. This generates a pulse (high-low-high) for an IRQ.
 */
static void lpc_manual_irq(int irq_num)
{
	uint32_t common_bits =
	    0x00000004 |  /* PULSE */
	    0x00000002 |  /* ONCHG - for quiet mode */
	    0x00000001;   /* SND - send immediately */

	/* Send out the IRQ first. */
	wait_send_serirq((1 << (irq_num + 16)) | common_bits);

	/* Generate a all-high frame to simulate a rising edge. */
	wait_send_serirq(common_bits);
}

/**
 * Generate SMI pulse to the host chipset via GPIO.
 *
 * If the x86 is in S0, SMI# is sampled at 33MHz, so minimum pulse length is
 * 60ns.  If the x86 is in S3, SMI# is sampled at 32.768KHz, so we need pulse
 * length >61us.  Both are short enough and events are infrequent, so just
 * delay for 65us.
 */
static void lpc_generate_smi(void)
{
	gpio_set_level(GPIO_PCH_SMI_L, 0);
	udelay(65);
	gpio_set_level(GPIO_PCH_SMI_L, 1);

	if (host_events & event_mask[LPC_HOST_EVENT_SMI])
		CPRINTF("[%T smi 0x%08x]\n",
			host_events & event_mask[LPC_HOST_EVENT_SMI]);
}

/**
 * Generate SCI pulse to the host chipset via LPC0SCI.
 */
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
	int csum;
	int i;

	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (args->result == EC_RES_IN_PROGRESS)
		return;

	/* Handle negative size */
	if (size < 0) {
		args->result = EC_RES_INVALID_RESPONSE;
		size = 0;
	}

	/* New-style response */
	lpc_host_args->flags =
		(host_cmd_flags & ~EC_HOST_ARGS_FLAG_FROM_HOST) |
		EC_HOST_ARGS_FLAG_TO_HOST;

	lpc_host_args->data_size = size;

	csum = args->command + lpc_host_args->flags +
		lpc_host_args->command_version +
		lpc_host_args->data_size;

	for (i = 0, out = (uint8_t *)args->response; i < size; i++, out++)
		csum += *out;

	lpc_host_args->checksum = (uint8_t)csum;

	/* Fail if response doesn't fit in the param buffer */
	if (size > EC_PROTO2_MAX_PARAM_SIZE)
		args->result = EC_RES_INVALID_RESPONSE;

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

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/*
	 * Write result to the data byte.  This sets the TOH bit in the
	 * status byte and triggers an IRQ on the host so the host can read
	 * the result.
	 *
	 * TODO: (crosbug.com/p/7496) or it would, if we actually set up host
	 * IRQs
	 */
	LPC_POOL_CMD[1] = pkt->driver_result;

	/* Clear the busy bit */
	task_disable_irq(LM4_IRQ_LPC);
	LM4_LPC_ST(LPC_CH_CMD) &= ~LM4_LPC_ST_BUSY;
	task_enable_irq(LM4_IRQ_LPC);
}

int lpc_keyboard_has_char(void)
{
	return (LM4_LPC_ST(LPC_CH_KEYBOARD) & LM4_LPC_ST_TOH) ? 1 : 0;
}

/* Return true if the FRMH is set */
int lpc_keyboard_input_pending(void)
{
	return (LM4_LPC_ST(LPC_CH_KEYBOARD) & LM4_LPC_ST_FRMH) ? 1 : 0;
}

/* Put a char to host buffer and send IRQ if specified. */
void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	LPC_POOL_KEYBOARD[1] = chr;
	if (send_irq) {
		lpc_manual_irq(1);  /* IRQ#1 */
	}
}

void lpc_keyboard_clear_buffer(void)
{
	/* Make sure the previous TOH and IRQ has been sent out. */
	wait_irq_sent();

	LM4_LPC_ST(LPC_CH_KEYBOARD) &= ~LM4_LPC_ST_TOH;

	/* Ensure there is no TOH set in this period. */
	wait_irq_sent();
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char())
		lpc_manual_irq(1);
}

int lpc_comx_has_char(void)
{
	return LM4_LPC_ST(LPC_CH_COMX) & LM4_LPC_ST_FRMH;
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
 * Update the host event status.
 *
 * Sends a pulse if masked event status becomes non-zero:
 *   - SMI pulse via EC_SMI_L GPIO
 *   - SCI pulse via LPC0SCI
 */
static void update_host_event_status(void) {
	int need_sci = 0;
	int need_smi = 0;
	uint32_t active_wake_events;

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

	/* Process the wake events. */
	active_wake_events = host_events & event_mask[LPC_HOST_EVENT_WAKE];
	board_process_wake_events(active_wake_events);

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
 * Handle write to ACPI I/O port
 *
 * @param is_cmd	Is write command (is_cmd=1) or data (is_cmd=0)
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
#ifdef CONFIG_PWM_KBLIGHT
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
		switch (acpi_addr) {
		case EC_ACPI_MEM_TEST:
			CPRINTF("[%T ACPI mem test 0x%02x]\n", data);
			acpi_mem_test = data;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * Debug output with CR not newline, because the host
			 * does a lot of keyboard backlights and it scrolls the
			 * debug console.
			 */
			CPRINTF("\r[%T ACPI kblight %d]", data);
			pwm_set_keyboard_backlight(data);
			break;
#endif
		default:
			CPRINTF("[%T ACPI write 0x%02x = 0x%02x]\n",
				acpi_addr, data);
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
 * Handle write to host command I/O ports.
 *
 * @param is_cmd	Is write command (1) or data (0)?
 */
static void handle_host_write(int is_cmd)
{
	/* Ignore data writes */
	if (!is_cmd) {
		LM4_LPC_ST(LPC_CH_CMD) &= ~LM4_LPC_ST_FRMH;
		return;
	}

	/* Set the busy bit */
	LM4_LPC_ST(LPC_CH_CMD) |= LM4_LPC_ST_BUSY;

	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = LPC_POOL_CMD[0];

	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_args.send_response = lpc_send_response;
	host_cmd_flags = lpc_host_args->flags;

	/* See if we have an old or new style command */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request = (const void *)LPC_POOL_CMD_DATA;
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so pass in the entire buffer */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response = (void *)LPC_POOL_CMD_DATA;
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;
		host_packet_receive(&lpc_packet);
		return;

	} else if (host_cmd_flags & EC_HOST_ARGS_FLAG_FROM_HOST) {
		/* Version 2 (link) style command */
		int size = lpc_host_args->data_size;
		int csum, i;

		host_cmd_args.version = lpc_host_args->command_version;
		host_cmd_args.params = params_copy;
		host_cmd_args.params_size = size;
		host_cmd_args.response = cmd_params;
		host_cmd_args.response_max = EC_PROTO2_MAX_PARAM_SIZE;
		host_cmd_args.response_size = 0;

		/* Verify params size */
		if (size > EC_PROTO2_MAX_PARAM_SIZE) {
			host_cmd_args.result = EC_RES_INVALID_PARAM;
		} else {
			const uint8_t *src = cmd_params;
			uint8_t *copy = params_copy;

			/*
			 * Verify checksum and copy params out of LPC space.
			 * This ensures the data acted on by the host command
			 * handler can't be changed by host writes after the
			 * checksum is verified.
			 */
			csum = host_cmd_args.command +
				host_cmd_flags +
				host_cmd_args.version +
				host_cmd_args.params_size;

			for (i = 0; i < size; i++) {
				csum += *src;
				*(copy++) = *(src++);
			}

			if ((uint8_t)csum != lpc_host_args->checksum)
				host_cmd_args.result = EC_RES_INVALID_CHECKSUM;
		}
	} else {
		/* Old style command, now unsupported */
		host_cmd_args.result = EC_RES_INVALID_COMMAND;
	}

	/* Hand off to host command handler */
	host_command_received(&host_cmd_args);
}

/**
 * LPC interrupt handler
 */
static void lpc_interrupt(void)
{
	uint32_t mis = LM4_LPC_LPCMIS;
	uint32_t st;

	/* Clear the interrupt bits we're handling */
	LM4_LPC_LPCIC = mis;

#ifdef HAS_TASK_HOSTCMD
	/* Handle ACPI command and data writes */
	st = LM4_LPC_ST(LPC_CH_ACPI);
	if (st & LM4_LPC_ST_FRMH)
		handle_acpi_write(st & LM4_LPC_ST_CMD);

	/* Handle user command writes */
	st = LM4_LPC_ST(LPC_CH_CMD);
	if (st & LM4_LPC_ST_FRMH)
		handle_host_write(st & LM4_LPC_ST_CMD);
#endif

	/*
	 * Handle port 80 writes (CH0MIS1).  Due to crosbug.com/p/12349 the
	 * interrupt status (mis & LM4_LPC_INT_MASK(LPC_CH_PORT80, 2))
	 * apparently gets lost on back-to-back writes to port 80, so check the
	 * FRMH bit in the channel status register to see if a write is
	 * pending.  Loop to handle bursts of back-to-back writes.
	 */
	while (LM4_LPC_ST(LPC_CH_PORT80) & LM4_LPC_ST_FRMH)
		port_80_write(LPC_POOL_PORT80[0]);

#ifdef HAS_TASK_KEYPROTO
	/* Handle keyboard interface writes */
	st = LM4_LPC_ST(LPC_CH_KEYBOARD);
	if (st & LM4_LPC_ST_FRMH)
		keyboard_host_write(LPC_POOL_KEYBOARD[0], st & LM4_LPC_ST_CMD);

	if (mis & LM4_LPC_INT_MASK(LPC_CH_KEYBOARD, 1)) {
		/* Host read data; wake up task to send remaining bytes */
		task_wake(TASK_ID_KEYPROTO);
	}
#endif

	/* Handle COMx */
	if (lpc_comx_has_char()) {
		/* Copy a character to the UART if there's space */
		if (uart_comx_putc_ok())
			uart_comx_putc(lpc_comx_get_char());
	}

	/* Debugging: print changes to LPC0RESET */
	if (mis & (1 << 31)) {
		if (LM4_LPC_LPCSTS & (1 << 10)) {
			int i;

			/* Store port 80 reset event */
			port_80_write(PORT_80_EVENT_RESET);

			/*
			 * Workaround for crosbug.com/p/12349; clear all FRMH
			 * bits so host writes will trigger interrupts.
			 */
			for (i = 0; i < 8; i++)
				LM4_LPC_ST(i) &= ~LM4_LPC_ST_FRMH;
		}

		CPRINTF("[%T LPC RESET# %sasserted]\n",
			(LM4_LPC_LPCSTS & (1<<10)) ? "" : "de");
	}
}
DECLARE_IRQ(LM4_IRQ_LPC, lpc_interrupt, 2);

/**
 * Preserve event masks across a sysjump.
 */
static void lpc_sysjump(void)
{
	system_add_jump_tag(LPC_SYSJUMP_TAG, 1,
			    sizeof(event_mask), event_mask);
}
DECLARE_HOOK(HOOK_SYSJUMP, lpc_sysjump, HOOK_PRIO_DEFAULT);

/**
 * Restore event masks after a sysjump.
 */
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

static void lpc_init(void)
{
	/* Enable RGCGLPC then delay a few clocks. */
	LM4_SYSTEM_RCGCLPC = 1;
	clock_wait_cycles(6);

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

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;

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
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

static void lpc_resume(void)
{
	/* Mask all host events until the host unmasks them itself.  */
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);

	/* Store port 80 event so we know where resume happened */
	port_80_write(PORT_80_EVENT_RESUME);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, lpc_resume, HOOK_PRIO_DEFAULT);

static void lpc_tick(void)
{
	/*
	 * Make sure pending LPC interrupts have been processed.
	 * This works around a LM4 bug where host writes sometimes
	 * don't trigger interrupts.  See crosbug.com/p/13965.
	 */
	task_trigger_irq(LM4_IRQ_LPC);
}
DECLARE_HOOK(HOOK_TICK, lpc_tick, HOOK_PRIO_DEFAULT);

/**
 * Get protocol information
 */
static int lpc_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = (1 << 2) | (1 << 3);
	r->max_request_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->max_response_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		     lpc_get_protocol_info,
		     EC_VER_MASK(0));
