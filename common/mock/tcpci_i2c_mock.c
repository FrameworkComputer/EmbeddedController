/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"

struct tcpci_reg {
	const char	*name;
	uint8_t		size;
	uint16_t	value;
};

#define TCPCI_REG(reg_name, reg_size)	\
	[reg_name] = { .name = #reg_name, .size = (reg_size) }

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
	TCPCI_REG(TCPC_REG_ALERT_EXT, 1),
	TCPCI_REG(TCPC_REG_DEV_CAP_1, 2),
	TCPCI_REG(TCPC_REG_DEV_CAP_2, 2),
	TCPCI_REG(TCPC_REG_STD_INPUT_CAP, 1),
	TCPCI_REG(TCPC_REG_STD_OUTPUT_CAP, 1),
	TCPCI_REG(TCPC_REG_CONFIG_EXT_1, 1),
	TCPCI_REG(TCPC_REG_MSG_HDR_INFO, 1),
	TCPCI_REG(TCPC_REG_RX_DETECT, 1),
	TCPCI_REG(TCPC_REG_RX_BYTE_CNT, 1),
	TCPCI_REG(TCPC_REG_RX_BUF_FRAME_TYPE, 1),
	TCPCI_REG(TCPC_REG_TRANSMIT, 1),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE, 2),
	TCPCI_REG(TCPC_REG_VBUS_SINK_DISCONNECT_THRESH, 2),
	TCPCI_REG(TCPC_REG_VBUS_STOP_DISCHARGE_THRESH, 2),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG, 2),
	TCPCI_REG(TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG, 2),
	TCPCI_REG(TCPC_REG_COMMAND, 1),
};

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

	if (out_size == 0) {
		ccprints("ERROR: out_size == 0");
		return EC_ERROR_UNKNOWN;
	}
	reg = tcpci_regs + *out;
	if (*out >= ARRAY_SIZE(tcpci_regs) || reg->size == 0) {
		ccprints("ERROR: unknown reg 0x%x", *out);
		return EC_ERROR_UNKNOWN;
	}
	if (out_size == 1) {
		if (in_size != reg->size) {
			ccprints("ERROR: in_size != %d", reg->size);
			return EC_ERROR_UNKNOWN;
		}
		ccprints("%s TCPCI read %s = 0x%x",
			 task_get_name(task_get_current()),
			 reg->name, reg->value);
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
		reg->value = value;
	}
	return EC_SUCCESS;
}
DECLARE_TEST_I2C_XFER(tcpci_i2c_xfer);
