/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Inter-Processor Communication (IPC) and Inter-Processor Interrupt (IPI)
 *
 * IPC is a communication bridge between AP and SCP.  AP/SCP sends an IPC
 * interrupt to SCP/AP to inform to collect the commmunication mesesages in the
 * shared buffer.
 *
 * There are 4 IPCs in the current architecture, from IPC0 to IPC3.  The
 * priority of IPC is proportional to its IPC index. IPC3 has the highest
 * priority and IPC0 has the lowest one.
 *
 * IPC0 may contain zero or more IPIs.  Each IPI represents a task or a service,
 * e.g. host command, or video encoding.  IPIs are recognized by IPI ID, which
 * should sync across AP and SCP.  Shared buffer should designated which IPI
 * ID it talks to.
 *
 * Currently, we don't have IPC handlers for IPC1, IPC2, and IPC3.
 */

#include "clock_chip.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "ipi_chip.h"
#include "mkbp_event.h"
#include "power.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "hwtimer.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

#define IPI_MAX_REQUEST_SIZE CONFIG_IPC_SHARED_OBJ_BUF_SIZE
/* Reserve 1 extra byte for HOSTCMD_TYPE and 3 bytes for padding. */
#define IPI_MAX_RESPONSE_SIZE (CONFIG_IPC_SHARED_OBJ_BUF_SIZE - 4)
#define HOSTCMD_TYPE_HOSTCMD 1
#define HOSTCMD_TYPE_HOSTEVENT 2

static volatile int16_t ipc0_enabled_count;
static struct mutex ipc0_lock;
static struct mutex ipi_lock;
/* IPC0 shared objects, including send object and receive object. */
static struct ipc_shared_obj *const scp_send_obj =
	(struct ipc_shared_obj *)CONFIG_IPC_SHARED_OBJ_ADDR;
static struct ipc_shared_obj *const scp_recv_obj =
	(struct ipc_shared_obj *)(CONFIG_IPC_SHARED_OBJ_ADDR +
				  sizeof(struct ipc_shared_obj));
static char ipi_ready;

#ifdef HAS_TASK_HOSTCMD
/*
 * hostcmd and hostevent share the same IPI ID, and use first byte type to
 * indicate its type.
 */
static struct hostcmd_data {
	const uint8_t type;
	/* To be compatible with CONFIG_HOSTCMD_ALIGNED */
	uint8_t response[IPI_MAX_RESPONSE_SIZE] __aligned(4);
} hc_cmd_obj = { .type = HOSTCMD_TYPE_HOSTCMD };
BUILD_ASSERT(sizeof(struct hostcmd_data) == CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

static struct host_packet ipi_packet;
#endif

/* Check if SCP to AP IPI is in use. */
static inline int is_ipi_busy(void)
{
	return SCP_HOST_INT & IPC_SCP2HOST_BIT;
}

/* If IPI is declared as a wake-up source, wake AP up. */
static inline void try_to_wakeup_ap(int32_t id)
{
#ifdef CONFIG_RPMSG_NAME_SERVICE
	if (id == IPI_NS_SERVICE)
		return;
#endif

	if (*ipi_wakeup_table[id])
		SCP_SPM_INT = SPM_INT_A2SPM;
}

void ipi_disable_irq(int irq)
{
	/* Only support SCP_IRQ_IPC0 for now. */
	if (irq != SCP_IRQ_IPC0)
		return;

	mutex_lock(&ipc0_lock);

	if ((--ipc0_enabled_count) == 0)
		task_disable_irq(irq);

	mutex_unlock(&ipc0_lock);
}

void ipi_enable_irq(int irq)
{
	/* Only support SCP_IRQ_IPC0 for now. */
	if (irq != SCP_IRQ_IPC0)
		return;

	mutex_lock(&ipc0_lock);

	if ((++ipc0_enabled_count) == 1) {
		int pending_ipc = SCP_GIPC_IN & SCP_GPIC_IN_CLEAR_ALL;

		task_enable_irq(irq);

		if (ipi_ready && pending_ipc)
			/*
			 * IPC may be triggered while SCP_IRQ_IPC0 was disabled.
			 * AP will still updates SCP_GIPC_IN.
			 * Trigger the IRQ handler if it has a
			 * pending IPC.
			 */
			task_trigger_irq(irq);
	}

	mutex_unlock(&ipc0_lock);
}

__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	int i;
	const task_id_t s3_suspend_tasks[] = {
#ifndef S3_SUSPEND_TASK_LIST
#define S3_SUSPEND_TASK_LIST
#endif
#define TASK(n, ...) TASK_ID_##n,
		S3_SUSPEND_TASK_LIST
	};

	if (state == HOST_SLEEP_EVENT_S3_SUSPEND) {
		ccprints("AP suspend");
		/*
		 * On AP suspend, Vcore is 0.6V, and we should not use ULPOSC2,
		 * which needs at least 0.7V.  Switch to ULPOSC1 instead.
		 */
		scp_use_clock(SCP_CLK_ULPOSC1);

		for (i = 0; i < ARRAY_SIZE(s3_suspend_tasks); ++i)
			task_disable_task(s3_suspend_tasks[i]);
	} else if (state == HOST_SLEEP_EVENT_S3_RESUME) {
		ccprints("AP resume");
		/* Vcore is raised to >=0.7V, switch back to ULPSOC2 */
		scp_use_clock(SCP_CLK_ULPOSC2);

		for (i = 0; i < ARRAY_SIZE(s3_suspend_tasks); ++i)
			task_enable_task(s3_suspend_tasks[i]);
	}
}

