/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/ucsi_v3.h>

const char *const ucsi_invalid_name = "OUTSIDE_VALID_RANGE";
const char *const ucsi_deprecated_name = "DEPRECATED";

static const char *const ucsi_command_names[] = {
	[UCSI_PPM_RESET] = "PPM_RESET",
	[UCSI_CANCEL] = "CANCEL",
	[UCSI_CONNECTOR_RESET] = "CONNECTOR_RESET",
	[UCSI_ACK_CC_CI] = "ACK_CC_CI",
	[UCSI_SET_NOTIFICATION_ENABLE] = "SET_NOTIFICATION_ENABLE",
	[UCSI_GET_CAPABILITY] = "GET_CAPABILITY",
	[UCSI_GET_CONNECTOR_CAPABILITY] = "GET_CONNECTOR_CAPABILITY",
	[UCSI_SET_CCOM] = "SET_CCOM",
	[UCSI_SET_UOR] = "SET_UOR",
	[UCSI_SET_PDR] = "SET_PDR",
	[UCSI_GET_ALTERNATE_MODES] = "GET_ALTERNATE_MODES",
	[UCSI_GET_CAM_SUPPORTED] = "GET_CAM_SUPPORTED",
	[UCSI_GET_CURRENT_CAM] = "GET_CURRENT_CAM",
	[UCSI_SET_NEW_CAM] = "SET_NEW_CAM",
	[UCSI_GET_PDOS] = "GET_PDOS",
	[UCSI_GET_CABLE_PROPERTY] = "GET_CABLE_PROPERTY",
	[UCSI_GET_CONNECTOR_STATUS] = "GET_CONNECTOR_STATUS",
	[UCSI_GET_ERROR_STATUS] = "GET_ERROR_STATUS",
	[UCSI_SET_POWER_LEVEL] = "SET_POWER_LEVEL",
	[UCSI_GET_PD_MESSAGE] = "GET_PD_MESSAGE",
	[UCSI_GET_ATTENTION_VDO] = "GET_ATTENTION_VDO",
	[UCSI_GET_CAM_CS] = "GET_CAM_CS",
	[UCSI_LPM_FW_UPDATE_REQUEST] = "LPM_FW_UPDATE_REQUEST",
	[UCSI_SECURITY_REQUEST] = "SECURITY_REQUEST",
	[UCSI_SET_RETIMER_MODE] = "SET_RETIMER_MODE",
	[UCSI_SET_SINK_PATH] = "SET_SINK_PATH",
	[UCSI_SET_PDOS] = "SET_PDOS",
	[UCSI_READ_POWER_LEVEL] = "READ_POWER_LEVEL",
	[UCSI_CHUNKING_SUPPORT] = "CHUNKING_SUPPORTED",
	[UCSI_VENDOR_DEFINED_COMMAND] = "VENDOR_DEFINED",
	[UCSI_SET_USB] = "SET_USB",
	[UCSI_GET_LPM_PPM_INFO] = "GET_LPM_PPM_INFO",
};

const char *const get_ucsi_command_name(enum ucsi_command_t cmd)
{
	if (cmd >= UCSI_CMD_MAX) {
		return ucsi_invalid_name;
	} else if (!ucsi_command_names[cmd]) {
		return ucsi_deprecated_name;
	} else {
		return ucsi_command_names[cmd];
	}
}

static const char *drp_mode_names[] = {
	"NORMAL",
	"TRY_SRC",
	"TRY_SNK",
};
BUILD_ASSERT(ARRAY_SIZE(drp_mode_names) == DRP_MAX_ENUM);

const char *get_drp_mode_name(enum drp_mode_t mode)
{
	if (mode < DRP_INVALID) {
		return drp_mode_names[mode];
	} else {
		return "INVALID DRP MODE";
	}
}
