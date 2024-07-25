/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ppm_common.h"

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <usbc/ppm.h>
#line 15 /* For coverage. Put after #includes and point to next line. */

LOG_MODULE_REGISTER(ppm_test, LOG_LEVEL_DBG);

#define PDC_NUM_PORTS 2
#define PDC_DEFAULT_CONNECTOR 1
#define PDC_DEFAULT_CONNECTOR_STATUS_CHANGE (1 << 14)
#define PDC_WAIT_FOR_ITERATIONS 3

#define CMD_WAIT_TIMEOUT K_MSEC(200)
#define CMD_QUEUE_SIZE 4

#define LPM_DATA_MAX 32
struct expected_command_t {
	uint32_t queue_header;

	uint8_t ucsi_command;
	int result;

	bool has_lpm_data;
	uint8_t lpm_data[LPM_DATA_MAX];
} __attribute__((aligned(4)));

struct ppm_test_fixture {
	struct ucsi_pd_driver *pd;
	struct ucsi_ppm_device *ppm_dev;

	union connector_status_t port_status[PDC_NUM_PORTS];

	int notified_count;

	/* Commands handling. */
	struct expected_command_t next_command_result;
	struct k_queue *cmd_queue;
	struct k_sem cmd_sem;

	/* Allocate fixed array of commands and use a free queue to avoid
	 * allocations.
	 */
	struct k_queue *free_cmd_queue;
	struct expected_command_t cmd_memory[CMD_QUEUE_SIZE];

	/* Notifications handling. */
	struct k_sem opm_sem;
};

static const struct ucsi_control_t enable_all_notifications = {
	.command = UCSI_SET_NOTIFICATION_ENABLE,
	.data_length = 0,
	.command_specific = { 0xff, 0xff, 0x1, 0x0, 0x0, 0x0 },
};

static void opm_notify_cb(void *ctx)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)ctx;

	fixture->notified_count++;
	LOG_DBG("OPM notify with count = %d", fixture->notified_count);
	k_sem_give(&fixture->opm_sem);
}

static enum ppm_states get_ppm_state(struct ppm_test_fixture *fixture)
{
	return ppm_test_get_state(fixture->ppm_dev);
}

static bool check_async_is_pending(struct ppm_test_fixture *fixture)
{
	return ppm_test_is_async_pending(fixture->ppm_dev);
}

static bool check_cmd_is_pending(struct ppm_test_fixture *fixture)
{
	return ppm_test_is_cmd_pending(fixture->ppm_dev);
}

static bool check_cci_matches(struct ppm_test_fixture *fixture,
			      const union cci_event_t *cci)
{
	union cci_event_t actual_cci;
	int rv = ucsi_ppm_read(fixture->ppm_dev, UCSI_CCI_OFFSET,
			       (void *)&actual_cci, sizeof(actual_cci));

	if (rv < 0) {
		return false;
	}

	return *((uint32_t *)cci) == *((uint32_t *)&actual_cci);
}

static void unblock_fake_driver_with_command(struct ppm_test_fixture *fixture,
					     uint8_t ucsi_command, int result,
					     uint8_t *lpm_data)
{
	fixture->next_command_result.ucsi_command = ucsi_command;
	fixture->next_command_result.result = result;
	fixture->next_command_result.has_lpm_data = lpm_data != NULL;
	if (lpm_data) {
		memcpy(fixture->next_command_result.lpm_data, lpm_data,
		       LPM_DATA_MAX);
	}

	k_sem_give(&fixture->cmd_sem);
	LOG_DBG("Signaled for command 0x%x", ucsi_command);
}

static void queue_command_for_fake_driver(struct ppm_test_fixture *fixture,
					  uint8_t ucsi_command, int result,
					  uint8_t *lpm_data)
{
	struct expected_command_t *cmd =
		k_queue_get(fixture->free_cmd_queue, K_NO_WAIT);
	zassert_true(cmd != NULL);
	if (!cmd) {
		return;
	}

	LOG_DBG("Queueing command result for 0x%x with result %d", ucsi_command,
		result);

	cmd->ucsi_command = ucsi_command;
	cmd->result = result;
	cmd->has_lpm_data = lpm_data != NULL;
	if (lpm_data) {
		memcpy(cmd->lpm_data, lpm_data, LPM_DATA_MAX);
	}

	k_queue_append(fixture->cmd_queue, cmd);
}

