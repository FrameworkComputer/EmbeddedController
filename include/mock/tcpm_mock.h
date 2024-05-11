/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for the TCPM interface */

#ifndef __MOCK_TCPM_MOCK_H
#define __MOCK_TCPM_MOCK_H

#include "common.h"
#include "tcpm/tcpm.h"

/* Copied from usb_prl_sm.c, line 99. */
#define MOCK_CHK_BUF_SIZE 7

/* Define a struct to hold the data we need to control the mocks. */
struct mock_tcpm_t {
	uint32_t mock_rx_chk_buf[MOCK_CHK_BUF_SIZE];
	uint32_t mock_header;
	int mock_has_pending_message;
};

extern struct mock_tcpm_t mock_tcpm[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_tcpm_reset(void);
void mock_tcpm_rx_msg(int port, uint16_t header, int cnt, const uint32_t *data);

#endif /* __MOCK_TCPM_MOCK_H */