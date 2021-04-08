/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SCP_MDP_H
#define __CROS_EC_SCP_MDP_H

struct mdp_msg_service {
	int id;
	unsigned char msg[20];
};
BUILD_ASSERT(member_size(struct mdp_msg_service, msg) <=
		CONFIG_IPC_SHARED_OBJ_BUF_SIZE);

/* Functions provided by private overlay. */
void mdp_common_init(void);
void mdp_ipi_task_handler(void *pvParameters);

#endif  /* __CROS_EC_SCP_MDP_H */
