/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_tcpci_emul

#include <logging/log.h>
LOG_MODULE_REGISTER(tcpci_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

#include <device.h>
#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <drivers/gpio/gpio_emul.h>

#include "tcpm/tcpci.h"

#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"

#define TCPCI_DATA_FROM_I2C_EMUL(_emul)					     \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct tcpci_emul_data, common)

/**
 * Number of emulated register. This include vendor registers defined in TCPCI
 * specification
 */
#define TCPCI_EMUL_REG_COUNT		0x100


/** Run-time data used by the emulator */
struct tcpci_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Current state of all emulated TCPCI registers */
	uint8_t reg[TCPCI_EMUL_REG_COUNT];

	/** Structures representing TX and RX buffers */
	struct tcpci_emul_msg *rx_msg;
	struct tcpci_emul_msg *tx_msg;

	/** Data that should be written to register (except TX_BUFFER) */
	uint16_t write_data;

	/** Return error when trying to write to RO register */
	bool error_on_ro_write;
	/** Return error when trying to write 1 to reserved bit */
	bool error_on_rsvd_write;

	/** User function called when alert line could change */
	tcpci_emul_alert_state_func alert_callback;
	/** Data passed to alert_callback */
	void *alert_callback_data;

	/** Callbacks for specific TCPCI device emulator */
	struct tcpci_emul_dev_ops *dev_ops;
	/** Callbacks for TCPCI partner */
	const struct tcpci_emul_partner_ops *partner;

	/** Reference to Alert# GPIO emulator. */
	const struct device *alert_gpio_port;
	gpio_pin_t alert_gpio_pin;
};

/**
 * @brief Returns number of bytes in specific register
 *
 * @param reg Register address
 *
 * @return Number of bytes
 */
