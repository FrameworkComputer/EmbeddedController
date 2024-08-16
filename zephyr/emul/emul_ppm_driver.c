/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UCSI PPM Driver */

#include "ec_commands.h"
#include "usb_pd.h"
#include "usbc/ppm.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys_clock.h>

#include <drivers/pdc.h>
#include <usbc/ppm.h>

LOG_MODULE_REGISTER(ppm, LOG_LEVEL_INF);

#define DT_DRV_COMPAT ucsi_ppm
#define UCSI_7BIT_PORTMASK(p) ((p) & 0x7F)
#define DT_PPM_DRV DT_INST(0, DT_DRV_COMPAT)
#define NUM_PORTS DT_PROP_LEN(DT_PPM_DRV, lpm)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of ucsi-ppm should be defined.");

static struct ucsi_ppm_device *emul_ucsi_ppm_device;
static int ucsi_init_ppm_retval;

void emul_ppm_driver_set_init_ppm_retval(int rv)
{
	ucsi_init_ppm_retval = rv;
}

static int ucsi_init_ppm(const struct device *device)
{
	return ucsi_init_ppm_retval;
}

void emul_ppm_driver_set_ucsi_ppm_device(struct ucsi_ppm_device *ppm_device)
{
	emul_ucsi_ppm_device = ppm_device;
}

static struct ucsi_ppm_device *ucsi_ppm_get_ppm_dev(const struct device *device)
{
	return emul_ucsi_ppm_device;
}

static int ucsi_ppm_execute_cmd_sync(const struct device *device,
				     struct ucsi_control_t *control,
				     uint8_t *lpm_data_out)
{
	return 0;
}

static int ucsi_get_active_port_count(const struct device *dev)
{
	return 1;
}

static struct ucsi_pd_driver ppm_drv = {
	.init_ppm = ucsi_init_ppm,
	.get_ppm_dev = ucsi_ppm_get_ppm_dev,
	.execute_cmd = ucsi_ppm_execute_cmd_sync,
	.get_active_port_count = ucsi_get_active_port_count,
};

static int ppm_init(const struct device *device)
{
	return 0;
}
DEVICE_DT_INST_DEFINE(0, &ppm_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_PDC_POWER_MGMT_INIT_PRIORITY, &ppm_drv);
