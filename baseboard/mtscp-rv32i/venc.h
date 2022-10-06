/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SCP_VENC_H
#define __CROS_EC_SCP_VENC_H

#include "compile_time_macros.h"

enum venc_type {
	VENC_H264,
	VENC_MAX,
};

struct venc_msg {
	enum venc_type type;
	unsigned char msg[288];
};
BUILD_ASSERT(member_size(struct venc_msg, msg) <=
	     CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void venc_h264_msg_handler(void *data);

#endif /* __CROS_EC_SCP_VENC_H */
