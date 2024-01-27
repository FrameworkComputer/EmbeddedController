/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include "include/pd_driver.h"
#include "include/platform.h"
#include "include/ppm.h"
#include "ppm_common.h"
}

// Forward declarations.
class PpmTest;
namespace
{
struct ucsi_pd_driver *open_test_pd(PpmTest *ppm_test);
void ucsi_opm_notify(void *context);

// Allocations made using platform_{malloc|calloc} need to use platform_free to
// delete. Use this as a custom deleter for std::unique_ptr for those structs.
template <typename T> struct FreeDeleter {
	void operator()(T *obj)
	{
		platform_free(static_cast<void *>(obj));
	}
};

using PpmDriverUniquePtr =
	std::unique_ptr<struct ucsi_ppm_driver,
			FreeDeleter<struct ucsi_ppm_driver> >;
using PdDriverUniquePtr = std::unique_ptr<struct ucsi_pd_driver,
					  FreeDeleter<struct ucsi_pd_driver> >;

const struct ucsi_cci kCciCmdComplete = { .cmd_complete = 1 };
const struct ucsi_cci kCciBusy = { .busy = 1 };
const struct ucsi_cci kCciError = { .error = 1, .cmd_complete = 1 };
const struct ucsi_cci kCciAckCommand = { .ack_command = 1 };

const uint8_t kDefaultAlertPort = 1;
} // namespace

// Test fixture class for the PPM state machine.
// This attempts to validate the overall PPM state machine described in the UCSI
// spec and implemented in |ppm_common|.
class PpmTest : public testing::Test {
    public:
	PpmTest()
	{
		pd_ = PdDriverUniquePtr(open_test_pd(this));
		ppm_ = PpmDriverUniquePtr(ppm_open(pd_.get()));
		platform_set_debug(false);
	}

	// --- START (struct ucsi_pd_driver) ---

	int init_ppm()
	{
		if (ppm_->register_notify(ppm_->dev, ucsi_opm_notify, this) ==
		    -1) {
			return -1;
		}

		return ppm_->init_and_wait(ppm_->dev, num_ports_);
	}

	struct ucsi_ppm_driver *get_ppm()
	{
		return ppm_.get();
	}

	int execute_cmd(struct ucsi_control *control, uint8_t *lpm_data_out)
	{
		uint8_t ucsi_command = control->command;

		// Either immediately return with the queued expected commands
		// OR fall back to blocking using the |cmd_notifier_| condition
		// variable. The latter requires notification via the test.
		if (!expected_commands_queue_.empty()) {
			// Make sure this is the expected command or return an
			// error.
			auto [expected_cmd, ret, lpm_data] =
				expected_commands_queue_.front();
			EXPECT_EQ(ucsi_command, expected_cmd);
			if (ucsi_command != expected_cmd) {
				return -1;
			}
			if (lpm_data.has_value()) {
				memcpy(lpm_data_out, lpm_data.value().data(),
				       lpm_data.value().size());
			}
			expected_commands_queue_.pop_front();
			return ret;
		}

		std::unique_lock<std::mutex> lock(cmd_lock_);
		auto status = cmd_notifier_.wait_for(
			lock, std::chrono::milliseconds(cmd_wait_timeout_ms_));
		EXPECT_NE(status, std::cv_status::timeout);
		auto [expected_cmd, ret, lpm_data] =
			cmd_notifier_expected_cmd_result_;
		EXPECT_EQ(expected_cmd, ucsi_command);

		if (status == std::cv_status::timeout ||
		    expected_cmd != ucsi_command) {
			ret = -1;
			return ret;
		}

		if (lpm_data.has_value()) {
			memcpy(lpm_data_out, lpm_data.value().data(),
			       lpm_data.value().size());
		}

		return ret;
	}

	void cleanup()
	{
		ppm_->cleanup(ppm_.get());
	}

	// ---- FINISH (struct ucsi_pd_driver)

	void opm_notify()
	{
		notified_count_++;
		opm_notifier_.notify_one();
	}

    protected:
	int Initialize()
	{
		// Init does a reset and that's it.
		QueueExpectedCommandWithResult({ UCSI_CMD_PPM_RESET, 0 });
		return init_ppm();
	}

