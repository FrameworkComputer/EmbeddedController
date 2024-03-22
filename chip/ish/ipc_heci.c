/* Copyright 2018 The ChromiumOS Authors
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
 * (Converged Security and Manageability Engine), Audio, Graphics and ISP.
 *
 * Both the initiator and target ends each have a 32-bit doorbell register and
 * 128-byte message regions. In addition, the following register pairs help in
 * synchronizing IPC.
 *
 *  - Peripheral Interrupt Status Register (PISR)
 *  - Peripheral Interrupt Mask Register (PIMR)
 *  - Doorbell Clear Status Register (DB CSR)
 */

#include "builtin/assert.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "ipc_heci.h"
#include "ish_fwst.h"
#include "queue.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ##args)

/*
 * comminucation protocol is defined in Linux Documentation
 * <kernel_root>/Documentation/hid/intel-ish-hid.txt
 */

/* MNG commands */
/* The ipc_mng_task manages IPC link. It should be the highest priority */
#define MNG_RX_CMPL_ENABLE 0
#define MNG_RX_CMPL_DISABLE 1
#define MNG_RX_CMPL_INDICATION 2
#define MNG_RESET_NOTIFY 3
#define MNG_RESET_NOTIFY_ACK 4
#define MNG_SYNC_FW_CLOCK 5
#define MNG_ILLEGAL_CMD 0xFF

/* Doorbell */
#define IPC_DB_MSG_LENGTH_FIELD 0x3FF
#define IPC_DB_MSG_LENGTH_SHIFT 0
#define IPC_DB_MSG_LENGTH_MASK \
	(IPC_DB_MSG_LENGTH_FIELD << IPC_DB_MSG_LENGTH_SHIFT)

#define IPC_DB_PROTOCOL_FIELD 0x0F
#define IPC_DB_PROTOCOL_SHIFT 10
#define IPC_DB_PROTOCOL_MASK (IPC_DB_PROTOCOL_FIELD << IPC_DB_PROTOCOL_SHIFT)

#define IPC_DB_CMD_FIELD 0x0F
#define IPC_DB_CMD_SHIFT 16
#define IPC_DB_CMD_MASK (IPC_DB_CMD_FIELD << IPC_DB_CMD_SHIFT)

#define IPC_DB_BUSY_SHIFT 31
#define IPC_DB_BUSY_MASK BIT(IPC_DB_BUSY_SHIFT)

#define IPC_DB_MSG_LENGTH(drbl) \
	(((drbl) & IPC_DB_MSG_LENGTH_MASK) >> IPC_DB_MSG_LENGTH_SHIFT)
#define IPC_DB_PROTOCOL(drbl) \
	(((drbl) & IPC_DB_PROTOCOL_MASK) >> IPC_DB_PROTOCOL_SHIFT)
#define IPC_DB_CMD(drbl) (((drbl) & IPC_DB_CMD_MASK) >> IPC_DB_CMD_SHIFT)
#define IPC_DB_BUSY(drbl) (!!((drbl) & IPC_DB_BUSY_MASK))

#define IPC_BUILD_DB(length, proto, cmd, busy)                         \
	(((busy) << IPC_DB_BUSY_SHIFT) | ((cmd) << IPC_DB_CMD_SHIFT) | \
	 ((proto) << IPC_DB_PROTOCOL_SHIFT) |                          \
	 ((length) << IPC_DB_MSG_LENGTH_SHIFT))

#define IPC_BUILD_MNG_DB(cmd, length) \
	IPC_BUILD_DB(length, IPC_PROTOCOL_MNG, cmd, 1)

#define IPC_BUILD_HECI_DB(length) IPC_BUILD_DB(length, IPC_PROTOCOL_HECI, 0, 1)

#define IPC_MSG_MAX_SIZE 0x80
#define IPC_HOST_MSG_QUEUE_SIZE 8
#define IPC_PMC_MSG_QUEUE_SIZE 2