static int tcpci_emul_reg_bytes(int reg)
{

	switch (reg) {
	case TCPC_REG_VENDOR_ID:
	case TCPC_REG_PRODUCT_ID:
	case TCPC_REG_BCD_DEV:
	case TCPC_REG_TC_REV:
	case TCPC_REG_PD_REV:
	case TCPC_REG_PD_INT_REV:
	case TCPC_REG_ALERT:
	case TCPC_REG_ALERT_MASK:
	case TCPC_REG_DEV_CAP_1:
	case TCPC_REG_DEV_CAP_2:
	case TCPC_REG_GENERIC_TIMER:
	case TCPC_REG_VBUS_VOLTAGE:
	case TCPC_REG_VBUS_SINK_DISCONNECT_THRESH:
	case TCPC_REG_VBUS_STOP_DISCHARGE_THRESH:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG:
	case TCPC_REG_VBUS_NONDEFAULT_TARGET:
		return 2;
	}

	/* Assume that other registers are one byte */
	return 1;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_set_reg(const struct emul *emul, int reg, uint16_t val)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t update_alert = 0;
	uint16_t alert;
	int byte;

	if (reg < 0 || reg > TCPCI_EMUL_REG_COUNT) {
		return -EINVAL;
	}

	/* Changing some registers has impact on alert register */
	switch (reg) {
	case TCPC_REG_POWER_STATUS:
		update_alert = TCPC_REG_ALERT_POWER_STATUS;
		break;
	case TCPC_REG_FAULT_STATUS:
		update_alert = TCPC_REG_ALERT_FAULT;
		break;
	case TCPC_REG_EXT_STATUS:
		update_alert = TCPC_REG_ALERT_EXT_STATUS;
		break;
	case TCPC_REG_ALERT_EXT:
		update_alert = TCPC_REG_ALERT_ALERT_EXT;
		break;
	}

	if (update_alert != 0) {
		tcpci_emul_get_reg(emul, TCPC_REG_ALERT, &alert);
		tcpci_emul_set_reg(emul, TCPC_REG_ALERT, alert | update_alert);
	}

	for (byte = tcpci_emul_reg_bytes(reg); byte > 0; byte--) {
		data->reg[reg] = val & 0xff;
		val >>= 8;
		reg++;
	}

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_get_reg(const struct emul *emul, int reg, uint16_t *val)
{
	struct tcpci_emul_data *data = emul->data;
	int byte;

	if (reg < 0 || reg > TCPCI_EMUL_REG_COUNT || val == NULL) {
		return -EINVAL;
	}

	*val = 0;

	byte = tcpci_emul_reg_bytes(reg);
	for (byte -= 1; byte >= 0; byte--) {
		*val <<= 8;
		*val |= data->reg[reg + byte];
	}

	return 0;
}

/**
 * @brief Check if alert line should be active based on alert registers and
 *        masks
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return State of alert line
 */
static bool tcpci_emul_check_int(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t alert_mask;
	uint16_t alert;

	tcpci_emul_get_reg(emul, TCPC_REG_ALERT, &alert);
	tcpci_emul_get_reg(emul, TCPC_REG_ALERT_MASK, &alert_mask);

	/*
	 * For nested interrupts alert group bit and alert register bit has to
	 * be unmasked
	 */
	if (alert & alert_mask & TCPC_REG_ALERT_ALERT_EXT &&
	    data->reg[TCPC_REG_ALERT_EXT] &
	    data->reg[TCPC_REG_ALERT_EXTENDED_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_EXT_STATUS &&
	    data->reg[TCPC_REG_EXT_STATUS] &
	    data->reg[TCPC_REG_EXT_STATUS_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_FAULT &&
	    data->reg[TCPC_REG_FAULT_STATUS] &
	    data->reg[TCPC_REG_FAULT_STATUS_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_POWER_STATUS &&
	    data->reg[TCPC_REG_POWER_STATUS] &
	    data->reg[TCPC_REG_POWER_STATUS_MASK]) {
		return true;
	}

	/* Nested alerts are handled above */
	alert &= ~(TCPC_REG_ALERT_ALERT_EXT | TCPC_REG_ALERT_EXT_STATUS |
		   TCPC_REG_ALERT_FAULT | TCPC_REG_ALERT_POWER_STATUS);
	if (alert & alert_mask) {
		return true;
	}

	return false;
}

/**
 * @brief If alert callback is provided, call it with current alert line state
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return 0 for success, or non-0 for errors.
 */
static int tcpci_emul_alert_changed(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	int rc;
	bool alert_is_active = tcpci_emul_check_int(emul);

	/** Trigger GPIO. */
	if (data->alert_gpio_port != NULL) {
		/* Triggers on edge falling, so set to 0 when there is an alert.
		 */
		rc = gpio_emul_input_set(data->alert_gpio_port,
					 data->alert_gpio_pin,
					 alert_is_active ? 0 : 1);
		if (rc != 0)
			return rc;
	}

	/* Nothing to do */
	if (data->alert_callback == NULL) {
		return 0;
	}

	data->alert_callback(emul, alert_is_active,
			     data->alert_callback_data);
	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_add_rx_msg(const struct emul *emul,
			  struct tcpci_emul_msg *rx_msg, bool alert)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t dev_cap_2;
	int rc;

	if (data->rx_msg == NULL) {
		tcpci_emul_get_reg(emul, TCPC_REG_DEV_CAP_2, &dev_cap_2);
		if ((!(dev_cap_2 & TCPC_REG_DEV_CAP_2_LONG_MSG) &&
		       rx_msg->cnt > 31) || rx_msg->cnt > 265) {
			LOG_ERR("Too long first message (%d)", rx_msg->cnt);
			return -EINVAL;
		}

		data->rx_msg = rx_msg;
	} else if (data->rx_msg->next == NULL) {
		if (rx_msg->cnt > 31) {
			LOG_ERR("Too long second message (%d)", rx_msg->cnt);
			return -EINVAL;
		}

		data->rx_msg->next = rx_msg;
		if (alert) {
			data->reg[TCPC_REG_ALERT + 1] |=
				TCPC_REG_ALERT_RX_BUF_OVF >> 8;
		}
	} else {
		LOG_ERR("Cannot setup third message");
		return -EINVAL;
	}

	if (alert) {
		if (rx_msg->cnt > 133) {
			data->reg[TCPC_REG_ALERT + 1] |=
				TCPC_REG_ALERT_RX_BEGINNING >> 8;
		}

		data->reg[TCPC_REG_ALERT] |= TCPC_REG_ALERT_RX_STATUS;

		rc = tcpci_emul_alert_changed(emul);
		if (rc != 0)
			return rc;
	}

	rx_msg->next = NULL;
	rx_msg->idx = 0;

	return 0;
}

/** Check description in emul_tcpci.h */
struct tcpci_emul_msg *tcpci_emul_get_tx_msg(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;

	return data->tx_msg;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_rev(const struct emul *emul, enum tcpci_emul_rev rev)
{
	switch (rev) {
	case TCPCI_EMUL_REV1_0_VER1_0:
		tcpci_emul_set_reg(emul, TCPC_REG_PD_INT_REV,
				   (TCPC_REG_PD_INT_REV_REV_1_0 << 8) |
				    TCPC_REG_PD_INT_REV_VER_1_0);
		return;
	case TCPCI_EMUL_REV2_0_VER1_1:
		tcpci_emul_set_reg(emul, TCPC_REG_PD_INT_REV,
				   (TCPC_REG_PD_INT_REV_REV_2_0 << 8) |
				    TCPC_REG_PD_INT_REV_VER_1_1);
		return;
	}
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_dev_ops(const struct emul *emul,
			    struct tcpci_emul_dev_ops *dev_ops)
{
	struct tcpci_emul_data *data = emul->data;

	data->dev_ops = dev_ops;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_alert_callback(const struct emul *emul,
				   tcpci_emul_alert_state_func alert_callback,
				   void *alert_callback_data)
{
	struct tcpci_emul_data *data = emul->data;

	data->alert_callback = alert_callback;
	data->alert_callback_data = alert_callback_data;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_partner_ops(const struct emul *emul,
				const struct tcpci_emul_partner_ops *partner)
{
	struct tcpci_emul_data *data = emul->data;

	data->partner = partner;
}

/**
 * @brief Get detected voltage for given CC resistor
 *
 * @param res CC pull resistor value
 * @param volt Voltage applied by port partner
 *
 * @return Voltage visible at CC resistor side
 */
static enum tcpc_cc_voltage_status tcpci_emul_detected_volt_for_res(
		enum tcpc_cc_pull res,
		enum tcpc_cc_voltage_status volt)
{
	switch (res) {
	case TYPEC_CC_RD:
		switch (volt) {
		/* As Rd we cannot detect another Rd or Ra */
		case TYPEC_CC_VOLT_RA:
		case TYPEC_CC_VOLT_RD:
			return TYPEC_CC_VOLT_OPEN;
		default:
			return volt;
		}
	case TYPEC_CC_RP:
		switch (volt) {
		/* As Rp we cannot detect another Rp */
		case TYPEC_CC_VOLT_RP_DEF:
		case TYPEC_CC_VOLT_RP_1_5:
		case TYPEC_CC_VOLT_RP_3_0:
			return TYPEC_CC_VOLT_OPEN;
		default:
			return volt;
		}
	default:
		/* As Ra or open we cannot detect anything */
		return TYPEC_CC_VOLT_OPEN;
	}
}

/** Check description in emul_tcpci.h */
int tcpci_emul_connect_partner(const struct emul *emul,
			       enum pd_power_role partner_power_role,
			       enum tcpc_cc_voltage_status partner_cc1,
			       enum tcpc_cc_voltage_status partner_cc2,
			       enum tcpc_cc_polarity polarity)
{
	uint16_t cc_status, alert, role_ctrl, power_status;
	enum tcpc_cc_voltage_status cc1_v, cc2_v;
	enum tcpc_cc_pull cc1_r, cc2_r;

	if (polarity == POLARITY_CC1) {
		cc1_v = partner_cc1;
		cc2_v = partner_cc2;
	} else {
		cc1_v = partner_cc2;
		cc2_v = partner_cc1;
	}

	tcpci_emul_get_reg(emul, TCPC_REG_CC_STATUS, &cc_status);
	if (TCPC_REG_CC_STATUS_LOOK4CONNECTION(cc_status)) {
		/* Change resistors values in case of DRP toggling */
		if (partner_power_role == PD_ROLE_SOURCE) {
			/* TCPCI is sink */
			cc1_r = TYPEC_CC_RD;
			cc2_r = TYPEC_CC_RD;
		} else {
			/* TCPCI is src */
			cc1_r = TYPEC_CC_RP;
			cc2_r = TYPEC_CC_RP;
		}
	} else {
		/* Use role control resistors values otherwise */
		tcpci_emul_get_reg(emul, TCPC_REG_ROLE_CTRL, &role_ctrl);
		cc1_r = TCPC_REG_ROLE_CTRL_CC1(role_ctrl);
		cc2_r = TCPC_REG_ROLE_CTRL_CC2(role_ctrl);
	}

	cc1_v = tcpci_emul_detected_volt_for_res(cc1_r, cc1_v);
	cc2_v = tcpci_emul_detected_volt_for_res(cc2_r, cc2_v);

	/* If CC status is TYPEC_CC_VOLT_RP_*, then BIT(2) is ignored */
	cc_status = TCPC_REG_CC_STATUS_SET(
				partner_power_role == PD_ROLE_SOURCE ? 1 : 0,
				cc2_v, cc1_v);
	tcpci_emul_set_reg(emul, TCPC_REG_CC_STATUS, cc_status);
	tcpci_emul_get_reg(emul, TCPC_REG_ALERT, &alert);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT,
			   alert | TCPC_REG_ALERT_CC_STATUS);

	if (partner_power_role == PD_ROLE_SOURCE) {
		tcpci_emul_get_reg(emul, TCPC_REG_POWER_STATUS, &power_status);
		if (power_status & TCPC_REG_POWER_STATUS_VBUS_DET) {
			/*
			 * Set TCPCI emulator VBUS to present (connected,
			 * above 4V) only if VBUS detection is enabled
			 */
			tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
					   TCPC_REG_POWER_STATUS_VBUS_PRES |
					   power_status);
		}
	}

	tcpci_emul_alert_changed(emul);

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_disconnect_partner(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t power_status;
	uint16_t val;
	uint16_t term;
	int rc;

	data->partner = NULL;
	/* Set both CC lines to open to indicate disconnect. */
	rc = tcpci_emul_get_reg(emul, TCPC_REG_CC_STATUS, &val);
	if (rc != 0)
		return rc;

	term = TCPC_REG_CC_STATUS_TERM(val);

	rc = tcpci_emul_set_reg(emul, TCPC_REG_CC_STATUS,
				TCPC_REG_CC_STATUS_SET(term, TYPEC_CC_VOLT_OPEN,
						       TYPEC_CC_VOLT_OPEN));
	if (rc != 0)
		return rc;

	data->reg[TCPC_REG_ALERT] |= TCPC_REG_ALERT_CC_STATUS;
	rc = tcpci_emul_alert_changed(emul);
	if (rc != 0)
		return rc;
	/* TODO: Wait until DisableSourceVbus (TCPC_REG_COMMAND_SRC_CTRL_LOW?),
	 * and then set VBUS present = 0 and vSafe0V = 1 after appropriate
	 * delays.
	 */

	/* Clear VBUS present in case if source partner is disconnected */
	tcpci_emul_get_reg(emul, TCPC_REG_POWER_STATUS, &power_status);
	if (power_status & TCPC_REG_POWER_STATUS_VBUS_PRES) {
		power_status &= ~TCPC_REG_POWER_STATUS_VBUS_PRES;
		tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS, power_status);
	}

	return 0;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_partner_msg_status(const struct emul *emul,
				   enum tcpci_emul_tx_status status)
{
	uint16_t alert;
	uint16_t tx_status_alert;

	switch (status) {
	case TCPCI_EMUL_TX_SUCCESS:
		tx_status_alert = TCPC_REG_ALERT_TX_SUCCESS;
		break;
	case TCPCI_EMUL_TX_DISCARDED:
		tx_status_alert = TCPC_REG_ALERT_TX_DISCARDED;
		break;
	case TCPCI_EMUL_TX_FAILED:
		tx_status_alert = TCPC_REG_ALERT_TX_FAILED;
		break;
	default:
		__ASSERT(0, "Invalid partner TX status 0x%x", status);
		return;
	}

	tcpci_emul_get_reg(emul, TCPC_REG_ALERT, &alert);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, alert | tx_status_alert);
	tcpci_emul_alert_changed(emul);
}

/** Mask reserved bits in each register of TCPCI */
static const uint8_t tcpci_emul_rsvd_mask[] = {
	[TCPC_REG_VENDOR_ID]				= 0x00,
	[TCPC_REG_VENDOR_ID + 1]			= 0x00,
	[TCPC_REG_PRODUCT_ID]				= 0x00,
	[TCPC_REG_PRODUCT_ID + 1]			= 0x00,
	[TCPC_REG_BCD_DEV]				= 0x00,
	[TCPC_REG_BCD_DEV + 1]				= 0xff,
	[TCPC_REG_TC_REV]				= 0x00,
	[TCPC_REG_TC_REV + 1]				= 0x00,
	[TCPC_REG_PD_REV]				= 0x00,
	[TCPC_REG_PD_REV + 1]				= 0x00,
	[TCPC_REG_PD_INT_REV]				= 0x00,
	[TCPC_REG_PD_INT_REV + 1]			= 0x00,
	[0x0c ... 0x0f]					= 0xff, /* Reserved */
	[TCPC_REG_ALERT]				= 0x00,
	[TCPC_REG_ALERT + 1]				= 0x00,
	[TCPC_REG_ALERT_MASK]				= 0x00,
	[TCPC_REG_ALERT_MASK + 1]			= 0x00,
	[TCPC_REG_POWER_STATUS_MASK]			= 0x00,
	[TCPC_REG_FAULT_STATUS_MASK]			= 0x00,
	[TCPC_REG_EXT_STATUS_MASK]			= 0xfe,
	[TCPC_REG_ALERT_EXTENDED_MASK]			= 0xf8,
	[TCPC_REG_CONFIG_STD_OUTPUT]			= 0x00,
	[TCPC_REG_TCPC_CTRL]				= 0x00,
	[TCPC_REG_ROLE_CTRL]				= 0x80,
	[TCPC_REG_FAULT_CTRL]				= 0x80,
	[TCPC_REG_POWER_CTRL]				= 0x00,
	[TCPC_REG_CC_STATUS]				= 0xc0,
	[TCPC_REG_POWER_STATUS]				= 0x00,
	[TCPC_REG_FAULT_STATUS]				= 0x00,
	[TCPC_REG_EXT_STATUS]				= 0xfe,
	[TCPC_REG_ALERT_EXT]				= 0xf8,
	[0x22]						= 0xff, /* Reserved */
	[TCPC_REG_COMMAND]				= 0x00,
	[TCPC_REG_DEV_CAP_1]				= 0x00,
	[TCPC_REG_DEV_CAP_1 + 1]			= 0x00,
	[TCPC_REG_DEV_CAP_2]				= 0x80,
	[TCPC_REG_DEV_CAP_2 + 1]			= 0x00,
	[TCPC_REG_STD_INPUT_CAP]			= 0xe0,
	[TCPC_REG_STD_OUTPUT_CAP]			= 0x00,
	[TCPC_REG_CONFIG_EXT_1]				= 0xfc,
	[0x2b]						= 0xff, /* Reserved */
	[TCPC_REG_GENERIC_TIMER]			= 0x00,
	[TCPC_REG_GENERIC_TIMER + 1]			= 0x00,
	[TCPC_REG_MSG_HDR_INFO]				= 0xe0,
	[TCPC_REG_RX_DETECT]				= 0x00,
	[TCPC_REG_RX_BUFFER ... 0x4f]			= 0x00,
	[TCPC_REG_TRANSMIT ... 0x69]			= 0x00,
	[TCPC_REG_VBUS_VOLTAGE]				= 0xf0,
	[TCPC_REG_VBUS_VOLTAGE + 1]			= 0x00,
	[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH]		= 0x00,
	[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH + 1]	= 0xfc,
	[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH]		= 0x00,
	[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH + 1]	= 0xfc,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG]		= 0x00,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG + 1]	= 0xfc,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG]		= 0x00,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG + 1]	= 0xfc,
	[TCPC_REG_VBUS_NONDEFAULT_TARGET]		= 0x00,
	[TCPC_REG_VBUS_NONDEFAULT_TARGET + 1]		= 0x00,
	[0x7c ... 0x7f]					= 0xff, /* Reserved */
	[0x80 ... TCPCI_EMUL_REG_COUNT - 1]		= 0x00,
};


/**
 * @brief Reset role control and header info registers to default values.
 *
 * @param emul Pointer to TCPCI emulator
 */
static void tcpci_emul_reset_role_ctrl(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t dev_cap_1;

	tcpci_emul_get_reg(emul, TCPC_REG_DEV_CAP_1, &dev_cap_1);
	switch (dev_cap_1 & TCPC_REG_DEV_CAP_1_PWRROLE_MASK) {
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_OR_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK_ACC:
		data->reg[TCPC_REG_ROLE_CTRL]			= 0x0a;
		data->reg[TCPC_REG_MSG_HDR_INFO]		= 0x04;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC:
		/* Dead batter */
		data->reg[TCPC_REG_ROLE_CTRL]			= 0x05;
		data->reg[TCPC_REG_MSG_HDR_INFO]		= 0x0d;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_DRP:
		/* Dead batter and dbg acc ind */
		data->reg[TCPC_REG_ROLE_CTRL]			= 0x4a;
		data->reg[TCPC_REG_MSG_HDR_INFO]		= 0x04;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP_ADPT_CBL:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP:
		/* Dead batter and dbg acc ind */
		data->reg[TCPC_REG_ROLE_CTRL]			= 0x4a;
		data->reg[TCPC_REG_MSG_HDR_INFO]		= 0x04;
		break;
	}
}

/**
 * @brief Reset registers to default values. Vendor and reserved registers
 *        are not changed.
 *
 * @param emul Pointer to TCPCI emulator
 * @return 0 if successful
 */
static int tcpci_emul_reset(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;

	data->reg[TCPC_REG_ALERT]				= 0x00;
	data->reg[TCPC_REG_ALERT + 1]				= 0x00;
	data->reg[TCPC_REG_ALERT_MASK]				= 0xff;
	data->reg[TCPC_REG_ALERT_MASK + 1]			= 0x7f;
	data->reg[TCPC_REG_POWER_STATUS_MASK]			= 0xff;
	data->reg[TCPC_REG_FAULT_STATUS_MASK]			= 0xff;
	data->reg[TCPC_REG_EXT_STATUS_MASK]			= 0x01;
	data->reg[TCPC_REG_ALERT_EXTENDED_MASK]			= 0x07;
	data->reg[TCPC_REG_CONFIG_STD_OUTPUT]			= 0x60;
	data->reg[TCPC_REG_TCPC_CTRL]				= 0x00;
	data->reg[TCPC_REG_FAULT_CTRL]				= 0x00;
	data->reg[TCPC_REG_POWER_CTRL]				= 0x60;
	data->reg[TCPC_REG_CC_STATUS]				= 0x00;
	data->reg[TCPC_REG_POWER_STATUS]			= 0x08;
	data->reg[TCPC_REG_FAULT_STATUS]			= 0x80;
	data->reg[TCPC_REG_EXT_STATUS]				= 0x00;
	data->reg[TCPC_REG_ALERT_EXT]				= 0x00;
	data->reg[TCPC_REG_COMMAND]				= 0x00;
	data->reg[TCPC_REG_CONFIG_EXT_1]			= 0x00;
	data->reg[TCPC_REG_GENERIC_TIMER]			= 0x00;
	data->reg[TCPC_REG_GENERIC_TIMER + 1]			= 0x00;
	data->reg[TCPC_REG_RX_DETECT]				= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE]			= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE + 1]			= 0x00;
	data->reg[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH]		= 0x8c;
	data->reg[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH + 1]	= 0x00;
	data->reg[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH]		= 0x20;
	data->reg[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH + 1]	= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG]		= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG + 1]	= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG]		= 0x00;
	data->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG + 1]	= 0x00;
	data->reg[TCPC_REG_VBUS_NONDEFAULT_TARGET]		= 0x00;
	data->reg[TCPC_REG_VBUS_NONDEFAULT_TARGET + 1]		= 0x00;

	tcpci_emul_reset_role_ctrl(emul);

	if (data->dev_ops && data->dev_ops->reset) {
		data->dev_ops->reset(emul, data->dev_ops);
	}

	return tcpci_emul_alert_changed(emul);
}

