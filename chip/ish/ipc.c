/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IPC module for ISH */

/**
 * IPC - Inter Processor Communication
 * -----------------------------------
 *
 * IPC is a bi-directional doorbell based message passing interface sans
 * session and transport layers, between hardware blocks. ISH uses IPC to
 * communicate with the Host, PMC (Power Management Controller), CSME
 * (Converged Security and  Manageability Engine), Audio, Graphics and ISP.
 *
 * Both the initiator and target ends each have a 32-bit doorbell register and
 * 128-byte message regions. In addition, the following register pairs help in
 * synchronizing IPC.
 *
 *  - Peripheral Interrupt Status Register (PISR)
 *  - Peripheral Interrupt Mask Register (PIMR)
 *  - Doorbell Clear Status Register (DB CSR)
 */

#include "registers.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "ipc.h"

#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)

static struct host_packet ipc_packet;	/* For host command processing */
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags;	/* Flags from host command */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static uint8_t mem_mapped[0x200] __attribute__ ((section(".bss.big_align")));
static struct ec_lpc_host_args *const ipc_host_args =
	(struct ec_lpc_host_args *)mem_mapped;

/* Array of peer contexts */
struct ipc_if_ctx ipc_peer_ctxs[IPC_PEERS_COUNT] = {
	[IPC_PEER_HOST_ID] = {
		.in_msg_reg = IPC_HOST2ISH_MSG_REGS,
		.out_msg_reg = IPC_ISH2HOST_MSG_REGS,
		.in_drbl_reg = IPC_HOST2ISH_DOORBELL,
		.out_drbl_reg = IPC_ISH2HOST_DOORBELL,
		.clr_bit = IPC_INT_ISH2HOST_CLR_BIT,
		.irq_in = ISH_IPC_HOST2ISH_IRQ,
		.irq_clr = ISH_IPC_ISH2HOST_CLR_IRQ,
	},
	/* Other peers (PMC, CSME, etc) to be added when required */
};

/* Peripheral Interrupt Mask Register bits */
static uint8_t pimr_bit_array[IPC_PEERS_COUNT][3] = {
	{
		IPC_PIMR_HOST2ISH_OFFS,
		IPC_PIMR_HOST2ISH_OFFS,
		IPC_PIMR_ISH2HOST_CLR_OFFS
	},
};

/* Get protocol information */
static int ipc_get_protocol_info(struct host_cmd_handler_args *args)
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
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, ipc_get_protocol_info,
		     EC_VER_MASK(0));

/* Set/un-set PIMR bits */
static void ipc_set_pimr(uint8_t peer_id, int set,
			 enum pimr_signal_type signal_type)
{
	uint32_t new_pimr_val;

	new_pimr_val = (1 << (pimr_bit_array[peer_id][signal_type]));

	interrupt_disable();

	if (set)
		REG32(IPC_PIMR) |= new_pimr_val;
	else
		REG32(IPC_PIMR) &= ~new_pimr_val;

	interrupt_enable();
}

/**
 * ipc_read: Host -> ISH communication
 *
 * 1. Host SW checks HOST2ISH doorbell bit[31] to ensure it is cleared.
 * 2. Host SW writes data to HOST2ISH message registers (upto 128 bytes).
 * 3. Host SW writes to HOST2ISH doorbell register, setting bit [31].
 * 4. ISH FW recieves interrupt, checks PISR[0] to realize the event.
 * 5. After reading data, ISH FW clears HOST2ISH DB bit[31].
 * 6. Host SW recieves interrupt, reads Host PISR bit[8] to realize
 *    the message was consumed by ISH FW.
 */
static int ipc_read(uint8_t peer_id, void *out_buff, uint32_t buff_size)
{
#ifdef ISH_DEBUG
	int i;
#endif
	struct ipc_if_ctx *ctx;
	int retval = EC_SUCCESS;

	ctx = &ipc_peer_ctxs[peer_id];

	if (buff_size > IPC_MSG_MAX_SIZE)
		retval = IPC_FAILURE;

	if (retval >= 0) {
		/* Copy message to out buffer. */
		memcpy(out_buff, (const uint32_t *)ctx->in_msg_reg, buff_size);
		retval = buff_size;

#ifdef ISH_DEBUG
		CPRINTF("ipc_read, len=0x%0x [", buff_size);
		for (i = 0; i < buff_size; i++)
			CPRINTF("0x%0x ", (uint8_t) ((char *)out_buff)[i]);
		CPUTS("]\n");
#endif
	}

	REG32(ctx->in_drbl_reg) = 0;
	ipc_set_pimr(peer_id, SET_PIMR, PIMR_SIGNAL_IN);

	return retval;
}

