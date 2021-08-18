/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

#define BUFFER_SIZE 100
#define VERIFY_TIMEOUT (5 * SECOND)

struct tcpci_reg {
	uint8_t		offset;
	uint8_t		size;
	uint16_t	value;
	const char	*name;
};

#define TCPCI_REG(reg_name, reg_size)					\
	[reg_name] = { .offset = (reg_name), .size = (reg_size),	\
			.value = 0, .name = #reg_name, }

static struct tcpci_reg tcpci_regs[] = {
	TCPCI_REG(TCPC_REG_VENDOR_ID, 2),
	TCPCI_REG(TCPC_REG_PRODUCT_ID, 2),
	TCPCI_REG(TCPC_REG_BCD_DEV, 2),
	TCPCI_REG(TCPC_REG_TC_REV, 2),
	TCPCI_REG(TCPC_REG_PD_REV, 2),
	TCPCI_REG(TCPC_REG_PD_INT_REV, 2),
	TCPCI_REG(TCPC_REG_ALERT, 2),
	TCPCI_REG(TCPC_REG_ALERT_MASK, 2),
	TCPCI_REG(TCPC_REG_POWER_STATUS_MASK, 1),
	TCPCI_REG(TCPC_REG_FAULT_STATUS_MASK, 1),
	TCPCI_REG(TCPC_REG_EXT_STATUS_MASK, 1),
	TCPCI_REG(TCPC_REG_ALERT_EXTENDED_MASK, 1),
	TCPCI_REG(TCPC_REG_CONFIG_STD_OUTPUT, 1),
	TCPCI_REG(TCPC_REG_TCPC_CTRL, 1),
	TCPCI_REG(TCPC_REG_ROLE_CTRL, 1),
	TCPCI_REG(TCPC_REG_FAULT_CTRL, 1),
	TCPCI_REG(TCPC_REG_POWER_CTRL, 1),
	TCPCI_REG(TCPC_REG_CC_STATUS, 1),
	TCPCI_REG(TCPC_REG_POWER_STATUS, 1),
	TCPCI_REG(TCPC_REG_FAULT_STATUS, 1),
	TCPCI_REG(TCPC_REG_EXT_STATUS, 1),
	TCPCI_REG(TCPC_REG_ALERT_EXT, 1),
	TCPCI_REG(TCPC_REG_DEV_CAP_1, 2),
	TCPCI_REG(TCPC_REG_DEV_CAP_2, 2),
	TCPCI_REG(TCPC_REG_STD_INPUT_CAP, 1),
	TCPCI_REG(TCPC_REG_STD_OUTPUT_CAP, 1),
	TCPCI_REG(TCPC_REG_CONFIG_EXT_1, 1),
	TCPCI_REG(TCPC_REG_MSG_HDR_INFO, 1),
	TCPCI_REG(TCPC_REG_RX_DETECT, 1),
	TCPCI_REG(TCPC_REG_RX_BUFFER, BUFFER_SIZE),
	TCPCI_REG(TCPC_REG_TRANSMIT, 1),
	TCPCI_REG(TCPC_REG_TX_BUFFER, BUFFER_SIZE),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE, 2),
	TCPCI_REG(TCPC_REG_VBUS_SINK_DISCONNECT_THRESH, 2),
	TCPCI_REG(TCPC_REG_VBUS_STOP_DISCHARGE_THRESH, 2),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG, 2),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG, 2),
	TCPCI_REG(TCPC_REG_COMMAND, 1),
};

static uint8_t tx_buffer[BUFFER_SIZE];
static int tx_pos = -1;
static int tx_msg_cnt;
static int tx_retry_cnt = -1;
static uint8_t rx_buffer[BUFFER_SIZE];
static int rx_pos = -1;

static const char * const ctrl_msg_name[] = {
	[0]				= "C-RSVD_0",
	[PD_CTRL_GOOD_CRC]		= "C-GOODCRC",
	[PD_CTRL_GOTO_MIN]		= "C-GOTOMIN",
	[PD_CTRL_ACCEPT]		= "C-ACCEPT",
	[PD_CTRL_REJECT]		= "C-REJECT",
	[PD_CTRL_PING]			= "C-PING",
	[PD_CTRL_PS_RDY]		= "C-PSRDY",
	[PD_CTRL_GET_SOURCE_CAP]	= "C-GET_SRC_CAP",
	[PD_CTRL_GET_SINK_CAP]		= "C-GET_SNK_CAP",
	[PD_CTRL_DR_SWAP]		= "C-DR_SWAP",
	[PD_CTRL_PR_SWAP]		= "C-PR_SWAP",
	[PD_CTRL_VCONN_SWAP]		= "C-VCONN_SW",
	[PD_CTRL_WAIT]			= "C-WAIT",
	[PD_CTRL_SOFT_RESET]		= "C-SOFT-RESET",
	[14]				= "C-RSVD_14",
	[15]				= "C-RSVD_15",
	[PD_CTRL_NOT_SUPPORTED]		= "C-NOT_SUPPORTED",
	[PD_CTRL_GET_SOURCE_CAP_EXT]	= "C-GET_SRC_CAP-EXT",
	[PD_CTRL_GET_STATUS]		= "C-GET-STATUS",
	[PD_CTRL_FR_SWAP]		= "C-FR_SWAP",
	[PD_CTRL_GET_PPS_STATUS]	= "C-GET_PPS_STATUS",
	[PD_CTRL_GET_COUNTRY_CODES]	= "C-GET_COUNTRY_CODES",
};