static void trigger_expected_connector_change(struct ppm_test_fixture *fixture,
					      uint8_t connector)
{
	uint8_t lpm_data[LPM_DATA_MAX];
	union connector_status_t *data = (union connector_status_t *)lpm_data;
	memset(lpm_data, 0, LPM_DATA_MAX);

	data->raw_conn_status_change_bits = PDC_DEFAULT_CONNECTOR_STATUS_CHANGE;

	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_STATUS,
				      /*result=*/sizeof(*data), lpm_data);
	ucsi_ppm_lpm_alert(fixture->ppm_dev, connector);
}

static int raw_ppm_read(struct ppm_test_fixture *fixture, uint32_t offset,
			void *buf, size_t length)
{
	return ucsi_ppm_read(fixture->ppm_dev, offset, buf, length);
}

static int raw_ppm_write(struct ppm_test_fixture *fixture, uint32_t offset,
			 const void *buf, size_t length)
{
	return ucsi_ppm_write(fixture->ppm_dev, offset, buf, length);
}

static int write_command(struct ppm_test_fixture *fixture,
			 struct ucsi_control_t *control)
{
	return ucsi_ppm_write(fixture->ppm_dev, UCSI_CONTROL_OFFSET,
			      (void *)control, sizeof(struct ucsi_control_t));
}

static int read_command_result(struct ppm_test_fixture *fixture, uint8_t *out,
			       size_t out_size)
{
	union cci_event_t cci;
	int rv = ucsi_ppm_read(fixture->ppm_dev, UCSI_CCI_OFFSET, (void *)&cci,
			       sizeof(cci));

	if (rv < 0) {
		return rv;
	}

	if (out_size < cci.data_len) {
		LOG_ERR("Data length in CCI (0x%x) greater than expected 0x%0x",
			cci.data_len);
		return -EINVAL;
	}

	return ucsi_ppm_read(fixture->ppm_dev, UCSI_MESSAGE_IN_OFFSET, out,
			     cci.data_len);
}

static int write_ack_command(struct ppm_test_fixture *fixture,
			     bool connector_change_ack,
			     bool command_complete_ack)
{
	struct ucsi_control_t control = { .command = UCSI_ACK_CC_CI,
					  .data_length = 0 };
	union ack_cc_ci_t ack_data = {
		.connector_change_ack = connector_change_ack,
		.command_complete_ack = command_complete_ack
	};
	memcpy(control.command_specific, &ack_data, sizeof(ack_data));
	return write_command(fixture, &control);
}

static int write_ppm_reset(struct ppm_test_fixture *fixture)
{
	struct ucsi_control_t control = { .command = UCSI_PPM_RESET,
					  .data_length = 0 };
	return write_command(fixture, &control);
}

static bool wait_for_async_event_to_process(struct ppm_test_fixture *fixture)
{
	bool is_async_pending = false;

	/*
	 * After an lpm alert is sent, the async event should process only if
	 * the state machine is in the right state. Try reading the pending
	 * state a few times to see if it clears.
	 */
	for (int i = 0; i < PDC_WAIT_FOR_ITERATIONS; ++i) {
		is_async_pending = check_async_is_pending(fixture);

		LOG_DBG("[%d]: Async is %s", i,
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
	 * main loop. Try reading the pending state a few times to see if it
	 * clears.
	 */
	for (int i = 0; i < PDC_WAIT_FOR_ITERATIONS; ++i) {
		is_cmd_pending = check_cmd_is_pending(fixture);

		LOG_DBG("[%d]: Command is %s", i,
			(is_cmd_pending ? "pending" : "not pending"));
		if (is_cmd_pending) {
			k_msleep(1);
		} else {
			break;
		}
	}

	return !is_cmd_pending;
}

static bool wait_for_notification(struct ppm_test_fixture *fixture,
				  int expected_count)
{
	if (expected_count <= fixture->notified_count)
		return true;

	while (fixture->notified_count < expected_count) {
		if (k_sem_take(&fixture->opm_sem, CMD_WAIT_TIMEOUT) < 0) {
			return false;
		}
	}

	return true;
}

static void enable_notifications_from_idle(struct ppm_test_fixture *fixture)
{
	struct ucsi_control_t control;
	memcpy(&control, &enable_all_notifications, sizeof(control));

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI,
				      /*result=*/0, /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack*/ false,
					/*command_complete_ack*/ true) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);
}