	void InitializeToIdleNotify()
	{
		ASSERT_EQ(Initialize(), 0);
		QueueExpectedCommandWithResult(
			{ UCSI_CMD_SET_NOTIFICATION_ENABLE, 0 });
		struct ucsi_control control = {
			.command = UCSI_CMD_SET_NOTIFICATION_ENABLE,
			.data_length = 0
		};
		EXPECT_NE(-1, WriteCommand(control));
		EXPECT_TRUE(WaitForCommandPendingState(false));
		EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
		QueueExpectedCommandWithResult({ UCSI_CMD_ACK_CC_CI, 0 });
		WriteAckCommand(/*connector_change_ack*/ false,
				/*command_complete_ack*/ true);
		EXPECT_TRUE(WaitForCommandPendingState(false));
		ASSERT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);
	}

	struct ppm_common_device *GetPpmData()
	{
		return reinterpret_cast<struct ppm_common_device *>(ppm_->dev);
	}

	struct ExpectedCommand {
		// expected PPM command to LPM
		uint8_t ucsi_command;
		int result;
		// Any data that the PDC returns.
		std::optional<std::vector<uint8_t> > lpm_data;
	};

	// Queue an expected PPM command to the LPM.
	// Call this function before performing an OPM write.
	void QueueExpectedCommandWithResult(ExpectedCommand cmd)
	{
		expected_commands_queue_.push_back(std::move(cmd));
	}

	// Provide a response from the LPM for a command that has already been
	// issued by the PPM.
	void CompleteSpecificCommand(ExpectedCommand expected_command)
	{
		// Update expected result with lock before notifying.
		{
			std::lock_guard<std::mutex> lock(cmd_lock_);
			cmd_notifier_expected_cmd_result_ =
				std::move(expected_command);
		}

		cmd_notifier_.notify_one();
	}

	int GetNotifiedCount() const
	{
		return notified_count_;
	}
	void ClearNotifiedCount()
	{
		notified_count_ = 0;
	}

	int ReadCCI(struct ucsi_cci &cci)
	{
		return ppm_->read(ppm_->dev, UCSI_CCI_OFFSET,
				  static_cast<void *>(&cci),
				  sizeof(struct ucsi_cci));
	}

	bool IsEqualCCI(const struct ucsi_cci &rhs)
	{
		struct ucsi_cci lhs;
		ReadCCI(lhs);
		return *reinterpret_cast<const uint32_t *>(&lhs) ==
		       *reinterpret_cast<const uint32_t *>(&rhs);
	}

	void EXPECT_CCI(const struct ucsi_cci &expected_cci, int line)
	{
		struct ucsi_cci cci;
		ReadCCI(cci);
		EXPECT_TRUE(IsEqualCCI(expected_cci))
			<< "CCI comparison failed on line " << line;
		EXPECT_EQ(expected_cci.end_of_message, cci.end_of_message);
		EXPECT_EQ(expected_cci.connector_changed,
			  cci.connector_changed);
		EXPECT_EQ(expected_cci.data_length, cci.data_length);
		EXPECT_EQ(expected_cci.vendor_defined_message,
			  cci.vendor_defined_message);
		EXPECT_EQ(expected_cci.reserved_0, cci.reserved_0);
		EXPECT_EQ(expected_cci.security_request, cci.security_request);
		EXPECT_EQ(expected_cci.fw_update_request,
			  cci.fw_update_request);
		EXPECT_EQ(expected_cci.not_supported, cci.not_supported);
		EXPECT_EQ(expected_cci.cancel_completed, cci.cancel_completed);
		EXPECT_EQ(expected_cci.reset_completed, cci.reset_completed);
		EXPECT_EQ(expected_cci.busy, cci.busy);
		EXPECT_EQ(expected_cci.ack_command, cci.ack_command);
		EXPECT_EQ(expected_cci.error, cci.error);
		EXPECT_EQ(expected_cci.cmd_complete, cci.cmd_complete);
	}

	bool WaitForNotification(int exp_notified_count)
	{
		if (exp_notified_count <= GetNotifiedCount())
			return true;
		while (GetNotifiedCount() < exp_notified_count) {
			std::unique_lock<std::mutex> lk(opm_notify_lock_);
			auto status = opm_notifier_.wait_for(
				lk, std::chrono::milliseconds(
					    cmd_wait_timeout_ms_));

			if (status == std::cv_status::timeout) {
				return false;
			}
		}
		return true;
	}

	void SendLpmAlert(uint8_t lpm_id)
	{
		ppm_->lpm_alert(ppm_->dev, lpm_id);
	}

	void TearDown() override
	{
		EXPECT_TRUE(expected_commands_queue_.empty());
	}

	// Set up a PPM alert on port 1. This results in a GET_CONNECTOR_STATUS
	// read, and a subsequent notification to the OPM.
	void TriggerConnectorChangedNotification(uint8_t lpm_id)
	{
		ucsiv3_get_connector_status_data data = {
			.connector_status_change = 1
		};
		auto lpm_data = reinterpret_cast<uint8_t *>(&data);
		size_t lpm_data_size = sizeof(data) / sizeof(uint8_t);
		QueueExpectedCommandWithResult(
			{ UCSI_CMD_GET_CONNECTOR_STATUS, 0,
			  std::vector<uint8_t>(lpm_data,
					       lpm_data + lpm_data_size) });
		SendLpmAlert(lpm_id);

		EXPECT_TRUE(WaitForAsyncEventPendingState(false));
	}

	bool WaitForAsyncEventPendingState(bool target_pending_state)
	{
		return wait_for_pending_async_event(target_pending_state, 3);
	}

	bool WaitForCommandPendingState(bool target_pending_state)
	{
		return wait_for_pending_command(target_pending_state, 3);
	}

	int WriteCommand(struct ucsi_control &control)
	{
		return ppm_->write(ppm_->dev, UCSI_CONTROL_OFFSET,
				   static_cast<void *>(&control),
				   sizeof(struct ucsi_control));
	}

	void WriteAckCommand(bool connector_change_ack,
			     bool command_complete_ack)
	{
		struct ucsi_control control = { .command = UCSI_CMD_ACK_CC_CI,
						.data_length = 0 };
		struct ucsiv3_ack_cc_ci_cmd ack_data = {
			.connector_change_ack = connector_change_ack,
			.command_complete_ack = command_complete_ack
		};
		memcpy(control.command_specific, &ack_data, sizeof(ack_data));
		ASSERT_NE(-1, WriteCommand(control));
	}

    private:
	bool wait_for_pending_command(bool target_pending_state,
				      int num_iterations)
	{
		bool current_cmd_pending = false;
		for (int i = 0; i < num_iterations; ++i) {
			platform_mutex_lock(GetPpmData()->ppm_lock);
			current_cmd_pending = is_pending_command();
			platform_mutex_unlock(GetPpmData()->ppm_lock);

			if (current_cmd_pending == target_pending_state) {
				break;
			} else {
				platform_condvar_signal(
					GetPpmData()->ppm_condvar);
				// No better option than sleep here
				// unfortunately. Keep this value low.
				std::this_thread::sleep_for(
					std::chrono::milliseconds(1));
			}
		}

		return current_cmd_pending == target_pending_state;
	}

	bool wait_for_pending_async_event(bool target_pending_state,
					  int num_iterations)
	{
		bool currently_pending = false;
		for (int i = 0; i < num_iterations; ++i) {
			platform_mutex_lock(GetPpmData()->ppm_lock);
			currently_pending = is_pending_async_event();
			platform_mutex_unlock(GetPpmData()->ppm_lock);

			if (currently_pending == target_pending_state) {
				break;
			} else {
				platform_condvar_signal(
					GetPpmData()->ppm_condvar);
				// No better option than sleep here
				// unfortunately. Keep this value low.
				std::this_thread::sleep_for(
					std::chrono::milliseconds(1));
			}
		}

		return currently_pending == target_pending_state;
	}

	bool is_pending_command()
	{
		return GetPpmData()->pending.command;
	}
	bool is_pending_async_event()
	{
		return GetPpmData()->pending.async_event;
	}

	PpmDriverUniquePtr ppm_;
	PdDriverUniquePtr pd_;

	std::mutex opm_notify_lock_;
	std::condition_variable opm_notifier_;
	int notified_count_ = 0;

	// If we are blocking execute_cmd to return a specific value, use these
	// variables.
	std::mutex cmd_lock_;
	std::condition_variable cmd_notifier_;
	ExpectedCommand cmd_notifier_expected_cmd_result_;

	// If we are expecting a list of commands, pop the command and return
	// the value listed.
	std::deque<ExpectedCommand> expected_commands_queue_;

	// Number of ports for fake pd driver.
	static constexpr int num_ports_ = 2;

	// Timeout for pending command. This is arbitrarily set (high enough in
	// case unit tests are run in high cpu environment but low enough that
	// tests complete quickly).
	static constexpr int cmd_wait_timeout_ms_ = 250;
};