static const char * const data_msg_name[] = {
	[0]				= "D-RSVD_0",
	[PD_DATA_SOURCE_CAP]		= "D-SRC_CAP",
	[PD_DATA_REQUEST]		= "D-REQUEST",
	[PD_DATA_BIST]			= "D-BIST",
	[PD_DATA_SINK_CAP]		= "D-SNK_CAP",
	/* 5-14 Reserved for REV 2.0 */
	[PD_DATA_BATTERY_STATUS]	= "D-BATTERY_STATUS",
	[PD_DATA_ALERT]			= "D-ALERT",
	[PD_DATA_GET_COUNTRY_INFO]	= "D-GET_COUNTRY_CODES",
	/* 8-14 Reserved for REV 3.0 */
	[PD_DATA_ENTER_USB]		= "D-ENTER_USB",
	[PD_DATA_VENDOR_DEF]		= "D-VDM",
};

static const char * const ext_msg_name[] = {
	[0]				= "X-RSVD_0",
	[PD_EXT_SOURCE_CAP]		= "X-SRC_CAP",
	[PD_EXT_STATUS]			= "X-STATUS",
	[PD_EXT_GET_BATTERY_CAP]	= "X-GET_BATTERY_CAP",
	[PD_EXT_GET_BATTERY_STATUS]	= "X-GET_BATTERY_STATUS",
	[PD_EXT_BATTERY_CAP]		= "X-BATTERY_CAP",
	[PD_EXT_GET_MANUFACTURER_INFO]	= "X-GET_MFR_INFO",
	[PD_EXT_MANUFACTURER_INFO]	= "X-MFR_INFO",
	[PD_EXT_SECURITY_REQUEST]	= "X-SECURITY_REQ",
	[PD_EXT_SECURITY_RESPONSE]	= "X-SECURITY_RESP",
	[PD_EXT_FIRMWARE_UPDATE_REQUEST] = "X-FW_UP_REQ",
	[PD_EXT_FIRMWARE_UPDATE_RESPONSE] = "X-FW_UP_RESP",
	[PD_EXT_PPS_STATUS]		= "X-PPS_STATUS",
	[PD_EXT_COUNTRY_INFO]		= "X-COUNTRY_INFO",
	[PD_EXT_COUNTRY_CODES]		= "X-COUNTRY_CODES",
};

static const char * const rev_name[] = {
	[PD_REV10] = "1.0",
	[PD_REV20] = "2.0",
	[PD_REV30] = "3.0",
	[3] = "RSVD",
};

static const char * const drole_name[] = {
	[PD_ROLE_UFP] = "UFP",
	[PD_ROLE_DFP] = "DFP",
};

static const char * const prole_name[] = {
	[PD_ROLE_SINK] = "SNK",
	[PD_ROLE_SOURCE] = "SRC",
};

static void print_header(const char *prefix, uint16_t header)
{
	int type  = PD_HEADER_TYPE(header);
	int drole = PD_HEADER_DROLE(header);
	int rev   = PD_HEADER_REV(header);
	int prole = PD_HEADER_PROLE(header);
	int id    = PD_HEADER_ID(header);
	int cnt   = PD_HEADER_CNT(header);
	int ext   = PD_HEADER_EXT(header);
	const char *name = ext	? ext_msg_name[type]
				: cnt
					? data_msg_name[type]
					: ctrl_msg_name[type];

	ccprints("%s header=0x%x [%s %s %s %s id=%d cnt=%d ext=%d]",
		 prefix, header,
		 name, drole_name[drole], rev_name[rev], prole_name[prole],
		 id, cnt, ext);
}

static bool dead_battery(void)
{
	return false;
}

static bool debug_accessory_indicator_supported(void)
{
	return true;
}