static int ipc_wait_until_msg_consumed(struct ipc_if_ctx *ctx, int timeout)
{
	int wait_sts = 0;
	uint32_t drbl;

	drbl = REG32(ctx->out_drbl_reg);
	if (!(drbl & IPC_DRBL_BUSY_BIT)) {
		/* doorbell is already cleared. we can continue */
		return 0;
	}

	while (1) {
		wait_sts = task_wait_event_mask(EVENT_FLAG_BIT_WRITE_IPC,
						timeout);
		drbl = REG32(ctx->out_drbl_reg);

		if (!(drbl & IPC_DRBL_BUSY_BIT)) {
			return 0;
		} else if (wait_sts != 0) {
			/* timeout */
			return wait_sts;
		}
	}
}

/**
 * ipc_write: ISH -> Host Communication
 *
 * 1. ISH FW ensures ISH2HOST doorbell busy bit [31] is cleared.
 * 2. ISH FW writes data (upto 128 bytes) to ISH2HOST message registers.
 * 3. ISH FW writes to ISH2HOST doorbell, busy bit (31) is set.
 * 4. Host SW receives interrupt, reads host PISR[0] to realize event.
 * 5. Upon reading data, Host driver clears ISH2HOST doorbell busy bit. This
 *    de-asserts the interrupt.
 * 6. ISH FW also receieves an interrupt for the clear event.
 */
static int ipc_write(uint8_t peer_id, void *buff, uint32_t buff_size)
{
	struct ipc_if_ctx *ctx;
	uint32_t drbl_val = 0;
#ifdef ISH_DEBUG
	int i;
#endif

	ctx = &ipc_peer_ctxs[peer_id];

	if (ipc_wait_until_msg_consumed(ctx, IPC_TIMEOUT)) {
		/* timeout */
		return IPC_FAILURE;
	}
#ifdef ISH_DEBUG
	CPRINTF("ipc_write, len=0x%0x [", buff_size);
	for (i = 0; i < buff_size; i++)
		CPRINTF("0x%0x ", (uint8_t) ((char *)buff)[i]);
	CPUTS("]\n");
#endif

	/* write message */
	if (buff_size <= IPC_MSG_MAX_SIZE) {
		/* write to message register */
		memcpy((uint32_t *) ctx->out_msg_reg, buff, buff_size);
		drbl_val = IPC_BUILD_HEADER(buff_size, IPC_PROTOCOL_ECP,
					    SET_BUSY);
	} else {
		return IPC_FAILURE;
	}

	/* write doorbell */
	REG32(ctx->out_drbl_reg) = drbl_val;

	return EC_SUCCESS;
}

uint8_t *lpc_get_memmap_range(void)
{
	return mem_mapped + 0x100;
}

static uint8_t *ipc_get_hostcmd_data_range(void)
{
	return mem_mapped;
}

static void ipc_send_response_packet(struct host_packet *pkt)
{
	ipc_write(IPC_PEER_HOST_ID, pkt->response, pkt->response_size);
}

void lpc_update_host_event_status(void)
{
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
}

/**
 * IPC interrupts are recieved by the FW when a) Host SW rings doorbell and
 * b) when Host SW clears doorbell busy bit [31].
 *
 * Doorbell Register (DB) bits
 * ----+-------+--------+-----------+--------+------------+--------------------
 *  31 | 30 29 |  28-20 |19 18 17 16| 15 14  | 13 12 11 10| 9 8 7 6 5 4 3 2 1 0
 * ----+-------+--------+-----------+--------+------------+--------------------
 * Busy|Options|Reserved|  Command  |Reserved|   Protocol |    Message Length
 * ----+-------+--------+-----------+--------+------------+--------------------
 *
 * ISH Peripheral Interrupt Status Register:
 *  Bit 0 - If set, indicates interrupt was caused by setting Host2ISH DB
 *
 * ISH Peripheral Interrupt Mask Register
 *  Bit 0 - If set, mask interrupt caused by Host2ISH DB
 *
 * ISH Peripheral DB Clear Status Register
 *  Bit 0 - If set, indicates interrupt was caused by clearing Host2ISH DB
 */
