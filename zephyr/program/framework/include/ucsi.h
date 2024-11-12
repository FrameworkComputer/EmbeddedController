/* Copyright 2023 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip UCSI
 */

#ifndef __CROS_EC_UCSI_H
#define __CROS_EC_UCSI_H

#include "common.h"

/************************************************/
/*	UCSI CONTROL DEFINITION                 */
/************************************************/
enum ucsi_control {
	CYPD_UCSI_START   = 0x01,
	CYPD_UCSI_STOP    = 0x02,
	CYPD_UCSI_SILENCE = 0x03,
	CYPD_UCSI_SIGNAL_CONNECT_EVENT_TO_OS = 0x04
};

enum ucsi_command {
	UCSI_CMD_RESERVE,
	UCSI_CMD_PPM_RESET,
	UCSI_CMD_CANCEL,
	UCSI_CMD_CONNECTOR_RESET,
	UCSI_CMD_ACK_CC_CI,
	UCSI_CMD_SET_NOTIFICATION_ENABLE,
	UCSI_CMD_GET_CAPABILITY,
	UCSI_CMD_GET_CONNECTOR_CAPABILITY,
	UCSI_CMD_SET_UOM,
	UCSI_CMD_SET_UOR,
	UCSI_CMD_SET_PDM,
	UCSI_CMD_SET_PDR,
	UCSI_CMD_GET_ALTERNATE_MODES,
	UCSI_CMD_GET_CAM_SUPPORTED,
	UCSI_CMD_GET_CURRENT_CAM,
	UCSI_CMD_SET_NEW_CAM,
	UCSI_CMD_GET_PDOS,
	UCSI_CMD_GET_CABLE_PROPERTY,
	UCSI_CMD_GET_CONNECTOR_STATUS,
	UCSI_CMD_GET_ERROR_STATUS,
};

/**
 * PD chip indicates to the UCSI change connector, only return 1(port0)/2(port1)
 */
enum pd_chip_ucsi_connector {
	PD_CHIP_UCSI_CONNECTOR_1 = 1,
	PD_CHIP_UCSI_CONNECTOR_2,
};

struct ucsi_to_pd_port_map {
	int pd_controller;
	int pd_controller_port;
};

extern struct ucsi_to_pd_port_map ucsi_pd_port_map[];

int ucsi_write_tunnel(void);
int ucsi_read_tunnel(int controller);
int ucsi_startup(int controller);
void ucsi_set_debug(bool enable);
void check_ucsi_event_from_host(void);
void record_ucsi_connector_change_event(int controller, int port);
#endif	/* __CROS_EC_UCSI_H */
