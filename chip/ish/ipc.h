/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IPC module for ISH */

#ifndef __CROS_EC_IPC_H
#define __CROS_ECIPC_H

#define IPC_FAILURE			-1
#define IPC_TIMEOUT			-1
#define UNSET_PIMR			0
#define SET_PIMR			1
#define SET_BUSY			1

#define IPC_PROTOCOL_MNG		3	/* Management protocol */
#define IPC_PROTOCOL_ECP		4	/* EC protocol */
#define MNG_TIME_UPDATE			5
#define MNG_HC_FW_READY			3	/* host command ready */

#define EVENT_FLAG_BIT_READ_IPC		(1<<0)
#define EVENT_FLAG_BIT_WRITE_IPC	(1<<2)

#define IPC_PIMR_HOST2ISH_OFFS		(0)
#define IPC_PIMR_HOST2ISH_OFFS		(0)
#define IPC_PIMR_ISH2HOST_CLR_OFFS	(11)
#define IPC_INT_ISH2HOST_CLR_OFFS	(0)
#define IPC_PISR_HOST2ISH_OFFS		IPC_PIMR_HOST2ISH_OFFS
#define IPC_MSG_MAX_SIZE		0x80
#define IPC_DRBL_BUSY_OFFS		(31)
#define IPC_HEADER_PROTOCOL_OFFSET	10
#define IPC_HEADER_PROTOCOL_MASK	(0x0F)
#define IPC_HEADER_MNG_CMD_MASK		(0x0F)
#define IPC_HEADER_LENGTH_MASK		(0x03FF)
#define IPC_HEADER_MNG_CMD_OFFSET	16
#define IPC_HEADER_LENGTH_OFFSET	0
#define IPC_OOB_MSG_OFFS		(30)

#define IPC_PIMR_HOST2ISH_BIT		(1 << IPC_PIMR_HOST2ISH_OFFS)
#define IPC_PIMR_ISH2HOST_CLR_MASK_BIT	(1 << IPC_PIMR_ISH2HOST_CLR_OFFS)
#define IPC_PIMR_HOST2ISH_BIT		(1 << IPC_PIMR_HOST2ISH_OFFS)
#define IPC_INT_ISH2HOST_CLR_BIT	(1 << IPC_INT_ISH2HOST_CLR_OFFS)
#define IPC_PISR_HOST2ISH_BIT		(1 << IPC_PISR_HOST2ISH_OFFS)
#define IPC_OOB_MSG_BIT			(1 << IPC_OOB_MSG_OFFS)
#define IPC_DRBL_BUSY_BIT		(1 << IPC_DRBL_BUSY_OFFS)

#define IPC_IS_BUSY(drbl_reg) \
	((drbl_reg & IPC_DRBL_BUSY_BIT) == ((uint32_t) IPC_DRBL_BUSY_BIT))

#define IPC_HEADER_GET_PROTOCOL(drbl_reg) \
	((drbl_reg >> IPC_HEADER_PROTOCOL_OFFSET) & IPC_HEADER_PROTOCOL_MASK)

#define IPC_HEADER_GET_MNG_CMD(drbl_reg) \
	((drbl_reg >> IPC_HEADER_MNG_CMD_OFFSET) & IPC_HEADER_MNG_CMD_MASK)

#define IPC_HEADER_GET_LENGTH(drbl_reg) \
	((drbl_reg >> IPC_HEADER_LENGTH_OFFSET) & IPC_HEADER_LENGTH_MASK)

#define IPC_BUILD_HEADER(length, protocol, busy) \
	((busy << IPC_DRBL_BUSY_OFFS) \
	 | (protocol << IPC_HEADER_PROTOCOL_OFFSET) \
	 | (length << IPC_HEADER_LENGTH_OFFSET))

#define IPC_BUILD_MNG_MSG(cmd, length) \
	((1 << IPC_DRBL_BUSY_OFFS)\
	 | (IPC_PROTOCOL_MNG << IPC_HEADER_PROTOCOL_OFFSET) \
	 | (cmd << IPC_HEADER_MNG_CMD_OFFSET)\
	 | (length << IPC_HEADER_LENGTH_OFFSET))

struct ipc_if_ctx {
	uint32_t in_msg_reg;
	uint32_t out_msg_reg;
	uint32_t in_drbl_reg;
	uint32_t out_drbl_reg;
	uint32_t clr_bit;
	uint8_t irq_in;
	uint8_t irq_clr;
};

struct ipc_oob_msg {
	uint32_t address;
	uint32_t length;
};

enum pimr_signal_type {
	PIMR_SIGNAL_IN = 0,
	PIMR_SIGNAL_OUT = 1,
	PIMR_SIGNAL_CLR = 2,
};

enum {
	IPC_PEER_HOST_ID = 0,
	IPC_PEERS_COUNT,
};

#endif				/* __CROS_ECIPC_H */
