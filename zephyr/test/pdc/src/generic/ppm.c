/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppm_common.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <include/pd_driver.h>
#include <include/ppm.h>

#define PDC_NUM_PORTS 2

struct ppm_test_fixture {
	struct ucsi_pd_driver *pd;
	struct ucsi_ppm_driver *ppm;

	struct ucsiv3_get_connector_status_data port_status[PDC_NUM_PORTS];
};

static void opm_notify_cb(void *ctx)
{
	/* TODO(b/340895744) - Signal to test that there's a notification */
}

static int initialize_fake(struct ppm_test_fixture *fixture)
{
	return fixture->pd->init_ppm((const struct device *)fixture);
}

static struct ppm_common_device *get_ppm_data(struct ppm_test_fixture *fixture)
{
	return (struct ppm_common_device *)fixture->ppm->dev;
}

/* TODO(b/339702957) - Move into ppm_common.h */
static enum ppm_states get_ppm_state(struct ppm_test_fixture *fixture)
{
	return get_ppm_data(fixture)->ppm_state;
}

/* Fake PD driver implementations. */

static int fake_pd_init_ppm(const struct device *device)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)device;

	int rv = fixture->ppm->register_notify(fixture->ppm->dev, opm_notify_cb,
					       fixture);

	if (rv < 0) {
		return rv;
	}

	return fixture->ppm->init_and_wait(fixture->ppm->dev, PDC_NUM_PORTS);
}

static struct ucsi_ppm_driver *fake_pd_get_ppm(const struct device *device)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)device;

	return fixture->ppm;
}

static int fake_pd_execute_cmd(const struct device *device,
			       struct ucsi_control *control,
			       uint8_t *lpm_data_out)
{
	return 0;
}

static int fake_pd_get_active_port_count(const struct device *dev)
{
	return PDC_NUM_PORTS;
}

/* Globals for the tests. */

static struct ppm_test_fixture test_fixture;

/* Fake PD driver used for emulating peer PDC. */
static struct ucsi_pd_driver fake_pd_driver = {
	.init_ppm = fake_pd_init_ppm,
	.get_ppm = fake_pd_get_ppm,
	.execute_cmd = fake_pd_execute_cmd,
	.get_active_port_count = fake_pd_get_active_port_count,
};

static void *ppm_test_setup(void)
{
	/* Open ppm_common implementation with fake driver for testing. */
	test_fixture.pd = &fake_pd_driver;
	test_fixture.ppm = ppm_open(test_fixture.pd, test_fixture.port_status,
				    (const struct device *)&test_fixture);

	return &test_fixture;
}

ZTEST_SUITE(ppm_test, /*predicate=*/NULL, ppm_test_setup,
	    /*before=*/NULL,
	    /*after=*/NULL, /*teardown=*/NULL);

/* On init, PPM should go into the Idle State. */
ZTEST_USER_F(ppm_test, test_initialize_to_idle)
{
	zassert_equal(initialize_fake(fixture), 0);

	/* System should be in the idle state at the end of init. */
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}
