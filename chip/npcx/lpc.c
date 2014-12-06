/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "acpi.h"
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
#include "system_chip.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

#define LPC_SYSJUMP_TAG 0x4c50          /* "LP" */

static uint32_t host_events;            /* Currently pending SCI/SMI events */
static uint32_t event_mask[3];          /* Event masks for each type */
static struct	host_packet lpc_packet;
static struct	host_cmd_handler_args host_cmd_args;
static uint8_t	host_cmd_flags;         /* Flags from host command */
static uint8_t	shm_mem_host_cmd[256] __aligned(8);
static uint8_t	shm_memmap[256] __aligned(8);
/* Params must be 32-bit aligned */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static int init_done;

static uint8_t * const cmd_params = (uint8_t *)shm_mem_host_cmd +
		EC_LPC_ADDR_HOST_PARAM - EC_LPC_ADDR_HOST_ARGS;
static struct ec_lpc_host_args * const lpc_host_args =
		(struct ec_lpc_host_args *)shm_mem_host_cmd;


#ifdef CONFIG_KEYBOARD_IRQ_GPIO
static void keyboard_irq_assert(void)
{
	/*
	 * Enforce signal-high for long enough for the signal to be pulled high
	 * by the external pullup resistor.  This ensures the host will see the
	 * following falling edge, regardless of the line state before this
	 * function call.
	 */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
	udelay(4);
	/* Generate a falling edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 0);
	udelay(4);
	/* Set signal high, now that we've generated the edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
}
#else
static inline void keyboard_irq_assert(void)
{
	/* Use serirq method. */
	/* Using manual IRQ for KBC  */
	SET_BIT(NPCX_HIIRQC, 0); /* set IRQ1B to high */
	CLEAR_BIT(NPCX_HICTRL, 0); /* set IRQ1 control by IRQB1 */
}
#endif

static void lpc_task_enable_irq(void){

	task_enable_irq(NPCX_IRQ_SHM);
	task_enable_irq(NPCX_IRQ_KBC_IBF);
	task_enable_irq(NPCX_IRQ_PM_CHAN_IBF);
	task_enable_irq(NPCX_IRQ_PORT80);
}
static void lpc_task_disable_irq(void){

	task_disable_irq(NPCX_IRQ_SHM);
	task_disable_irq(NPCX_IRQ_KBC_IBF);
	task_disable_irq(NPCX_IRQ_PM_CHAN_IBF);
	task_disable_irq(NPCX_IRQ_PORT80);
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
#ifdef CONFIG_SCI_GPIO
	/* Enforce signal-high for long enough to debounce high */
	gpio_set_level(GPIO_PCH_SMI_L, 1);
	udelay(65);
	/* Generate a falling edge */
	gpio_set_level(GPIO_PCH_SMI_L, 0);
	udelay(65);
	/* Set signal high, now that we've generated the edge */
	gpio_set_level(GPIO_PCH_SMI_L, 1);
#else
	NPCX_HIPMIE(PM_CHAN_1) |= NPCX_HIPMIE_SMIE;
#endif
	if (host_events & event_mask[LPC_HOST_EVENT_SMI])
		CPRINTS("smi 0x%08x",
			host_events & event_mask[LPC_HOST_EVENT_SMI]);
}

/**
 * Generate SCI pulse to the host chipset via LPC0SCI.
 */
static void lpc_generate_sci(void)
{
#ifdef CONFIG_SCI_GPIO
	/* Enforce signal-high for long enough to debounce high */
	gpio_set_level(CONFIG_SCI_GPIO, 1);
	udelay(65);
	/* Generate a falling edge */
	gpio_set_level(CONFIG_SCI_GPIO, 0);
	udelay(65);
	/* Set signal high, now that we've generated the edge */
	gpio_set_level(CONFIG_SCI_GPIO, 1);
#else
	SET_BIT(NPCX_HIPMIE(PM_CHAN_1), NPCX_HIPMIE_SCIE);
#endif

	if (host_events & event_mask[LPC_HOST_EVENT_SCI])
		CPRINTS("sci 0x%08x",
			host_events & event_mask[LPC_HOST_EVENT_SCI]);
}