static int verify_transmit(enum tcpm_sop_type want_tx_type,
			   int want_tx_retry,
			   enum pd_ctrl_msg_type want_ctrl_msg,
			   enum pd_data_msg_type want_data_msg,
			   int timeout)
{
	uint64_t end_time = get_time().val + timeout;

	/*
	 * Check that nothing was already transmitted. This ensures that all
	 * transmits are checked, and the test stays in sync with the code
	 * being tested.
	 */
	TEST_EQ(tcpci_regs[TCPC_REG_TRANSMIT].value, 0, "%d");

	/* Now wait for the expected message to be transmitted. */
	while (get_time().val < end_time) {
		if (tcpci_regs[TCPC_REG_TRANSMIT].value != 0) {
			int tx_type = TCPC_REG_TRANSMIT_TYPE(
				tcpci_regs[TCPC_REG_TRANSMIT].value);
			int tx_retry = TCPC_REG_TRANSMIT_RETRY(
				tcpci_regs[TCPC_REG_TRANSMIT].value);
			uint16_t header = UINT16_FROM_BYTE_ARRAY_LE(
						tx_buffer, 1);
			int pd_type  = PD_HEADER_TYPE(header);
			int pd_cnt   = PD_HEADER_CNT(header);

			TEST_EQ(tx_type, want_tx_type, "%d");
			if (want_tx_retry >= 0)
				TEST_EQ(tx_retry, want_tx_retry, "%d");

			if (want_ctrl_msg != 0) {
				TEST_EQ(pd_type, want_ctrl_msg, "0x%x");
				TEST_EQ(pd_cnt, 0, "%d");
			}
			if (want_data_msg != 0) {
				TEST_EQ(pd_type, want_data_msg, "0x%x");
				TEST_GE(pd_cnt, 1, "%d");
			}

			tcpci_regs[TCPC_REG_TRANSMIT].value = 0;
			return EC_SUCCESS;
		}
		task_wait_event(5 * MSEC);
	}
	TEST_ASSERT(0);
	return EC_ERROR_UNKNOWN;
}

int verify_tcpci_transmit(enum tcpm_sop_type tx_type,
			  enum pd_ctrl_msg_type ctrl_msg,
			  enum pd_data_msg_type data_msg)
{
	return verify_transmit(tx_type, -1,
			       ctrl_msg, data_msg,
			       VERIFY_TIMEOUT);
}

int verify_tcpci_tx_timeout(enum tcpm_sop_type tx_type,
			    enum pd_ctrl_msg_type ctrl_msg,
			    enum pd_data_msg_type data_msg,
			    int timeout)
{
	return verify_transmit(tx_type, -1,
			       ctrl_msg, data_msg,
			       timeout);
}

int verify_tcpci_tx_retry_count(enum tcpm_sop_type tx_type,
				enum pd_ctrl_msg_type ctrl_msg,
				enum pd_data_msg_type data_msg,
				int retry_count)
{
	return verify_transmit(tx_type, retry_count,
			       ctrl_msg, data_msg,
			       VERIFY_TIMEOUT);
}

int verify_tcpci_tx_with_data(enum tcpm_sop_type tx_type,
			      enum pd_data_msg_type data_msg,
			      uint8_t *data,
			      int data_bytes,
			      int *msg_len,
			      int timeout)
{
	int rv;

	if (timeout <= 0)
		timeout = VERIFY_TIMEOUT;

	rv = verify_transmit(tx_type, -1,
			     0, data_msg,
			     timeout);
	if (!rv) {
		TEST_NE(data, NULL, "%p");
		TEST_GE(data_bytes, tx_msg_cnt, "%d");
		memcpy(data, tx_buffer, tx_msg_cnt);
		if (msg_len)
			*msg_len = tx_msg_cnt;
	}
	return rv;
}

