/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AMD SB-RMI (Side-band Remote Management Interface) Driver */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "sb_rmi.h"
#include "stdbool.h"
#include "time.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

#define SB_RMI_MAILBOX_TIMEOUT_MS 200
#define SB_RMI_MAILBOX_RETRY_DELAY_MS 5

/**
 * Write an SB-RMI register
 */
static int sb_rmi_write(const int reg, int data)
{
	return i2c_write8(I2C_PORT_THERMAL_AP, SB_RMI_I2C_ADDR_FLAGS0, reg,
			  data);
}

/**
 * Read an SB-RMI register
 */
static int sb_rmi_read(const int reg, int *data)
{
	return i2c_read8(I2C_PORT_THERMAL_AP, SB_RMI_I2C_ADDR_FLAGS0, reg,
			 data);
}

/**
 * Set SB-RMI software interrupt
 */
static int sb_rmi_assert_interrupt(bool assert)
{
	return sb_rmi_write(SB_RMI_SW_INTR_REG, assert ? 0x1 : 0x0);
}

/**
 * Execute a SB-RMI mailbox transaction
 *
 * cmd:
 *	See "SB-RMI Soft Mailbox Message" table in PPR for command id
 * msg_in:
 *	Message In buffer
 * msg_out:
 *	Message Out buffer
 */