#define IPC_HANDLE_PEER_ID_SHIFT 4
#define IPC_HANDLE_PROTOCOL_SHIFT 0
#define IPC_HANDLE_PROTOCOL_MASK 0x0F
#define IPC_BUILD_HANDLE(peer_id, protocol) \
	((ipc_handle_t)(((peer_id) << IPC_HANDLE_PEER_ID_SHIFT) | (protocol)))
#define IPC_BUILD_MNG_HANDLE(peer_id) \
	IPC_BUILD_HANDLE((peer_id), IPC_PROTOCOL_MNG)
#define IPC_BUILD_HOST_MNG_HANDLE() IPC_BUILD_MNG_HANDLE(IPC_PEER_ID_HOST)
#define IPC_HANDLE_PEER_ID(handle) \
	((uint32_t)(handle) >> IPC_HANDLE_PEER_ID_SHIFT)
#define IPC_HANDLE_PROTOCOL(handle) \
	((uint32_t)(handle) & IPC_HANDLE_PROTOCOL_MASK)
#define IPC_IS_VALID_HANDLE(handle)                      \
	(IPC_HANDLE_PEER_ID(handle) < IPC_PEERS_COUNT && \
	 IPC_HANDLE_PROTOCOL(handle) < IPC_PROTOCOL_COUNT)

struct ipc_msg {
	uint32_t drbl;
	uint32_t *timestamp_of_outgoing_doorbell;
	uint8_t payload[IPC_MSG_MAX_SIZE];
} __packed;

struct ipc_rst_payload {
	uint16_t reset_id;
	uint16_t reserved;
};

struct ipc_oob_msg {
	uint32_t address;
	uint32_t length;
};

struct ipc_msg_event {
	task_id_t task_id;
	uint32_t event;
	uint8_t enabled;
};

/*
 * IPC interface context
 * This is per-IPC context.
 */
struct ipc_if_ctx {
	volatile uint8_t *in_msg_reg;
	volatile uint8_t *out_msg_reg;
	volatile uint32_t *in_drbl_reg;
	volatile uint32_t *out_drbl_reg;
	uint32_t clr_busy_bit;
	uint32_t pimr_2ish_bit;
	uint32_t pimr_2host_clearing_bit;
	uint8_t irq_in;
	uint8_t irq_clr;
	uint16_t reset_id;
	struct ipc_msg_event msg_events[IPC_PROTOCOL_COUNT];
	struct mutex lock;
	struct mutex write_lock;

	struct queue tx_queue;
	uint8_t is_tx_ipc_busy;
	uint8_t initialized;
};

/* list of peer contexts */
static struct ipc_if_ctx ipc_peer_ctxs[IPC_PEERS_COUNT] = {
	[IPC_PEER_ID_HOST] = {
		.in_msg_reg = IPC_HOST2ISH_MSG_BASE,
		.out_msg_reg = IPC_ISH2HOST_MSG_BASE,
		.in_drbl_reg = IPC_HOST2ISH_DOORBELL_ADDR,
		.out_drbl_reg = IPC_ISH2HOST_DOORBELL_ADDR,
		.clr_busy_bit = IPC_DB_CLR_STS_ISH2HOST_BIT,
		.pimr_2ish_bit = IPC_PIMR_HOST2ISH_BIT,
		.pimr_2host_clearing_bit = IPC_PIMR_ISH2HOST_CLR_BIT,
		.irq_in = ISH_IPC_HOST2ISH_IRQ,
		.irq_clr = ISH_IPC_ISH2HOST_CLR_IRQ,
		.tx_queue = QUEUE_NULL(IPC_HOST_MSG_QUEUE_SIZE, struct ipc_msg),
	},
	/* Other peers (PMC, CSME, etc) to be added when required */
};

static inline struct ipc_if_ctx *ipc_get_if_ctx(const uint32_t peer_id)
{
	return &ipc_peer_ctxs[peer_id];
}