int verify_tcpci_possible_tx(struct possible_tx possible[],
			     int possible_cnt,
			     int *found_index,
			     uint8_t *data,
			     int data_bytes,
			     int *msg_len,
			     int timeout)
{
	bool assert_on_timeout = true;
	uint64_t end_time;

	*found_index = -1;

	if (timeout <= 0) {
		timeout = VERIFY_TIMEOUT;
		assert_on_timeout = false;
	}
	end_time = get_time().val + timeout;

	/*
	 * Check that nothing was already transmitted. This ensures that all
	 * transmits are checked, and the test stays in sync with the code
	 * being tested.
	 */
	TEST_EQ(tcpci_regs[TCPC_REG_TRANSMIT].value, 0, "%d");

	/* Now wait for the expected message to be transmitted. */
	while (get_time().val < end_time) {
		if (tcpci_regs[TCPC_REG_TRANSMIT].value != 0) {
			int i;
			int tx_type = TCPC_REG_TRANSMIT_TYPE(
				tcpci_regs[TCPC_REG_TRANSMIT].value);
			uint16_t header = UINT16_FROM_BYTE_ARRAY_LE(
						tx_buffer, 1);
			int pd_type  = PD_HEADER_TYPE(header);
			int pd_cnt   = PD_HEADER_CNT(header);

			for (i = 0; i < possible_cnt; ++i) {
				int want_tx_type = possible[i].tx_type;
				int want_ctrl_msg = possible[i].ctrl_msg;
				int want_data_msg = possible[i].data_msg;

				if (tx_type != want_tx_type)
					continue;

				if (want_ctrl_msg != 0) {
					if (pd_type != want_ctrl_msg ||
					    pd_cnt != 0)
						continue;
				}
				if (want_data_msg != 0) {
					if (pd_type != want_data_msg ||
					    pd_cnt == 0)
						continue;

					if (data != NULL) {
						TEST_GE(data_bytes,
							tx_msg_cnt, "%d");
						memcpy(data, tx_buffer,
						       tx_msg_cnt);
					}
					if (msg_len != NULL)
						*msg_len = tx_msg_cnt;
				}
				*found_index = i;
				tcpci_regs[TCPC_REG_TRANSMIT].value = 0;
				return EC_SUCCESS;
			}
			return EC_ERROR_UNKNOWN;
		}
		task_wait_event(5 * MSEC);
	}
	if (assert_on_timeout)
		TEST_ASSERT(0);

	return EC_ERROR_TIMEOUT;
}

void mock_tcpci_receive(enum pd_msg_type sop, uint16_t header,
			uint32_t *payload)
{
	int i;

	rx_buffer[0] = 3 + (PD_HEADER_CNT(header) * 4);
	rx_buffer[1] = sop;
	rx_buffer[2] = header & 0xFF;
	rx_buffer[3] = (header >> 8) & 0xFF;

	if (rx_buffer[0] >= BUFFER_SIZE) {
		ccprints("ERROR: rx too large");
		return;
	}

	for (i = 4; i < rx_buffer[0]; i += 4) {
		rx_buffer[i] = *payload & 0xFF;
		rx_buffer[i+1] = (*payload >> 8) & 0xFF;
		rx_buffer[i+2] = (*payload >> 16) & 0xFF;
		rx_buffer[i+3] = (*payload >> 24) & 0xFF;
		payload++;
	}

	rx_pos = 0;
}

/*****************************************************************************
 * TCPCI register reset values
 *
 * These values are from USB Type-C Port Controller Interface Specification
 * Revision 2.0, Version 1.2,
 */
static void tcpci_reset_register_masks(void)
{
	/*
	 * Using table 4-1 for default mask values
	 */
	tcpci_regs[TCPC_REG_ALERT_MASK].value =			0x7FFF;
	tcpci_regs[TCPC_REG_POWER_STATUS_MASK].value =		0xFF;
	tcpci_regs[TCPC_REG_FAULT_STATUS_MASK].value =		0xFF;
	tcpci_regs[TCPC_REG_EXT_STATUS_MASK].value =		0x01;
	tcpci_regs[TCPC_REG_ALERT_EXTENDED_MASK].value =	0x07;
}

static void tcpci_reset_register_defaults(void)
{
	int i;

	/* Default all registers to 0 and then overwrite if they are not */
	for (i = 0; i < ARRAY_SIZE(tcpci_regs); i++)
		tcpci_regs[i].value = 0;

	/* Type-C Release 1,3 */
	tcpci_regs[TCPC_REG_TC_REV].value =			0x0013;
	/* PD Revision 3.0 Version 1.2 */
	tcpci_regs[TCPC_REG_PD_REV].value =			0x3012;
	/* PD Interface Revision 2.0, Version 1.1 */
	tcpci_regs[TCPC_REG_PD_INT_REV].value =			0x2011;

	tcpci_reset_register_masks();

	tcpci_regs[TCPC_REG_CONFIG_STD_OUTPUT].value =
			TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N |
			TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;

	tcpci_regs[TCPC_REG_POWER_CTRL].value =
			TCPC_REG_POWER_CTRL_VOLT_ALARM_DIS |
			TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS;

	tcpci_regs[TCPC_REG_FAULT_STATUS].value =
			TCPC_REG_FAULT_STATUS_ALL_REGS_RESET;

	tcpci_regs[TCPC_REG_DEV_CAP_1].value =
			TCPC_REG_DEV_CAP_1_SOURCE_VBUS |
			TCPC_REG_DEV_CAP_1_SINK_VBUS |
			TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP |
			TCPC_REG_DEV_CAP_1_SRC_RESISTOR_RP_3P0_1P5_DEF;

	/*
	 * Using table 4-17 to get the default Role Control and
	 * Message Header Info register values.
	 */
	switch (mock_tcpci_get_reg(TCPC_REG_DEV_CAP_1) &
			TCPC_REG_DEV_CAP_1_PWRROLE_MASK) {
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_OR_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK_ACC:
		tcpci_regs[TCPC_REG_ROLE_CTRL].value =		0x0A;
		tcpci_regs[TCPC_REG_MSG_HDR_INFO].value =	0x04;
		break;

	case TCPC_REG_DEV_CAP_1_PWRROLE_DRP:
		if (dead_battery())
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x0A;
		else if (debug_accessory_indicator_supported())
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x4A;
		else
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x0F;
		tcpci_regs[TCPC_REG_MSG_HDR_INFO].value =	0x04;
		break;

	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC:
		if (!dead_battery())
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x05;
		tcpci_regs[TCPC_REG_MSG_HDR_INFO].value =	0x0D;
		break;

	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP_ADPT_CBL:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP:
		if (dead_battery())
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x0A;
		else if (debug_accessory_indicator_supported())
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x4A;
		else
			tcpci_regs[TCPC_REG_ROLE_CTRL].value =	0x0F;
		tcpci_regs[TCPC_REG_MSG_HDR_INFO].value =	0x04;
		break;
	}
}
/*****************************************************************************/