namespace
{

inline PpmTest *ppm_cast(struct ucsi_pd_device *dev)
{
	return reinterpret_cast<PpmTest *>(dev);
}

int init_ppm(struct ucsi_pd_device *dev)
{
	return ppm_cast(dev)->init_ppm();
}

struct ucsi_ppm_driver *get_ppm(struct ucsi_pd_device *dev)
{
	return ppm_cast(dev)->get_ppm();
}

int execute_cmd(struct ucsi_pd_device *dev, struct ucsi_control *control,
		uint8_t *lpm_data_out)
{
	return ppm_cast(dev)->execute_cmd(control, lpm_data_out);
}

void cleanup(struct ucsi_pd_driver *driver)
{
	ppm_cast(driver->dev)->cleanup();
}

void ucsi_opm_notify(void *context)
{
	ppm_cast(static_cast<ucsi_pd_device *>(context))->opm_notify();
}

struct ucsi_pd_driver *open_test_pd(PpmTest *ppm_test)
{
	struct ucsi_pd_driver *drv = static_cast<struct ucsi_pd_driver *>(
		platform_calloc(1, sizeof(struct ucsi_pd_driver)));
	drv->dev = reinterpret_cast<struct ucsi_pd_device *>(ppm_test);
	drv->init_ppm = init_ppm;
	drv->get_ppm = get_ppm;
	drv->execute_cmd = execute_cmd;
	drv->cleanup = cleanup;

