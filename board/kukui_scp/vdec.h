/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SCP_VDEC_H
#define __CROS_EC_SCP_VDEC_H

#include "chip/mt_scp/registers.h"
#include "compile_time_macros.h"
#include "queue.h"

enum vdec_type {
	VDEC_H264,
	VDEC_VP8,
	VDEC_VP9,
	VDEC_MAX,
};

typedef void (*vdec_msg_handler)(void *msg);

struct vdec_msg {
	enum vdec_type type;
	unsigned char msg[48];
};

BUILD_ASSERT(member_size(struct vdec_msg, msg) <= CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void vdec_h264_service_init(void);
void vdec_h264_msg_handler(void *data);

#endif /* __CROS_EC_SCP_VDEC_H */
