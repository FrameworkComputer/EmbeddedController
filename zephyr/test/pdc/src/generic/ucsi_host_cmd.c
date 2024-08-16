/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <usbc/ppm.h>

struct ucsi_ppm_device {
	void *ptr;
};

int eppm_init(void);
void emul_ppm_driver_set_ucsi_ppm_device(struct ucsi_ppm_device *ppm_device);
void emul_ppm_driver_set_init_ppm_retval(int rv);

FAKE_VALUE_FUNC(int, ucsi_ppm_write, struct ucsi_ppm_device *, unsigned int,
		const void *, size_t);

FAKE_VALUE_FUNC(int, ucsi_ppm_read, struct ucsi_ppm_device *, unsigned int,
		void *, size_t);

FAKE_VALUE_FUNC(int, ucsi_ppm_register_notify, struct ucsi_ppm_device *,
		ucsi_ppm_notify_cb *, void *);

ZTEST_USER(ucsi_host_cmd, test_eppm_init_enodev)
{
	int rv;

	emul_ppm_driver_set_init_ppm_retval(1);

	rv = eppm_init();
	zassert_equal(rv, -ENODEV);
}

ZTEST_USER(ucsi_host_cmd, test_get_error)
{
	struct ec_params_ucsi_ppm_get params = {
		.offset = 1,
		.size = 1,
	};
	enum ec_status rv;
	struct ucsi_ppm_device fake_ppm_device;

	emul_ppm_driver_set_ucsi_ppm_device(&fake_ppm_device);
	eppm_init();

	ucsi_ppm_read_fake.return_val = -1;
	rv = ec_cmd_ucsi_ppm_get(NULL, &params);
	zassert_equal(rv, EC_RES_ERROR);
	zassert_equal(ucsi_ppm_read_fake.call_count, 1);
	zassert_equal(ucsi_ppm_read_fake.arg0_val, &fake_ppm_device);
	zassert_equal(ucsi_ppm_read_fake.arg1_val, 1);
}

ZTEST_USER(ucsi_host_cmd, test_get_success)
{
	struct ec_params_ucsi_ppm_get params = {
		.offset = 1,
		.size = 1,
	};
	enum ec_status rv;
	struct ucsi_ppm_device fake_ppm_device;

	emul_ppm_driver_set_ucsi_ppm_device(&fake_ppm_device);
	eppm_init();

	ucsi_ppm_read_fake.return_val = 0;
	rv = ec_cmd_ucsi_ppm_get(NULL, &params);
	zassert_equal(rv, EC_RES_SUCCESS);
	zassert_equal(ucsi_ppm_read_fake.call_count, 1);
	zassert_equal(ucsi_ppm_read_fake.arg0_val, &fake_ppm_device);
	zassert_equal(ucsi_ppm_read_fake.arg1_val, 1);
}

ZTEST_USER(ucsi_host_cmd, test_get_unavailable)
{
	struct ec_params_ucsi_ppm_get params = {
		.offset = 1,
		.size = 1,
	};
	enum ec_status rv;

	rv = ec_cmd_ucsi_ppm_get(NULL, &params);
	zassert_equal(rv, EC_RES_UNAVAILABLE);
	zassert_equal(ucsi_ppm_read_fake.call_count, 0);
}

ZTEST_USER(ucsi_host_cmd, test_set_error)
{
	struct ec_params_ucsi_ppm_set params = {
		.offset = 1,
	};
	enum ec_status rv;
	struct ucsi_ppm_device fake_ppm_device;

	emul_ppm_driver_set_ucsi_ppm_device(&fake_ppm_device);
	eppm_init();

	ucsi_ppm_write_fake.return_val = 1;
	rv = ec_cmd_ucsi_ppm_set(NULL, &params);
	zassert_equal(rv, EC_RES_ERROR);
	zassert_equal(ucsi_ppm_write_fake.call_count, 1);
	zassert_equal(ucsi_ppm_write_fake.arg0_val, &fake_ppm_device);
	zassert_equal(ucsi_ppm_write_fake.arg1_val, 1);
	zassert_equal(ucsi_ppm_write_fake.arg2_val, params.data);
}

ZTEST_USER(ucsi_host_cmd, test_set_success)
{
	struct ec_params_ucsi_ppm_set params = {
		.offset = 1,
	};
	enum ec_status rv;
	struct ucsi_ppm_device fake_ppm_device;

	emul_ppm_driver_set_ucsi_ppm_device(&fake_ppm_device);
	eppm_init();

	ucsi_ppm_write_fake.return_val = 0;
	rv = ec_cmd_ucsi_ppm_set(NULL, &params);
	zassert_equal(rv, EC_RES_SUCCESS);
	zassert_equal(ucsi_ppm_write_fake.call_count, 1);
	zassert_equal(ucsi_ppm_write_fake.arg0_val, &fake_ppm_device);
	zassert_equal(ucsi_ppm_write_fake.arg1_val, 1);
	zassert_equal(ucsi_ppm_write_fake.arg2_val, params.data);
}

ZTEST_USER(ucsi_host_cmd, test_set_unavailable)
{
	struct ec_params_ucsi_ppm_set params = {
		.offset = 0,
	};
	enum ec_status rv;

	rv = ec_cmd_ucsi_ppm_set(NULL, &params);
	zassert_equal(rv, EC_RES_UNAVAILABLE);
	zassert_equal(ucsi_ppm_write_fake.call_count, 0);
}

static void ucsi_host_cmd_before(void *fixture)
{
	RESET_FAKE(ucsi_ppm_write);
	RESET_FAKE(ucsi_ppm_read);
	RESET_FAKE(ucsi_ppm_register_notify);
	emul_ppm_driver_set_init_ppm_retval(0);
	emul_ppm_driver_set_ucsi_ppm_device(NULL);
	eppm_init();
}

ZTEST_SUITE(ucsi_host_cmd, NULL, NULL, ucsi_host_cmd_before, NULL, NULL);
