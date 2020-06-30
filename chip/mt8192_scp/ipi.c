/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "ipi_chip.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_IPI, format, ##args)
#define CPRINTS(format, args...) cprints(CC_IPI, format, ##args)

static uint8_t init_done;

static struct mutex ipi_lock;
static struct ipc_shared_obj *const ipi_send_buf =
	(struct ipc_shared_obj *)CONFIG_IPC_SHARED_OBJ_ADDR;

static int ipi_is_busy(void)
{
	return SCP_SCP2APMCU_IPC_SET & IPC_SCP2HOST;
}

static void ipi_wake_ap(int32_t id)
{
	if (*ipi_wakeup_table[id])
		SCP_SCP2SPM_IPC_SET = IPC_SCP2HOST;
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

	/* interrupt AP to handle the message */
	ipi_wake_ap(id);
	SCP_SCP2APMCU_IPC_SET = IPC_SCP2HOST;

	if (wait)
		while (ipi_is_busy())
			;

	ret = EC_SUCCESS;
error:
	mutex_unlock(&ipi_lock);
	return ret;
}

static void ipi_enable_deferred(void)
{
	struct scp_run_t scp_run;
	int ret;

	init_done = 1;

	/* inform AP that SCP is up */
	scp_run.signaled = 1;
	strncpy(scp_run.fw_ver, system_get_version(EC_IMAGE_RW),
		SCP_FW_VERSION_LEN);
	scp_run.dec_capability = VCODEC_CAPABILITY_4K_DISABLED;
	scp_run.enc_capability = 0;

	ret = ipi_send(SCP_IPI_INIT, (void *)&scp_run, sizeof(scp_run), 1);
	if (ret) {
		CPRINTS("failed to send initialization IPC messages");
		init_done = 0;
		return;
	}
}
DECLARE_DEFERRED(ipi_enable_deferred);

static void ipi_init(void)
{
	memset(ipi_send_buf, 0, sizeof(struct ipc_shared_obj));

	/* enable IRQ after all tasks are up */
	hook_call_deferred(&ipi_enable_deferred_data, 0);
}
DECLARE_HOOK(HOOK_INIT, ipi_init, HOOK_PRIO_DEFAULT);
