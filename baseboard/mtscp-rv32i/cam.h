/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CAM_SRV_H
#define __CROS_EC_CAM_SRV_H

#include "ipi_chip.h"

struct cam_msg {
	unsigned char id;
	unsigned char msg[86];
};

BUILD_ASSERT(member_size(struct cam_msg, msg) <=
	     CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void ipi_cam_handler(void *data);

#endif /* __CROS_EC_CAM_SRV_H */