/**
 * @brief Set alert and fault registers to indicate i2c interface fault
 *
 * @param emul Pointer to TCPCI emulator
 * @return 0 if successful
 */
static int tcpci_emul_set_i2c_interface_err(const struct emul *emul)
{
	uint16_t fault_status;

	tcpci_emul_get_reg(emul, TCPC_REG_FAULT_STATUS, &fault_status);
	fault_status |= TCPC_REG_FAULT_STATUS_I2C_INTERFACE_ERR;
	tcpci_emul_set_reg(emul, TCPC_REG_FAULT_STATUS, fault_status);

	return tcpci_emul_alert_changed(emul);
}

/**
 * @brief Handle read from RX buffer registers for TCPCI rev 1.0 and rev 2.0
 *
 * @param emul Pointer to TCPCI emulator
 * @param reg First byte of last i2c write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readded
 *
 * @return 0 on success
 * @return -EIO invalid read request
 */
static int tcpci_emul_handle_rx_buf(const struct emul *emul, int reg,
				    uint8_t *val, int bytes)
{
	struct tcpci_emul_data *data = emul->data;
	int is_rev1;

	is_rev1 = data->reg[TCPC_REG_PD_INT_REV] == TCPC_REG_PD_INT_REV_REV_1_0;

	if (!is_rev1 && reg != TCPC_REG_RX_BUFFER) {
		LOG_ERR("Register 0x%x defined only for revision 1.0", reg);
		tcpci_emul_set_i2c_interface_err(emul);
		return -EIO;
	}

	switch (reg) {
	case TCPC_REG_RX_BUFFER:
		if (data->rx_msg == NULL) {
			if (bytes < 2) {
				*val = 0;
			} else {
				LOG_ERR("Accessing RX buffer with no msg");
				tcpci_emul_set_i2c_interface_err(emul);
				return -EIO;
			}
			return 0;
		}
		if (bytes == 0) {
			*val = data->rx_msg->cnt;
		} else if (is_rev1) {
			LOG_ERR("Revision 1.0 has only byte count at 0x30");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		} else if (bytes == 1) {
			*val = data->rx_msg->type;
		} else if (data->rx_msg->idx < data->rx_msg->cnt) {
			*val = data->rx_msg->buf[data->rx_msg->idx];
			data->rx_msg->idx++;
		} else {
			LOG_ERR("Reading past RX buffer");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		break;

	case TCPC_REG_RX_BUF_FRAME_TYPE:
		if (bytes != 0) {
			LOG_ERR("Reading byte %d from 1 byte register 0x%x",
				bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		if (data->rx_msg == NULL) {
			*val = 0;
		} else {
			*val = data->rx_msg->type;
		}
		break;

	case TCPC_REG_RX_HDR:
		if (bytes > 1) {
			LOG_ERR("Reading byte %d from 2 byte register 0x%x",
				bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		if (data->rx_msg == NULL) {
			LOG_ERR("Accessing RX buffer with no msg");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		*val = data->rx_msg->buf[bytes];
		break;

	case TCPC_REG_RX_DATA:
		if (data->rx_msg == NULL) {
			LOG_ERR("Accessing RX buffer with no msg");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		if (bytes < data->rx_msg->cnt - 2) {
			/* rx_msg cnt include two bytes of header */
			*val = data->rx_msg->buf[bytes + 2];
			data->rx_msg->idx++;
		} else {
			LOG_ERR("Reading past RX buffer");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		break;
	}

	return 0;
}

/**
 * @brief Function called for each byte of read message
 *
 * @param i2c_emul Pointer to TCPCI emulator
 * @param reg First byte of last write message
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already readded
 *
 * @return 0 on success
 */
static int tcpci_emul_read_byte(struct i2c_emul *i2c_emul, int reg,
				uint8_t *val, int bytes)
{
	struct tcpci_emul_data *data;
	const struct emul *emul;

	emul = i2c_emul->parent;
	data = TCPCI_DATA_FROM_I2C_EMUL(i2c_emul);

	LOG_DBG("TCPCI 0x%x: read reg 0x%x", i2c_emul->addr, reg);

	if (data->dev_ops && data->dev_ops->read_byte) {
		switch (data->dev_ops->read_byte(emul, data->dev_ops, reg, val,
						 bytes)) {
		case TCPCI_EMUL_CONTINUE:
			break;
		case TCPCI_EMUL_DONE:
			return 0;
		case TCPCI_EMUL_ERROR:
		default:
			return -EIO;
		}
	}

	switch (reg) {
	/* 16 bits values */
	case TCPC_REG_VENDOR_ID:
	case TCPC_REG_PRODUCT_ID:
	case TCPC_REG_BCD_DEV:
	case TCPC_REG_TC_REV:
	case TCPC_REG_PD_REV:
	case TCPC_REG_PD_INT_REV:
	case TCPC_REG_ALERT:
	case TCPC_REG_ALERT_MASK:
	case TCPC_REG_DEV_CAP_1:
	case TCPC_REG_DEV_CAP_2:
	case TCPC_REG_VBUS_VOLTAGE:
	case TCPC_REG_VBUS_SINK_DISCONNECT_THRESH:
	case TCPC_REG_VBUS_STOP_DISCHARGE_THRESH:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG:
	case TCPC_REG_VBUS_NONDEFAULT_TARGET:
		if (bytes > 1) {
			LOG_ERR("Reading byte %d from 2 byte register 0x%x",
				bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		*val = data->reg[reg + bytes];
		break;

	/* 8 bits values */
	case TCPC_REG_POWER_STATUS_MASK:
	case TCPC_REG_FAULT_STATUS_MASK:
	case TCPC_REG_EXT_STATUS_MASK:
	case TCPC_REG_ALERT_EXTENDED_MASK:
	case TCPC_REG_CONFIG_STD_OUTPUT:
	case TCPC_REG_TCPC_CTRL:
	case TCPC_REG_ROLE_CTRL:
	case TCPC_REG_FAULT_CTRL:
	case TCPC_REG_POWER_CTRL:
	case TCPC_REG_CC_STATUS:
	case TCPC_REG_POWER_STATUS:
	case TCPC_REG_FAULT_STATUS:
	case TCPC_REG_EXT_STATUS:
	case TCPC_REG_ALERT_EXT:
	case TCPC_REG_STD_INPUT_CAP:
	case TCPC_REG_STD_OUTPUT_CAP:
	case TCPC_REG_CONFIG_EXT_1:
	case TCPC_REG_MSG_HDR_INFO:
	case TCPC_REG_RX_DETECT:
		if (bytes != 0) {
			LOG_ERR("Reading byte %d from 1 byte register 0x%x",
				bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		*val = data->reg[reg];
		break;

	case TCPC_REG_RX_BUFFER:
	case TCPC_REG_RX_BUF_FRAME_TYPE:
	case TCPC_REG_RX_HDR:
	case TCPC_REG_RX_DATA:
		return tcpci_emul_handle_rx_buf(emul, reg, val, bytes);

	default:
		LOG_ERR("Reading from reg 0x%x which is WO or undefined", reg);
		tcpci_emul_set_i2c_interface_err(emul);
		return -EIO;
	}

	return 0;
}

/**
 * @brief Function called for each byte of write message. Data are stored
 *        in write_data field of tcpci_emul_data or in tx_msg in case of
 *        writing to TX buffer.
 *
 * @param i2c_emul Pointer to TCPCI emulator
 * @param reg First byte of write message
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 * @return -EIO on invalid write to TX buffer
 */
static int tcpci_emul_write_byte(struct i2c_emul *i2c_emul, int reg,
				 uint8_t val, int bytes)
{
	struct tcpci_emul_data *data;
	const struct emul *emul;
	int is_rev1;

	emul = i2c_emul->parent;
	data = TCPCI_DATA_FROM_I2C_EMUL(i2c_emul);

	if (data->dev_ops && data->dev_ops->write_byte) {
		switch (data->dev_ops->write_byte(emul, data->dev_ops, reg, val,
						  bytes)) {
		case TCPCI_EMUL_CONTINUE:
			break;
		case TCPCI_EMUL_DONE:
			return 0;
		case TCPCI_EMUL_ERROR:
		default:
			return -EIO;
		}
	}

	is_rev1 = data->reg[TCPC_REG_PD_INT_REV] == TCPC_REG_PD_INT_REV_REV_1_0;
	switch (reg) {
	case TCPC_REG_TX_BUFFER:
		if (is_rev1) {
			if (bytes > 1) {
				LOG_ERR("Rev 1.0 has only byte count at 0x51");
				tcpci_emul_set_i2c_interface_err(emul);
				return -EIO;
			}
			data->tx_msg->idx = val;
		}

		if (bytes == 1) {
			data->tx_msg->cnt = val;
		} else {
			if (data->tx_msg->cnt > 0) {
				data->tx_msg->cnt--;
				data->tx_msg->buf[data->tx_msg->idx] = val;
				data->tx_msg->idx++;
			} else {
				LOG_ERR("Writing past TX buffer");
				tcpci_emul_set_i2c_interface_err(emul);
				return -EIO;
			}
		}

		return 0;

	case TCPC_REG_TX_DATA:
		if (!is_rev1) {
			LOG_ERR("Register 0x%x defined only for revision 1.0",
				reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}

		/* Skip header and account reg byte */
		bytes += 2 - 1;

		if (bytes > 29) {
			LOG_ERR("Writing past TX buffer");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		data->tx_msg->buf[bytes] = val;
		return 0;

	case TCPC_REG_TX_HDR:
		if (!is_rev1) {
			LOG_ERR("Register 0x%x defined only for revision 1.0",
				reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}

		/* Account reg byte */
		bytes -= 1;

		if (bytes > 1) {
			LOG_ERR("Writing byte %d to 2 byte register 0x%x",
				 bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		data->tx_msg->buf[bytes] = val;
		return 0;
	}

	if (bytes == 1) {
		data->write_data = val;
	} else if (bytes == 2) {
		data->write_data |= (uint16_t)val << 8;
	}

	return 0;
}

/**
 * @brief Handle writes to command register
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return 0 on success
 * @return -EIO on unknown command value
 */
static int tcpci_emul_handle_command(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	uint16_t role_ctrl;
	uint16_t pwr_ctrl;

	switch (data->write_data & 0xff) {
	case TCPC_REG_COMMAND_RESET_TRANSMIT_BUF:
		data->tx_msg->idx = 0;
		break;
	case TCPC_REG_COMMAND_RESET_RECEIVE_BUF:
		if (data->rx_msg) {
			data->rx_msg->idx = 0;
		}
		break;
	case TCPC_REG_COMMAND_LOOK4CONNECTION:
		tcpci_emul_get_reg(emul, TCPC_REG_ROLE_CTRL, &role_ctrl);
		tcpci_emul_get_reg(emul, TCPC_REG_POWER_CTRL, &pwr_ctrl);

		/*
		 * Start DRP toggling only if auto discharge is disabled,
		 * DRP is enabled and CC1/2 are both Rp or Rd
		 */
		if (!(pwr_ctrl & TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT)
		    && TCPC_REG_ROLE_CTRL_DRP(role_ctrl) &&
		    (TCPC_REG_ROLE_CTRL_CC1(role_ctrl) ==
		     TCPC_REG_ROLE_CTRL_CC2(role_ctrl)) &&
		    (TCPC_REG_ROLE_CTRL_CC1(role_ctrl) == TYPEC_CC_RP ||
		     TCPC_REG_ROLE_CTRL_CC1(role_ctrl) == TYPEC_CC_RD)) {
			/* Set Look4Connection and clear CC1/2 state */
			tcpci_emul_set_reg(
				emul, TCPC_REG_CC_STATUS,
				TCPC_REG_CC_STATUS_LOOK4CONNECTION_MASK);
		}
		break;
	case TCPC_REG_COMMAND_ENABLE_VBUS_DETECT:
	case TCPC_REG_COMMAND_SNK_CTRL_LOW:
	case TCPC_REG_COMMAND_SNK_CTRL_HIGH:
	case TCPC_REG_COMMAND_SRC_CTRL_LOW:
	case TCPC_REG_COMMAND_SRC_CTRL_HIGH:
	case TCPC_REG_COMMAND_I2CIDLE:
		break;
	default:
		tcpci_emul_set_i2c_interface_err(emul);
		return -EIO;
	}

	/*
	 * Set command register to allow easier inspection of last
	 * command sent
	 */
	tcpci_emul_set_reg(emul, TCPC_REG_COMMAND, data->write_data & 0xff);
	return 0;
}

/**
 * @brief Handle write to transmit register
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return 0 on success
 */
static int tcpci_emul_handle_transmit(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;

	data->tx_msg->cnt = data->tx_msg->idx;
	data->tx_msg->type = TCPC_REG_TRANSMIT_TYPE(data->write_data);
	data->tx_msg->idx = 0;

	if (data->partner && data->partner->transmit) {
		data->partner->transmit(emul, data->partner, data->tx_msg,
				TCPC_REG_TRANSMIT_TYPE(data->write_data),
				TCPC_REG_TRANSMIT_RETRY(data->write_data));
	}

	return 0;
}

/**
 * @brief Load next rx message and inform partner which message was consumed
 *        by TCPC
 *
 * @param emul Pointer to TCPCI emulator
 *
 * @return 0 when there is no new message to load
 * @return 1 when new rx message is loaded
 */
static int tcpci_emul_get_next_rx_msg(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;
	struct tcpci_emul_msg *consumed_msg;

	if (data->rx_msg == NULL) {
		return 0;
	}

	consumed_msg = data->rx_msg;
	data->rx_msg = consumed_msg->next;

	/* Inform partner */
	if (data->partner && data->partner->rx_consumed) {
		data->partner->rx_consumed(emul, data->partner, consumed_msg);
	}

	/* Prepare new loaded message */
	if (data->rx_msg) {
		data->rx_msg->idx = 0;

		return 1;
	}

	return 0;
}

/**
 * @brief Handle I2C write message. It is checked if accessed register isn't RO
 *        and reserved bits are set to 0.
 *
 * @param i2c_emul Pointer to TCPCI emulator
 * @param reg Register which is written
 * @param msg_len Length of handled I2C message
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int tcpci_emul_handle_write(struct i2c_emul *i2c_emul, int reg,
				   int msg_len)
{
	struct tcpci_emul_data *data;
	const struct emul *emul;
	uint16_t rsvd_mask = 0;
	uint16_t alert_val;
	bool inform_partner = false;
	bool alert_changed = false;
	int reg_bytes;
	int rc;

	/* This write message was setting register before read */
	if (msg_len == 1) {
		return 0;
	}

	/* Exclude register address byte from message length */
	msg_len--;

	emul = i2c_emul->parent;
	data = TCPCI_DATA_FROM_I2C_EMUL(i2c_emul);

	LOG_DBG("TCPCI 0x%x: write reg 0x%x val 0x%x", i2c_emul->addr, reg,
		data->write_data);

	if (data->dev_ops && data->dev_ops->handle_write) {
		switch (data->dev_ops->handle_write(emul, data->dev_ops, reg,
						    msg_len)) {
		case TCPCI_EMUL_CONTINUE:
			break;
		case TCPCI_EMUL_DONE:
			return 0;
		case TCPCI_EMUL_ERROR:
		default:
			return -EIO;
		}
	}

	switch (reg) {
	/* Alert registers */
	case TCPC_REG_ALERT:
		/* Overflow is cleared by Receive SOP message status */
		data->write_data &= ~TCPC_REG_ALERT_RX_BUF_OVF;
		if (data->write_data & TCPC_REG_ALERT_RX_STATUS) {
			data->write_data |= TCPC_REG_ALERT_RX_BUF_OVF;
			/* Do not clear RX status if there is new message */
			if (tcpci_emul_get_next_rx_msg(emul)) {
				data->write_data &= ~TCPC_REG_ALERT_RX_STATUS;
			}
		}
	/* fallthrough */
	case TCPC_REG_FAULT_STATUS:
	case TCPC_REG_ALERT_EXT:
		/* Clear bits where TCPM set 1 */
		tcpci_emul_get_reg(emul, reg, &alert_val);
		data->write_data = alert_val & (~data->write_data);
	/* fallthrough */
	case TCPC_REG_ALERT_MASK:
	case TCPC_REG_POWER_STATUS_MASK:
	case TCPC_REG_FAULT_STATUS_MASK:
	case TCPC_REG_EXT_STATUS_MASK:
	case TCPC_REG_ALERT_EXTENDED_MASK:
		alert_changed = true;
		break;

	/* Control registers */
	case TCPC_REG_TCPC_CTRL:
	case TCPC_REG_ROLE_CTRL:
	case TCPC_REG_FAULT_CTRL:
	case TCPC_REG_POWER_CTRL:
		inform_partner = true;
		break;

	/* Simple write registers */
	case TCPC_REG_VBUS_SINK_DISCONNECT_THRESH:
	case TCPC_REG_VBUS_STOP_DISCHARGE_THRESH:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG:
	case TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG:
	case TCPC_REG_VBUS_NONDEFAULT_TARGET:
	case TCPC_REG_CONFIG_STD_OUTPUT:
	case TCPC_REG_MSG_HDR_INFO:
	case TCPC_REG_RX_DETECT:
		break;

	case TCPC_REG_CONFIG_EXT_1:
		if (data->write_data & TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR &&
		    ((data->reg[TCPC_REG_STD_INPUT_CAP] &
		      TCPC_REG_STD_INPUT_CAP_SRC_FR_SWAP) == BIT(4)) &&
		    data->reg[TCPC_REG_STD_OUTPUT_CAP] &
		    TCPC_REG_STD_OUTPUT_CAP_SNK_DISC_DET) {
			tcpci_emul_set_i2c_interface_err(emul);
			return 0;
		}
		break;

	case TCPC_REG_COMMAND:
		if (msg_len != 1) {
			tcpci_emul_set_i2c_interface_err(emul);
			LOG_ERR("Writing byte %d to 1 byte register 0x%x",
				msg_len, reg);
			return -EIO;
		}
		return tcpci_emul_handle_command(emul);

	case TCPC_REG_TRANSMIT:
		if (msg_len != 1) {
			tcpci_emul_set_i2c_interface_err(emul);
			LOG_ERR("Writing byte %d to 1 byte register 0x%x",
				msg_len, reg);
			return -EIO;
		}
		return tcpci_emul_handle_transmit(emul);

	case TCPC_REG_GENERIC_TIMER:
		/* TODO: Add timer */
		return 0;

	/* Already handled in tcpci_emul_write_byte() */
	case TCPC_REG_TX_BUFFER:
	case TCPC_REG_TX_DATA:
	case TCPC_REG_TX_HDR:
		return 0;
	default:
		tcpci_emul_set_i2c_interface_err(emul);
		LOG_ERR("Write to reg 0x%x which is RO, undefined or unaligned",
			reg);
		return -EIO;
	}

	reg_bytes = tcpci_emul_reg_bytes(reg);

	/* Compute reserved bits mask */
	switch (reg_bytes) {
	case 2:
		rsvd_mask = tcpci_emul_rsvd_mask[reg + 1];
	case 1:
		rsvd_mask <<= 8;
		rsvd_mask |= tcpci_emul_rsvd_mask[reg];
		break;
	}

	/* Check reserved bits */
	if (data->error_on_rsvd_write && rsvd_mask & data->write_data) {
		tcpci_emul_set_i2c_interface_err(emul);
		LOG_ERR("Writing 0x%x to reg 0x%x with rsvd bits mask 0x%x",
			data->write_data, reg, rsvd_mask);
		return -EIO;
	}

	/* Check if I2C write message has correct length */
	if (msg_len != reg_bytes) {
		tcpci_emul_set_i2c_interface_err(emul);
		LOG_ERR("Writing byte %d to %d byte register 0x%x",
			msg_len, reg_bytes, reg);
		return -EIO;
	}

	/* Set new value of register */
	tcpci_emul_set_reg(emul, reg, data->write_data);

	if (alert_changed) {
		rc = tcpci_emul_alert_changed(emul);
		if (rc != 0)
			return rc;
	}

	if (inform_partner && data->partner && data->partner->control_change) {
		data->partner->control_change(emul, data->partner);
	}

	return 0;
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        register.
 *
 * @param i2c_emul Pointer to TCPCI emulator
 * @param reg First byte of last write message
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int tcpci_emul_access_reg(struct i2c_emul *i2c_emul, int reg, int bytes,
				 bool read)
{
	return reg;
}

/* Device instantiation */

/** Check description in emul_tcpci.h */
struct i2c_emul *tcpci_emul_get_i2c_emul(const struct emul *emul)
{
	struct tcpci_emul_data *data = emul->data;

	return &data->common.emul;
}

/**
 * @brief Set up a new TCPCI emulator
 *
 * This should be called for each TCPCI device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int tcpci_emul_init(const struct emul *emul, const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct tcpci_emul_data *data = emul->data;
	int ret;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	ret = i2c_emul_register(parent, emul->dev_label, &data->common.emul);
	if (ret != 0)
		return ret;

	return tcpci_emul_reset(emul);
}

#define TCPCI_EMUL(n)							\
	uint8_t tcpci_emul_tx_buf_##n[128];				\
	static struct tcpci_emul_msg tcpci_emul_tx_msg_##n = {		\
		.buf = tcpci_emul_tx_buf_##n,				\
	};								\
									\
	static struct tcpci_emul_data tcpci_emul_data_##n = {		\
		.tx_msg = &tcpci_emul_tx_msg_##n,			\
		.error_on_ro_write = true,				\
		.error_on_rsvd_write = true,				\
		.common = {						\
			.write_byte = tcpci_emul_write_byte,		\
			.finish_write = tcpci_emul_handle_write,	\
			.read_byte = tcpci_emul_read_byte,		\
			.access_reg = tcpci_emul_access_reg,		\
		},							\
		.alert_gpio_port = COND_CODE_1(				\
			DT_INST_NODE_HAS_PROP(n, alert_gpio),		\
			(DEVICE_DT_GET(DT_GPIO_CTLR(			\
				DT_INST_PROP(n, alert_gpio), gpios))),	\
			(NULL)),					\
		.alert_gpio_pin = COND_CODE_1(				\
			DT_INST_NODE_HAS_PROP(n, alert_gpio),		\
			(DT_GPIO_PIN(DT_INST_PROP(n, alert_gpio),	\
				gpios)),				\
			(0)),						\
	};								\
									\
	static const struct i2c_common_emul_cfg tcpci_emul_cfg_##n = {	\
		.i2c_label = DT_INST_BUS_LABEL(n),			\
		.dev_label = DT_INST_LABEL(n),                          \
		.data = &tcpci_emul_data_##n.common,			\
		.addr = DT_INST_REG_ADDR(n),				\
	};								\
	EMUL_DEFINE(tcpci_emul_init, DT_DRV_INST(n),			\
		    &tcpci_emul_cfg_##n, &tcpci_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(TCPCI_EMUL)