static inline struct ipc_if_ctx *ipc_handle_to_if_ctx(const ipc_handle_t handle)
{
	return ipc_get_if_ctx(IPC_HANDLE_PEER_ID(handle));
}

static inline void ipc_enable_pimr_db_interrupt(const struct ipc_if_ctx *ctx)
{
	IPC_PIMR |= ctx->pimr_2ish_bit;
}

static inline void ipc_disable_pimr_db_interrupt(const struct ipc_if_ctx *ctx)
{
	IPC_PIMR &= ~ctx->pimr_2ish_bit;
}

static inline void
ipc_enable_pimr_clearing_interrupt(const struct ipc_if_ctx *ctx)
{
	IPC_PIMR |= ctx->pimr_2host_clearing_bit;
}

static inline void
ipc_disable_pimr_clearing_interrupt(const struct ipc_if_ctx *ctx)
{
	IPC_PIMR &= ~ctx->pimr_2host_clearing_bit;
}

static void write_payload_and_ring_drbl(const struct ipc_if_ctx *ctx,
					uint32_t drbl, const uint8_t *payload,
					size_t payload_size)
{
	memcpy((void *)(ctx->out_msg_reg), payload, payload_size);
	*(ctx->out_drbl_reg) = drbl;
}

static int ipc_write_raw_timestamp(struct ipc_if_ctx *ctx, uint32_t drbl,
				   const uint8_t *payload, size_t payload_size,
				   uint32_t *timestamp)
{
	struct queue *q = &ctx->tx_queue;
	struct ipc_msg *msg;
	size_t tail, space;
	int res = 0;

	mutex_lock(&ctx->write_lock);

	ipc_disable_pimr_clearing_interrupt(ctx);
	if (ctx->is_tx_ipc_busy) {
		space = queue_space(q);
		if (space) {
			tail = q->state->tail & (q->buffer_units - 1);
			msg = (struct ipc_msg *)q->buffer + tail;
			msg->drbl = drbl;
			msg->timestamp_of_outgoing_doorbell = timestamp;
			memcpy(msg->payload, payload, payload_size);
			queue_advance_tail(q, 1);
		} else {
			CPRINTS("tx queue is full");
			res = -IPC_ERR_TX_QUEUE_FULL;
		}

		ipc_enable_pimr_clearing_interrupt(ctx);
		goto write_unlock;
	}
	ctx->is_tx_ipc_busy = 1;
	ipc_enable_pimr_clearing_interrupt(ctx);

	write_payload_and_ring_drbl(ctx, drbl, payload, payload_size);

	/* We wrote inline, take timestamp now */
	if (timestamp)
		*timestamp = __hw_clock_source_read();

write_unlock:
	mutex_unlock(&ctx->write_lock);
	return res;
}

static int ipc_write_raw(struct ipc_if_ctx *ctx, uint32_t drbl,
			 const uint8_t *payload, size_t payload_size)
{
	return ipc_write_raw_timestamp(ctx, drbl, payload, payload_size, NULL);
}

static int ipc_send_reset_notify(const ipc_handle_t handle)
{
	struct ipc_rst_payload *ipc_rst;
	struct ipc_if_ctx *ctx;
	struct ipc_msg msg;

	ctx = ipc_handle_to_if_ctx(handle);
	ctx->reset_id = (uint16_t)ish_fwst_get_reset_id();
	ipc_rst = (struct ipc_rst_payload *)msg.payload;
	ipc_rst->reset_id = ctx->reset_id;

	msg.drbl = IPC_BUILD_MNG_DB(MNG_RESET_NOTIFY, sizeof(*ipc_rst));
	ipc_write_raw(ctx, msg.drbl, msg.payload, IPC_DB_MSG_LENGTH(msg.drbl));

	return 0;
}