	return drv;
}
} // namespace

// On init, we should go to the Idle state.
TEST_F(PpmTest, InitializesToIdle)
{
	// Make sure we initialize correctly.
	ASSERT_EQ(Initialize(), 0);

	// System should be in the idle state at init.
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE);
}

// From the Idle state, only PPM_RESET and SET_NOTIFICATION_ENABLE is allowed.
TEST_F(PpmTest, Idle_DropsUnexpectedCommands)
{
	ASSERT_EQ(Initialize(), 0);

	// Try all commands except PPM_RESET and SET_NOTIFICATION_ENABLE
	for (uint8_t cmd = UCSI_CMD_PPM_RESET; cmd <= UCSI_CMD_VENDOR_CMD;
	     cmd++) {
		if (cmd == UCSI_CMD_PPM_RESET ||
		    cmd == UCSI_CMD_SET_NOTIFICATION_ENABLE) {
			continue;
		}

		struct ucsi_control control = { .command = cmd,
						.data_length = 0 };
		// Make sure Write completed and then wait for pending command
		// to be cleared. Only the .command part will really matter as
		// that's how we determine whether the next command should be
		// executed.
		ASSERT_NE(-1, WriteCommand(control));
		EXPECT_TRUE(WaitForCommandPendingState(false));
		EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE);
	}

	// Write SET_NOTIFICATION_ENABLE and wait for state transition.
	QueueExpectedCommandWithResult({ UCSI_CMD_SET_NOTIFICATION_ENABLE, 0 });
	struct ucsi_control control = {
		.command = UCSI_CMD_SET_NOTIFICATION_ENABLE, .data_length = 0
	};
	ASSERT_NE(-1, WriteCommand(control));
	EXPECT_TRUE(WaitForCommandPendingState(false));
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
}

// From the Idle state, we process async events but we do not notify the OPM or
// change the PPM state (i.e. silently drop).
TEST_F(PpmTest, Idle_SilentlyProcessesAsyncEvent)
{
	ASSERT_EQ(Initialize(), 0);

	ClearNotifiedCount();

	// Set up a PPM alert with lpm_id=1
	SendLpmAlert(kDefaultAlertPort);

	EXPECT_TRUE(WaitForAsyncEventPendingState(false));
	EXPECT_EQ(0, GetNotifiedCount());
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE);
}

