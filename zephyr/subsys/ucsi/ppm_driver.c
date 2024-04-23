/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UCSI PPM Driver */

#include "cros_board_info.h"
#include "ec_commands.h"
#include "include/pd_driver.h"
#include "include/ppm.h"
#include "ppm_common.h"
#include "usb_pd.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <drivers/pdc.h>

LOG_MODULE_REGISTER(ppm, LOG_LEVEL_INF);

#define DT_DRV_COMPAT ucsi_ppm
#define UCSI_7BIT_PORTMASK(p) ((p) & 0x7F)
#define DT_PPM_DRV DT_INST(0, DT_DRV_COMPAT)
#define NUM_PORTS DT_PROP_LEN(DT_PPM_DRV, lpm)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of ucsi,ppm should be defined.");

struct ucsi_commands_t {
	uint8_t command;
	uint8_t command_copy_length;
};

#define UCSI_CMD_ENTRY(cmd, length)                            \
	{                                                      \
		.command = cmd, .command_copy_length = length, \
	}

struct ucsi_commands_t ucsi_commands[UCSI_CMD_VENDOR_CMD + 1] = {
	UCSI_CMD_ENTRY(UCSI_CMD_RESERVED, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_PPM_RESET, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_CANCEL, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_CONNECTOR_RESET, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_ACK_CC_CI, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_NOTIFICATION_ENABLE, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAPABILITY, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CONNECTOR_CAPABILITY, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_CCOM, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_UOR, 2),
	UCSI_CMD_ENTRY(obsolete_UCSI_CMD_SET_PDM, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_PDR, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ALTERNATE_MODES, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAM_SUPPORTED, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CURRENT_CAM, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_NEW_CAM, 6),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_PDOS, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CABLE_PROPERTY, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CONNECTOR_STATUS, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ERROR_STATUS, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_POWER_LEVEL, 6),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_PD_MESSAGE, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_ATTENTION_VDO, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_reserved_0x17, 0),
	UCSI_CMD_ENTRY(UCSI_CMD_GET_CAM_CS, 2),
	UCSI_CMD_ENTRY(UCSI_CMD_LPM_FW_UPDATE_REQUEST, 4),
	UCSI_CMD_ENTRY(UCSI_CMD_SECURITY_REQUEST, 5),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_RETIMER_MODE, 5),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_SINK_PATH, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_SET_PDOS, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_READ_POWER_LEVEL, 3),
	UCSI_CMD_ENTRY(UCSI_CMD_CHUNKING_SUPPORT, 1),
	UCSI_CMD_ENTRY(UCSI_CMD_VENDOR_CMD, 6),
};

#define PHANDLE_TO_DEV(node_id, prop, idx) \
	[idx] = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

struct ppm_config {
	const struct device *lpm[NUM_PORTS];
	uint8_t active_port_count;
};
static const struct ppm_config ppm_config = {
	.lpm = { DT_FOREACH_PROP_ELEM(DT_PPM_DRV, lpm, PHANDLE_TO_DEV) },
	.active_port_count = NUM_PORTS,
};

struct ppm_data {
	struct ucsi_ppm_driver *ppm;
	struct ucsiv3_get_connector_status_data
		port_status[NUM_PORTS] __aligned(4);
};
static struct ppm_data ppm_data;

static int ucsi_ppm_init(const struct device *device)
{
	const struct ppm_config *cfg =
		(const struct ppm_config *)device->config;
	struct ppm_data *dat = (struct ppm_data *)device->data;

	return dat->ppm->init_and_wait(dat->ppm->dev, cfg->active_port_count);
}

static struct ucsi_ppm_driver *ucsi_ppm_get(const struct device *device)
{
	struct ppm_data *dat = (struct ppm_data *)device->data;

	return dat->ppm;
}

static int ucsi_ppm_execute_cmd(const struct device *device,
				struct ucsi_control *control,
				uint8_t *lpm_data_out)
{
	const struct ppm_config *cfg =
		(const struct ppm_config *)device->config;
	struct ppm_data *dat = (struct ppm_data *)device->data;
	struct ppm_common_device *dev =
		(struct ppm_common_device *)dat->ppm->dev;
	uint8_t ucsi_command = control->command;
	uint8_t conn; /* 1:port=0, 2:port=1, ... */
	uint8_t data_size;

	if (ucsi_command == 0 || ucsi_command > UCSI_CMD_VENDOR_CMD) {
		LOG_ERR("Invalid command 0x%x", ucsi_command);
		return -1;
	}

	/*
	 * Most commands pass the connector number starting at bit 16 - which
	 * aligns to command_specific[0] but GET_ALTERNATE_MODE moves this to
	 * bit 24 and some commands don't use a connector number at all
	 */
	switch (ucsi_command) {
	case UCSI_CMD_PPM_RESET:
	case UCSI_CMD_SET_NOTIFICATION_ENABLE:
		return -ENOTSUP;
	case UCSI_CMD_CONNECTOR_RESET:
	case UCSI_CMD_GET_CONNECTOR_CAPABILITY:
	case UCSI_CMD_GET_CAM_SUPPORTED:
	case UCSI_CMD_GET_CURRENT_CAM:
	case UCSI_CMD_SET_NEW_CAM:
	case UCSI_CMD_GET_PDOS:
	case UCSI_CMD_GET_CABLE_PROPERTY:
	case UCSI_CMD_GET_CONNECTOR_STATUS:
	case UCSI_CMD_GET_ERROR_STATUS:
	case UCSI_CMD_GET_PD_MESSAGE:
	case UCSI_CMD_GET_ATTENTION_VDO:
	case UCSI_CMD_GET_CAM_CS:
		conn = UCSI_7BIT_PORTMASK(control->command_specific[0]);
		break;
	case UCSI_CMD_GET_ALTERNATE_MODES:
		conn = UCSI_7BIT_PORTMASK(control->command_specific[1]);
		break;
	default:
		conn = 1;
	}

	if (conn == 0 || conn > dev->num_ports) {
		LOG_ERR("Invalid conn=%d", conn);
		return -EINVAL;
	}

	data_size = ucsi_commands[ucsi_command].command_copy_length;
	LOG_INF("%s: Executing conn=%u cmd=0x%02x data_size=%d", __func__, conn,
		ucsi_command, data_size);
	return pdc_execute_command_sync(cfg->lpm[conn - 1], ucsi_command,
					data_size, control->command_specific,
					lpm_data_out);
}

static int ucsi_get_active_port_count(const struct device *dev)
{
	return NUM_PORTS;
}

static struct ucsi_pd_driver ppm_drv = {
	.init_ppm = ucsi_ppm_init,
	.get_ppm = ucsi_ppm_get,
	.execute_cmd = ucsi_ppm_execute_cmd,
	.get_active_port_count = ucsi_get_active_port_count,
};

static int ppm_init(const struct device *device)
{
	struct ppm_data *dat = (struct ppm_data *)device->data;
	const struct ucsi_pd_driver *drv = device->api;
	union ec_common_control ctrl;

	if (cbi_get_common_control(&ctrl) || !ctrl.ucsi_enabled) {
		LOG_INF("PPM disabled in CBI");
		return -ENODEV;
	}

	/* Initialize the PPM. */
	dat->ppm = ppm_open(drv, dat->port_status);
	if (!dat->ppm) {
		LOG_ERR("Failed to open PPM");
		return -ENODEV;
	}

	return 0;
}
DEVICE_DT_INST_DEFINE(0, &ppm_init, NULL, &ppm_data, &ppm_config, POST_KERNEL,
		      CONFIG_PDC_POWER_MGMT_INIT_PRIORITY, &ppm_drv);
