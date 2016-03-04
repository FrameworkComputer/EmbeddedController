/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for MEC1322 */

#include "acpi.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "port80.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "chipset.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)

#define LPC_SYSJUMP_TAG 0x4c50  /* "LP" */

static uint8_t mem_mapped[0x200] __attribute__((section(".bss.big_align")));

static uint32_t host_events;     /* Currently pending SCI/SMI events */
static uint32_t event_mask[3];   /* Event masks for each type */
static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags;   /* Flags from host command */

static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static int init_done;

static struct ec_lpc_host_args * const lpc_host_args =
	(struct ec_lpc_host_args *)mem_mapped;

static void keyboard_irq_assert(void)
{
#ifdef CONFIG_KEYBOARD_IRQ_GPIO
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
#else
	/*
	 * SERIRQ is automatically sent by KBC
	 */
#endif
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
}

static void lpc_generate_sci(void)
{
#ifdef CONFIG_SCI_GPIO
	gpio_set_level(CONFIG_SCI_GPIO, 0);
	udelay(65);
	gpio_set_level(CONFIG_SCI_GPIO, 1);
#else
	MEC1322_ACPI_PM_STS |= 1;
	udelay(65);
	MEC1322_ACPI_PM_STS &= ~1;
#endif
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
	return mem_mapped + 0x100;
}

static uint8_t *lpc_get_hostcmd_data_range(void)
{
	return mem_mapped;
}

/**
 * Update the host event status.
 *
 * Sends a pulse if masked event status becomes non-zero:
 *   - SMI pulse via PCH_SMI_L GPIO
 *   - SCI pulse via PCH_SCI_L GPIO
 */
static void update_host_event_status(void)
{
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable LPC interrupt while updating status register */
	task_disable_irq(MEC1322_IRQ_ACPIEC0_IBF);

	if (host_events & event_mask[LPC_HOST_EVENT_SMI]) {
		/* Only generate SMI for first event */
		if (!(MEC1322_ACPI_EC_STATUS(0) & EC_LPC_STATUS_SMI_PENDING))
			need_smi = 1;
		MEC1322_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_SMI_PENDING;
	} else {
		MEC1322_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_SMI_PENDING;
	}

	if (host_events & event_mask[LPC_HOST_EVENT_SCI]) {
		/* Generate SCI for every event */
		need_sci = 1;
		MEC1322_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_SCI_PENDING;
	} else {
		MEC1322_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_SCI_PENDING;
	}

	/* Copy host events to mapped memory */
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = host_events;

	task_enable_irq(MEC1322_IRQ_ACPIEC0_IBF);

	/* Process the wake events. */
	lpc_update_wake(host_events & event_mask[LPC_HOST_EVENT_WAKE]);

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result to the data byte. */
	MEC1322_ACPI_EC_EC2OS(1, 0) = pkt->driver_result;

	/* Clear the busy bit, so the host knows the EC is done. */
	MEC1322_ACPI_EC_STATUS(1) &= ~EC_LPC_STATUS_PROCESSING;
}

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



/*
 * Most registers in LPC module are reset when the host is off. We need to
 * set up LPC again when the host is starting up.
 */
static void setup_lpc(void)
{
	gpio_config_module(MODULE_LPC, 1);

	/* Set up interrupt on LRESET# deassert */
	MEC1322_INT_SOURCE(19) = 1 << 1;
	MEC1322_INT_ENABLE(19) |= 1 << 1;
	MEC1322_INT_BLK_EN |= 1 << 19;
	task_enable_irq(MEC1322_IRQ_GIRQ19);

	/* Set up ACPI0 for 0x62/0x66 */
	MEC1322_LPC_ACPI_EC0_BAR = 0x00628304;
	MEC1322_INT_ENABLE(15) |= 1 << 6;
	MEC1322_INT_BLK_EN |= 1 << 15;
	/* Clear STATUS_PROCESSING bit in case it was set during sysjump */
	MEC1322_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_PROCESSING;
	task_enable_irq(MEC1322_IRQ_ACPIEC0_IBF);

	/* Set up ACPI1 for 0x200/0x204 */
	MEC1322_LPC_ACPI_EC1_BAR = 0x02008407;
	MEC1322_INT_ENABLE(15) |= 1 << 8;
	MEC1322_INT_BLK_EN |= 1 << 15;
	MEC1322_ACPI_EC_STATUS(1) &= ~EC_LPC_STATUS_PROCESSING;
	task_enable_irq(MEC1322_IRQ_ACPIEC1_IBF);

	/* Set up 8042 interface at 0x60/0x64 */
	MEC1322_LPC_8042_BAR = 0x00608104;

	/* Set up indication of Auxillary sts */
	MEC1322_8042_KB_CTRL |= 1 << 7;

	MEC1322_8042_ACT |= 1;
	MEC1322_INT_ENABLE(15) |= ((1 << 13) | (1 << 14));
	MEC1322_INT_BLK_EN |= 1 << 15;
	task_enable_irq(MEC1322_IRQ_8042EM_IBF);
	task_enable_irq(MEC1322_IRQ_8042EM_OBF);

#ifndef CONFIG_KEYBOARD_IRQ_GPIO
	/* Set up SERIRQ for keyboard */
	MEC1322_8042_KB_CTRL |= (1 << 5);
	MEC1322_LPC_SIRQ(1) = 0x01;
#endif

	/* Set up EMI module for memory mapped region, base address 0x800 */
	MEC1322_LPC_EMI_BAR = 0x0800800f;
	MEC1322_INT_ENABLE(15) |= 1 << 2;
	MEC1322_INT_BLK_EN |= 1 << 15;
	task_enable_irq(MEC1322_IRQ_EMI);

	/* Access data RAM through alias address */
	MEC1322_EMI_MBA0 = (uint32_t)mem_mapped - 0x118000 + 0x20000000;

	/*
	 * Limit EMI read / write range. First 256 bytes are RW for host
	 * commands. Second 256 bytes are RO for mem-mapped data.
	 */
	MEC1322_EMI_MRL0 = 0x200;
	MEC1322_EMI_MWL0 = 0x100;

	/* Set up Mailbox for Port80 trapping */
	MEC1322_MBX_INDEX = 0xff;
	MEC1322_LPC_MAILBOX_BAR = 0x00808901;

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;

	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */
	update_host_event_status();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, setup_lpc, HOOK_PRIO_FIRST);

