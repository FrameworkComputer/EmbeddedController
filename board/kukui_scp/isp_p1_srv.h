/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISP_P1_SRV_H
#define __CROS_EC_ISP_P1_SRV_H

#include "ipi_chip.h"

struct isp_msg {
	unsigned char id;
	unsigned char msg[140];
};

BUILD_ASSERT(member_size(struct isp_msg, msg) <=
	     CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void isp_msg_handler(void *data);

#endif /* __CROS_EC_ISP_P1_SRV_H */