static bool initialize_fake(struct ppm_test_fixture *fixture)
{
	write_ppm_reset(fixture);
	return wait_for_cmd_to_process(fixture);
}

static void initialize_fake_to_idle_notify(struct ppm_test_fixture *fixture)
{
	zassert_true(initialize_fake(fixture));
	enable_notifications_from_idle(fixture);
}

/* Fake PD driver implementations. */

static int fake_pd_init_ppm(const struct device *device)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)device;

	int rv = ucsi_ppm_register_notify(fixture->ppm_dev, opm_notify_cb,
					  fixture);

	if (rv < 0) {
		return rv;
	}

	return ucsi_ppm_init_and_wait(fixture->ppm_dev);
}

static struct ucsi_ppm_device *fake_pd_get_ppm_dev(const struct device *device)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)device;

	return fixture->ppm_dev;
}

static int fake_pd_execute_cmd(const struct device *device,
			       struct ucsi_control_t *control,
			       uint8_t *lpm_data_out)
{
	struct ppm_test_fixture *fixture = (struct ppm_test_fixture *)device;
	uint8_t ucsi_command = control->command;
	int rv;

	LOG_DBG("Executing fake cmd for UCSI:0x%x", ucsi_command);

	/* Return any commands that were queued up to return. */
	if (!k_queue_is_empty(fixture->cmd_queue)) {
		struct expected_command_t *cmd =
			(struct expected_command_t *)k_queue_get(
				fixture->cmd_queue, K_NO_WAIT);

		if (cmd == NULL) {
			LOG_DBG("Command queue is unexpectedly empty!");
			return -ENOTSUP;
		}

		k_queue_append(fixture->free_cmd_queue, cmd);

		if (ucsi_command != cmd->ucsi_command) {
			LOG_DBG("Expected queued command 0x%x doesn't match actual 0x%x",
				cmd->ucsi_command, ucsi_command);
			return -ENOTSUP;
		}

		if (cmd->has_lpm_data && cmd->result > 0) {
			memcpy(lpm_data_out, cmd->lpm_data, cmd->result);
		}

		LOG_DBG("Returning queued result: %d", cmd->result);
		return cmd->result;
	}

	/* Since there were no commands queued up, wait for a signal to use a
	 * single next command.
	 */
	rv = k_sem_take(&fixture->cmd_sem, CMD_WAIT_TIMEOUT);

	if (rv != 0 ||
	    ucsi_command != fixture->next_command_result.ucsi_command) {
		LOG_DBG("Sem take result(%d). Expected command %x vs actual %x",
			fixture->next_command_result.ucsi_command,
			ucsi_command);
		return -ENOTSUP;
	}

	rv = fixture->next_command_result.result;

	if (fixture->next_command_result.has_lpm_data && rv > 0) {
		memcpy(lpm_data_out, fixture->next_command_result.lpm_data, rv);
	}

	LOG_DBG("Returning specific result: %d", rv);
	return rv;
}

static int fake_pd_get_active_port_count(const struct device *dev)
{
	return PDC_NUM_PORTS;
}

/* Globals for the tests. */

static struct ppm_test_fixture test_fixture;
K_QUEUE_DEFINE(cmd_queue);
K_QUEUE_DEFINE(free_cmd_queue);

/* Fake PD driver used for emulating peer PDC. */
static struct ucsi_pd_driver fake_pd_driver = {
	.init_ppm = fake_pd_init_ppm,
	.get_ppm_dev = fake_pd_get_ppm_dev,
	.execute_cmd = fake_pd_execute_cmd,
	.get_active_port_count = fake_pd_get_active_port_count,
};

static void *ppm_test_setup(void)
{
	test_fixture.pd = &fake_pd_driver;

	test_fixture.cmd_queue = &cmd_queue;
	test_fixture.free_cmd_queue = &free_cmd_queue;

	for (int i = 0; i < CMD_QUEUE_SIZE; ++i) {
		k_queue_append(test_fixture.free_cmd_queue,
			       &test_fixture.cmd_memory[i]);
	}

	k_sem_init(&test_fixture.cmd_sem, 0, 1);
	k_sem_init(&test_fixture.opm_sem, 0, 1);

	queue_command_for_fake_driver(&test_fixture, UCSI_PPM_RESET,
				      /*result=*/0,
				      /*lpm_data=*/NULL);

	/* Open ppm_common implementation with fake driver for testing. */
	test_fixture.ppm_dev = ppm_data_init(
		test_fixture.pd, (const struct device *)&test_fixture,
		test_fixture.port_status, PDC_NUM_PORTS);

	test_fixture.pd->init_ppm((const struct device *)&test_fixture);

	return &test_fixture;
}

