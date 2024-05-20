/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppm_common.h"

#include <zephyr/device.h>
#include <zephyr/ztest.h>

#include <include/pd_driver.h>
#include <include/platform.h>
#include <include/ppm.h>

#define PDC_NUM_PORTS 2
#define PDC_DEFAULT_CONNECTOR 1

#define PDC_WAIT_FOR_ITERATIONS 3

struct ppm_test_fixture {
	struct ucsi_pd_driver *pd;
	struct ucsi_ppm_driver *ppm;

	struct ucsiv3_get_connector_status_data port_status[PDC_NUM_PORTS];

	int notified_count;
};

static const struct ucsi_control enable_all_notifications = {
	.command = UCSI_CMD_SET_NOTIFICATION_ENABLE,
	.data_length = 0,
	.command_specific = { 0xff, 0xff, 0x1, 0x0, 0x0, 0x0 },
};

static void opm_notify_cb(void *ctx)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)ctx;

	fixture->notified_count++;
	/* TODO(b/340895744) - Signal to test that there's a notification */
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

static int initialize_fake(struct ppm_test_fixture *fixture)
{
	return fixture->pd->init_ppm((const struct device *)fixture);
}

static int write_command(struct ppm_test_fixture *fixture,
			 struct ucsi_control *control)
{
	return fixture->ppm->write(fixture->ppm->dev, UCSI_CONTROL_OFFSET,
				   (void *)control,
				   sizeof(struct ucsi_control));
}

static bool wait_for_async_event_to_process(struct ppm_test_fixture *fixture)
{
	bool is_async_pending = false;

	/*
	 * After an lpm alert is sent, the async event should process only if
	 * the state machine is in the right state. We check the pending state
	 * behind the lock and then sleep to allow the loop to run.
	 */
	for (int i = 0; i < PDC_WAIT_FOR_ITERATIONS; ++i) {
		platform_mutex_lock(get_ppm_data(fixture)->ppm_lock);
		is_async_pending = get_ppm_data(fixture)->pending.async_event;
		platform_mutex_unlock(get_ppm_data(fixture)->ppm_lock);

		DLOG("[%d]: Async is %s", i,
		     (is_async_pending ? "pending" : "not pending"));
		if (is_async_pending) {
			k_msleep(1);
		} else {
			break;
		}
	}

	return !is_async_pending;
}

static bool wait_for_cmd_to_process(struct ppm_test_fixture *fixture)
{
	bool is_cmd_pending = false;

	/*
	 * After calling write, the command will be pending and will trigger the
	 * main loop. We sleep after grabbing mutex + checking the pending to
	 * give the main loop time to run.
	 */
	for (int i = 0; i < PDC_WAIT_FOR_ITERATIONS; ++i) {
		platform_mutex_lock(get_ppm_data(fixture)->ppm_lock);
		is_cmd_pending = get_ppm_data(fixture)->pending.command;
		platform_mutex_unlock(get_ppm_data(fixture)->ppm_lock);

		DLOG("[%d]: Command is %s", i,
		     (is_cmd_pending ? "pending" : "not pending"));
		if (is_cmd_pending) {
			k_msleep(1);
		} else {
			break;
		}
	}

	return !is_cmd_pending;
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
	platform_set_debug(true);

	test_fixture.pd = &fake_pd_driver;
	return &test_fixture;
}

static void ppm_test_before(void *f)
{
	/* Open ppm_common implementation with fake driver for testing. */
	test_fixture.ppm = ppm_open(test_fixture.pd, test_fixture.port_status,
				    (const struct device *)&test_fixture);
}

static void ppm_test_after(void *f)
{
	/* Must clean up between tests to re-init the state machine. */
	test_fixture.ppm->cleanup(test_fixture.ppm);
}

ZTEST_SUITE(ppm_test, /*predicate=*/NULL, ppm_test_setup, ppm_test_before,
	    ppm_test_after, /*teardown=*/NULL);

/* On init, PPM should go into the Idle State. */
ZTEST_USER_F(ppm_test, test_initialize_to_idle)
{
	zassert_equal(initialize_fake(fixture), 0);

	/* System should be in the idle state at the end of init. */
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}

/* From the IDLE state, only PPM_RESET and SET_NOTIFICATION_ENABLE is allowed.
 */
ZTEST_USER_F(ppm_test, test_IDLE_drops_unexpected_commands)
{
	zassert_equal(initialize_fake(fixture), 0);

	/* Try all commands except PPM_RESET and SET_NOTIFICATION_ENABLE.
	 * They should result in no change to the state.
	 */
	for (uint8_t cmd = UCSI_CMD_PPM_RESET; cmd <= UCSI_CMD_MAX; cmd++) {
		if (cmd == UCSI_CMD_PPM_RESET ||
		    cmd == UCSI_CMD_SET_NOTIFICATION_ENABLE) {
			continue;
		}

		struct ucsi_control control = { .command = cmd,
						.data_length = 0 };

		/* Make sure Write completed and then wait for pending command
		 * to be cleared. Only the .command part will really matter as
		 * that's how we determine whether the next command should be
		 * executed.
		 */
		zassert_false(write_command(fixture, &control) < 0,
			      "Failed to write command: 0x%x", cmd);
		zassert_true(wait_for_cmd_to_process(fixture),
			     "Failed waiting for cmd to process: 0x%x", cmd);
		zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE,
			      "Not in idle state after running cmd: 0x%x", cmd);
	}

	/* SET_NOTIFICATION_ENABLE should then switch it to a non-idle state. */
	struct ucsi_control control;
	memcpy(&control, &enable_all_notifications, sizeof(control));

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);
}

/* From the Idle state, we process async events but we do not notify the OPM or
 * change the PPM state (i.e. silently drop).
 */
ZTEST_USER_F(ppm_test, test_IDLE_silently_processes_async_event)
{
	zassert_equal(initialize_fake(fixture), 0);
	fixture->notified_count = 0;

	/* Send an alert on default connector. */
	fixture->ppm->lpm_alert(fixture->ppm->dev, PDC_DEFAULT_CONNECTOR);

	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_equal(fixture->notified_count, 0);
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}