/* Send data from SCP to AP. */
int ipi_send(int32_t id, const void *buf, uint32_t len, int wait)
{
	if (!ipi_ready)
		return EC_ERROR_BUSY;

	/* TODO(b:117917141): Remove this check completely. */
	if (in_interrupt_context()) {
		CPRINTS("Err: invoke %s() in ISR CTX", __func__);
		return EC_ERROR_BUSY;
	}

	if (len > sizeof(scp_send_obj->buffer))
		return EC_ERROR_INVAL;

	ipi_disable_irq(SCP_IRQ_IPC0);
	mutex_lock(&ipi_lock);

	/* Check if there is already an IPI pending in AP. */
	if (is_ipi_busy()) {
		/*
		 * If the following conditions meet,
		 *   1) There is an IPI pending in AP.
		 *   2) The incoming IPI is a wakeup IPI.
		 * then it assumes that AP is in suspend state.
		 * Send a AP wakeup request to SPM.
		 *
		 * The incoming IPI will be checked if it's a wakeup source.
		 */
		try_to_wakeup_ap(id);

		mutex_unlock(&ipi_lock);
		ipi_enable_irq(SCP_IRQ_IPC0);
		CPRINTS("Err: IPI Busy, %d", id);

		return EC_ERROR_BUSY;
	}


	scp_send_obj->id = id;
	scp_send_obj->len = len;
	memcpy(scp_send_obj->buffer, buf, len);

	/* Send IPI to AP: interrutp AP to receive IPI messages. */
	try_to_wakeup_ap(id);
	SCP_HOST_INT = IPC_SCP2HOST_BIT;

	while (wait && is_ipi_busy())
		;

	mutex_unlock(&ipi_lock);
	ipi_enable_irq(SCP_IRQ_IPC0);

	return EC_SUCCESS;
}

static void ipi_handler(void)
{
	if (scp_recv_obj->id >= IPI_COUNT) {
		CPRINTS("#ERR IPI %d", scp_recv_obj->id);
		return;
	}

	/*
	 * Only print IPI that is not host command channel, which will
	 * be printed by host command driver.
	 */
	if (scp_recv_obj->id != IPI_HOST_COMMAND)
		CPRINTS("IPI %d", scp_recv_obj->id);

	/*
	 * Pass the buffer to handler. Each handler should be in charge of
	 * the buffer copying/reading before returning from handler.
	 */
	ipi_handler_table[scp_recv_obj->id](
		scp_recv_obj->id, scp_recv_obj->buffer, scp_recv_obj->len);
}

void ipi_inform_ap(void)
{
	struct scp_run_t scp_run;
	int ret;
#ifdef CONFIG_RPMSG_NAME_SERVICE
	struct rpmsg_ns_msg ns_msg;
#endif

	scp_run.signaled = 1;
	strncpy(scp_run.fw_ver, system_get_version(EC_IMAGE_RW),
		SCP_FW_VERSION_LEN);
	scp_run.dec_capability = VCODEC_CAPABILITY_4K_DISABLED;
	scp_run.enc_capability = 0;

	ret = ipi_send(IPI_SCP_INIT, (void *)&scp_run, sizeof(scp_run), 1);

	if (ret)
		ccprintf("Failed to send initialization IPC messages.\n");

#ifdef CONFIG_RPMSG_NAME_SERVICE
	ns_msg.id = IPI_HOST_COMMAND;
	strncpy(ns_msg.name, "cros-ec-rpmsg", RPMSG_NAME_SIZE);
	ret = ipi_send(IPI_NS_SERVICE, &ns_msg, sizeof(ns_msg), 1);
	if (ret)
		ccprintf("Failed to announce host command channel.\n");
#endif
}

