/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/intel_altmode.h"
#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <drivers/pdc.h>
#include <usbc/ppm.h>

#define DT_PPM_DRV DT_INST(0, ucsi_ppm)
#define NUM_PORTS DT_PROP_LEN(DT_PPM_DRV, lpm)
#define DT_PPM_DEV DEVICE_DT_GET(DT_NODELABEL(ppm_driver_nodelabel))

struct ucsi_ppm_device {
	void *ptr;
};

int ppm_init(const struct device *device);

struct ppm_data {
	struct ucsi_ppm_device *ppm_dev;
	union connector_status_t port_status[NUM_PORTS] __aligned(4);
	struct pdc_callback cc_cb;
	struct pdc_callback ci_cb;
	union cci_event_t cci_event;
};

struct ppm_config {
	const struct device *lpm[NUM_PORTS];
	uint8_t active_port_count;
};

FAKE_VALUE_FUNC(struct ucsi_ppm_device *, ppm_data_init,
		const struct ucsi_pd_driver *, const struct device *,
		union connector_status_t *, int);

FAKE_VALUE_FUNC(int, ucsi_ppm_init_and_wait, struct ucsi_ppm_device *);

FAKE_VALUE_FUNC(bool, ucsi_ppm_get_next_connector_status,
		struct ucsi_ppm_device *, uint8_t *,
		union connector_status_t **);

FAKE_VOID_FUNC(ucsi_ppm_lpm_alert, struct ucsi_ppm_device *, uint8_t);

ZTEST_USER(ppm_driver, test_execute_cmd_bad_command)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	uint8_t out[512];
	struct ucsi_control_t control;
	int rv;

	ppm_dev = DT_PPM_DEV;
	drv = ppm_dev->api;

	/* UCSI command 0x00 is reserved. */
	control.command = 0;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, -1);

	control.command = UCSI_CMD_MAX;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, -1);
}

ZTEST_USER(ppm_driver, test_execute_cmd_nop)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	struct ucsi_control_t control;
	uint8_t out[512];
	int rv;

	ppm_dev = DT_PPM_DEV;
	drv = ppm_dev->api;

	control.command = UCSI_PPM_RESET;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, 0);

	control.command = UCSI_SET_NOTIFICATION_ENABLE;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, 0);
}

ZTEST_USER(ppm_driver, test_execute_cmd_invalid_connector)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	struct ucsi_control_t control;
	uint8_t out[512];
	int rv;

	ppm_dev = DT_PPM_DEV;
	drv = ppm_dev->api;

	/* Invalid connector# */
	control.command = UCSI_CONNECTOR_RESET;
	control.command_specific[0] = 0;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, -ERANGE, "rv=%d", rv);

	control.command = UCSI_CONNECTOR_RESET;
	control.command_specific[0] = NUM_PORTS + 1;
	rv = drv->execute_cmd(ppm_dev, &control, out);
	zassert_equal(rv, -ERANGE, "rv=%d", rv);
}

ZTEST_USER(ppm_driver, test_get_active_port_count)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	struct ppm_config *cfg;
	int rv;

	ppm_dev = DT_PPM_DEV;
	drv = ppm_dev->api;

	rv = drv->get_active_port_count(ppm_dev);
	cfg = (struct ppm_config *)ppm_dev->config;
	zassert_equal(rv, NUM_PORTS);
}

ZTEST_USER(ppm_driver, test_get_ppm_dev)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	struct ucsi_ppm_device ucsi_ppm_dev;
	int rv;

	ppm_dev = DT_PPM_DEV;
	drv = ppm_dev->api;

	ppm_data_init_fake.return_val = &ucsi_ppm_dev;
	rv = ppm_init(ppm_dev);
	zassert_equal(rv, 0);

	zassert_equal(drv->get_ppm_dev(ppm_dev), &ucsi_ppm_dev);
}

ZTEST_USER(ppm_driver, test_init_ppm)
{
	const struct device *ppm_dev;
	const struct ucsi_pd_driver *drv;
	struct ppm_data *data;
	int rv;

	ppm_dev = DT_PPM_DEV;

	zassert_not_null(ppm_dev);

	ucsi_ppm_init_and_wait_fake.return_val = 1;
	drv = ppm_dev->api;

	rv = drv->init_ppm(ppm_dev);
	data = (struct ppm_data *)ppm_dev->data;
	zassert_equal(ucsi_ppm_init_and_wait_fake.call_count, 1);
	zassert_equal(ucsi_ppm_init_and_wait_fake.arg0_val, data->ppm_dev);
	zassert_equal(rv, 1);
}

ZTEST_USER(ppm_driver, test_ppm_init_fail_in_ppm_data_init)
{
	const struct device *ppm_dev;
	int rv;

	ppm_dev = DT_PPM_DEV;
	ppm_data_init_fake.return_val = NULL;
	rv = ppm_init(ppm_dev);
	zassert_equal(rv, -ENODEV);
}

static void ppm_driver_before(void *fixture)
{
	RESET_FAKE(ppm_data_init);
	RESET_FAKE(ucsi_ppm_init_and_wait);
	RESET_FAKE(ucsi_ppm_get_next_connector_status);
	RESET_FAKE(ucsi_ppm_lpm_alert);
}

ZTEST_SUITE(ppm_driver, NULL, NULL, ppm_driver_before, NULL, NULL);