/**
 * Update the level-sensitive wake signal to the AP.
 *
 * @param wake_events	Currently asserted wake events
 */
static void lpc_update_wake(uint32_t wake_events)
{
	/*
	 * Mask off power button event, since the AP gets that through a
	 * separate dedicated GPIO.
	 */
	wake_events &= ~EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON);

	/* Signal is asserted low when wake events is non-zero */
	gpio_set_level(GPIO_PCH_WAKE_L, !wake_events);
}

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)shm_memmap;
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

	/* Write result to the data byte.  This sets the TOH status bit. */
	NPCX_HIPMDO(PM_CHAN_2) = args->result;
	/* Clear processing flag */
	CLEAR_BIT(NPCX_HIPMST(PM_CHAN_2), 2);
}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result to the data byte.  This sets the TOH status bit. */
	NPCX_HIPMDO(PM_CHAN_2) = pkt->driver_result;
	/* Clear processing flag */
	CLEAR_BIT(NPCX_HIPMST(PM_CHAN_2), 2);
}

int lpc_keyboard_has_char(void)
{
	/* if OBF  '1', that mean still have a data in the FIFO */
	return (NPCX_HIKMST&0x01) ? 1 : 0;
}

/* Return true if the FRMH is set */
int lpc_keyboard_input_pending(void)
{
	return (NPCX_HIKMST&0x02) ? 1 : 0;
}

/* Put a char to host buffer and send IRQ if specified. */
void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	UPDATE_BIT(NPCX_HICTRL, NPCX_HICTRL_OBFKIE, send_irq);
	NPCX_HIKDO = chr;
	task_enable_irq(NPCX_IRQ_KBC_OBF);
}

void lpc_keyboard_clear_buffer(void)
{
	/* Make sure the previous TOH and IRQ has been sent out. */
	udelay(4);
	/*FW_OBF write 1*/
	NPCX_HICTRL |= 0x80;
	/* Ensure there is no TOH set in this period. */
	udelay(4);
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char())
		keyboard_irq_assert();
}

/**
 * Update the host event status.
 *
 * Sends a pulse if masked event status becomes non-zero:
 *   - SMI pulse via EC_SMI_L GPIO
 *   - SCI pulse via LPC0SCI
 */
