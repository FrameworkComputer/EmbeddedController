/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock of Device Policy Manager implementation */

#ifndef __MOCK_USB_PD_DPM_MOCK_H
#define __MOCK_USB_PD_DPM_MOCK_H

#include "common.h"
#include "usb_pd_dpm.h"

/* Defaults should all be 0 values. */
struct mock_dpm_port_t {
	bool mode_entry_done;
	bool mode_exit_request;
};

extern struct mock_dpm_port_t dpm[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_dpm_reset(void);

#endif /* __MOCK_USB_PD_DPM_MOCK_H */