static void lpc_resume(void)
{
#ifdef CONFIG_POWER_S0IX
	if (chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_ON))
#endif
	{
		/* Mask all host events until the host unmasks them itself.  */
		lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);
	}
	/* Store port 80 event so we know where resume happened */
	port_80_write(PORT_80_EVENT_RESUME);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, lpc_resume, HOOK_PRIO_DEFAULT);



static void lpc_init(void)
{
	/* Activate LPC interface */
	MEC1322_LPC_ACT |= 1;

	/*
	* Ring Oscillator not permitted to shut down
	* until LPC activate bit is cleared
	*/
	MEC1322_LPC_CLK_CTRL |= 3;

	/* Initialize host args and memory map to all zero */
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	setup_lpc();

	/* Restore event masks if needed */
	lpc_post_sysjump();
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void lpc_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(lpc_chipset_reset);
#endif

void girq19_interrupt(void)
{
	/* Check interrupt result for LRESET# trigger */
	if (MEC1322_INT_RESULT(19) & (1 << 1)) {
		/* Initialize LPC module when LRESET# is deasserted */
		if (!lpc_get_pltrst_asserted()) {
			setup_lpc();
		} else {
			/* Store port 80 reset event */
			port_80_write(PORT_80_EVENT_RESET);

#ifdef CONFIG_CHIPSET_RESET_HOOK
			/* Notify HOOK_CHIPSET_RESET */
			hook_call_deferred(lpc_chipset_reset, MSEC);
#endif
		}

		CPRINTS("LPC RESET# %sasserted",
			lpc_get_pltrst_asserted() ? "" : "de");

		/* Clear interrupt source */
		MEC1322_INT_SOURCE(19) = 1 << 1;
	}
}
DECLARE_IRQ(MEC1322_IRQ_GIRQ19, girq19_interrupt, 1);

void emi_interrupt(void)
{
	port_80_write(MEC1322_EMI_H2E_MBX);
}
DECLARE_IRQ(MEC1322_IRQ_EMI, emi_interrupt, 1);

/*
 * Port80 POST code polling limitation:
 * - POST code 0xFF is ignored.
 */
int port_80_read(void)
{
	int data;

	/* read MBX_INDEX for POST code */
	data = MEC1322_MBX_INDEX;

	/* clear MBX_INDEX for next POST code*/
	MEC1322_MBX_INDEX = 0xff;

	/* mark POST code 0xff as invalid */
	if (data == 0xff)
		data = PORT_80_IGNORE;

	return data;
}

void acpi_0_interrupt(void)
{
	uint8_t value, result, is_cmd;

	is_cmd = MEC1322_ACPI_EC_STATUS(0) & EC_LPC_STATUS_LAST_CMD;

	/* Set the bust bi */
	MEC1322_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_PROCESSING;

	/* Read command/data; this clears the FRMH bit. */
	value = MEC1322_ACPI_EC_OS2EC(0, 0);

	/* Handle whatever this was. */
	if (acpi_ap_to_ec(is_cmd, value, &result))
		MEC1322_ACPI_EC_EC2OS(0, 0) = result;

	/* Clear the busy bit */
	MEC1322_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_PROCESSING;

	/*
	 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty / Output Buffer
	 * Full condition on the kernel channel.
	 */
	lpc_generate_sci();
}
DECLARE_IRQ(MEC1322_IRQ_ACPIEC0_IBF, acpi_0_interrupt, 1);

void acpi_1_interrupt(void)
{
	uint8_t st = MEC1322_ACPI_EC_STATUS(1);
	if (!(st & EC_LPC_STATUS_FROM_HOST) ||
	    !(st & EC_LPC_STATUS_LAST_CMD))
		return;

	/* Set the busy bit */
	MEC1322_ACPI_EC_STATUS(1) |= EC_LPC_STATUS_PROCESSING;

	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = MEC1322_ACPI_EC_OS2EC(1, 0);

	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_flags = lpc_host_args->flags;

	/* We only support new style command (v3) now */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request = (const void *)lpc_get_hostcmd_data_range();
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so pass in the entire buffer */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response = (void *)lpc_get_hostcmd_data_range();
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;
		host_packet_receive(&lpc_packet);
		return;
	} else {
		/* Old style command unsupported */
		host_cmd_args.result = EC_RES_INVALID_COMMAND;
	}

	/* Hand off to host command handler */
	host_command_received(&host_cmd_args);
}
DECLARE_IRQ(MEC1322_IRQ_ACPIEC1_IBF, acpi_1_interrupt, 1);

