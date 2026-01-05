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

enum ucsi_commands {
	/* UCSI 1.x */
	UCSI_CMD_RESERVED = 0,
	UCSI_CMD_PPM_RESET = 0x01,
	UCSI_CMD_CANCEL = 0x02,
	UCSI_CMD_CONNECTOR_RESET = 0x03,
	UCSI_CMD_ACK_CC_CI = 0x04,
	UCSI_CMD_SET_NOTIFICATION_ENABLE = 0x05,
	UCSI_CMD_GET_CAPABILITY = 0x06,
	UCSI_CMD_GET_CONNECTOR_CAPABILITY = 0x07,
	UCSI_CMD_SET_CCOM = 0x08,
	UCSI_CMD_SET_UOR = 0x09,
	obsolete_UCSI_CMD_SET_PDM = 0x0A,
	UCSI_CMD_SET_PDR = 0x0B,
	UCSI_CMD_GET_ALTERNATE_MODES = 0x0C,
	UCSI_CMD_GET_CAM_SUPPORTED = 0x0D,
	UCSI_CMD_GET_CURRENT_CAM = 0x0E,
	UCSI_CMD_SET_NEW_CAM = 0x0F,
	UCSI_CMD_GET_PDOS = 0x10,
	UCSI_CMD_GET_CABLE_PROPERTY = 0x11,
	UCSI_CMD_GET_CONNECTOR_STATUS = 0x12,
	UCSI_CMD_GET_ERROR_STATUS = 0x13,
	UCSI_CMD_SET_POWER_LEVEL = 0x14,
	UCSI_CMD_GET_PD_MESSAGE = 0x15,
	/* UCSI 2.x */
	UCSI_CMD_GET_ATTENTION_VDO = 0x16,
	UCSI_CMD_reserved_0x17 = 0x17,
	UCSI_CMD_GET_CAM_CS = 0x18,
	UCSI_CMD_LPM_FW_UPDATE_REQUEST = 0x19,
	UCSI_CMD_SECURITY_REQUEST = 0x1A,
	UCSI_CMD_SET_RETIMER_MODE = 0x1B,
	UCSI_CMD_SET_SINK_PATH = 0x1C,
	/* UCSI 3.x */
	UCSI_CMD_SET_PDOS = 0x1D,
	UCSI_CMD_READ_POWER_LEVEL = 0x1E,
	UCSI_CMD_CHUNKING_SUPPORT = 0x1F,
	UCSI_CMD_VENDOR_CMD = 0x20,
	UCSI_CMD_MAX,
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

enum ucsi_port {
	UCSI_PORT_1,
	UCSI_PORT_2,
	UCSI_PORT_3,
	UCSI_PORT_4,
	UCSI_PORT_5,
};

extern struct ucsi_to_pd_port_map ucsi_pd_port_map[];

int ucsi_write_tunnel(void);
int ucsi_read_tunnel(int controller);
int ucsi_startup(int controller);
int ucsi_message_out_offset(void);
int ucsi_to_pd_port(int ucsi_port);
int pd_to_ucsi_port(int pd_port);
void ucsi_set_debug(bool enable);
void check_ucsi_event_from_host(void);
void record_ucsi_connector_change_event(int controller, int port);
void setup_ucsi_pd_mapping(void);
#endif	/* __CROS_EC_UCSI_H */
