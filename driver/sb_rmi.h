/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AMD SB-RMI (Side-band Remote Management Interface) Driver */

#ifndef __CROS_EC_SB_RMI_H
#define __CROS_EC_SB_RMI_H

#include "common.h"

#define SB_RMI_OUT_BND_MSG0_REG 0x30
#define SB_RMI_OUT_BND_MSG1_REG 0x31
#define SB_RMI_OUT_BND_MSG2_REG 0x32
#define SB_RMI_OUT_BND_MSG3_REG 0x33
#define SB_RMI_OUT_BND_MSG4_REG 0x34
#define SB_RMI_OUT_BND_MSG5_REG 0x35
#define SB_RMI_OUT_BND_MSG6_REG 0x36
#define SB_RMI_OUT_BND_MSG7_REG 0x37

#define SB_RMI_IN_BND_MSG0_REG 0x38
#define SB_RMI_IN_BND_MSG1_REG 0x39
#define SB_RMI_IN_BND_MSG2_REG 0x3a
#define SB_RMI_IN_BND_MSG3_REG 0x3b
#define SB_RMI_IN_BND_MSG4_REG 0x3c
#define SB_RMI_IN_BND_MSG5_REG 0x3d
#define SB_RMI_IN_BND_MSG6_REG 0x3e
#define SB_RMI_IN_BND_MSG7_REG 0x3f

#define SB_RMI_SW_INTR_REG 0x40
#define SB_RMI_STATUS_REG 0x02

#define SB_RMI_WRITE_STT_SENSOR_CMD 0x3A

#define SB_RMI_MAILBOX_SUCCESS 0x0
#define SB_RMI_MAILBOX_ERROR_ABORTED 0x1
#define SB_RMI_MAILBOX_ERROR_UNKNOWN_CMD 0x2
#define SB_RMI_MAILBOX_ERROR_INVALID_CORE 0x3

/* Socket ID 0 */
#define SB_RMI_I2C_ADDR_FLAGS0 0x3c
/* Socket ID 1 */
#define SB_RMI_I2C_ADDR_FLAGS1 0x30

/**
 * Execute a SB-RMI mailbox transaction
 *
 * cmd:
 *	See "SB-RMI Soft Mailbox Message" table in PPR for command id
 * msg_in:
 *	Message In buffer
 * msg_out:
 *	Message Out buffer
 */
int sb_rmi_mailbox_xfer(int cmd, uint32_t msg_in, uint32_t *msg_out_ptr);

#endif /* __CROS_EC_SB_RMI_H */