static void ppm_test_before(void *f)
{
	/* Clear state. */
	test_fixture.notified_count = 0;

	/* Clear command queue. */
	struct expected_command_t *cmd;
	while ((cmd = k_queue_get(test_fixture.cmd_queue, K_NO_WAIT))) {
		k_queue_append(test_fixture.free_cmd_queue, cmd);
	}

	/* Reset semaphores. */
	k_sem_reset(&test_fixture.cmd_sem);
	k_sem_reset(&test_fixture.opm_sem);
}

const union cci_event_t cci_cmd_complete = { .command_completed = 1 };
const union cci_event_t cci_busy = { .busy = 1 };
const union cci_event_t cci_error = { .error = 1, .command_completed = 1 };
const union cci_event_t cci_ack_command = { .acknowledge_command = 1 };
const union cci_event_t cci_connector_change_1 = { .connector_change = 1 };

ZTEST_SUITE(ppm_test, /*predicate=*/NULL, ppm_test_setup, ppm_test_before,
	    /*after=*/NULL, /*teardown=*/NULL);

/* On init, PPM should go into the Idle State. */
ZTEST_USER_F(ppm_test, test_initialize_to_idle)
{
	zassert_true(initialize_fake(fixture));

	/* System should be in the idle state at the end of init. */
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}

/* From the IDLE state, only PPM_RESET and SET_NOTIFICATION_ENABLE is allowed.
 */
ZTEST_USER_F(ppm_test, test_IDLE_drops_unexpected_commands)
{
	zassert_true(initialize_fake(fixture));

	/* Try all commands except PPM_RESET and SET_NOTIFICATION_ENABLE.
	 * They should result in no change to the state.
	 */
	for (uint8_t cmd = UCSI_PPM_RESET; cmd <= UCSI_CMD_MAX; cmd++) {
		if (cmd == UCSI_PPM_RESET ||
		    cmd == UCSI_SET_NOTIFICATION_ENABLE) {
			continue;
		}

		struct ucsi_control_t control = { .command = cmd,
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
	struct ucsi_control_t control;
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
	zassert_true(initialize_fake(fixture));
	fixture->notified_count = 0;

	/* Send an alert on default connector. */
	ucsi_ppm_lpm_alert(fixture->ppm_dev, PDC_DEFAULT_CONNECTOR);

	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_equal(fixture->notified_count, 0);
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}

/* From the Idle Notify, complete a full command loop:
 *   - Send command, CCI notifies busy
 *   - Command complete, CCI notifies command complete.
 *   - Send ACK_CC_CI, CCI notifies busy
 *   - Command complete, CCI notifies ack command complete.
 */
ZTEST_USER_F(ppm_test, test_IDLENOTIFY_full_command_loop)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = 0;
	fixture->notified_count = 0;

	/* Emulate a UCSI write from the OPM, and wait for a notification with
	 * CCI.busy=1
	 */
	struct ucsi_control_t control = { .command = UCSI_GET_ALTERNATE_MODES,
					  .data_length = 0 };
	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_busy));

	/* Send a fake response from the PD driver, and expect a notification to
	 * the OPM with CCI.cmd_complete=1.
	 */
	unblock_fake_driver_with_command(fixture, UCSI_GET_ALTERNATE_MODES,
					 /*result=*/0, /*lpm_data=*/NULL);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_cmd_complete));

	/* OPM acknowledges the PPM's cmd_complete. */
	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI,
				      /*result=*/0,
				      /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack*/ false,
					/*command_complete_ack*/ true) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_ack_command));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);
}

/* When processing an async event, PPM will figure out which port changed and
 * then send the connector change event for that port.
 */
ZTEST_USER_F(ppm_test,
	     test_IDLENOTIFY_process_async_event_and_send_connector_change)
{
	initialize_fake_to_idle_notify(fixture);

	int notified_count = 0;
	fixture->notified_count = 0;

	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));

	union cci_event_t cci = { .connector_change = PDC_DEFAULT_CONNECTOR };
	zassert_true(check_cci_matches(fixture, &cci));
}