static void ipc_interrupt_handler(void)
{
	uint32_t pisr = REG32(IPC_PISR);
	uint32_t pimr = REG32(IPC_PIMR);
	uint32_t busy_clear = REG32(IPC_BUSY_CLEAR);
	uint32_t drbl = REG32(IPC_ISH2HOST_MSG_REGS);
	uint8_t proto, cmd;

	if ((pisr & IPC_PISR_HOST2ISH_BIT)
	    && (pimr & IPC_PIMR_HOST2ISH_BIT)) {

		/* New message arrived */
		ipc_set_pimr(IPC_PEER_HOST_ID, UNSET_PIMR, PIMR_SIGNAL_IN);
		task_set_event(TASK_ID_IPC_COMM, EVENT_FLAG_BIT_READ_IPC, 0);
		proto = IPC_HEADER_GET_PROTOCOL(drbl);
		cmd = IPC_HEADER_GET_MNG_CMD(drbl);

		if ((proto == IPC_PROTOCOL_MNG) && (cmd == MNG_TIME_UPDATE))
			/* Ignoring time update from host */
			;
	}

	if ((busy_clear & IPC_INT_ISH2HOST_CLR_BIT)
	    && (pimr & IPC_PIMR_ISH2HOST_CLR_MASK_BIT)) {
		/* Written message cleared */
		REG32(IPC_BUSY_CLEAR) = IPC_ISH_FWSTS;
		task_set_event(TASK_ID_IPC_COMM, EVENT_FLAG_BIT_WRITE_IPC, 0);
	}
}
DECLARE_IRQ(ISH_IPC_HOST2ISH_IRQ, ipc_interrupt_handler);

/* Task that listens for incomming IPC messages from Host and initiate host
 * command processing.
 */
void ipc_comm_task(void)
{
	int ret = 0;
	uint32_t out_drbl, pkt_len;

	for (;;) {

		ret = task_wait_event_mask(EVENT_FLAG_BIT_READ_IPC
					   | EVENT_FLAG_BIT_WRITE_IPC, -1);

		if ((ret & EVENT_FLAG_BIT_WRITE_IPC))
			continue;
		else if (!(ret & EVENT_FLAG_BIT_READ_IPC))
			continue;

		/* Read the command byte.  This clears the FRMH bit in
		 * the status byte.
		 */
		out_drbl = REG32(IPC_HOST2ISH_DOORBELL);
		pkt_len = out_drbl & IPC_HEADER_LENGTH_MASK;

		ret = ipc_read(IPC_PEER_HOST_ID, ipc_host_args, pkt_len);
		host_cmd_args.command = EC_COMMAND_PROTOCOL_3;

		host_cmd_args.result = EC_RES_SUCCESS;
		host_cmd_flags = ipc_host_args->flags;

		/* We only support new style command (v3) now */
		if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
			ipc_packet.send_response = ipc_send_response_packet;

			ipc_packet.request =
			    (const void *)ipc_get_hostcmd_data_range();
			ipc_packet.request_temp = params_copy;
			ipc_packet.request_max = sizeof(params_copy);
			/* Don't know the request size so pass in
			 * the entire buffer
			 */
			ipc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

			ipc_packet.response =
			    (void *)ipc_get_hostcmd_data_range();
			ipc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
			ipc_packet.response_size = 0;

			ipc_packet.driver_result = EC_RES_SUCCESS;
			host_packet_receive(&ipc_packet);
			usleep(10);	/* To force yield */

			continue;
		} else {
			/* Old style command unsupported */
			host_cmd_args.result = EC_RES_INVALID_COMMAND;
		}

		/* Hand off to host command handler */
		host_command_received(&host_cmd_args);
	}
}

static void setup_ipc(void)
{

	task_enable_irq(ISH_IPC_HOST2ISH_IRQ);
	task_enable_irq(ISH_IPC_ISH2HOST_CLR_IRQ);

	ipc_set_pimr(IPC_PEER_HOST_ID, SET_PIMR, PIMR_SIGNAL_IN);
	ipc_set_pimr(IPC_PEER_HOST_ID, SET_PIMR, PIMR_SIGNAL_CLR);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, setup_ipc, HOOK_PRIO_FIRST);

static void ipc_init(void)
{
	CPRINTS("ipc_init");

	/* Initialize host args and memory map to all zero */
	memset(ipc_host_args, 0, sizeof(*ipc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	setup_ipc();

	CPUTS("*** MNG Host Command FW ready ****\n");
	REG32(IPC_ISH2HOST_DOORBELL) = IPC_BUILD_MNG_MSG(MNG_HC_FW_READY, 1);
}
DECLARE_HOOK(HOOK_INIT, ipc_init, HOOK_PRIO_INIT_LPC);

/* On boards without a host, this command is used to set up IPC */
static int ipc_command_init(int argc, char **argv)
{
	ipc_init();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ipcinit, ipc_command_init, NULL, NULL);