int sb_rmi_mailbox_xfer(int cmd, uint32_t msg_in, uint32_t *msg_out_ptr)
{
	/**
	 * The sequence is as follows:
	 * 1. The initiator (BMC) indicates that command is to be serviced by
	 *    firmware by writing 0x80 to SBRMI::InBndMsg_inst7 (SBRMI_x3F).
	 * This register must be set to 0x80 after reset.
	 * 2. The initiator (BMC) writes the command to SBRMI::InBndMsg_inst0
	 *    (SBRMI_x38).
	 * 3. For write operations or read operations which require additional
	 *    addressing information as shown in the table above, the initiator
	 *    (BMC) writes Command Data In[31:0] to SBRMI::InBndMsg_inst[4:1]
	 *    {SBRMI_x3C(MSB):SBRMI_x39(LSB)}.
	 * 4. The initiator (BMC) writes 0x01 to SBRMI::SoftwareInterrupt to
	 *    notify firmware to perform the requested read or write command.
	 * 5. Firmware reads the message and performs the defined action.
	 * 6. Firmware writes the original command to outbound message register
	 *    SBRMI::OutBndMsg_inst0 (SBRMI_x30).
	 * 7. Firmware will write SBRMI::Status[SwAlertSts]=1 to generate an
	 *    ALERT (if enabled) to initiator (BMC) to indicate completion of
	 * the requested command. Firmware must (if applicable) put the message
	 *    data into the message registers SBRMI::OutBndMsg_inst[4:1]
	 *    {SBRMI_x34(MSB):SBRMI_x31(LSB)}.
	 * 8. For a read operation, the initiator (BMC) reads the firmware
	 *    response Command Data Out[31:0] from SBRMI::OutBndMsg_inst[4:1]
	 *    {SBRMI_x34(MSB):SBRMI_x31(LSB)}.
	 * 9. BMC must write 1'b1 to SBRMI::Status[SwAlertSts] to clear the
	 *     ALERT to initiator (BMC). It is recommended to clear the ALERT
	 *     upon completion of the current mailbox command.
	 */
	int val;
	bool alerted;
	timestamp_t start;
	const int ap_comm_failure_threshold = 2;
	static int ap_comm_failure_count;

	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_NOT_POWERED;

	/**
	 * Step 1: writing 0x80 to SBRMI::InBndMsg_inst7 (SBRMI_x3F) to
	 *         indicate that command is to be serviced and to make sure
	 *         SBRMIx40[Software Interrupt] is cleared
	 */
	RETURN_ERROR(sb_rmi_write(SB_RMI_IN_BND_MSG7_REG, 0x80));
	RETURN_ERROR(sb_rmi_assert_interrupt(0));

	/* Step 2: writes the command to SBRMI::InBndMsg_inst0 (SBRMI_x38) */
	RETURN_ERROR(sb_rmi_write(SB_RMI_IN_BND_MSG0_REG, cmd));
	/* Step 3: msgIn to {SBRMI_x3C(MSB):SBRMI_x39(LSB)} */
	RETURN_ERROR(sb_rmi_write(SB_RMI_IN_BND_MSG1_REG, msg_in & 0xFF));
	RETURN_ERROR(
		sb_rmi_write(SB_RMI_IN_BND_MSG2_REG, (msg_in >> 8) & 0xFF));
	RETURN_ERROR(
		sb_rmi_write(SB_RMI_IN_BND_MSG3_REG, (msg_in >> 16) & 0xFF));
	RETURN_ERROR(
		sb_rmi_write(SB_RMI_IN_BND_MSG4_REG, (msg_in >> 24) & 0xFF));

	/**
	 * Step 4: writes 0x01 to SBRMIx40[Software Interrupt] to notify
	 *         firmware to start service.
	 */
	RETURN_ERROR(sb_rmi_assert_interrupt(1));

	/**
	 * Step 5: SoC do the service
	 * Step 6: The original command will be copied to SBRMI::OutBndMsg_inst0
	 *         (SBRMI_x30)
	 * Step 7: wait SBRMIx02[SwAlertSts] to 1 which indicate the completion
	 *         of a mailbox operation
	 */
	alerted = false;
	start = get_time();
	do {
		if (sb_rmi_read(SB_RMI_STATUS_REG, &val))
			break;
		if (val & 0x02) {
			alerted = true;
			break;
		}
		crec_msleep(SB_RMI_MAILBOX_RETRY_DELAY_MS);
	} while (time_since32(start) < SB_RMI_MAILBOX_TIMEOUT_MS * MSEC);

	if (!alerted) {
		/**
		 * Don't spam the EC logs when the AP is hung. Instead, log the
		 * first few failures, and then indicate the AP is likely hung.
		 */
		if (ap_comm_failure_count < ap_comm_failure_threshold) {
			CPRINTS("SB-SMI: Mailbox transfer timeout");
		} else if (ap_comm_failure_count == ap_comm_failure_threshold) {
			CPRINTS("RMI: The AP is failing to respond despite "
				"being powered on.");
		}
		++ap_comm_failure_count;

		return EC_ERROR_TIMEOUT;
	}

	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG0_REG, &val));
	if (val != cmd) {
		/**
		 * Don't spam the EC logs when the AP is hung. Instead, log the
		 * first few failures, and then indicate the AP is likely hung.
		 */
		if (ap_comm_failure_count < ap_comm_failure_threshold) {
			CPRINTS("RMI: Unexpected command value in out bound "
				"message");
		} else if (ap_comm_failure_count == ap_comm_failure_threshold) {
			CPRINTS("RMI: The AP is failing to respond despite "
				"being powered on.");
		}
		++ap_comm_failure_count;

		return EC_ERROR_UNKNOWN;
	}

	/**
	 * This AP communication was successful.
	 * Reset the count to log the next AP communication failure.
	 */
	ap_comm_failure_count = 0;

	/* Step 8: read msgOut from {SBRMI_x34(MSB):SBRMI_x31(LSB)} */
	*msg_out_ptr = 0;
	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG1_REG, &val));
	*msg_out_ptr |= val;
	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG2_REG, &val));
	*msg_out_ptr |= val << 8;
	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG3_REG, &val));
	*msg_out_ptr |= val << 16;
	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG4_REG, &val));
	*msg_out_ptr |= val << 24;

	/**
	 * Step 9: BMC must write 1'b1 to SBRMI::Status[SwAlertSts] to clear
	 *          the ALERT to initiator (BMC). It is recommended to clear the
	 *          ALERT upon completion of the current mailbox command.
	 */
	RETURN_ERROR(sb_rmi_write(SB_RMI_STATUS_REG, 0x2));

	/* Step 10: read the return code from OutBndMsg_inst7 (SBRMI_x37) */
	RETURN_ERROR(sb_rmi_read(SB_RMI_OUT_BND_MSG7_REG, &val));

	switch (val) {
	case SB_RMI_MAILBOX_SUCCESS:
		return EC_SUCCESS;
	case SB_RMI_MAILBOX_ERROR_ABORTED:
		return EC_ERROR_UNKNOWN;
	case SB_RMI_MAILBOX_ERROR_UNKNOWN_CMD:
		return EC_ERROR_INVAL;
	case SB_RMI_MAILBOX_ERROR_INVALID_CORE:
		return EC_ERROR_PARAM1;
	default:
		return EC_ERROR_UNKNOWN;
	}
}
