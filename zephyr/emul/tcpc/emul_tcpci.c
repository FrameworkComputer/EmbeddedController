/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"
#include "tcpm/tcpci.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#line 25
LOG_MODULE_REGISTER(tcpci_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

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

/**
 * @brief Get value of given register of TCPCI
 *
 * @param ctx Pointer to TCPCI context
 * @param reg Register address
 * @param val Pointer where value should be stored
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 *                 or val is NULL
 */
static int get_reg(const struct tcpci_ctx *ctx, int reg, uint16_t *val)
{
	int byte;

	if (reg < 0 || reg > TCPCI_EMUL_REG_COUNT || val == NULL) {
		return -EINVAL;
	}

	*val = 0;

	byte = tcpci_emul_reg_bytes(reg);
	if (byte == 2) {
		*val = sys_get_le16(&ctx->reg[reg]);
	} else {
		*val = ctx->reg[reg];
	}

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_get_reg(const struct emul *emul, int reg, uint16_t *val)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	return get_reg(ctx, reg, val);
}

/**
 * @brief Set value of given register of TCPCI
 *
 * @param ctx Pointer to TCPCI context
 * @param reg Register address which value will be changed
 * @param val New value of the register
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 */
static int set_reg(struct tcpci_ctx *ctx, int reg, uint16_t val)
{
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
		get_reg(ctx, TCPC_REG_ALERT, &alert);
		set_reg(ctx, TCPC_REG_ALERT, alert | update_alert);
	}

	byte = tcpci_emul_reg_bytes(reg);
	if (byte == 2) {
		sys_put_le16(val, &ctx->reg[reg]);
	} else {
		ctx->reg[reg] = val;
	}

	return 0;
}

/**
 * @brief Update value of given register of TCPCI
 *
 * @param ctx Pointer to TCPCI context
 * @param reg Register address which value will be changed
 * @param val New value of the register
 * @param mask Mask to apply with the val
 *
 * @return 0 on success
 * @return -EINVAL when register is out of range defined in TCPCI specification
 */
static int update_reg(struct tcpci_ctx *ctx, int reg, uint16_t val,
		      uint16_t mask)
{
	uint16_t v;

	if (get_reg(ctx, reg, &v)) {
		return -EINVAL;
	}

	v &= ~mask;
	v |= (val & mask);

	if (set_reg(ctx, reg, v)) {
		return -EINVAL;
	}

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_set_reg(const struct emul *emul, int reg, uint16_t val)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	return set_reg(ctx, reg, val);
}

/**
 * @brief Check if alert line should be active based on alert registers and
 *        masks
 *
 * @param ctx Pointer to TCPCI context
 *
 * @return State of alert line
 */