void mock_tcpci_reset(void)
{
	tcpci_reset_register_defaults();
}

void mock_tcpci_set_reg(int reg_offset, uint16_t value)
{
	struct tcpci_reg *reg = tcpci_regs + reg_offset;

	reg->value = value;
	ccprints("TCPCI mock set %s = 0x%x",  reg->name, reg->value);
}

void mock_tcpci_set_reg_bits(int reg_offset, uint16_t mask)
{
	struct tcpci_reg *reg = tcpci_regs + reg_offset;
	uint16_t old_value = reg->value;

	reg->value |= mask;
	ccprints("TCPCI mock set bits %s (mask=0x%x) = 0x%x -> 0x%x",
		 reg->name, mask, old_value, reg->value);
}

void mock_tcpci_clr_reg_bits(int reg_offset, uint16_t mask)
{
	struct tcpci_reg *reg = tcpci_regs + reg_offset;
	uint16_t old_value = reg->value;

	reg->value &= ~mask;
	ccprints("TCPCI mock clr bits %s (mask=0x%x) = 0x%x -> 0x%x",
		 reg->name, mask, old_value, reg->value);
}

uint16_t mock_tcpci_get_reg(int reg_offset)
{
	return tcpci_regs[reg_offset].value;
}

int tcpci_i2c_xfer(int port, uint16_t addr_flags,
		const uint8_t *out, int out_size,
		uint8_t *in, int in_size, int flags)
{
	struct tcpci_reg *reg;

	if (port != I2C_PORT_HOST_TCPC) {
		ccprints("ERROR: wrong I2C port %d", port);
		return EC_ERROR_UNKNOWN;
	}
	if (addr_flags != MOCK_TCPCI_I2C_ADDR_FLAGS) {
		ccprints("ERROR: wrong I2C address 0x%x", addr_flags);
		return EC_ERROR_UNKNOWN;
	}

	if (rx_pos > 0) {
		if (rx_pos + in_size > rx_buffer[0] + 1) {
			ccprints("ERROR: rx in_size");
			return EC_ERROR_UNKNOWN;
		}
		memcpy(in, rx_buffer + rx_pos, in_size);
		rx_pos	+= in_size;
		if (rx_pos == rx_buffer[0] + 1) {
			print_header("RX", UINT16_FROM_BYTE_ARRAY_LE(
						rx_buffer, 2));
			rx_pos = -1;
		}
		return EC_SUCCESS;
	}

	if (out_size == 0) {
		ccprints("ERROR: out_size == 0");
		return EC_ERROR_UNKNOWN;
	}
	if (tx_pos != -1) {
		if (tx_pos + out_size > BUFFER_SIZE) {
			ccprints("ERROR: tx out_size");
			return EC_ERROR_UNKNOWN;
		}
		memcpy(tx_buffer + tx_pos, out, out_size);
		tx_pos += out_size;
		tx_msg_cnt = tx_pos;
		if (tx_pos > 0 && tx_pos == tx_buffer[0] + 1) {
			print_header("TX", UINT16_FROM_BYTE_ARRAY_LE(
						tx_buffer, 1));
			tx_pos = -1;
			tx_retry_cnt = -1;
		}
		return EC_SUCCESS;
	}
	reg = tcpci_regs + *out;
	if (*out >= ARRAY_SIZE(tcpci_regs) || reg->size == 0) {
		ccprints("ERROR: unknown reg 0x%x", *out);
		return EC_ERROR_UNKNOWN;
	}
	if (reg->offset == TCPC_REG_TX_BUFFER) {
		if (tx_pos != -1) {
			ccprints("ERROR: TCPC_REG_TX_BUFFER not ready");
			return EC_ERROR_UNKNOWN;
		}
		tx_pos = 0;
		tx_msg_cnt = 0;
		if (out_size != 1) {
			ccprints("ERROR: TCPC_REG_TX_BUFFER out_size != 1");
			return EC_ERROR_UNKNOWN;
		}
	} else if (reg->offset == TCPC_REG_RX_BUFFER) {
		if (rx_pos != 0) {
			ccprints("ERROR: TCPC_REG_RX_BUFFER not ready");
			return EC_ERROR_UNKNOWN;
		}
		if (in_size > BUFFER_SIZE || in_size > rx_buffer[0]) {
			ccprints("ERROR: TCPC_REG_RX_BUFFER in_size");
			return EC_ERROR_UNKNOWN;
		}
		memcpy(in, rx_buffer, in_size);
		rx_pos += in_size;
	} else if (out_size == 1) {
		if (in_size != reg->size) {
			ccprints("ERROR: %s in_size %d != %d", reg->name,
				 in_size, reg->size);
			return EC_ERROR_UNKNOWN;
		}
		if (reg->size == 1)
			in[0] = reg->value;
		else if (reg->size == 2) {
			in[0] = reg->value;
			in[1] = reg->value >> 8;
		}
	} else {
		uint16_t value = 0;

		if (in_size != 0) {
			ccprints("ERROR: in_size != 0");
			return EC_ERROR_UNKNOWN;
		}
		if (out_size != reg->size + 1) {
			ccprints("ERROR: out_size != %d", reg->size + 1);
			return EC_ERROR_UNKNOWN;
		}
		if (reg->size == 1)
			value = out[1];
		else if (reg->size == 2)
			value = out[1] + (out[2] << 8);
		ccprints("%s TCPCI write %s = 0x%x",
			 task_get_name(task_get_current()),
			 reg->name, value);
		if (reg->offset == TCPC_REG_ALERT)
			reg->value &= ~value;
		else
			reg->value = value;
	}
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_XFER(tcpci_i2c_xfer);

void tcpci_register_dump(void)
{
	int reg;
	int cc1, cc2;

	ccprints("********* TCPCI Register Dump ***********");
	reg = mock_tcpci_get_reg(TCPC_REG_ALERT);
	ccprints("TCPC_REG_ALERT        = 0x%08X", reg);
	if (reg) {
		if (reg & BIT(0))
			ccprints("\t0001: CC Status");
		if (reg & BIT(1))
			ccprints("\t0002: Power Status");
		if (reg & BIT(2))
			ccprints("\t0004: Received SOP* Message Status");
		if (reg & BIT(3))
			ccprints("\t0008: Received Hard Reset");
		if (reg & BIT(4))
			ccprints("\t0010: Transmit SOP* Message Failed");
		if (reg & BIT(5))
			ccprints("\t0020: Transmit SOP* Message Discarded");
		if (reg & BIT(6))
			ccprints("\t0040: Transmit SOP* Message Successful");
		if (reg & BIT(7))
			ccprints("\t0080: Vbus Voltage Alarm Hi");
		if (reg & BIT(8))
			ccprints("\t0100: Vbus Voltage Alarm Lo");
		if (reg & BIT(9))
			ccprints("\t0200: Fault");
		if (reg & BIT(10))
			ccprints("\t0400: Rx Buffer Overflow");
		if (reg & BIT(11))
			ccprints("\t0800: Vbus Sink Disconnect Detected");
		if (reg & BIT(12))
			ccprints("\t1000: Beginning SOP* Message Status");
		if (reg & BIT(13))
			ccprints("\t2000: Extended Status");
		if (reg & BIT(14))
			ccprints("\t4000: Alert Extended");
		if (reg & BIT(15))
			ccprints("\t8000: Vendor Defined Alert");
	}

	reg = mock_tcpci_get_reg(TCPC_REG_TCPC_CTRL);
	ccprints("TCPC_REG_TCPC_CTRL    = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Plug Orientation FLIP");
	if (reg & BIT(1))
		ccprints("\t02: BIST Test Mode");
	if (reg & (BIT(2) | BIT(3))) {
		switch ((reg >> 2) & 3) {
		case 2:
			ccprints("\t08: Enable Clock Stretching");
			break;
		case 3:
			ccprints("\t0C: Enable Clock Stretching if !Alert");
			break;
		}
	}
	if (reg & BIT(4))
		ccprints("\t10: Debug Accessory controlled by TCPM");
	if (reg & BIT(5))
		ccprints("\t20: Watchdog Timer enabled");
	if (reg & BIT(6))
		ccprints("\t40: Looking4Connection Alert enabled");
	if (reg & BIT(7))
		ccprints("\t80: SMBus PEC enabled");

	reg = mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL);
	ccprints("TCPC_REG_ROLE_CTRL    = 0x%04X", reg);
	cc1 = (reg >> 0) & 3;
	switch (cc1) {
	case 0:
		ccprints("\t00: CC1 == Ra");
		break;
	case 1:
		ccprints("\t01: CC1 == Rp");
		break;
	case 2:
		ccprints("\t02: CC1 == Rd");
		break;
	case 3:
		ccprints("\t03: CC1 == OPEN");
		break;
	}
	cc2 = (reg >> 2) & 3;
	switch (cc2) {
	case 0:
		ccprints("\t00: CC2 == Ra");
		break;
	case 1:
		ccprints("\t04: CC2 == Rp");
		break;
	case 2:
		ccprints("\t08: CC2 == Rd");
		break;
	case 3:
		ccprints("\t0C: CC2 == OPEN");
		break;
	}
	switch ((reg >> 4) & 3) {
	case 0:
		ccprints("\t00: Rp Value == default");
		break;
	case 1:
		ccprints("\t10: Rp Value == 1.5A");
		break;
	case 2:
		ccprints("\t20: Rp Value == 3A");
		break;
	}
	if (reg & BIT(6))
		ccprints("\t40: DRP");

	reg = mock_tcpci_get_reg(TCPC_REG_FAULT_CTRL);
	ccprints("TCPC_REG_FAULT_CTRL   = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Vconn Over Current Fault");
	if (reg & BIT(1))
		ccprints("\t02: Vbus OVP Fault");
	if (reg & BIT(2))
		ccprints("\t04: Vbus OCP Fault");
	if (reg & BIT(3))
		ccprints("\t08: Vbus Discharge Fault");
	if (reg & BIT(4))
		ccprints("\t10: Force OFF Vbus");

	reg = mock_tcpci_get_reg(TCPC_REG_POWER_CTRL);
	ccprints("TCPC_REG_POWER_CTRL   = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Enable Vconn");
	if (reg & BIT(1))
		ccprints("\t02: Vconn Power Supported");
	if (reg & BIT(2))
		ccprints("\t04: Force Discharge");
	if (reg & BIT(3))
		ccprints("\t08: Enable Bleed Discharge");
	if (reg & BIT(4))
		ccprints("\t10: Auto Discharge Disconnect");
	if (reg & BIT(5))
		ccprints("\t20: Disable Voltage Alarms");
	if (reg & BIT(6))
		ccprints("\t40: VBUS_VOLTAGE monitor disabled");
	if (reg & BIT(7))
		ccprints("\t80: Fast Role Swap enabled");

	reg = mock_tcpci_get_reg(TCPC_REG_CC_STATUS);
	ccprints("TCPC_REG_CC_STATUS    = 0x%04X", reg);
	switch ((reg >> 0) & 3) {
	case 0:
		switch (cc1) {
		case 1:
			ccprints("\t00: CC1-Rp SRC.Open");
			break;
		case 2:
			ccprints("\t00: CC1-Rd SNK.Open");
			break;
		}
		break;
	case 1:
		switch (cc1) {
		case 1:
			ccprints("\t01: CC1-Rp SRC.Ra");
			break;
		case 2:
			ccprints("\t01: CC1-Rd SNK.Default");
			break;
		}
		break;
	case 2:
		switch (cc1) {
		case 1:
			ccprints("\t02: CC1-Rp SRC.Rd");
			break;
		case 2:
			ccprints("\t02: CC1-Rd SNK.Power1.5");
			break;
		}
		break;
	case 3:
		switch (cc1) {
		case 2:
			ccprints("\t03: CC1-Rd SNK.Power3.0");
			break;
		}
		break;
	}
	switch ((reg >> 2) & 3) {
	case 0:
		switch (cc2) {
		case 1:
			ccprints("\t00: CC2-Rp SRC.Open");
			break;
		case 2:
			ccprints("\t00: CC2-Rd SNK.Open");
			break;
		}
		break;
	case 1:
		switch (cc2) {
		case 1:
			ccprints("\t04: CC2-Rp SRC.Ra");
			break;
		case 2:
			ccprints("\t04: CC2-Rd SNK.Default");
			break;
		}
		break;
	case 2:
		switch (cc2) {
		case 1:
			ccprints("\t08: CC2-Rp SRC.Rd");
			break;
		case 2:
			ccprints("\t08: CC2-Rd SNK.Power1.5");
			break;
		}
		break;
	case 3:
		switch (cc2) {
		case 2:
			ccprints("\t0C: CC2-Rd SNK.Power3.0");
			break;
		}
		break;
	}
	if (reg & BIT(4))
		ccprints("\t10: Presenting Rd");
	else
		ccprints("\t00: Presenting Rp");
	if (reg & BIT(5))
		ccprints("\t20: Looking4Connection");

	reg = mock_tcpci_get_reg(TCPC_REG_POWER_STATUS);
	ccprints("TCPC_REG_POWER_STATUS = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Sinking Vbus");
	if (reg & BIT(1))
		ccprints("\t02: Vconn Present");
	if (reg & BIT(2))
		ccprints("\t04: Vbus Present");
	if (reg & BIT(3))
		ccprints("\t08: Vbus Detect enabled");
	if (reg & BIT(4))
		ccprints("\t10: Sourcing Vbus");
	if (reg & BIT(5))
		ccprints("\t20: Sourcing non-default voltage");
	if (reg & BIT(6))
		ccprints("\t40: TCPC Initialization");
	if (reg & BIT(7))
		ccprints("\t80: Debug Accessory Connected");

	reg = mock_tcpci_get_reg(TCPC_REG_FAULT_STATUS);
	ccprints("TCPC_REG_FAULT_STATUS = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: I2C Interface Error");
	if (reg & BIT(1))
		ccprints("\t02: Vconn Over Current Fault");
	if (reg & BIT(2))
		ccprints("\t04: Vbus OVP Fault");
	if (reg & BIT(3))
		ccprints("\t08: Vbus OCP Fault");
	if (reg & BIT(4))
		ccprints("\t10: Forced Discharge Failed");
	if (reg & BIT(5))
		ccprints("\t20: Auto Discharge Failed");
	if (reg & BIT(6))
		ccprints("\t40: Force OFF Vbus");
	if (reg & BIT(7))
		ccprints("\t80: TCPCI Registers Reset2Default");

	reg = mock_tcpci_get_reg(TCPC_REG_EXT_STATUS);
	ccprints("TCPC_REG_EXT_STATUS   = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Vbus is at vSafe0V");

	reg = mock_tcpci_get_reg(TCPC_REG_ALERT_EXT);
	ccprints("TCPC_REG_ALERT_EXT    = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: SNK Fast Role Swap");
	if (reg & BIT(1))
		ccprints("\t02: SRC Fast Role Swap");
	if (reg & BIT(2))
		ccprints("\t04: Timer Expired");

	reg = mock_tcpci_get_reg(TCPC_REG_COMMAND);
	ccprints("TCPC_REG_COMMAND      = 0x%04X", reg);
	switch (reg) {
	case 0x11:
		ccprints("\t11: WakeI2C");
		break;
	case 0x22:
		ccprints("\t22: DisableVbusDetect");
		break;
	case 0x33:
		ccprints("\t33: EnableVbusDetect");
		break;
	case 0x44:
		ccprints("\t44: DisableSinkVbus");
		break;
	case 0x55:
		ccprints("\t55: SinkVbus");
		break;
	case 0x66:
		ccprints("\t66: DisableSourceVbus");
		break;
	case 0x77:
		ccprints("\t77: SourceVbusDefaultVoltage");
		break;
	case 0x88:
		ccprints("\t88: SourceVbusNondefaultVoltage");
		break;
	case 0x99:
		ccprints("\t99: Looking4Connection");
		break;
	case 0xAA:
		ccprints("\tAA: RxOneMore");
		break;
	case 0xCC:
		ccprints("\tCC: SendFRSwapSignal");
		break;
	case 0xDD:
		ccprints("\tDD: ResetTransmitBuffer");
		break;
	case 0xEE:
		ccprints("\tEE: ResetReceiveBuffer");
		break;
	case 0xFF:
		ccprints("\tFF: I2C Idle");
		break;
	}

	reg = mock_tcpci_get_reg(TCPC_REG_MSG_HDR_INFO);
	ccprints("TCPC_REG_MSG_HDR_INFO = 0x%04X", reg);
	if (reg & BIT(0))
		ccprints("\t01: Power Role SRC");
	else
		ccprints("\t00: Power Role SNK");
	switch ((reg >> 1) & 3) {
	case 0:
		ccprints("\t00: PD Revision 1.0");
		break;
	case 1:
		ccprints("\t02: PD Revision 2.0");
		break;
	case 2:
		ccprints("\t04: PD Revision 3.0");
		break;
	}
	if (reg & BIT(3))
		ccprints("\t08: Data Role DFP");
	else
		ccprints("\t00: Data Role UFP");
	if (reg & BIT(4))
		ccprints("\t10: Message originating from Cable Plug");
	else
		ccprints("\t00: Message originating from SRC/SNK/DRP");

	reg = mock_tcpci_get_reg(TCPC_REG_RX_BUFFER);
	ccprints("TCPC_REG_RX_BUFFER    = 0x%04X", reg);

	reg = mock_tcpci_get_reg(TCPC_REG_TRANSMIT);
	ccprints("TCPC_REG_TRANSMIT     = 0x%04X", reg);
	ccprints("*****************************************");
}