#ifdef HAS_TASK_KEYPROTO
void kb_ibf_interrupt(void)
{
	if (lpc_keyboard_input_pending())
		keyboard_host_write(MEC1322_8042_H2E,
				    MEC1322_8042_STS & (1 << 3));
	task_wake(TASK_ID_KEYPROTO);
}
DECLARE_IRQ(MEC1322_IRQ_8042EM_IBF, kb_ibf_interrupt, 1);

void kb_obf_interrupt(void)
{
	task_wake(TASK_ID_KEYPROTO);
}
DECLARE_IRQ(MEC1322_IRQ_8042EM_OBF, kb_obf_interrupt, 1);
#endif

int lpc_keyboard_has_char(void)
{
	return (MEC1322_8042_STS & (1 << 0)) ? 1 : 0;
}

int lpc_keyboard_input_pending(void)
{
	return (MEC1322_8042_STS & (1 << 1)) ? 1 : 0;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	MEC1322_8042_E2H = chr;
	if (send_irq)
		keyboard_irq_assert();
}

void lpc_keyboard_clear_buffer(void)
{
	volatile char dummy __attribute__((unused));

	dummy = MEC1322_8042_OBF_CLR;
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char())
		keyboard_irq_assert();
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

void lpc_set_acpi_status_mask(uint8_t mask)
{
	MEC1322_ACPI_EC_STATUS(0) |= mask;
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
	MEC1322_ACPI_EC_STATUS(0) &= ~mask;
}

int lpc_get_pltrst_asserted(void)
{
	return (MEC1322_LPC_BUS_MONITOR & (1<<1)) ? 1 : 0;
}

/* Enable LPC ACPI-EC0 interrupts */
void lpc_enable_acpi_interrupts(void)
{
	task_enable_irq(MEC1322_IRQ_ACPIEC0_IBF);
}

/* Disable LPC ACPI-EC0 interrupts */
void lpc_disable_acpi_interrupts(void)
{
	task_disable_irq(MEC1322_IRQ_ACPIEC0_IBF);
}

/* On boards without a host, this command is used to set up LPC */
static int lpc_command_init(int argc, char **argv)
{
	lpc_init();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lpcinit, lpc_command_init, NULL, NULL, NULL);

/* Get protocol information */
static int lpc_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = (1 << 3);
	r->max_request_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->max_response_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		lpc_get_protocol_info,
		EC_VER_MASK(0));

#ifdef CONFIG_POWER_S0IX
static void lpc_clear_host_events(void)
{
	while (lpc_query_host_event_state() != 0);
}

/*
 * In AP S0 -> S3 & S0ix transitions,
 * the chipset_suspend is called.
 *
 * The chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)
 * is used to detect the S0ix transiton.
 *
 * During S0ix entry, the wake mask for lid open is enabled.
 *
 */
void lpc_enable_wake_mask_for_lid_open(void)
{
	if ((chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)) ||
				chipset_in_state(CHIPSET_STATE_STANDBY)) {
		uint32_t mask = 0;

		mask = ((lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE)) |
			EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN));

		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, mask);
}	}

/*
 * In AP S0ix & S3 -> S0 transitions,
 * the chipset_resume hook is called.
 *
 * During S0ix exit, the wake mask for lid open is disabled.
 * All pending events are cleared
 *
 */
void lpc_disable_wake_mask_for_lid_open(void)
{
	if ((chipset_in_state(CHIPSET_STATE_STANDBY | CHIPSET_STATE_ON)) ||
				chipset_in_state(CHIPSET_STATE_ON)) {
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);
		lpc_clear_host_events();
	}
}

#endif
