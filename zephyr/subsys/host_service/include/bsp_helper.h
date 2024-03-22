/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/****** IPC helper definitions *****/

#ifndef __IPC_HELPER_H
#define __IPC_HELPER_H

#include "../heci/heci_internal.h"

#include <zephyr/device.h>

#define MNG_CAP_SUPPORTED 1

#define PROTOCOL_BOOT 0
#define PROTOCOL_HECI 1
#define PROTOCOL_MCTP 2
#define PROTOCOL_MNG 3
#define PROTOCOL_INVAILD 0xF

#define HEADER_LENGTH_MASK (0x03FF)
#define HEADER_LENGTH1_MASK (0xF)
#define HEADER_PROTOCOL_MASK (0x0F)
#define HEADER_MNG_CMD_MASK (0x0F)
#define HEADER_CORE_MASK (0x0F)

#define HEADER_LENGTH_OFFSET 0
#define HEADER_PROTOCOL_OFFSET 10
#define HEADER_MNG_CMD_OFFSET 16
#define HEADER_LENGTH1_OFFSET 20
#define HEADER_CORE_OFFSET 24
#define DRBL_BUSY_OFFS 31

#define RTD3_NOTIFIED_STUCK ((uint32_t)-1)

#define HEADER_GET_LENGTH(drbl_reg)                                     \
	((((drbl_reg) >> HEADER_LENGTH_OFFSET) & HEADER_LENGTH_MASK) +  \
	 ((((drbl_reg) >> HEADER_LENGTH1_OFFSET) & HEADER_LENGTH1_MASK) \
	  << HEADER_PROTOCOL_OFFSET))
#define HEADER_GET_COREID(drbl_reg) \
	(((drbl_reg) >> HEADER_CORE_OFFSET) & HEADER_CORE_MASK)
#define HEADER_GET_PROTOCOL(drbl_reg) \
	(((drbl_reg) >> HEADER_PROTOCOL_OFFSET) & HEADER_PROTOCOL_MASK)
#define HEADER_GET_MNG_CMD(drbl_reg) \
	(((drbl_reg) >> HEADER_MNG_CMD_OFFSET) & HEADER_MNG_CMD_MASK)

#define BUILD_DRBL(length, protocol)                                   \
	(((1 << DRBL_BUSY_OFFS) |                                      \
	  (CONFIG_HECI_CORE_ID << HEADER_CORE_OFFSET) |                \
	  ((protocol) << HEADER_PROTOCOL_OFFSET) |                     \
	  (((length) & HEADER_LENGTH_MASK) << HEADER_LENGTH_OFFSET)) | \
	 (((length) >> HEADER_PROTOCOL_OFFSET) << HEADER_LENGTH1_OFFSET))

#define BUILD_MNG_DRBL(cmd, length)                     \
	(((1 << DRBL_BUSY_OFFS) |                       \
	  (CONFIG_HECI_CORE_ID << HEADER_CORE_OFFSET) | \
	  ((PROTOCOL_MNG) << HEADER_PROTOCOL_OFFSET) |  \
	  ((cmd) << HEADER_MNG_CMD_OFFSET) |            \
	  ((length) << HEADER_LENGTH_OFFSET)))

int send_rx_complete(void);

void heci_reset(void);

/*!
 * \fn int mng_host_access_req(uint32_t timeout)
 * \brief request access to host
 * \param[in] timeout: timeout time (in milliseconds)
 * \return 0 or error codes
 *
 * @note mng_host_access_req and mng_host_access_dereq should be called in pair
 */
extern int (*mng_host_access_req)(uint32_t timeout);

void mng_host_access_dereq(void);

#endif /* __IPC_HELPER_H */
