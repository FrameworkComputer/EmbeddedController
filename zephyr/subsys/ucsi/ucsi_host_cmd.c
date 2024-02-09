/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UCSI host command */

#include "hooks.h"
#include "include/pd_driver.h"
#include "include/platform.h"
#include "include/ppm.h"
#include "ppm_common.h"
#include "usb_pd.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ucsi, LOG_LEVEL_INF);

#define DEV_CAST_FROM(v) (struct ppm_common_device *)(v)

static struct ucsi_ppm_driver *ppm_drv;

/* Sort of main */
static int eppm_init(void)
{
	const struct ucsi_pd_driver *drv;
	const struct device *pdc_dev;
	struct ppm_common_device *ppm_dev;

	pdc_dev = DEVICE_DT_GET(DT_INST(0, ucsi_ppm));
	drv = pdc_dev->api;
	if (!drv) {
		LOG_ERR("Failed to open PDC");
		return -ENODEV;
	}

	/* Start a PPM task. */
	if (drv->init_ppm(pdc_dev)) {
		LOG_ERR("Failed to init PPM");
		return -ENODEV;
	}

	ppm_drv = drv->get_ppm(pdc_dev);
	ppm_dev = DEV_CAST_FROM(ppm_drv->dev);
	LOG_INF("Initialized PPM num_ports=%u", ppm_dev->num_ports);

	return 0;
}
SYS_INIT(eppm_init, APPLICATION, 99);