/* Send invalid UCSI command and expect an error. */
ZTEST_USER_F(ppm_test, test_IDLENOTIFY_send_invalid_ucsi_command)
{
	initialize_fake_to_idle_notify(fixture);

	struct ucsi_control_t control = { .command = UCSI_CMD_MAX,
					  .data_length = 0 };

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_true(check_cci_matches(fixture, &cci_error));

	/* Ack the error before getting the error status. */
	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI,
				      /*result=*/0, /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture, /*connector_change_ack=*/false,
					/*command_complete_ack=*/true) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_true(check_cci_matches(fixture, &cci_ack_command));

	int notified_count = fixture->notified_count;
	control.command = UCSI_GET_ERROR_STATUS;
	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));

	union error_status_t data;
	union cci_event_t complete_with_size = cci_cmd_complete;
	complete_with_size.data_len = sizeof(data);

	zassert_true(check_cci_matches(fixture, &complete_with_size));
	zassert_false(read_command_result(fixture, (uint8_t *)&data,
					  sizeof(data)) < 0);
	zassert_true(data.unrecognized_command);
}

/*
 * While in the processing command state, the PPM is busy and should reject any
 * new commands that are sent.
 */
ZTEST_EXPECT_SKIP(ppm_test, test_PROCESSING_busy_rejects_commands);
ZTEST_USER_F(ppm_test, test_PROCESSING_busy_rejects_commands)
{
	/* TODO(b/340895744) - Not yet implemented. */
	ztest_test_skip();
}

/*
 * While in the processing command state, we still allow the cancel command to
 * be sent WHILE a command is in progress. If a command is cancellable, it will
 * replace the current command.
 */
ZTEST_EXPECT_SKIP(ppm_test, test_PROCESSING_busy_allows_cancel_command);
ZTEST_USER_F(ppm_test, test_PROCESSING_busy_allows_cancel_command)
{
	/* TODO(b/340895744) - Cancel is not yet implemented. */
	ztest_test_skip();
}

/*
 * When waiting for command complete, any command that's not ACK_CC_CI should
 * get rejected.
 */
ZTEST_USER_F(ppm_test, test_CCACK_error_if_not_command_complete)
{
	zassert_true(initialize_fake(fixture));

	int notified_count = 0;
	fixture->notified_count = 0;

	struct ucsi_control_t control;
	memcpy(&control, &enable_all_notifications, sizeof(control));

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	/* one notification each for busy and command complete. */
	notified_count += 2;
	zassert_equal(notified_count, fixture->notified_count);

	/* Resend the previous command instead of a CC Ack. */
	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);
}

/*
 * The PPM state machine allows you to both ACK Command Complete AND
 * ACK Connector Indication. Make sure this is supported in the command loop
 * path.
 */
ZTEST_USER_F(ppm_test, test_CCACK_support_simultaneous_ack_CC_and_CI)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));

	notified_count = 0;
	fixture->notified_count = 0;

	/* PPM is waiting for a connector_change_ack from the OPM now. Don't
	 * send it, instead send a new command.
	 */
	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_CAPABILITY,
				      /*result=*/0, /*lpm_data=*/NULL);
	zassert_false(write_command(fixture, &control) < 0);
	/* Wait for both busy + complete. */
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_cmd_complete));

	uint8_t changed_port_num;
	union connector_status_t *status;

	zassert_true(ucsi_ppm_get_next_connector_status(
		fixture->ppm_dev, &changed_port_num, &status));
	zassert_equal(changed_port_num, PDC_DEFAULT_CONNECTOR);
	zassert_true(status != NULL);
	zassert_equal(status->raw_conn_status_change_bits,
		      PDC_DEFAULT_CONNECTOR_STATUS_CHANGE);

	/* PPM is waiting for connector_change_ack and command_complete_ack.
	 * Send them together.
	 */
	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI, /*result=*/0,
				      /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack=*/true,
					/*command_complete_ack=*/true) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));

	zassert_true(check_cci_matches(fixture, &cci_ack_command));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);
	zassert_false(ucsi_ppm_get_next_connector_status(
		fixture->ppm_dev, &changed_port_num, &status));
}

/*
 * If an async event is seen while a command is processing and waiting for an
 * ack, ignore it until the current command loop finishes.
 */