#ifdef HAS_TASK_HOSTCMD
#if defined(CONFIG_MKBP_USE_CUSTOM)
int mkbp_set_host_active_via_custom(int active, uint32_t *timestamp)
{
	static const uint8_t hc_evt_obj = HOSTCMD_TYPE_HOSTEVENT;

	/* This should be moved into ipi_send for more accuracy */
	if (timestamp)
		*timestamp = __hw_clock_source_read();

	if (active)
		return ipi_send(IPI_HOST_COMMAND, &hc_evt_obj,
				sizeof(hc_evt_obj), 1);
	return EC_SUCCESS;
}
#endif

static void ipi_send_response_packet(struct host_packet *pkt)
{
	int ret;

	ret = ipi_send(IPI_HOST_COMMAND, &hc_cmd_obj,
		       pkt->response_size +
			       offsetof(struct hostcmd_data, response),
		       1);
	if (ret)
		CPRINTS("#ERR IPI HOSTCMD %d", ret);
}

static void ipi_hostcmd_handler(int32_t id, void *buf, uint32_t len)
{
	uint8_t *in_msg = buf;
	struct ec_host_request *r = (struct ec_host_request *)in_msg;
	int i;

	if (in_msg[0] != EC_HOST_REQUEST_VERSION) {
		CPRINTS("ERROR: Protocol V2 is not supported!");
		CPRINTF("in_msg=[");
		for (i = 0; i < len; i++)
			CPRINTF("%02x ", in_msg[i]);
		CPRINTF("]\n");
		return;
	}

	/* Protocol version 3 */

	ipi_packet.send_response = ipi_send_response_packet;

	/*
	 * Just assign the buffer to request, host_packet_receive
	 * handles the buffer copy.
	 */
	ipi_packet.request = (void *)r;
	ipi_packet.request_temp = NULL;
	ipi_packet.request_max = IPI_MAX_REQUEST_SIZE;
	ipi_packet.request_size = host_request_expected_size(r);

	ipi_packet.response = hc_cmd_obj.response;
	/* Reserve space for the preamble and trailing byte */
	ipi_packet.response_max = IPI_MAX_RESPONSE_SIZE;
	ipi_packet.response_size = 0;

	ipi_packet.driver_result = EC_RES_SUCCESS;

	host_packet_receive(&ipi_packet);
}
DECLARE_IPI(IPI_HOST_COMMAND, ipi_hostcmd_handler, 0);

/*
 * Get protocol information
 */
static enum ec_status ipi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions |= BIT(3);
	r->max_request_packet_size = IPI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = IPI_MAX_RESPONSE_SIZE;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, ipi_get_protocol_info,
		     EC_VER_MASK(0));
#endif

static void ipi_enable_ipc0_deferred(void)
{
	/* Clear IPC0 IRQs. */
	SCP_GIPC_IN = SCP_GPIC_IN_CLEAR_ALL;

	/* All tasks are up, we can safely enable IPC0 IRQ now. */
	SCP_INTC_IRQ_ENABLE |= IPC0_IRQ_EN;
	ipi_enable_irq(SCP_IRQ_IPC0);

	ipi_ready = 1;

	/* Inform AP that SCP is inited.  */
	ipi_inform_ap();

	CPRINTS("ipi init");
}
DECLARE_DEFERRED(ipi_enable_ipc0_deferred);

/* Initialize IPI. */
static void ipi_init(void)
{
	/* Clear send share buffer. */
	memset(scp_send_obj, 0, sizeof(struct ipc_shared_obj));

	/* Enable IRQ after all tasks are up.  */
	hook_call_deferred(&ipi_enable_ipc0_deferred_data, 0);
}
DECLARE_HOOK(HOOK_INIT, ipi_init, HOOK_PRIO_DEFAULT);

void ipc_handler(void)
{
	/* TODO(b/117917141): We only support IPC_ID(0) for now. */
	if (SCP_GIPC_IN & SCP_GIPC_IN_CLEAR_IPCN(0)) {
		ipi_handler();
		SCP_GIPC_IN &= SCP_GIPC_IN_CLEAR_IPCN(0);
	}

	SCP_GIPC_IN &= (SCP_GPIC_IN_CLEAR_ALL & ~SCP_GIPC_IN_CLEAR_IPCN(0));
}
DECLARE_IRQ(SCP_IRQ_IPC0, ipc_handler, 4);
