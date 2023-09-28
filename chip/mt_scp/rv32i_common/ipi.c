/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "cache.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hostcmd.h"
#include "ipi_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "video.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

#define SCP_AP2SCP_IRQ CONCAT2(SCP_IRQ_GIPC_IN, SCP_CORE_SN)

static uint8_t init_done;

static struct mutex ipi_lock;
static struct ipc_shared_obj *const ipi_send_buf =
	(struct ipc_shared_obj *)CONFIG_IPC_SHARED_OBJ_ADDR;
static struct ipc_shared_obj *const ipi_recv_buf =
	(struct ipc_shared_obj *)(CONFIG_IPC_SHARED_OBJ_ADDR +
				  sizeof(struct ipc_shared_obj));

static atomic_t disable_irq_count, saved_int_mask;

void ipi_disable_irq(void)
{
	if (atomic_read_add(&disable_irq_count, 1) == 0)
		saved_int_mask = read_clear_int_mask();
}

void ipi_enable_irq(void)
{
	if (atomic_read_sub(&disable_irq_count, 1) == 1)
		set_int_mask(saved_int_mask);
}

static int ipi_is_busy(void)
{
	return ipi_op_scp2ap_is_irq_set();
}

static void ipi_wake_ap(int32_t id)
{
	if (id >= IPI_COUNT)
		return;

	if (*ipi_wakeup_table[id])
		ipi_op_wake_ap();
}

int ipi_send(int32_t id, const void *buf, uint32_t len, int wait)
{
	int ret;

	if (!init_done) {
		CPRINTS("IPI has not initialized");
		return EC_ERROR_BUSY;
	}

	if (in_interrupt_context()) {
		CPRINTS("invoke %s() in ISR context", __func__);
		return EC_ERROR_BUSY;
	}

	if (len > sizeof(ipi_send_buf->buffer)) {
		CPRINTS("data length exceeds limitation");
		return EC_ERROR_INVAL;
	}

	ipi_disable_irq();
	mutex_lock(&ipi_lock);

	if (ipi_is_busy()) {
		/*
		 * If the following conditions meet,
		 *   1) There is an IPI pending in AP.
		 *   2) The incoming IPI is a wakeup IPI.
		 * then it assumes that AP is in suspend state.
		 * Send a AP wakeup request to SPM.
		 *
		 * The incoming IPI will be checked if it's a wakeup source.
		 */
		ipi_wake_ap(id);

		CPRINTS("IPI busy, id=%d", id);
		ret = EC_ERROR_BUSY;
		goto error;
	}

	ipi_send_buf->id = id;
	ipi_send_buf->len = len;
	memcpy(ipi_send_buf->buffer, buf, len);

	/* flush memory cache (if any) */
	cache_flush_dcache_range((uintptr_t)ipi_send_buf,
				 sizeof(*ipi_send_buf));

	/* interrupt AP to handle the message */
	ipi_wake_ap(id);
	ipi_op_scp2ap_irq_set();

	if (wait)
		while (ipi_is_busy())
			;

	ret = EC_SUCCESS;
error:
	mutex_unlock(&ipi_lock);
	ipi_enable_irq();
	return ret;
}

#ifndef HAVE_PRIVATE_MT_SCP
__overridable uint32_t video_get_dec_capability(void)
{
	return 0;
}
__overridable uint32_t video_get_enc_capability(void)
{
	return 0;
}
#endif

static void ipi_enable_deferred(void)
{
	struct scp_run_t scp_run;
	int ret;

	init_done = 1;

	/* inform AP that SCP is up */
	scp_run.signaled = 1;
	strncpy(scp_run.fw_ver, system_get_version(EC_IMAGE_RW),
		SCP_FW_VERSION_LEN);
	scp_run.dec_capability = video_get_dec_capability();
	scp_run.enc_capability = video_get_enc_capability();

	ret = ipi_send(SCP_IPI_INIT, (void *)&scp_run, sizeof(scp_run), 1);
	if (ret) {
		CPRINTS("failed to send initialization IPC messages");
		init_done = 0;
		return;
	}

#ifdef HAS_TASK_HOSTCMD
	hostcmd_init();
#endif

	task_enable_irq(SCP_AP2SCP_IRQ);
}
DECLARE_DEFERRED(ipi_enable_deferred);

static void ipi_init(void)
{
	memset(ipi_send_buf, 0, sizeof(struct ipc_shared_obj));
	memset(ipi_recv_buf, 0, sizeof(struct ipc_shared_obj));

	/* enable IRQ after all tasks are up */
	hook_call_deferred(&ipi_enable_deferred_data, 0);
}
DECLARE_HOOK(HOOK_INIT, ipi_init, HOOK_PRIO_DEFAULT);

static void ipi_handler(void)
{
	if (ipi_recv_buf->id >= IPI_COUNT) {
		CPRINTS("invalid IPI, id=%d", ipi_recv_buf->id);
		return;
	}

	ipi_handler_table[ipi_recv_buf->id](
		ipi_recv_buf->id, ipi_recv_buf->buffer, ipi_recv_buf->len);
}

static void irq_group7_handler(void)
{
	extern volatile int ec_int;

	if (ipi_op_ap2scp_is_irq_set()) {
		ipi_handler();
		ipi_op_ap2scp_irq_clr();
		asm volatile("fence.i" ::: "memory");
		task_clear_pending_irq(ec_int);
	}
}
DECLARE_IRQ(7, irq_group7_handler, 0);
