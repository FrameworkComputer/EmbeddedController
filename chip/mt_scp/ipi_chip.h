/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_IPI_CHIP_H
#define __CROS_EC_IPI_CHIP_H

#include "chip/mt_scp/registers.h"
#include "common.h"

#define IPC_MAX 1
#define IPC_ID(n) (n)

/*
 * Length of EC version string is at most 32 byte (NULL included), which
 * also aligns SCP fw_version length.
 */
#define SCP_FW_VERSION_LEN 32

/*
 * Video decoder supported capability:
 * BIT(4): 0 enable 4K
 *         1 disable 4K
 */
#define VCODEC_CAPABILITY_4K_DISABLED BIT(4)

#ifndef IPI_SCP_INIT
#error If CONFIG_IPI is enabled, IPI_SCP_INIT must be defined.
#endif

/*
 * Share buffer layout for IPI_SCP_INIT response. This structure should sync
 * across kernel and EC.
 */
struct scp_run_t {
	uint32_t signaled;
	int8_t fw_ver[SCP_FW_VERSION_LEN];
	uint32_t dec_capability;
	uint32_t enc_capability;
};

/*
 * The layout of the IPC0 AP/SCP shared buffer.
 * This should sync across kernel and EC.
 */
struct ipc_shared_obj {
	/* IPI ID */
	int32_t id;
	/* Length of the contents in buffer. */
	uint32_t len;
	/* Shared buffer contents. */
	uint8_t buffer[CONFIG_IPC_SHARED_OBJ_BUF_SIZE];
};

/* Send a IPI contents to AP. This shouldn't be used in ISR context. */
int ipi_send(int32_t id, const void *buf, uint32_t len, int wait);

/* Size of the rpmsg device name, should sync across kernel and EC. */
#define RPMSG_NAME_SIZE 32

/*
 * The layout of name service message.
 * This should sync across kernel and EC.
 */
struct rpmsg_ns_msg {
	/* Name of the corresponding rpmsg_driver. */
	char name[RPMSG_NAME_SIZE];
	/* IPC ID */
	uint32_t id;
};

/*
 * IPC Handler.
 */
void ipc_handler(void);

/*
 * An IPC IRQ could be shared across many IPI handlers.
 * Those handlers would usually operate on disabling or enabling the IPC IRQ.
 * This may disorder the actual timing to on/off the IRQ when there are many
 * tasks try to operate on it.  As a result, any access to the SCP_IRQ_*
 * should go through ipi_{en,dis}able_irq(), which support a counter to
 * enable/disable the IRQ at correct timeing.
 */
/* Disable IPI IRQ. */
void ipi_disable_irq(int irq);
/* Enable IPI IRQ. */
void ipi_enable_irq(int irq);

/* IPI tables */
extern void (*ipi_handler_table[])(int32_t, void *, uint32_t);
extern int *ipi_wakeup_table[];

/* Helper macros to build the IPI handler and wakeup functions. */
#define IPI_HANDLER(id) CONCAT3(ipi_, id, _handler)
#define IPI_WAKEUP(id) CONCAT3(ipi_, id, _wakeup)

/*
 * Macro to declare an IPI handler.
 * _id: The ID of the IPI
 * handler: The IPI handler function
 * is_wakeup_src: Declare IPI ID as a wake-up source or not
 */
#define DECLARE_IPI(_id, handler, is_wakeup_src) \
	struct ipi_num_check##_id { \
		int tmp1[_id < IPI_COUNT ? 1 : -1]; \
		int tmp2[is_wakeup_src == 0 || is_wakeup_src == 1 ? 1 : -1]; \
	};  \
	void __keep IPI_HANDLER(_id)(int32_t id, void *buf, uint32_t len) \
	{ \
		handler(id, buf, len); \
	} \
	const int __keep IPI_WAKEUP(_id) = is_wakeup_src

#endif /* __CROS_EC_IPI_CHIP_H */