// From the Idle Notify, complete a full command loop:
// - Send command, CCI notifies busy
// - Command complete, CCI notifies command complete.
// - Send ACK_CC_CI, CCI notifies busy
// - Command complete, CCI notifies ack command complete.
TEST_F(PpmTest, IdleNotify_FullCommandLoop)
{
	InitializeToIdleNotify();
	int notified_count = GetNotifiedCount();

	// Emulate a UCSI write from the OPM, and wait for a notification with
	// CCI.busy=1
	struct ucsi_control control = { .command = UCSI_CMD_GET_ALTERNATE_MODES,
					.data_length = 0 };
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(++notified_count));
	EXPECT_CCI(kCciBusy, __LINE__);

	// Send a fake response from the PD driver, and expect a notification to
	// the OPM with CCI.cmd_complete=1.
	CompleteSpecificCommand({ UCSI_CMD_GET_ALTERNATE_MODES, 0 });
	EXPECT_TRUE(WaitForCommandPendingState(false));
	ASSERT_TRUE(WaitForNotification(++notified_count));
	EXPECT_CCI(kCciCmdComplete, __LINE__);

	// OPM acknowledges the PPM's cmd_complete.
	QueueExpectedCommandWithResult({ UCSI_CMD_ACK_CC_CI, 0 });
	WriteAckCommand(/*connector_change_ack*/ false,
			/*command_complete_ack*/ true);
	ASSERT_TRUE(WaitForNotification(++notified_count));
	EXPECT_CCI(kCciAckCommand, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);
}

// When processing an async event, PPM will figure out which port changed and
// then send the connector change event for that port.
TEST_F(PpmTest, IdleNotify_ProcessAsyncEventAndSendConnectorChange)
{
	InitializeToIdleNotify();
	int notified_count = GetNotifiedCount();
	TriggerConnectorChangedNotification(kDefaultAlertPort);
	EXPECT_EQ(++notified_count, GetNotifiedCount());
	struct ucsi_cci cci = { .connector_changed = kDefaultAlertPort };
	EXPECT_CCI(cci, __LINE__);
}

// While in the processing command state, the PPM is busy and should reject any
// new commands that are sent.
TEST_F(PpmTest, ProcessingCommand_BusyRejectsCommands)
{
	GTEST_SKIP() << "Not implemented";
}

// While in the processing command state, we still allow the cancel command to
// be sent WHILE a command is in progress. If a command is cancellable, it will
// replace the current command
TEST_F(PpmTest, ProcessingCommand_BusyAllowsCancelCommand)
{
	GTEST_SKIP() << "Not implemented";
}

// When waiting for command complete, any command that's not ACK_CC_CI should
// get rejected.
TEST_F(PpmTest, WaitForCmdAck_ErrorIfNotCommandComplete)
{
	ASSERT_EQ(Initialize(), 0);
	ClearNotifiedCount();

	QueueExpectedCommandWithResult({ UCSI_CMD_SET_NOTIFICATION_ENABLE, 0 });
	struct ucsi_control control = {
		.command = UCSI_CMD_SET_NOTIFICATION_ENABLE, .data_length = 0
	};
	ASSERT_NE(-1, WriteCommand(control));
	EXPECT_TRUE(WaitForCommandPendingState(false));
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
	EXPECT_EQ(2, GetNotifiedCount()); // one notification each for busy and
					  // command complete

	// Resend the previous command instead of a CC Ack.
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(3));
	EXPECT_CCI(kCciError, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
}

// The PPM state machine allows you to both ACK Command Complete AND
// ACK Connector Indication. Make sure this is supported in the command loop
// path.
TEST_F(PpmTest, WaitForCmdAck_SupportSimultaneousAckCCAndCI)
{
	InitializeToIdleNotify();

	TriggerConnectorChangedNotification(kDefaultAlertPort);
	int notified_count = GetNotifiedCount();

	// PPM is waiting for a connector_change_ack from the OPM now. Don't
	// send it, instead send a new command.
	struct ucsi_control control = {
		.command = UCSI_CMD_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	QueueExpectedCommandWithResult(
		{ UCSI_CMD_GET_CONNECTOR_CAPABILITY, 0 });
	ASSERT_NE(-1, WriteCommand(control)); // TODO: can connector changed
					      // indicator in CCI be cleared
					      // here?
	ASSERT_TRUE(WaitForNotification(++notified_count));
	EXPECT_CCI(kCciCmdComplete, __LINE__);

	// PPM is waiting for connector_change_ack and command_complete_ack.
	// Send them together.
	QueueExpectedCommandWithResult({ UCSI_CMD_ACK_CC_CI, 0 });
	WriteAckCommand(/*connector_change_ack*/ true,
			/*command_complete_ack*/ true);
	// one busy notification for ACK_CC_CI and one for ack_command
	ASSERT_TRUE(WaitForNotification(notified_count + 2));
	EXPECT_CCI(kCciAckCommand, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);
	EXPECT_EQ(GetPpmData()->per_port_status[0].connector_status_change, 0);
	EXPECT_EQ(GetPpmData()->last_connector_changed, -1);
}

// When waiting for a Connection Indicator Ack, we accept an immediate ACK_CC_CI
// to switch the state back to Idle with Notifications.
TEST_F(PpmTest, WaitForCIAck_AckImmediatelyOrLater)
{
	InitializeToIdleNotify();
	TriggerConnectorChangedNotification(kDefaultAlertPort);
	ClearNotifiedCount();

	QueueExpectedCommandWithResult({ UCSI_CMD_ACK_CC_CI, 0 });
	WriteAckCommand(/*connector_change_ack*/ true,
			/*command_complete_ack*/ false);
	ASSERT_TRUE(WaitForNotification(1));
	EXPECT_CCI(kCciAckCommand, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);
}

// If we get an ACK_CC_CI when there is no active connector indication, we
// should fail. In this scenario, the starting state needs to be IdleNotify but
// occurs when the OPM sends other commands after receiving Connector Change
// Indication.
TEST_F(PpmTest, WaitForCIAck_FailIfNoActiveConnectorIndication)
{
	InitializeToIdleNotify();
	int notified_count = GetNotifiedCount();

	WriteAckCommand(/*connector_change_ack*/ true,
			/*command_complete_ack*/ false);
	ASSERT_TRUE(WaitForNotification(notified_count + 1));
	EXPECT_CCI(kCciError, __LINE__);

	EXPECT_TRUE(WaitForCommandPendingState(false));
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);
}