static bool tcpci_emul_check_int(const struct tcpci_ctx *ctx)
{
	uint16_t alert_mask;
	uint16_t alert;

	get_reg(ctx, TCPC_REG_ALERT, &alert);
	get_reg(ctx, TCPC_REG_ALERT_MASK, &alert_mask);

	/*
	 * For nested interrupts alert group bit and alert register bit has to
	 * be unmasked
	 */
	if (alert & alert_mask & TCPC_REG_ALERT_ALERT_EXT &&
	    ctx->reg[TCPC_REG_ALERT_EXT] &
		    ctx->reg[TCPC_REG_ALERT_EXTENDED_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_EXT_STATUS &&
	    ctx->reg[TCPC_REG_EXT_STATUS] &
		    ctx->reg[TCPC_REG_EXT_STATUS_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_FAULT &&
	    ctx->reg[TCPC_REG_FAULT_STATUS] &
		    ctx->reg[TCPC_REG_FAULT_STATUS_MASK]) {
		return true;
	}

	if (alert & alert_mask & TCPC_REG_ALERT_POWER_STATUS &&
	    ctx->reg[TCPC_REG_POWER_STATUS] &
		    ctx->reg[TCPC_REG_POWER_STATUS_MASK]) {
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
 * @param emul Pointer to TCPC emulator
 *
 * @return 0 for success, or non-0 for errors.
 */
static int tcpci_emul_alert_changed(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	int rc;
	bool alert_is_active = tcpci_emul_check_int(ctx);

	/** Trigger GPIO. */
	if (ctx->irq_gpio.port != NULL) {
		/* Triggers on edge falling, so set to 0 when there is an alert.
		 */
		rc = gpio_emul_input_set(ctx->irq_gpio.port, ctx->irq_gpio.pin,
					 alert_is_active ? 0 : 1);
		if (rc != 0)
			return rc;
	}

	/* Nothing to do */
	if (ctx->alert_callback == NULL) {
		return 0;
	}

	ctx->alert_callback(emul, alert_is_active, ctx->alert_callback_data);
	return 0;
}

/**
 * @brief Load next rx message and inform partner which message was consumed
 *        by TCPC
 *
 * @param emul Pointer to TCPC emulator
 *
 * @return 0 when there is no new message to load
 * @return 1 when new rx message is loaded
 */
static int tcpci_emul_get_next_rx_msg(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	struct tcpci_emul_msg *consumed_msg;

	if (ctx->rx_msg == NULL) {
		return 0;
	}

	consumed_msg = ctx->rx_msg;
	ctx->rx_msg = consumed_msg->next;

	/* Inform partner */
	if (ctx->partner && ctx->partner->rx_consumed) {
		ctx->partner->rx_consumed(emul, ctx->partner, consumed_msg);
	}

	/* Prepare new loaded message */
	if (ctx->rx_msg) {
		ctx->rx_msg->idx = 0;

		return 1;
	}

	return 0;
}

/**
 * @brief Reset mask registers that are reset upon receiving or transmitting
 *        Hard Reset message.
 *
 * @param ctx Pointer to TCPCI context
 */
static void tcpci_emul_reset_mask_regs(struct tcpci_ctx *ctx)
{
	ctx->reg[TCPC_REG_ALERT_MASK] = 0xff;
	ctx->reg[TCPC_REG_ALERT_MASK + 1] = 0x7f;
	ctx->reg[TCPC_REG_POWER_STATUS_MASK] = 0xff;
	ctx->reg[TCPC_REG_EXT_STATUS_MASK] = 0x01;
	ctx->reg[TCPC_REG_ALERT_EXTENDED_MASK] = 0x07;
}

/**
 * @brief Perform actions that are expected by TCPC on disabling PD message
 *        delivery (clear RECEIVE_DETECT register and clear already received
 *        messages in buffer)
 *
 * @param emul Pointer to TCPC emulator
 */
static void tcpci_emul_disable_pd_msg_delivery(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	set_reg(ctx, TCPC_REG_RX_DETECT, 0);
	/* Clear received messages */
	while (tcpci_emul_get_next_rx_msg(emul))
		;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_add_rx_msg(const struct emul *emul,
			  struct tcpci_emul_msg *rx_msg, bool alert)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t rx_detect_mask;
	uint16_t rx_detect;
	uint16_t dev_cap_2;
	uint16_t alert_reg;
	int rc;

	/* Acquire lock to prevent race conditions with TCPM accessing I2C */
	rc = i2c_common_emul_lock_data(&ctx->common, K_FOREVER);
	if (rc != 0) {
		LOG_ERR("Failed to acquire TCPCI lock");
		return rc;
	}

	switch (rx_msg->sop_type) {
	case TCPCI_MSG_SOP:
		rx_detect_mask = TCPC_REG_RX_DETECT_SOP;
		break;
	case TCPCI_MSG_SOP_PRIME:
		rx_detect_mask = TCPC_REG_RX_DETECT_SOPP;
		break;
	case TCPCI_MSG_SOP_PRIME_PRIME:
		rx_detect_mask = TCPC_REG_RX_DETECT_SOPPP;
		break;
	case TCPCI_MSG_SOP_DEBUG_PRIME:
		rx_detect_mask = TCPC_REG_RX_DETECT_SOPP_DBG;
		break;
	case TCPCI_MSG_SOP_DEBUG_PRIME_PRIME:
		rx_detect_mask = TCPC_REG_RX_DETECT_SOPPP_DBG;
		break;
	case TCPCI_MSG_TX_HARD_RESET:
		rx_detect_mask = TCPC_REG_RX_DETECT_HRST;
		break;
	case TCPCI_MSG_CABLE_RESET:
		rx_detect_mask = TCPC_REG_RX_DETECT_CABLE_RST;
		break;
	default:
		i2c_common_emul_unlock_data(&ctx->common);
		return -EINVAL;
	}

	get_reg(ctx, TCPC_REG_RX_DETECT, &rx_detect);
	if (!(rx_detect & rx_detect_mask)) {
		/*
		 * TCPCI will not respond with GoodCRC, so from partner emulator
		 * point of view it failed to send message
		 */
		i2c_common_emul_unlock_data(&ctx->common);
		return TCPCI_EMUL_TX_FAILED;
	}

	get_reg(ctx, TCPC_REG_ALERT, &alert_reg);

	/* Handle HardReset */
	if (rx_msg->sop_type == TCPCI_MSG_TX_HARD_RESET) {
		tcpci_emul_disable_pd_msg_delivery(emul);
		tcpci_emul_reset_mask_regs(ctx);

		alert_reg |= TCPC_REG_ALERT_RX_HARD_RST;
		set_reg(ctx, TCPC_REG_ALERT, alert_reg);
		rc = tcpci_emul_alert_changed(emul);

		i2c_common_emul_unlock_data(&ctx->common);
		return rc;
	}

	/* Handle CableReset */
	if (rx_msg->sop_type == TCPCI_MSG_CABLE_RESET) {
		tcpci_emul_disable_pd_msg_delivery(emul);
		/* Rest of CableReset handling is the same as SOP* message */
	}

	if (ctx->rx_msg == NULL) {
		get_reg(ctx, TCPC_REG_DEV_CAP_2, &dev_cap_2);
		if ((!(dev_cap_2 & TCPC_REG_DEV_CAP_2_LONG_MSG) &&
		     rx_msg->cnt > 31) ||
		    rx_msg->cnt > 265) {
			LOG_ERR("Too long first message (%d)", rx_msg->cnt);
			i2c_common_emul_unlock_data(&ctx->common);
			return -EINVAL;
		}

		ctx->rx_msg = rx_msg;
	} else if (ctx->rx_msg->next == NULL) {
		if (rx_msg->cnt > 31) {
			LOG_ERR("Too long second message (%d)", rx_msg->cnt);
			i2c_common_emul_unlock_data(&ctx->common);
			return -EINVAL;
		}

		ctx->rx_msg->next = rx_msg;
		if (alert) {
			alert_reg |= TCPC_REG_ALERT_RX_BUF_OVF;
		}
	} else {
		LOG_ERR("Cannot setup third message");
		i2c_common_emul_unlock_data(&ctx->common);
		return -EINVAL;
	}

	if (alert) {
		if (rx_msg->cnt > 133) {
			alert_reg |= TCPC_REG_ALERT_RX_BEGINNING;
		}

		alert_reg |= TCPC_REG_ALERT_RX_STATUS;
		set_reg(ctx, TCPC_REG_ALERT, alert_reg);

		rc = tcpci_emul_alert_changed(emul);
		if (rc != 0) {
			i2c_common_emul_unlock_data(&ctx->common);
			return rc;
		}
	}

	rx_msg->next = NULL;
	rx_msg->idx = 0;

	i2c_common_emul_unlock_data(&ctx->common);
	return TCPCI_EMUL_TX_SUCCESS;
}

/** Check description in emul_tcpci.h */
struct tcpci_emul_msg *tcpci_emul_get_tx_msg(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	return ctx->tx_msg;
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

void tcpci_emul_set_vbus_voltage(const struct emul *emul, uint32_t vbus_mv)
{
	uint16_t meas;
	uint16_t scale = 0;

	__ASSERT(!(vbus_mv % TCPC_REG_VBUS_VOLTAGE_LSB),
		 "vbus_mv must be divisible by %d (%d)",
		 TCPC_REG_VBUS_VOLTAGE_LSB, vbus_mv);

	meas = vbus_mv / TCPC_REG_VBUS_VOLTAGE_LSB;

	while (meas >= (1 << 10) && scale < 3) {
		__ASSERT(!(meas & 1), "vbus_mv %d does not fit into the reg.",
			 vbus_mv);
		meas >>= 1;
		scale += 1;
	}
	__ASSERT(scale < 3, "scale %d, meas %d doesn't fit into the reg.",
		 scale, meas);

	tcpci_emul_set_reg(emul, TCPC_REG_VBUS_VOLTAGE, (scale << 10) | meas);
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_alert_callback(const struct emul *emul,
				   tcpci_emul_alert_state_func alert_callback,
				   void *alert_callback_data)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	ctx->alert_callback = alert_callback;
	ctx->alert_callback_data = alert_callback_data;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_set_partner_ops(const struct emul *emul,
				const struct tcpci_emul_partner_ops *partner)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	ctx->partner = partner;
}

/**
 * @brief Get detected voltage for given CC resistor
 *
 * @param res CC pull resistor value
 * @param volt Voltage applied by port partner
 *
 * @return Voltage visible at CC resistor side
 */
static enum tcpc_cc_voltage_status
tcpci_emul_detected_volt_for_res(enum tcpc_cc_pull res,
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
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t cc_status, alert, role_ctrl;
	enum tcpc_cc_voltage_status cc1_v, cc2_v;
	enum tcpc_cc_pull cc1_r, cc2_r;
	int rc;

	if (polarity == POLARITY_CC1) {
		cc1_v = partner_cc1;
		cc2_v = partner_cc2;
	} else {
		cc1_v = partner_cc2;
		cc2_v = partner_cc1;
	}

	get_reg(ctx, TCPC_REG_CC_STATUS, &cc_status);
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
		get_reg(ctx, TCPC_REG_ROLE_CTRL, &role_ctrl);
		cc1_r = TCPC_REG_ROLE_CTRL_CC1(role_ctrl);
		cc2_r = TCPC_REG_ROLE_CTRL_CC2(role_ctrl);
	}

	cc1_v = tcpci_emul_detected_volt_for_res(cc1_r, cc1_v);
	cc2_v = tcpci_emul_detected_volt_for_res(cc2_r, cc2_v);

	/* If CC status is TYPEC_CC_VOLT_RP_*, then BIT(2) is ignored */
	cc_status = TCPC_REG_CC_STATUS_SET(
		partner_power_role == PD_ROLE_SOURCE ? 1 : 0, cc2_v, cc1_v);
	set_reg(ctx, TCPC_REG_CC_STATUS, cc_status);
	get_reg(ctx, TCPC_REG_ALERT, &alert);
	set_reg(ctx, TCPC_REG_ALERT, alert | TCPC_REG_ALERT_CC_STATUS);

	if (partner_power_role == PD_ROLE_SOURCE) {
		rc = tcpci_emul_set_vbus_level(emul, VBUS_PRESENT);
		if (rc)
			return rc;
	}

	tcpci_emul_alert_changed(emul);

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_disconnect_partner(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t val;
	uint16_t term;
	int rc;

	tcpci_emul_disable_pd_msg_delivery(emul);
	if (ctx->partner && ctx->partner->disconnect) {
		ctx->partner->disconnect(emul, ctx->partner);
	}
	ctx->partner = NULL;

	/* Set both CC lines to open to indicate disconnect. */
	rc = get_reg(ctx, TCPC_REG_CC_STATUS, &val);
	if (rc != 0)
		return rc;

	term = TCPC_REG_CC_STATUS_TERM(val);

	rc = set_reg(ctx, TCPC_REG_CC_STATUS,
		     TCPC_REG_CC_STATUS_SET(term, TYPEC_CC_VOLT_OPEN,
					    TYPEC_CC_VOLT_OPEN));
	if (rc != 0)
		return rc;

	ctx->reg[TCPC_REG_ALERT] |= TCPC_REG_ALERT_CC_STATUS;
	rc = tcpci_emul_alert_changed(emul);
	if (rc != 0)
		return rc;
	/* TODO: Wait until DisableSourceVbus (TCPC_REG_COMMAND_SRC_CTRL_LOW?),
	 * and then set VBUS present = 0 and vSafe0V = 1 after appropriate
	 * delays.
	 */

	/* Clear VBUS present in case if source partner is disconnected */
	rc = tcpci_emul_set_vbus_level(emul, VBUS_REMOVED);
	if (rc != 0)
		return rc;

	return 0;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_partner_msg_status(const struct emul *emul,
				   enum tcpci_emul_tx_status status)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
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
	case TCPCI_EMUL_TX_CABLE_HARD_RESET:
		tx_status_alert = TCPC_REG_ALERT_TX_SUCCESS |
				  TCPC_REG_ALERT_TX_FAILED;
		break;
	default:
		__ASSERT(0, "Invalid partner TX status 0x%x", status);
		return;
	}

	get_reg(ctx, TCPC_REG_ALERT, &alert);
	set_reg(ctx, TCPC_REG_ALERT, alert | tx_status_alert);
	tcpci_emul_alert_changed(emul);
}

/** Mask reserved bits in each register of TCPCI */
static const uint8_t tcpci_emul_rsvd_mask[] = {
	[TCPC_REG_VENDOR_ID] = 0x00,
	[TCPC_REG_VENDOR_ID + 1] = 0x00,
	[TCPC_REG_PRODUCT_ID] = 0x00,
	[TCPC_REG_PRODUCT_ID + 1] = 0x00,
	[TCPC_REG_BCD_DEV] = 0x00,
	[TCPC_REG_BCD_DEV + 1] = 0xff,
	[TCPC_REG_TC_REV] = 0x00,
	[TCPC_REG_TC_REV + 1] = 0x00,
	[TCPC_REG_PD_REV] = 0x00,
	[TCPC_REG_PD_REV + 1] = 0x00,
	[TCPC_REG_PD_INT_REV] = 0x00,
	[TCPC_REG_PD_INT_REV + 1] = 0x00,
	[0x0c ... 0x0f] = 0xff, /* Reserved */
	[TCPC_REG_ALERT] = 0x00,
	[TCPC_REG_ALERT + 1] = 0x00,
	[TCPC_REG_ALERT_MASK] = 0x00,
	[TCPC_REG_ALERT_MASK + 1] = 0x00,
	[TCPC_REG_POWER_STATUS_MASK] = 0x00,
	[TCPC_REG_FAULT_STATUS_MASK] = 0x00,
	[TCPC_REG_EXT_STATUS_MASK] = 0xfe,
	[TCPC_REG_ALERT_EXTENDED_MASK] = 0xf8,
	[TCPC_REG_CONFIG_STD_OUTPUT] = 0x00,
	[TCPC_REG_TCPC_CTRL] = 0x00,
	[TCPC_REG_ROLE_CTRL] = 0x80,
	[TCPC_REG_FAULT_CTRL] = 0x80,
	[TCPC_REG_POWER_CTRL] = 0x00,
	[TCPC_REG_CC_STATUS] = 0xc0,
	[TCPC_REG_POWER_STATUS] = 0x00,
	[TCPC_REG_FAULT_STATUS] = 0x00,
	[TCPC_REG_EXT_STATUS] = 0xfe,
	[TCPC_REG_ALERT_EXT] = 0xf8,
	[0x22] = 0xff, /* Reserved */
	[TCPC_REG_COMMAND] = 0x00,
	[TCPC_REG_DEV_CAP_1] = 0x00,
	[TCPC_REG_DEV_CAP_1 + 1] = 0x00,
	[TCPC_REG_DEV_CAP_2] = 0x80,
	[TCPC_REG_DEV_CAP_2 + 1] = 0x00,
	[TCPC_REG_STD_INPUT_CAP] = 0xe0,
	[TCPC_REG_STD_OUTPUT_CAP] = 0x00,
	[TCPC_REG_CONFIG_EXT_1] = 0xfc,
	[0x2b] = 0xff, /* Reserved */
	[TCPC_REG_GENERIC_TIMER] = 0x00,
	[TCPC_REG_GENERIC_TIMER + 1] = 0x00,
	[TCPC_REG_MSG_HDR_INFO] = 0xe0,
	[TCPC_REG_RX_DETECT] = 0x00,
	[TCPC_REG_RX_BUFFER... 0x4f] = 0x00,
	[TCPC_REG_TRANSMIT... 0x69] = 0x00,
	[TCPC_REG_VBUS_VOLTAGE] = 0xf0,
	[TCPC_REG_VBUS_VOLTAGE + 1] = 0x00,
	[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH] = 0x00,
	[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH + 1] = 0xfc,
	[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH] = 0x00,
	[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH + 1] = 0xfc,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG] = 0x00,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG + 1] = 0xfc,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG] = 0x00,
	[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG + 1] = 0xfc,
	[TCPC_REG_VBUS_NONDEFAULT_TARGET] = 0x00,
	[TCPC_REG_VBUS_NONDEFAULT_TARGET + 1] = 0x00,
	[0x7c ... 0x7f] = 0xff, /* Reserved */
	[0x80 ... TCPCI_EMUL_REG_COUNT - 1] = 0x00,
};

/**
 * @brief Reset role control and header info registers to default values.
 *
 * @param ctx Pointer to TCPCI context
 */
static void tcpci_emul_reset_role_ctrl(struct tcpci_ctx *ctx)
{
	uint16_t dev_cap_1;

	get_reg(ctx, TCPC_REG_DEV_CAP_1, &dev_cap_1);
	switch (dev_cap_1 & TCPC_REG_DEV_CAP_1_PWRROLE_MASK) {
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_OR_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SNK_ACC:
		ctx->reg[TCPC_REG_ROLE_CTRL] = 0x0a;
		ctx->reg[TCPC_REG_MSG_HDR_INFO] = 0x04;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC:
		/* Dead batter */
		ctx->reg[TCPC_REG_ROLE_CTRL] = 0x05;
		ctx->reg[TCPC_REG_MSG_HDR_INFO] = 0x0d;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_DRP:
		/* Dead batter and dbg acc ind */
		ctx->reg[TCPC_REG_ROLE_CTRL] = 0x4a;
		ctx->reg[TCPC_REG_MSG_HDR_INFO] = 0x04;
		break;
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP_ADPT_CBL:
	case TCPC_REG_DEV_CAP_1_PWRROLE_SRC_SNK_DRP:
		/* Dead batter and dbg acc ind */
		ctx->reg[TCPC_REG_ROLE_CTRL] = 0x4a;
		ctx->reg[TCPC_REG_MSG_HDR_INFO] = 0x04;
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
int tcpci_emul_reset(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	ctx->reg[TCPC_REG_ALERT] = 0x00;
	ctx->reg[TCPC_REG_ALERT + 1] = 0x00;
	ctx->reg[TCPC_REG_FAULT_STATUS_MASK] = 0xff;
	ctx->reg[TCPC_REG_CONFIG_STD_OUTPUT] = 0x60;
	ctx->reg[TCPC_REG_TCPC_CTRL] = 0x00;
	ctx->reg[TCPC_REG_FAULT_CTRL] = 0x00;
	ctx->reg[TCPC_REG_POWER_CTRL] = 0x60;
	ctx->reg[TCPC_REG_CC_STATUS] = 0x00;
	ctx->reg[TCPC_REG_POWER_STATUS] = 0x08;
	ctx->reg[TCPC_REG_FAULT_STATUS] = 0x80;
	ctx->reg[TCPC_REG_EXT_STATUS] = 0x00;
	ctx->reg[TCPC_REG_ALERT_EXT] = 0x00;
	ctx->reg[TCPC_REG_COMMAND] = 0x00;
	ctx->reg[TCPC_REG_CONFIG_EXT_1] = 0x00;
	ctx->reg[TCPC_REG_GENERIC_TIMER] = 0x00;
	ctx->reg[TCPC_REG_GENERIC_TIMER + 1] = 0x00;
	ctx->reg[TCPC_REG_RX_DETECT] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE + 1] = 0x00;
	ctx->reg[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH] = 0x8c;
	ctx->reg[TCPC_REG_VBUS_SINK_DISCONNECT_THRESH + 1] = 0x00;
	ctx->reg[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH] = 0x20;
	ctx->reg[TCPC_REG_VBUS_STOP_DISCHARGE_THRESH + 1] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_HI_CFG + 1] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG] = 0x00;
	ctx->reg[TCPC_REG_VBUS_VOLTAGE_ALARM_LO_CFG + 1] = 0x00;
	ctx->reg[TCPC_REG_VBUS_NONDEFAULT_TARGET] = 0x00;
	ctx->reg[TCPC_REG_VBUS_NONDEFAULT_TARGET + 1] = 0x00;

	tcpci_emul_reset_mask_regs(ctx);
	tcpci_emul_reset_role_ctrl(ctx);

	return tcpci_emul_alert_changed(emul);
}