static int ipc_send_cmpl_indication(struct ipc_if_ctx *ctx)
{
	struct ipc_msg msg = { 0 };

	msg.drbl = IPC_BUILD_MNG_DB(MNG_RX_CMPL_INDICATION, 0);
	ipc_write_raw(ctx, msg.drbl, msg.payload, IPC_DB_MSG_LENGTH(msg.drbl));

	return 0;
}

static int ipc_get_protocol_data(const struct ipc_if_ctx *ctx,
				 const uint32_t protocol, uint8_t *buf,
				 const size_t buf_size)
{
	int len = 0, payload_size;
	uint8_t *src = NULL, *dest = NULL;
	struct ipc_msg *msg;
	uint32_t drbl_val;

	drbl_val = *(ctx->in_drbl_reg);
	payload_size = IPC_DB_MSG_LENGTH(drbl_val);

	if (payload_size > IPC_MAX_PAYLOAD_SIZE) {
		CPRINTS("invalid msg : payload is too big");
		return -IPC_ERR_INVALID_MSG;
	}

	switch (protocol) {
	case IPC_PROTOCOL_HECI:
		/* copy only payload which is a heci packet */
		len = payload_size;
		break;
	case IPC_PROTOCOL_MNG:
		/* copy including doorbell which forms a ipc packet */
		len = payload_size + sizeof(drbl_val);
		break;
	default:
		CPRINTS("protocol %d not supported yet", protocol);
		break;
	}

	if (len > buf_size) {
		CPRINTS("buffer is smaller than payload");
		return -IPC_ERR_TOO_SMALL_BUFFER;
	}

	if (IS_ENABLED(IPC_HECI_DEBUG))
		CPRINTF("ipc p=%d, db=0x%0x, payload_size=%d\n", protocol,
			drbl_val, IPC_DB_MSG_LENGTH(drbl_val));

	switch (protocol) {
	case IPC_PROTOCOL_HECI:
		src = (uint8_t *)ctx->in_msg_reg;
		dest = buf;
		break;
	case IPC_PROTOCOL_MNG:
		src = (uint8_t *)ctx->in_msg_reg;
		msg = (struct ipc_msg *)buf;
		msg->drbl = drbl_val;
		dest = msg->payload;
		break;
	default:
		break;
	}

	if (src && dest)
		memcpy(dest, src, payload_size);

	return len;
}

static void set_pimr_and_send_rx_complete(struct ipc_if_ctx *ctx)
{
	ipc_enable_pimr_db_interrupt(ctx);
	ipc_send_cmpl_indication(ctx);
}

static void handle_msg_recv_interrupt(const uint32_t peer_id)
{
	struct ipc_if_ctx *ctx;
	uint32_t drbl_val, payload_size, protocol, invalid_msg = 0;

	ctx = ipc_get_if_ctx(peer_id);
	ipc_disable_pimr_db_interrupt(ctx);

	drbl_val = *(ctx->in_drbl_reg);
	protocol = IPC_DB_PROTOCOL(drbl_val);
	payload_size = IPC_DB_MSG_LENGTH(drbl_val);

	if (payload_size > IPC_MSG_MAX_SIZE)
		invalid_msg = 1;

	if (!ctx->msg_events[protocol].enabled)
		invalid_msg = 2;

	if (!invalid_msg) {
		/* send event to task */
		task_set_event(ctx->msg_events[protocol].task_id,
			       ctx->msg_events[protocol].event);
	} else {
		CPRINTS("discard msg (%d) : %d", protocol, invalid_msg);

		*(ctx->in_drbl_reg) = 0;
		set_pimr_and_send_rx_complete(ctx);
	}
}