// TODO(UCSI WG): Clarify PPM behavior when incorrect ACK is received. Current
// implementation returns a PPM error, but does not change PPM state. The
// FailIfSendCIAck and FailIfNoAck tests validate the implementation.

// When waiting for a Command Complete Ack, send a Connector Change Ack instead
TEST_F(PpmTest, WaitForCCAck_FailIfSendCIAck)
{
	InitializeToIdleNotify();
	ClearNotifiedCount();

	// Send a command and reach PPM_STATE_WAITING_CC_ACK
	struct ucsi_control control = {
		.command = UCSI_CMD_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	QueueExpectedCommandWithResult(
		{ UCSI_CMD_GET_CONNECTOR_CAPABILITY, 0 });
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(2));
	EXPECT_CCI(kCciCmdComplete, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);

	// Send a bad ack and expect an error and no state change
	WriteAckCommand(true /*connector_change_ack*/,
			false /*command_complete_ack*/);
	ASSERT_TRUE(WaitForNotification(3));
	EXPECT_CCI(kCciError, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
}

// When waiting for a Command Complete Ack, send an Ack without setting either
// Command Complete Ack or Connector Change Ack
TEST_F(PpmTest, WaitForCCAck_FailIfNoAck)
{
	InitializeToIdleNotify();
	ClearNotifiedCount();

	// Send a command and reach PPM_STATE_WAITING_CC_ACK
	struct ucsi_control control = {
		.command = UCSI_CMD_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};
	QueueExpectedCommandWithResult(
		{ UCSI_CMD_GET_CONNECTOR_CAPABILITY, 0 });
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(2));
	EXPECT_CCI(kCciCmdComplete, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);

	// Send a bad ack and expect an error and no state change
	WriteAckCommand(false /*connector_change_ack*/,
			false /*command_complete_ack*/);
	ASSERT_TRUE(WaitForNotification(3));
	EXPECT_CCI(kCciError, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
}

// When an LPM command fails, check that the appropriate CCI bits are set, and
// that the next command succeeds.
TEST_F(PpmTest, LpmError_AcceptsNewCommand)
{
	InitializeToIdleNotify();
	ClearNotifiedCount();

	struct ucsi_control control = {
		.command = UCSI_CMD_GET_CONNECTOR_CAPABILITY, .data_length = 0
	};

	// Return an error from the LPM and expect a CCI error.
	QueueExpectedCommandWithResult(
		{ UCSI_CMD_GET_CONNECTOR_CAPABILITY, -1 });
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(2));
	EXPECT_CCI(kCciError, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_IDLE_NOTIFY);

	// Test acceptance of new message
	ClearNotifiedCount();
	QueueExpectedCommandWithResult(
		{ UCSI_CMD_GET_CONNECTOR_CAPABILITY, 0 });
	ASSERT_NE(-1, WriteCommand(control));
	ASSERT_TRUE(WaitForNotification(2));
	EXPECT_CCI(kCciCmdComplete, __LINE__);
	EXPECT_EQ(GetPpmData()->ppm_state, PPM_STATE_WAITING_CC_ACK);
}