/**
 * @brief Set alert and fault registers to indicate i2c interface fault
 *
 * @param emul Pointer to TCPC emulator
 * @return 0 if successful
 */
static int tcpci_emul_set_i2c_interface_err(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t fault_status;

	get_reg(ctx, TCPC_REG_FAULT_STATUS, &fault_status);
	fault_status |= TCPC_REG_FAULT_STATUS_I2C_INTERFACE_ERR;
	set_reg(ctx, TCPC_REG_FAULT_STATUS, fault_status);

	return tcpci_emul_alert_changed(emul);
}

/**
 * @brief Handle read from RX buffer registers for TCPCI rev 1.0 and rev 2.0
 *
 * @param emul Pointer to TCPC emulator
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
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	int is_rev1;

	is_rev1 = ctx->reg[TCPC_REG_PD_INT_REV] == TCPC_REG_PD_INT_REV_REV_1_0;

	if (!is_rev1 && reg != TCPC_REG_RX_BUFFER) {
		LOG_ERR("Register 0x%x defined only for revision 1.0", reg);
		tcpci_emul_set_i2c_interface_err(emul);
		return -EIO;
	}

	switch (reg) {
	case TCPC_REG_RX_BUFFER:
		if (ctx->rx_msg == NULL) {
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
			/* TCPCI message size count include type byte */
			*val = ctx->rx_msg->cnt + 1;
		} else if (is_rev1) {
			LOG_ERR("Revision 1.0 has only byte count at 0x30");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		} else if (bytes == 1) {
			*val = ctx->rx_msg->sop_type;
		} else if (ctx->rx_msg->idx < ctx->rx_msg->cnt) {
			*val = ctx->rx_msg->buf[ctx->rx_msg->idx];
			ctx->rx_msg->idx++;
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
		if (ctx->rx_msg == NULL) {
			*val = 0;
		} else {
			*val = ctx->rx_msg->sop_type;
		}
		break;

	case TCPC_REG_RX_HDR:
		if (bytes > 1) {
			LOG_ERR("Reading byte %d from 2 byte register 0x%x",
				bytes, reg);
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		if (ctx->rx_msg == NULL) {
			LOG_ERR("Accessing RX buffer with no msg");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		*val = ctx->rx_msg->buf[bytes];
		break;

	case TCPC_REG_RX_DATA:
		if (ctx->rx_msg == NULL) {
			LOG_ERR("Accessing RX buffer with no msg");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		if (bytes < ctx->rx_msg->cnt - 2) {
			/* rx_msg cnt include two bytes of header */
			*val = ctx->rx_msg->buf[bytes + 2];
			ctx->rx_msg->idx++;
		} else {
			LOG_ERR("Reading past RX buffer");
			tcpci_emul_set_i2c_interface_err(emul);
			return -EIO;
		}
		break;
	}

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_read_byte(const struct emul *emul, int reg, uint8_t *val,
			 int bytes)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

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
		*val = ctx->reg[reg + bytes];
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
		*val = ctx->reg[reg];
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

/** Check description in emul_tcpci.h */
int tcpci_emul_write_byte(const struct emul *emul, int reg, uint8_t val,
			  int bytes)
{
	int is_rev1;
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	is_rev1 = ctx->reg[TCPC_REG_PD_INT_REV] == TCPC_REG_PD_INT_REV_REV_1_0;
	switch (reg) {
	case TCPC_REG_TX_BUFFER:
		if (is_rev1) {
			if (bytes > 1) {
				LOG_ERR("Rev 1.0 has only byte count at 0x51");
				tcpci_emul_set_i2c_interface_err(emul);
				return -EIO;
			}
			ctx->tx_msg->idx = val;
		}

		if (bytes == 1) {
			ctx->tx_msg->cnt = val;
		} else {
			if (ctx->tx_msg->cnt > 0) {
				ctx->tx_msg->cnt--;
				ctx->tx_msg->buf[ctx->tx_msg->idx] = val;
				ctx->tx_msg->idx++;
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
		ctx->tx_msg->buf[bytes] = val;
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
		ctx->tx_msg->buf[bytes] = val;
		return 0;
	}

	if (bytes == 1) {
		ctx->write_data = val;
	} else if (bytes == 2) {
		ctx->write_data |= (uint16_t)val << 8;
	}

	return 0;
}

/**
 * @brief Handle writes to command register
 *
 * @param emul Pointer to TCPC emulator
 *
 * @return 0 on success
 * @return -EIO on unknown command value
 */
static int tcpci_emul_handle_command(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t role_ctrl;
	uint16_t pwr_ctrl;

	switch (ctx->write_data & 0xff) {
	case TCPC_REG_COMMAND_RESET_TRANSMIT_BUF:
		ctx->tx_msg->idx = 0;
		break;
	case TCPC_REG_COMMAND_RESET_RECEIVE_BUF:
		if (ctx->rx_msg) {
			ctx->rx_msg->idx = 0;
		}
		break;
	case TCPC_REG_COMMAND_LOOK4CONNECTION:
		get_reg(ctx, TCPC_REG_ROLE_CTRL, &role_ctrl);
		get_reg(ctx, TCPC_REG_POWER_CTRL, &pwr_ctrl);

		/*
		 * Start DRP toggling only if auto discharge is disabled,
		 * DRP is enabled and CC1/2 are both Rp or Rd
		 */
		if (!(pwr_ctrl &
		      TCPC_REG_POWER_CTRL_AUTO_DISCHARGE_DISCONNECT) &&
		    TCPC_REG_ROLE_CTRL_DRP(role_ctrl) &&
		    (TCPC_REG_ROLE_CTRL_CC1(role_ctrl) ==
		     TCPC_REG_ROLE_CTRL_CC2(role_ctrl)) &&
		    (TCPC_REG_ROLE_CTRL_CC1(role_ctrl) == TYPEC_CC_RP ||
		     TCPC_REG_ROLE_CTRL_CC1(role_ctrl) == TYPEC_CC_RD)) {
			/* Set Look4Connection and clear CC1/2 state */
			set_reg(ctx, TCPC_REG_CC_STATUS,
				TCPC_REG_CC_STATUS_LOOK4CONNECTION_MASK);
		}
		break;
	case TCPC_REG_COMMAND_DISABLE_VBUS_DETECT:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
		break;
	case TCPC_REG_COMMAND_ENABLE_VBUS_DETECT:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0xFF,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
		break;
	case TCPC_REG_COMMAND_SNK_CTRL_LOW:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0,
			   TCPC_REG_POWER_STATUS_SINKING_VBUS);
		break;
	case TCPC_REG_COMMAND_SNK_CTRL_HIGH:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0xFF,
			   TCPC_REG_POWER_STATUS_SINKING_VBUS);
		break;
	case TCPC_REG_COMMAND_SRC_CTRL_LOW:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
		break;
	case TCPC_REG_COMMAND_SRC_CTRL_HIGH:
		update_reg(ctx, TCPC_REG_POWER_STATUS, 0xFF,
			   TCPC_REG_POWER_STATUS_SOURCING_VBUS);
		break;
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
	set_reg(ctx, TCPC_REG_COMMAND, ctx->write_data & 0xff);
	return 0;
}

/**
 * @brief Handle write to transmit register
 *
 * @param emul Pointer to TCPC emulator
 *
 * @return 0 on success
 * @return -EIO when sending SOP message with less than 2 bytes in TX buffer
 */
static int tcpci_emul_handle_transmit(const struct emul *emul)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	enum tcpci_msg_type type;

	ctx->tx_msg->cnt = ctx->tx_msg->idx;
	ctx->tx_msg->sop_type = TCPC_REG_TRANSMIT_TYPE(ctx->write_data);
	ctx->tx_msg->idx = 0;

	type = TCPC_REG_TRANSMIT_TYPE(ctx->write_data);

	if (type < NUM_SOP_STAR_TYPES && ctx->tx_msg->cnt < 2) {
		LOG_ERR("Transmitting too short message (%d)",
			ctx->tx_msg->cnt);
		tcpci_emul_set_i2c_interface_err(emul);
		return -EIO;
	}

	if (ctx->partner && ctx->partner->transmit) {
		ctx->partner->transmit(
			emul, ctx->partner, ctx->tx_msg, type,
			TCPC_REG_TRANSMIT_RETRY(ctx->write_data));
	}

	switch (type) {
	case TCPCI_MSG_TX_HARD_RESET:
		tcpci_emul_disable_pd_msg_delivery(emul);
		tcpci_emul_reset_mask_regs(ctx);
		__fallthrough;
	case TCPCI_MSG_CABLE_RESET:
		/*
		 * Cable and Hard reset are special and set success and fail
		 * in Alert reg regardless of the outcome of the transmission
		 */
		tcpci_emul_partner_msg_status(emul,
					      TCPCI_EMUL_TX_CABLE_HARD_RESET);
		break;
	default:
		break;
	}

	return 0;
}

/** Check description in emul_tcpci.h */
int tcpci_emul_handle_write(const struct emul *emul, int reg, int msg_len)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
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

	switch (reg) {
	/* Alert registers */
	case TCPC_REG_ALERT:
		/* Overflow is cleared by Receive SOP message status */
		ctx->write_data &= ~TCPC_REG_ALERT_RX_BUF_OVF;
		if (ctx->write_data & TCPC_REG_ALERT_RX_STATUS) {
			ctx->write_data |= TCPC_REG_ALERT_RX_BUF_OVF;
			/* Do not clear RX status if there is new message */
			if (tcpci_emul_get_next_rx_msg(emul)) {
				ctx->write_data &= ~TCPC_REG_ALERT_RX_STATUS;
			}
		}
		__fallthrough;
	case TCPC_REG_FAULT_STATUS:
	case TCPC_REG_ALERT_EXT:
		/* Clear bits where TCPM set 1 */
		get_reg(ctx, reg, &alert_val);
		ctx->write_data = alert_val & (~ctx->write_data);
		__fallthrough;
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
		if (ctx->write_data & TCPC_REG_CONFIG_EXT_1_FR_SWAP_SNK_DIR &&
		    ((ctx->reg[TCPC_REG_STD_INPUT_CAP] &
		      TCPC_REG_STD_INPUT_CAP_SRC_FR_SWAP) == BIT(4)) &&
		    ctx->reg[TCPC_REG_STD_OUTPUT_CAP] &
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
		__fallthrough;
	case 1:
		rsvd_mask <<= 8;
		rsvd_mask |= tcpci_emul_rsvd_mask[reg];
		break;
	}

	/* Check reserved bits */
	if (ctx->error_on_rsvd_write && rsvd_mask & ctx->write_data) {
		tcpci_emul_set_i2c_interface_err(emul);
		LOG_ERR("Writing 0x%x to reg 0x%x with rsvd bits mask 0x%x",
			ctx->write_data, reg, rsvd_mask);
		return -EIO;
	}

	/* Check if I2C write message has correct length */
	if (msg_len != reg_bytes) {
		tcpci_emul_set_i2c_interface_err(emul);
		LOG_ERR("Writing byte %d to %d byte register 0x%x", msg_len,
			reg_bytes, reg);
		return -EIO;
	}

	/* Set new value of register */
	set_reg(ctx, reg, ctx->write_data);

	if (alert_changed) {
		rc = tcpci_emul_alert_changed(emul);
		if (rc != 0)
			return rc;
	}

	if (inform_partner && ctx->partner && ctx->partner->control_change) {
		ctx->partner->control_change(emul, ctx->partner);
	}

	return 0;
}

/** Check description in emul_tcpci.h */
void tcpci_emul_i2c_init(const struct emul *emul, const struct device *i2c_dev)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;

	ctx->common.emul.addr = tcpc_data->i2c_cfg.addr;
	ctx->common.i2c = i2c_dev;
	ctx->common.cfg = &tcpc_data->i2c_cfg;

	i2c_common_emul_init(&ctx->common);
}

/** Check description in emul_tcpci.h */
int tcpci_emul_set_vbus_level(const struct emul *emul, enum vbus_level level)
{
	struct tcpc_emul_data *tcpc_data = emul->data;
	struct tcpci_ctx *ctx = tcpc_data->tcpci_ctx;
	uint16_t revision;
	int rc;
	uint16_t power_status;
	uint16_t ext_status;

	switch (level) {
	case VBUS_SAFE0V:
		power_status = TCPC_REG_POWER_STATUS_VBUS_DET;
		ext_status = TCPC_REG_EXT_STATUS_SAFE0V;
		break;
	case VBUS_PRESENT:
		power_status = TCPC_REG_POWER_STATUS_VBUS_DET |
			       TCPC_REG_POWER_STATUS_VBUS_PRES;
		ext_status = 0;
		break;
	case VBUS_REMOVED:
		power_status = TCPC_REG_POWER_STATUS_VBUS_DET;
		ext_status = 0;
		break;
	default:
		return EC_ERROR_PARAM1;
	}

	rc = get_reg(ctx, TCPC_REG_PD_INT_REV, &revision);
	if (rc)
		return rc;
	rc = update_reg(ctx, TCPC_REG_POWER_STATUS, power_status,
			TCPC_REG_POWER_STATUS_VBUS_DET |
				TCPC_REG_POWER_STATUS_VBUS_PRES);
	if (rc)
		return rc;
	if (TCPC_REG_PD_INT_REV_REV(revision) == TCPC_REG_PD_INT_REV_REV_2_0) {
		rc = update_reg(ctx, TCPC_REG_EXT_STATUS, ext_status,
				TCPC_REG_EXT_STATUS_SAFE0V);
		if (rc)
			return rc;
	}
	rc = tcpci_emul_alert_changed(emul);
	if (rc)
		return rc;
	return EC_SUCCESS;
}