static void handle_busy_clear_interrupt(const uint32_t peer_id)
{
	struct ipc_if_ctx *ctx;
	struct ipc_msg *msg;
	struct queue *q;
	size_t head;

	ctx = ipc_get_if_ctx(peer_id);

	/*
	 * Resetting interrupt status bit should be done
	 * before sending an item in tx_queue.
	 */
	IPC_BUSY_CLEAR = ctx->clr_busy_bit;

	/*
	 * No need to use sync mechanism here since the accesing the queue
	 * happens only when either this IRQ is disabled or
	 * in ISR context(here) of this IRQ.
	 */
	if (!queue_is_empty(&ctx->tx_queue)) {
		q = &ctx->tx_queue;
		head = q->state->head & (q->buffer_units - 1);
		msg = (struct ipc_msg *)(q->buffer + head * q->unit_bytes);
		write_payload_and_ring_drbl(ctx, msg->drbl, msg->payload,
					    IPC_DB_MSG_LENGTH(msg->drbl));
		if (msg->timestamp_of_outgoing_doorbell)
			*msg->timestamp_of_outgoing_doorbell =
				__hw_clock_source_read();

		queue_advance_head(q, 1);
	} else {
		ctx->is_tx_ipc_busy = 0;
	}
}

/**
 * IPC interrupts are received by the FW when a) Host SW rings doorbell and
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
static void ipc_host2ish_isr(void)
{
	uint32_t pisr = IPC_PISR;
	uint32_t pimr = IPC_PIMR;

	/*
	 * Ensure that the host IPC write power is requested after getting an
	 * interrupt otherwise the resume message will never get delivered (via
	 * host ipc communication). Resume is where we would like to restore all
	 * power settings, but that is too late for this power request.
	 */
	if (IS_ENABLED(CHIP_FAMILY_ISH5))
		PMU_VNN_REQ = VNN_REQ_IPC_HOST_WRITE & ~PMU_VNN_REQ;

	if ((pisr & IPC_PISR_HOST2ISH_BIT) && (pimr & IPC_PIMR_HOST2ISH_BIT))
		handle_msg_recv_interrupt(IPC_PEER_ID_HOST);
}
#ifndef CONFIG_ISH_HOST2ISH_COMBINED_ISR
DECLARE_IRQ(ISH_IPC_HOST2ISH_IRQ, ipc_host2ish_isr);
#endif

static void ipc_host2ish_busy_clear_isr(void)
{
	uint32_t busy_clear = IPC_BUSY_CLEAR;
	uint32_t pimr = IPC_PIMR;

	if ((busy_clear & IPC_DB_CLR_STS_ISH2HOST_BIT) &&
	    (pimr & IPC_PIMR_ISH2HOST_CLR_BIT))
		handle_busy_clear_interrupt(IPC_PEER_ID_HOST);
}
#ifndef CONFIG_ISH_HOST2ISH_COMBINED_ISR
DECLARE_IRQ(ISH_IPC_ISH2HOST_CLR_IRQ, ipc_host2ish_busy_clear_isr);
#endif

static __maybe_unused void ipc_host2ish_combined_isr(void)
{
	ipc_host2ish_isr();
	ipc_host2ish_busy_clear_isr();
}
#ifdef CONFIG_ISH_HOST2ISH_COMBINED_ISR
DECLARE_IRQ(ISH_IPC_HOST2ISH_IRQ, ipc_host2ish_combined_isr);
#endif

int ipc_write_timestamp(const ipc_handle_t handle, const void *buf,
			const size_t buf_size, uint32_t *timestamp)
{
	int ret;
	struct ipc_if_ctx *ctx;
	uint32_t drbl = 0;
	const uint8_t *payload = NULL;
	int payload_size;
	uint32_t protocol;

	if (!IPC_IS_VALID_HANDLE(handle))
		return -EC_ERROR_INVAL;

	protocol = IPC_HANDLE_PROTOCOL(handle);
	ctx = ipc_handle_to_if_ctx(handle);

	if (ctx->initialized == 0) {
		CPRINTS("open_ipc() for the peer is never called");
		return -EC_ERROR_INVAL;
	}

	if (!ctx->msg_events[protocol].enabled) {
		CPRINTS("call open_ipc() for the protocol first");
		return -EC_ERROR_INVAL;
	}

	switch (protocol) {
	case IPC_PROTOCOL_BOOT:
		break;
	case IPC_PROTOCOL_HECI:
		drbl = IPC_BUILD_HECI_DB(buf_size);
		payload = buf;
		break;
	case IPC_PROTOCOL_MCTP:
		break;
	case IPC_PROTOCOL_MNG:
		drbl = ((struct ipc_msg *)buf)->drbl;
		payload = ((struct ipc_msg *)buf)->payload;
		break;
	case IPC_PROTOCOL_ECP:
		/* TODO : EC protocol */
		break;
	}

	payload_size = IPC_DB_MSG_LENGTH(drbl);
	if (payload_size > IPC_MSG_MAX_SIZE) {
		/* too much input */
		return -EC_ERROR_OVERFLOW;
	}

	ret = ipc_write_raw_timestamp(ctx, drbl, payload, payload_size,
				      timestamp);
	if (ret)
		return ret;

	return buf_size;
}

