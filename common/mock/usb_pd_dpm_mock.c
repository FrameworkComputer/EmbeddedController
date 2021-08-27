/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Mock of Device Policy Manager implementation
 * Refer to USB PD 3.0 spec, version 2.0, sections 8.2 and 8.3
 */

#include "usb_pd.h"
#include "mock/usb_pd_dpm_mock.h"
#include "memory.h"
#include "usb_pd_tcpm.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_dpm_port_t dpm[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_dpm_reset(void)
{
	/* Reset all values to 0. */
	memset(dpm, 0, sizeof(dpm));
}

void dpm_init(int port)
{
	dpm[port].mode_entry_done = false;
	dpm[port].mode_exit_request = false;
}

void dpm_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		uint32_t *vdm)
{
}

void dpm_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		uint8_t vdm_cmd)
{
}

void dpm_set_mode_exit_request(int port)
{
}

void dpm_run(int port)
{
}

void dpm_evaluate_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo)
{
}

void dpm_add_non_pd_sink(int port)
{
}

void dpm_remove_sink(int port)
{
}

void dpm_remove_source(int port)
{
}

int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnt;
}
