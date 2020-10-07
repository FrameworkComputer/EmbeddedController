/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"
#include "timer.h"

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
static uint8_t rx_buffer[BUFFER_SIZE];
static int rx_pos = -1;

static const char * const ctrl_msg_name[] = {
	[0]                      = "RSVD-C0",
	[PD_CTRL_GOOD_CRC]       = "GOODCRC",
	[PD_CTRL_GOTO_MIN]       = "GOTOMIN",
	[PD_CTRL_ACCEPT]         = "ACCEPT",
	[PD_CTRL_REJECT]         = "REJECT",
	[PD_CTRL_PING]           = "PING",
	[PD_CTRL_PS_RDY]         = "PSRDY",
	[PD_CTRL_GET_SOURCE_CAP] = "GSRCCAP",
	[PD_CTRL_GET_SINK_CAP]   = "GSNKCAP",
	[PD_CTRL_DR_SWAP]        = "DRSWAP",
	[PD_CTRL_PR_SWAP]        = "PRSWAP",
	[PD_CTRL_VCONN_SWAP]     = "VCONNSW",
	[PD_CTRL_WAIT]           = "WAIT",
	[PD_CTRL_SOFT_RESET]     = "SFT-RST",
	[14]                     = "RSVD-C14",
	[15]                     = "RSVD-C15",
	[PD_CTRL_NOT_SUPPORTED]  = "NOT-SUPPORTED",
	[PD_CTRL_GET_SOURCE_CAP_EXT] = "GSRCCAP-EXT",
	[PD_CTRL_GET_STATUS]     = "GET-STATUS",
	[PD_CTRL_FR_SWAP]        = "FRSWAP",
	[PD_CTRL_GET_PPS_STATUS] = "GET-PPS-STATUS",
	[PD_CTRL_GET_COUNTRY_CODES] = "GET-COUNTRY-CODES",
};

static const char * const data_msg_name[] = {
	[0]                      = "RSVD-D0",
	[PD_DATA_SOURCE_CAP]     = "SRCCAP",
	[PD_DATA_REQUEST]        = "REQUEST",
	[PD_DATA_BIST]           = "BIST",
	[PD_DATA_SINK_CAP]       = "SNKCAP",
	/* 5-14 Reserved */
	[PD_DATA_VENDOR_DEF]     = "VDM",
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
	const char *name = cnt ? data_msg_name[type] : ctrl_msg_name[type];

	ccprints("%s header=0x%x [%s %s %s %s id=%d cnt=%d ext=%d]",
		 prefix, header,
		 name, drole_name[drole], rev_name[rev], prole_name[prole],
		 id, cnt, ext);
}

static int verify_transmit(enum tcpm_transmit_type want_tx_type,
			   int want_tx_retry,
			   enum pd_ctrl_msg_type want_ctrl_msg,
			   enum pd_data_msg_type want_data_msg,
			   int timeout)
{
	uint64_t end_time = get_time().val + timeout;

	TEST_EQ(tcpci_regs[TCPC_REG_TRANSMIT].value, 0, "%d");
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

int verify_tcpci_transmit(enum tcpm_transmit_type tx_type,
			  enum pd_ctrl_msg_type ctrl_msg,
			  enum pd_data_msg_type data_msg)
{
	return verify_transmit(tx_type, -1, ctrl_msg, data_msg, VERIFY_TIMEOUT);
}

int verify_tcpci_tx_timeout(enum tcpm_transmit_type tx_type,
			    enum pd_ctrl_msg_type ctrl_msg,
			    enum pd_data_msg_type data_msg,
			    int timeout)
{
	return verify_transmit(tx_type, -1, ctrl_msg, data_msg, timeout);
}

int verify_tcpci_tx_retry_count(enum tcpm_transmit_type tx_type,
				int retry_count)
{
	return verify_transmit(tx_type, retry_count, 0, 0, VERIFY_TIMEOUT);
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

void mock_tcpci_reset(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tcpci_regs); i++)
		tcpci_regs[i].value = 0;
}

void mock_tcpci_set_reg(int reg_offset, uint16_t value)
{
	struct tcpci_reg *reg = tcpci_regs + reg_offset;

	reg->value = value;
	ccprints("TCPCI mock set %s = 0x%x",  reg->name, reg->value);
}

uint16_t mock_tcpci_get_reg(int reg_offset)
{
	return tcpci_regs[reg_offset].value;
}

int tcpci_i2c_xfer(int port, uint16_t slave_addr_flags,
		const uint8_t *out, int out_size,
		uint8_t *in, int in_size, int flags)
{
	struct tcpci_reg *reg;

	if (port != I2C_PORT_HOST_TCPC) {
		ccprints("ERROR: wrong I2C port %d", port);
		return EC_ERROR_UNKNOWN;
	}
	if (slave_addr_flags != MOCK_TCPCI_I2C_ADDR_FLAGS) {
		ccprints("ERROR: wrong I2C address 0x%x", slave_addr_flags);
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
		if (tx_pos > 0 && tx_pos == tx_buffer[0] + 1) {
			print_header("TX", UINT16_FROM_BYTE_ARRAY_LE(
						tx_buffer, 1));
			tx_pos = -1;
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
