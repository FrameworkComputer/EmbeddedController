/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for MEC1322 */

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "registers.h"
#include "task.h"
#include "util.h"

static uint8_t mem_mapped[0x200] __attribute__((section(".bss.big_align")));

static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags;   /* Flags from host command */

static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __attribute__((aligned(4)));

static struct ec_lpc_host_args * const lpc_host_args =
	(struct ec_lpc_host_args *)mem_mapped;

uint8_t *lpc_get_memmap_range(void)
{
	return mem_mapped + 0x100;
}

static uint8_t *lpc_get_hostcmd_data_range(void)
{
	return mem_mapped;
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

/*
 * Most registers in LPC module are reset when the host is off. We need to
 * set up LPC again when the host is starting up.
 */
static void setup_lpc(void)
{
	uintptr_t ptr = (uintptr_t)mem_mapped;

	/* EMI module only takes alias memory address */
	if (ptr < 0x120000)
		ptr = ptr - 0x118000 + 0x20000000;

	/* Set up ACPI1 for 0x200/0x204 */
	MEC1322_LPC_ACPI_EC1_BAR = 0x02008407;
	MEC1322_INT_ENABLE(15) |= 1 << 8;
	MEC1322_INT_BLK_EN |= 1 << 15;
	task_enable_irq(MEC1322_IRQ_ACPIEC1_IBF);

	/* Set up EMI module for memory mapped region.
	 * TODO(crosbug.com/p/24107): Use LPC memory transaction for this
	 *                            when we have updated info of memory BAR
	 *                            register.
	 */
	MEC1322_LPC_EMI_BAR = 0x0800800f;
	MEC1322_EMI_MBA0 = ptr;
	MEC1322_EMI_MRL0 = 0x200;
	MEC1322_EMI_MWL0 = 0x200;

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, setup_lpc, HOOK_PRIO_FIRST);

static void lpc_init(void)
{
	/* Activate LPC interface */
	MEC1322_LPC_ACT |= 1;

	/* Initialize host args and memory map to all zero */
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

static void acpi_1_interrupt(void)
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

void lpc_set_host_event_state(uint32_t mask)
{
	/* TODO(crosbug.com/p/24107): Host event */
}

int lpc_query_host_event_state(void)
{
	/* TODO(crosbug.com/p/24107): Host event */
	return 0;
}

void lpc_set_host_event_mask(enum lpc_host_event_type type, uint32_t mask)
{
	/* TODO(crosbug.com/p/24107): Host event */
}

uint32_t lpc_get_host_event_mask(enum lpc_host_event_type type)
{
	/* TODO(crosbug.com/p/24107): Host event */
	return 0;
}

/* On boards without a host, this command is used to set up LPC */
static int lpc_command_init(int argc, char **argv)
{
	setup_lpc();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lpcinit, lpc_command_init, NULL, NULL, NULL);