static void update_host_event_status(void)
{
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable LPC interrupt while updating status register */
	lpc_task_disable_irq();
	if (host_events & event_mask[LPC_HOST_EVENT_SMI]) {
		/* Only generate SMI for first event */
		if (!(NPCX_HIPMIE(PM_CHAN_1) & NPCX_HIPMIE_SMIE))
			need_smi = 1;
		SET_BIT(NPCX_HIPMIE(PM_CHAN_1), NPCX_HIPMIE_SMIE);
	} else
		CLEAR_BIT(NPCX_HIPMIE(PM_CHAN_1), NPCX_HIPMIE_SMIE);

	if (host_events & event_mask[LPC_HOST_EVENT_SCI]) {
		/* Generate SCI for every event */
		need_sci = 1;
		SET_BIT(NPCX_HIPMIE(PM_CHAN_1), NPCX_HIPMIE_SCIE);
	} else
		CLEAR_BIT(NPCX_HIPMIE(PM_CHAN_1), NPCX_HIPMIE_SCIE);

	/* Copy host events to mapped memory */
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = host_events;

	lpc_task_enable_irq();

	/* Process the wake events. */
	lpc_update_wake(host_events & event_mask[LPC_HOST_EVENT_WAKE]);

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

int lpc_query_host_event_state(void)
{
	const uint32_t any_mask = event_mask[0] | event_mask[1] | event_mask[2];
	int evt_index = 0;
	int i;

	for (i = 0; i < 32; i++) {
		const uint32_t e = (1 << i);

		if (host_events & e) {
			host_clear_events(e);

			/*
			 * If host hasn't unmasked this event, drop it.  We do
			 * this at query time rather than event generation time
			 * so that the host has a chance to unmask events
			 * before they're dropped by a query.
			 */
			if (!(e & any_mask))
				continue;

			evt_index = i + 1;	/* Events are 1-based */
			break;
		}
	}

	return evt_index;
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

int lpc_get_pltrst_asserted(void)
{
	/* TODO: (Simon) need to define GPIO_PLTRST */
	return 0;
}

/**
 * Handle write to ACPI I/O port
 *
 * @param is_cmd	Is write command (is_cmd=1) or data (is_cmd=0)
 */
static void handle_acpi_write(int is_cmd)
{
	uint8_t value, result;

	/* Read command/data; this clears the FRMH status bit. */
	value = NPCX_HIPMDI(PM_CHAN_1);

	/* Handle whatever this was. */
	if (acpi_ap_to_ec(is_cmd, value, &result))
		NPCX_HIPMDO(PM_CHAN_1) = result;

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
	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = NPCX_HIPMDI(PM_CHAN_2);

	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_args.send_response = lpc_send_response;
	host_cmd_flags = lpc_host_args->flags;

	/* See if we have an old or new style command */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request = (const void *)shm_mem_host_cmd;
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so pass in the entire buffer */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response = (void *)shm_mem_host_cmd;
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;
		/* Set processing flag */
		SET_BIT(NPCX_HIPMST(PM_CHAN_2), 2);
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


void lpc_shm_interrupt(void){
}
DECLARE_IRQ(NPCX_IRQ_SHM, lpc_shm_interrupt, 2);

void lpc_kbc_ibf_interrupt(void)
{
#ifdef CONFIG_KEYBOARD_PROTOCOL_8042
	/* If "command" input 0, else 1*/
	keyboard_host_write(NPCX_HIKMDI, (NPCX_HIKMST & 0x08) ? 1 : 0);
#endif
}
DECLARE_IRQ(NPCX_IRQ_KBC_IBF, lpc_kbc_ibf_interrupt, 2);

void lpc_kbc_obf_interrupt(void){
	/* reserve for future handle */
	if (!IS_BIT_SET(NPCX_HICTRL, 0)) {
		SET_BIT(NPCX_HICTRL, 0);    /* back to H/W control of IRQ1 */
		CLEAR_BIT(NPCX_HIIRQC, 0);  /* back to default of IRQB1 */
	}
	task_disable_irq(NPCX_IRQ_KBC_OBF);
}
DECLARE_IRQ(NPCX_IRQ_KBC_OBF, lpc_kbc_obf_interrupt, 2);

void lpc_pmc_ibf_interrupt(void){
	/* Channel-1 for ACPI usage*/
	/* Channel-2 for Host Command usage , so the argument data had been
	 * put on the share memory firstly*/
	if (NPCX_HIPMST(PM_CHAN_1) & 0x02)
		handle_acpi_write((NPCX_HIPMST(PM_CHAN_1)&0x08) ? 1 : 0);
	else if (NPCX_HIPMST(PM_CHAN_2)&0x02)
		handle_host_write((NPCX_HIPMST(PM_CHAN_2)&0x08) ? 1 : 0);
}
DECLARE_IRQ(NPCX_IRQ_PM_CHAN_IBF, lpc_pmc_ibf_interrupt, 2);

void lpc_pmc_obf_interrupt(void){
}
DECLARE_IRQ(NPCX_IRQ_PM_CHAN_OBF, lpc_pmc_obf_interrupt, 2);

void lpc_port80_interrupt(void){
	port_80_write((NPCX_GLUE_SDPD0<<0) | (NPCX_GLUE_SDPD1<<8));
	/* No matter what , just clear error status bit */
	SET_BIT(NPCX_DP80STS, 7);
	SET_BIT(NPCX_DP80STS, 5);
}
DECLARE_IRQ(NPCX_IRQ_PORT80, lpc_port80_interrupt, 2);

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
	/* Enable clock for LPC peripheral */
	clock_enable_peripheral(CGC_OFFSET_LPC, CGC_LPC_MASK,
			CGC_MODE_RUN | CGC_MODE_SLEEP);
	/* Switching to LPC interface */
	NPCX_DEVCNT |= 0x04;
	/* Enable 4E/4F */
	if (!IS_BIT_SET(NPCX_MSWCTL1, 3)) {
		NPCX_HCBAL = 0x4E;
		NPCX_HCBAH = 0x0;
	}
	/* Clear Host Access Hold state */
	NPCX_SMC_CTL = 0xC0;

	/* Initialize Hardware for UART Host */
#if CONFIG_UART_HOST
	/* Init COMx LPC UART */
	/* FMCLK have to using 50MHz */
	NPCX_DEVALT(0xB) = 0xFF;
	/* Make sure Host Access unlock */
	CLEAR_BIT(NPCX_LKSIOHA, 2);
	/* Clear Host Access Lock Violation */
	SET_BIT(NPCX_SIOLV, 2);
#endif

	/* Don't stall SHM transactions */
	NPCX_SHM_CTL = NPCX_SHM_CTL & ~0x40;
	/* Semaphore and Indirect access disable */
	NPCX_SHCFG = 0xE0;
	/* Disable Protect Win1&2*/
	NPCX_WIN_WR_PROT(0) = 0;
	NPCX_WIN_WR_PROT(1) = 0;
	NPCX_WIN_RD_PROT(0) = 0;
	NPCX_WIN_RD_PROT(1) = 0;
	/* Open Win1 256 byte for Host CMD, Win2 256 for MEMMAP*/
	NPCX_WIN_SIZE = 0x88;
	NPCX_WIN_BASE(0) = (uint32_t)shm_mem_host_cmd;
	NPCX_WIN_BASE(1) = (uint32_t)shm_memmap;

	/* Turn on PMC2 for Host Command usage */
	SET_BIT(NPCX_HIPMCTL(PM_CHAN_2), 0);
	SET_BIT(NPCX_HIPMCTL(PM_CHAN_2), 1);
	/* enable PMC2 IRQ */
	SET_BIT(NPCX_HIPMIE(PM_CHAN_2), 0);
	/* IRQ control from HW */
	SET_BIT(NPCX_HIPMIE(PM_CHAN_2), 3);
	/*
	 * Set required control value (avoid setting HOSTWAIT bit at this stage)
	 */
	NPCX_SMC_CTL = NPCX_SMC_CTL&~0x7F;
	/* Clear status */
	NPCX_SMC_STS = NPCX_SMC_STS;
	/* Create mailbox */

	/*
	 * Init KBC
	 * Clear OBF status, PM1 IBF/OBF INT enable, IRQ11 enable,
	 * IBF(K&M) INT enable, OBF(K&M) empty INT enable ,
	 * OBF Mouse Full INT enable and OBF KB Full INT enable
	 */
	NPCX_HICTRL = 0xFF;
	/* Normally Polarity IRQ1,12,11 type (level + high) setting */
	NPCX_HIIRQC = 0x00;	/* Make sure to default */

	/*
	 * Init PORT80
	 * Enable Port80, Enable Port80 function & Interrupt & Read auto
	 */
	NPCX_DP80CTL = 0x29;
	SET_BIT(NPCX_GLUE_SDP_CTS, 3);
	SET_BIT(NPCX_GLUE_SDP_CTS, 0);
	/* Just turn on IRQE */
	NPCX_HIPMIE(PM_CHAN_1) = 0x01;
	lpc_task_enable_irq();

	/* Initialize host args and memory map to all zero */
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
			EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
			EC_HOST_CMD_FLAG_VERSION_3;



	/* Restore event masks if needed */
	lpc_post_sysjump();

	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */

	update_host_event_status();

	/* initial IO port address via SIB-write modules */
	system_lpc_host_register_init();
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

/* Get protocol information */
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
