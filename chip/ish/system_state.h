/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __SYSTEM_STATE_H
#define __SYSTEM_STATE_H

#define HECI_FIXED_SYSTEM_STATE_ADDR				13

struct ss_subsys_device;

struct system_state_callbacks {
	int (*resume)(struct ss_subsys_device *ss_device);
	int (*suspend)(struct ss_subsys_device *ss_device);
};

struct ss_subsys_device {
	struct system_state_callbacks *cbs;
};

/* register system state client */
int ss_subsys_register_client(struct ss_subsys_device *ss_device);

/*
 * this function is called by HECI layer when there's a message for
 * system state subsystem
 */
void heci_handle_system_state_msg(uint8_t *msg, const size_t length);

#endif /* __SYSTEM_STATE_H */