ipc_handle_t ipc_open(const enum ipc_peer_id peer_id,
		      const enum ipc_protocol protocol, const uint32_t event)
{
	struct ipc_if_ctx *ctx;

	if (protocol >= IPC_PROTOCOL_COUNT || peer_id >= IPC_PEERS_COUNT)
		return IPC_INVALID_HANDLE;

	ctx = ipc_get_if_ctx(peer_id);
	mutex_lock(&ctx->lock);
	if (ctx->msg_events[protocol].enabled) {
		mutex_unlock(&ctx->lock);
		return IPC_INVALID_HANDLE;
	}

	ctx->msg_events[protocol].task_id = task_get_current();
	ctx->msg_events[protocol].enabled = 1;
	ctx->msg_events[protocol].event = event;

	/* For HECI protocol, set HECI UP status when IPC link is ready */
	if (peer_id == IPC_PEER_ID_HOST && protocol == IPC_PROTOCOL_HECI &&
	    ish_fwst_is_ilup_set())
		ish_fwst_set_hup();

	if (ctx->initialized == 0) {
		task_enable_irq(ctx->irq_in);
		if (!IS_ENABLED(CONFIG_ISH_HOST2ISH_COMBINED_ISR))
			task_enable_irq(ctx->irq_clr);

		ipc_enable_pimr_db_interrupt(ctx);
		ipc_enable_pimr_clearing_interrupt(ctx);

		ctx->initialized = 1;
	}
	mutex_unlock(&ctx->lock);

	return IPC_BUILD_HANDLE(peer_id, protocol);
}

static void handle_mng_commands(const ipc_handle_t handle,
				const struct ipc_msg *msg)
{
	struct ipc_rst_payload *ipc_rst;
	struct ipc_if_ctx *ctx;
	uint32_t peer_id = IPC_HANDLE_PEER_ID(handle);

	ctx = ipc_handle_to_if_ctx(handle);

	switch (IPC_DB_CMD(msg->drbl)) {
	case MNG_RX_CMPL_ENABLE:
	case MNG_RX_CMPL_DISABLE:
	case MNG_RX_CMPL_INDICATION:
	case MNG_RESET_NOTIFY:
		CPRINTS("msg not handled %d", IPC_DB_CMD(msg->drbl));
		break;
	case MNG_RESET_NOTIFY_ACK:
		ipc_rst = (struct ipc_rst_payload *)msg->payload;
		if (peer_id == IPC_PEER_ID_HOST &&
		    ipc_rst->reset_id == ctx->reset_id) {
			ish_fwst_set_ilup();
			if (ctx->msg_events[IPC_PROTOCOL_HECI].enabled)
				ish_fwst_set_hup();
		}

		break;
	case MNG_SYNC_FW_CLOCK:
		/* Not supported currently, but kernel sends this about ~20s */
		break;
	}
}

static int do_ipc_read(struct ipc_if_ctx *ctx, const uint32_t protocol,
		       uint8_t *buf, const size_t buf_size)
{
	int len;

	len = ipc_get_protocol_data(ctx, protocol, buf, buf_size);

	*(ctx->in_drbl_reg) = 0;
	set_pimr_and_send_rx_complete(ctx);

	return len;
}