ZTEST_USER_F(ppm_test, test_CCACK_ignore_async_event_processing)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	/* Make sure to set notification with all UCSI bits set. */
	struct ucsi_control_t control;
	memcpy(&control, &enable_all_notifications, sizeof(control));

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);
	/* Wait for both busy + complete. */
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));

	/* The next expected command is ACK_CC_CI. Do this before triggering the
	 * lpm alert.
	 */
	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI, /*result=*/0,
				      /*lpm_data=*/NULL);

	/* Send LPM alert which should queue an async event for processing.
	 * No notification goes out for this and async event remains
	 * unprocessed.
	 */
	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_false(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, notified_count));

	/* OPM acknowledges the PPM's cmd_complete. */
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack=*/false,
					/*command_complete_ack=*/true) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));

	/* After handling the command loop, we will see the pending command and
	 * go into the WAITING_ASYNC_EV_ACK state.
	 */
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_ASYNC_EV_ACK);
}

/*
 * TODO(UCSI WG): Clarify PPM behavior when incorrect ACK is received. Current
 * implementation returns a PPM error, but does not change PPM state.
 * |test_CCACK_fail_if_send_ci_ack| and |test_CCACK_fail_if_no_ack| validate
 * this behavior.
 */

/*
 * When waiting for a Command Complete Ack, send a Connector Change Ack instead.
 */
ZTEST_USER_F(ppm_test, test_CCACK_fail_if_send_ci_ack)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	// Send a command and reach PPM_STATE_WAITING_CC_ACK
	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_CAPABILITY,
				      /*result=*/0,
				      /*lpm_data=*/NULL);
	zassert_false(write_command(fixture, &control) < 0);
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_cmd_complete));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	/* Send an unexpected connector change ack and expect an error and no
	 * state change.
	 */
	zassert_false(write_ack_command(fixture, /*connector_change_ack=*/true,
					/*command_complete_ack=*/false) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);
}

/* When waiting for a Command Complete Ack, send an Ack without setting either
 * Command Complete Ack or Connector Change Ack.
 */
ZTEST_USER_F(ppm_test, test_CCACK_fail_if_no_ack)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	// Send a command and reach PPM_STATE_WAITING_CC_ACK
	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_CAPABILITY,
				      /*result=*/0,
				      /*lpm_data=*/NULL);

	zassert_false(write_command(fixture, &control) < 0);
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_cmd_complete));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	/* Send an invalid ack and expect an error and no state change. */
	zassert_false(write_ack_command(fixture, /*connector_change_ack=*/false,
					/*command_complete_ack=*/false) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);
}

/* When waiting for a Connection Indicator Ack, we accept an immediate ACK_CC_CI
 * to switch the state back to Idle with Notifications. Trying to use command
 * complete in that state should also fail.
 */
ZTEST_USER_F(ppm_test, test_CIACK_ack_immediately)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_connector_change_1));

	notified_count = 0;
	fixture->notified_count = 0;

	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI, /*result=*/0,
				      /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack=*/true,
					/*command_complete_ack=*/false) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_ack_command));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);

	/* Re-trigger a connector change. */
	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_connector_change_1));

	/* Trying to do command complete in ASYNC_EV_ACK stage should fail. */
	zassert_false(write_ack_command(fixture,
					/*connector_change_ack=*/false,
					/*command_complete_ack=*/true) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
}

/* If we get an ACK_CC_CI when there is no active connector indication, we
 * should fail. In this scenario, the starting state needs to be IdleNotify but
 * occurs when the OPM sends other commands after receiving Connector Change
 * Indication.
 */
ZTEST_USER_F(ppm_test, test_CIACK_fail_if_no_active_connector_indication)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	zassert_false(write_ack_command(fixture,
					/*connector_change_ack=*/true,
					/*command_complete_ack=*/false) < 0);
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);
}

/*
 * When an LPM command fails, check that the appropriate CCI bits are set, and
 * that the next command must be ACK_CC_CI.
 */
ZTEST_USER_F(ppm_test, test_lpm_error_requires_ack)
{
	initialize_fake_to_idle_notify(fixture);
	int notified_count = fixture->notified_count;

	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};

	/* Return an error from the LPM and expect a CCI error. */
	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_CAPABILITY,
				      /*result=*/-EBUSY, /*lpm_data=*/NULL);
	zassert_false(write_command(fixture, &control) < 0);
	notified_count += 2;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	/* Test acceptance of new message. This should fail because it's not
	 * ACK_CC_CI.
	 */
	zassert_false(write_command(fixture, &control) < 0);
	notified_count += 1;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_error));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	/* ACK_CC_CI should now put this back into normal state. */
	queue_command_for_fake_driver(fixture, UCSI_ACK_CC_CI,
				      /*result=*/0, /*lpm_data=*/NULL);
	zassert_false(write_ack_command(fixture, /*connector_change_ack=*/false,
					/*command_complete_ack=*/true) < 0);
	notified_count += 1;
	zassert_true(wait_for_notification(fixture, notified_count));
	zassert_true(check_cci_matches(fixture, &cci_ack_command));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE_NOTIFY);
}

