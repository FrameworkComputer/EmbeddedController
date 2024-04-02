/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CAM_SRV_H
#define __CROS_EC_CAM_SRV_H

#include "ipi_chip.h"

#include <stdbool.h>

/*
 * IMPORTANT:
 * Please check MAX_MTKCAM_IPI_EVENT_SIZE if IPI message structure changes
 */
#define MAX_MTKCAM_IPI_EVENT_SIZE 588

struct cam_msg {
	unsigned char id;
	unsigned char msg[MAX_MTKCAM_IPI_EVENT_SIZE];
};

BUILD_ASSERT(member_size(struct cam_msg, msg) <=
	     CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void ipi_cam_handler(void *data);
#if defined(BOARD_GERALT_SCP_CORE1)
void ipi_img_handler(void *data);
int32_t startRED(void);
void img_task_handler(void);
bool img_task_working = false;
#endif
#endif /* __CROS_EC_CAM_SRV_H */
