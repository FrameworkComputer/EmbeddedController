/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bluey ADSP I2C port configuration */

#include "adsp_comms.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adsp_comms, LOG_LEVEL_INF);

#define ADSP_I2C_TARGET_ADDRESS 0x0C
#define ADSP_COMMS_I2C_PKT_SIZE 4

static uint8_t rx_buf[ADSP_COMMS_I2C_PKT_SIZE];
static uint8_t rx_idx;

struct adsp_comms_msg {
	uint8_t fid;
	uint8_t addr;
	uint16_t data;
};

K_MSGQ_DEFINE(adsp_comms_msgq, sizeof(struct adsp_comms_msg),
	      CONFIG_ADSP_COMMS_MSGQ_SIZE, 4);

/* Find and call registered callbacks for a given feature ID and register
 * address */
static void process_callbacks(uint8_t fid, uint8_t addr, uint16_t data)
{
	bool handled = false;

	STRUCT_SECTION_FOREACH(adsp_comms_callback, cb_entry)
	{
		if (cb_entry->fid == fid && cb_entry->addr == addr) {
			if (cb_entry->cb) {
				cb_entry->cb(fid, addr, data);
				handled = true;
			}
		}
	}

	if (!handled) {
		LOG_INF("Processed I2C (no cb): Feature 0x%02x, Reg 0x%02x, Data 0x%04x",
			fid, addr, data);
	}
}

static void adsp_comms_task(void *p1, void *p2, void *p3)
{
	struct adsp_comms_msg msg;

	LOG_INF("ADSP comms task started");

	while (1) {
		if (k_msgq_get(&adsp_comms_msgq, &msg, K_FOREVER) == 0) {
			process_callbacks(msg.fid, msg.addr, msg.data);
		}
	}
}

K_THREAD_DEFINE(adsp_comms_tid, CONFIG_ADSP_COMMS_STACK_SIZE, adsp_comms_task,
		NULL, NULL, NULL, CONFIG_ADSP_COMMS_PRIORITY, 0, 0);

static int adsp_i2c_write_requested(struct i2c_target_config *config)
{
	rx_idx = 0;
	return 0;
}

static int adsp_i2c_write_received(struct i2c_target_config *config,
				   uint8_t val)
{
	if (rx_idx < sizeof(rx_buf)) {
		rx_buf[rx_idx++] = val;
	} else {
		return -ENOMEM;
	}
	return 0;
}

static int adsp_i2c_read_requested(struct i2c_target_config *config,
				   uint8_t *val)
{
	/* Default response for read requests */
	*val = 0xFF;
	return 0;
}

static int adsp_i2c_read_processed(struct i2c_target_config *config,
				   uint8_t *val)
{
	/* Default response for subsequent read bytes */
	*val = 0xFF;
	return 0;
}

static int adsp_i2c_stop(struct i2c_target_config *config)
{
	if (rx_idx == ADSP_COMMS_I2C_PKT_SIZE) {
		struct adsp_comms_msg msg;

		msg.addr = rx_buf[0];
		msg.fid = rx_buf[1];
		msg.data = (rx_buf[3] << 8) | rx_buf[2];

		if (k_msgq_put(&adsp_comms_msgq, &msg, K_NO_WAIT) != 0) {
			LOG_WRN("ADSP comms queue full");
		}
	} else if (rx_idx > 0) {
		LOG_WRN("Incomplete I2C packet: %d bytes", rx_idx);
	}

	rx_idx = 0;
	return 0;
}

/* i2c target mode callback definition */
static const struct i2c_target_callbacks target_callbacks = {
	.write_requested = adsp_i2c_write_requested,
	.write_received = adsp_i2c_write_received,
	.read_requested = adsp_i2c_read_requested,
	.read_processed = adsp_i2c_read_processed,
	.stop = adsp_i2c_stop,
};

struct i2c_target_config target_cfg = {
	.address = ADSP_I2C_TARGET_ADDRESS,
	.callbacks = &target_callbacks,
};

/* Before AP power on turn the I2C_PORT_ADSP to Target mode */
void board_chipset_startup_i2c_target(void)
{
	int ret;
	const struct device *i2c_dev = i2c_get_device_for_port(I2C_PORT_ADSP);

	LOG_INF("Configuring I2C_PORT_ADSP as Target.");
	ret = i2c_target_register(i2c_dev, &target_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to register I2C target (err %d)", ret);
		return;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup_i2c_target,
	     HOOK_PRIO_DEFAULT);

/*
 * After the AP is powered-off turn back the I2C_PORT_ADSP to controller mode
 * allowing to access the battery pack
 */
void board_chipset_shutdown_complete_i2c_controller(void)
{
	int ret;
	const struct device *i2c_dev = i2c_get_device_for_port(I2C_PORT_ADSP);
	uint32_t dev_config;

	ret = i2c_get_config(i2c_dev, &dev_config);
	if (ret < 0) {
		LOG_ERR("Failed to fetch device config (err %d)", ret);
		return;
	}

	ret = i2c_target_unregister(i2c_dev, &target_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to unregister I2C target (err %d)", ret);
		return;
	}

	/* Re-configure as a controller. */
	LOG_INF("Configuring I2C_PORT_ADSP as Controller.");
	ret = i2c_configure(i2c_dev, dev_config);
	if (ret < 0) {
		LOG_ERR("Failed to re-configure I2C controller (err %d)", ret);
		return;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE,
	     board_chipset_shutdown_complete_i2c_controller, HOOK_PRIO_DEFAULT);