/* Make sure we can call PPM_RESET in all states. We already test the IDLE state
 * but we should also test IDLE_NOTIFY, WAITING_CC_ACK and WAITING_ASYNC_EV_ACK.
 */
ZTEST_USER_F(ppm_test, test_ppm_reset_works_in_all_states)
{
	/* Test at IDLE_NOTIFY. */
	initialize_fake_to_idle_notify(fixture);
	zassert_false(write_ppm_reset(fixture) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);

	/* Test at WAITING_CC_ACK. */
	enable_notifications_from_idle(fixture);
	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	queue_command_for_fake_driver(fixture, UCSI_GET_CONNECTOR_CAPABILITY,
				      /*result=*/0,
				      /*lpm_data=*/NULL);

	zassert_false(write_command(fixture, &control) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_CC_ACK);

	zassert_false(write_ppm_reset(fixture) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);

	/* Test at WAITING_ASYNC_EV_ACK. */
	enable_notifications_from_idle(fixture);
	int notified_count = fixture->notified_count;

	trigger_expected_connector_change(fixture, PDC_DEFAULT_CONNECTOR);
	zassert_true(wait_for_async_event_to_process(fixture));
	zassert_true(wait_for_notification(fixture, ++notified_count));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_WAITING_ASYNC_EV_ACK);

	zassert_false(write_ppm_reset(fixture) < 0);
	zassert_true(wait_for_cmd_to_process(fixture));
	zassert_equal(get_ppm_state(fixture), PPM_STATE_IDLE);
}

/* Check that read and write do proper bounds checking. */
ZTEST_USER_F(ppm_test, test_invalid_read_writes)
{
	size_t ucsi_region_size = sizeof(struct ucsi_memory_region);
	const char buf[32];

	initialize_fake_to_idle_notify(fixture);

	/* Read outside bounds of UCSI memory region should fail. */
	zassert_equal(raw_ppm_read(fixture, /*offset=*/0, /*buf=*/NULL,
				   /*length=*/ucsi_region_size + 1),
		      -EINVAL);
	zassert_equal(raw_ppm_read(fixture, /*offset=*/ucsi_region_size,
				   /*buf=*/NULL,
				   /*length=*/1),
		      -EINVAL);

	/* Buf and length must be specified for writes. */
	zassert_equal(raw_ppm_write(fixture, /*offset=*/0, /*buf=*/NULL,
				    /*length=*/4),
		      -EINVAL);
	zassert_equal(raw_ppm_write(fixture, /*offset=*/0, buf, /*length=*/0),
		      -EINVAL);

	/*
	 * OPM can only write to CONTROL and anywhere in MESSAGE_OUT region.
	 * Fail everything else.
	 */
	zassert_equal(raw_ppm_write(fixture, UCSI_CONTROL_OFFSET - 1, buf,
				    sizeof(struct ucsi_control_t)),
		      -EINVAL);
	zassert_equal(raw_ppm_write(fixture, UCSI_CONTROL_OFFSET + 4, buf,
				    sizeof(struct ucsi_control_t)),
		      -EINVAL);
	zassert_equal(raw_ppm_write(fixture, UCSI_MESSAGE_OUT_OFFSET - 1, buf,
				    sizeof(struct ucsi_control_t)),
		      -EINVAL);
	zassert_equal(
		raw_ppm_write(fixture, UCSI_MESSAGE_OUT_OFFSET + 1, buf,
			      sizeof(struct ucsi_control_t) + MESSAGE_OUT_SIZE),
		-EINVAL);

	/* Invalid writes to control should fail. */
	zassert_equal(raw_ppm_write(fixture, UCSI_CONTROL_OFFSET, buf,
				    sizeof(struct ucsi_control_t) + 1),
		      -EINVAL);

	/* Writing while busy should result in failure. */
	struct ucsi_control_t control = {
		.command = UCSI_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	/* First write succeeds and second one responds with -EBUSY. */
	zassert_false(write_command(fixture, &control) < 0);
	zassert_equal(write_command(fixture, &control), -EBUSY);
}
