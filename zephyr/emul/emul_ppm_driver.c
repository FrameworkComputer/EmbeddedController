/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UCSI PPM Driver */

#include "ec_commands.h"
#include "emul/emul_ppm_driver.h"
#include "usb_pd.h"
#include "usbc/ppm.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys_clock.h>

#include <drivers/pdc.h>
#include <usbc/ppm.h>

LOG_MODULE_REGISTER(ppm, LOG_LEVEL_INF);

#define DT_DRV_COMPAT ucsi_ppm
#define UCSI_7BIT_PORTMASK(p) ((p) & 0x7F)
#define DT_PPM_DRV DT_INST(0, DT_DRV_COMPAT)
#define NUM_PORTS DT_NUM_INST_STATUS_OKAY(named_usbc_port)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of ucsi-ppm should be defined.");

/*
 * Public emulator control API
 */

void emul_ppm_driver_reset(void)
{
	RESET_FAKE(ppm_driver_mock_init_ppm);
	RESET_FAKE(ppm_driver_mock_get_ppm_dev);
	RESET_FAKE(ppm_driver_mock_execute_cmd_sync);
	RESET_FAKE(ppm_driver_mock_get_active_port_count);

	ppm_driver_mock_get_active_port_count_fake.return_val = 1;
}

/*
 * API implementation functions using FFF fakes
 */

DEFINE_FAKE_VALUE_FUNC(int, ppm_driver_mock_init_ppm, const struct device *);
DEFINE_FAKE_VALUE_FUNC(struct ucsi_ppm_device *, ppm_driver_mock_get_ppm_dev,
		       const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, ppm_driver_mock_execute_cmd_sync,
		       const struct device *, struct ucsi_control_t *,
		       uint8_t *);
DEFINE_FAKE_VALUE_FUNC(int, ppm_driver_mock_get_active_port_count,
		       const struct device *);

static struct ucsi_pd_driver ppm_drv = {
	.init_ppm = ppm_driver_mock_init_ppm,
	.get_ppm_dev = ppm_driver_mock_get_ppm_dev,
	.execute_cmd = ppm_driver_mock_execute_cmd_sync,
	.get_active_port_count = ppm_driver_mock_get_active_port_count,
};

static int ppm_init(const struct device *device)
{
	emul_ppm_driver_reset();

	return 0;
}

DEVICE_DT_INST_DEFINE(0, &ppm_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_PDC_POWER_MGMT_INIT_PRIORITY, &ppm_drv);