static int ipc_check_read_validity(const struct ipc_if_ctx *ctx,
				   const uint32_t protocol)
{
	if (ctx->initialized == 0)
		return -EC_ERROR_INVAL;

	if (!ctx->msg_events[protocol].enabled)
		return -EC_ERROR_INVAL;

	/* ipc_read() should be called by the same task called ipc_open() */
	if (ctx->msg_events[protocol].task_id != task_get_current())
		return -IPC_ERR_INVALID_TASK;

	return 0;
}

/*
 * ipc_read should be called by the same task context which called ipc_open()
 */
int ipc_read(const ipc_handle_t handle, void *buf, const size_t buf_size,
	     int timeout_us)
{
	struct ipc_if_ctx *ctx;
	uint32_t events, protocol, drbl_protocol, drbl_val;
	int ret;

	if (!IPC_IS_VALID_HANDLE(handle))
		return -EC_ERROR_INVAL;

	protocol = IPC_HANDLE_PROTOCOL(handle);
	ctx = ipc_handle_to_if_ctx(handle);

	ret = ipc_check_read_validity(ctx, protocol);
	if (ret)
		return ret;

	if (timeout_us) {
		events = task_wait_event_mask(ctx->msg_events[protocol].event,
					      timeout_us);

		if (events & TASK_EVENT_TIMER)
			return -EC_ERROR_TIMEOUT;

		if (!(events & ctx->msg_events[protocol].event))
			return -EC_ERROR_UNKNOWN;
	} else {
		/* check if msg for the protocol is available */
		drbl_val = *(ctx->in_drbl_reg);
		drbl_protocol = IPC_DB_PROTOCOL(drbl_val);
		if (!(protocol == drbl_protocol) || !IPC_DB_BUSY(drbl_val))
			return -IPC_ERR_MSG_NOT_AVAILABLE;
	}

	return do_ipc_read(ctx, protocol, buf, buf_size);
}

/* event flag for MNG msg */
#define EVENT_FLAG_BIT_MNG_MSG TASK_EVENT_CUSTOM_BIT(0)

/*
 * This task handles MNG messages
 */
void ipc_mng_task(void)
{
	int payload_size;
	struct ipc_msg msg;
	ipc_handle_t handle;

	/*
	 * Ensure that power for host IPC writes is requested and ack'ed
	 */
	if (IS_ENABLED(CHIP_FAMILY_ISH5)) {
		PMU_VNN_REQ = VNN_REQ_IPC_HOST_WRITE & ~PMU_VNN_REQ;
		while (!(PMU_VNN_REQ_ACK & PMU_VNN_REQ_ACK_STATUS))
			continue;
	}

	handle = ipc_open(IPC_PEER_ID_HOST, IPC_PROTOCOL_MNG,
			  EVENT_FLAG_BIT_MNG_MSG);

	ASSERT(handle != IPC_INVALID_HANDLE);

	ipc_send_reset_notify(handle);

	while (1) {
		payload_size = ipc_read(handle, &msg, sizeof(msg), -1);

		/* allow doorbell with any payload */
		if (payload_size < 0) {
			CPRINTS("ipc_read error. discard msg");
			continue; /* TODO: retry several and exit */
		}

		/* handle MNG commands */
		handle_mng_commands(handle, &msg);
	}
}

void ipc_init(void)
{
	int i;
	struct ipc_if_ctx *ctx;

	for (i = 0; i < IPC_PEERS_COUNT; i++) {
		ctx = ipc_get_if_ctx(i);
		queue_init(&ctx->tx_queue);
	}

	/* inform host firmware is running */
	ish_fwst_set_fw_status(FWSTS_FW_IS_RUNNING);
}
DECLARE_HOOK(HOOK_INIT, ipc_init, HOOK_PRIO_DEFAULT);
