/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

#include "anx74xx.h"
#include "ec_commands.h"
#include "ps8xxx.h"
#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

static int tcpc_vbus[CONFIG_USB_PD_PORT_COUNT];

/* Save the selected rp value */
static int selected_rp[CONFIG_USB_PD_PORT_COUNT];

static int init_alert_mask(int port)
{
	uint16_t mask;

	/*
	 * Create mask of alert events that will cause the TCPC to
	 * signal the TCPM via the Alert# gpio line.
	 */
	mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
		| TCPC_REG_ALERT_POWER_STATUS
#endif
		;
	/* Set the alert mask in TCPC */
	return tcpc_write16(port, TCPC_REG_ALERT_MASK, mask);
}

static int clear_alert_mask(int port)
{
	return tcpc_write16(port, TCPC_REG_ALERT_MASK, 0);
}

static int init_power_status_mask(int port)
{
	uint8_t mask;
	int rv;

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	mask = TCPC_REG_POWER_STATUS_VBUS_PRES;
#else
	mask = 0;
#endif
	rv = tcpc_write(port, TCPC_REG_POWER_STATUS_MASK , mask);

	return rv;
}

static int clear_power_status_mask(int port)
{
	return tcpc_write(port, TCPC_REG_POWER_STATUS_MASK, 0);
}

int tcpci_tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int status;
	int rv;

	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);

	/* If tcpc read fails, return error and CC as open */
	if (rv) {
		*cc1 = TYPEC_CC_VOLT_OPEN;
		*cc2 = TYPEC_CC_VOLT_OPEN;
		return rv;
	}

	*cc1 = TCPC_REG_CC_STATUS_CC1(status);
	*cc2 = TCPC_REG_CC_STATUS_CC2(status);

	/*
	 * If status is not open, then OR in termination to convert to
	 * enum tcpc_cc_voltage_status.
	 */
	if (*cc1 != TYPEC_CC_VOLT_OPEN)
		*cc1 |= TCPC_REG_CC_STATUS_TERM(status) << 2;
	if (*cc2 != TYPEC_CC_VOLT_OPEN)
		*cc2 |= TCPC_REG_CC_STATUS_TERM(status) << 2;

	return rv;
}

static int tcpci_tcpm_get_power_status(int port, int *status)
{
	return tcpc_read(port, TCPC_REG_POWER_STATUS, status);
}

int tcpci_tcpm_select_rp_value(int port, int rp)
{
	selected_rp[port] = rp;
	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
void tcpci_tcpc_discharge_vbus(int port, int enable)
{
	int reg;

	if (tcpc_read(port, TCPC_REG_POWER_CTRL, &reg))
		return;

	if (enable)
		reg |= TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	else
		reg &= ~TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;

	tcpc_write(port, TCPC_REG_POWER_CTRL, reg);
}
#endif

static int set_role_ctrl(int port, int toggle, int rp, int pull)
{
	return tcpc_write(port, TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(toggle, rp, pull, pull));
}

int tcpci_tcpm_set_cc(int port, int pull)
{
	/* Set manual control, and set both CC lines to the same pull */
	return set_role_ctrl(port, 0, selected_rp[port], pull);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
int tcpci_tcpc_drp_toggle(int port, int enable)
{
	int rv;

	if (!enable) {
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
		struct usb_mux *mux = &usb_muxes[port];
		if (mux->board_init)
			return mux->board_init(mux);
#endif
		return EC_SUCCESS;
	}
	/* Set auto drp toggle */
	rv = set_role_ctrl(port, 1, TYPEC_RP_USB, TYPEC_CC_RD);

	/* Set Look4Connection command */
	rv |= tcpc_write(port, TCPC_REG_COMMAND,
			 TCPC_REG_COMMAND_LOOK4CONNECTION);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	rv |= tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
#endif
	return rv;
}
#endif

int tcpci_tcpm_set_polarity(int port, int polarity)
{
	return tcpc_write(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_SET(polarity));
}

#ifdef CONFIG_USBC_PPC
int tcpci_tcpm_set_snk_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SNK_CTRL_HIGH :
		TCPC_REG_COMMAND_SNK_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}

int tcpci_tcpm_set_src_ctrl(int port, int enable)
{
	int cmd = enable ? TCPC_REG_COMMAND_SRC_CTRL_HIGH :
		TCPC_REG_COMMAND_SRC_CTRL_LOW;

	return tcpc_write(port, TCPC_REG_COMMAND, cmd);
}
#endif

int tcpci_tcpm_set_vconn(int port, int enable)
{
	int reg, rv;

	rv = tcpc_read(port, TCPC_REG_POWER_CTRL, &reg);
	if (rv)
		return rv;
	reg &= ~TCPC_REG_POWER_CTRL_VCONN(1);
	reg |= TCPC_REG_POWER_CTRL_VCONN(enable);
	return tcpc_write(port, TCPC_REG_POWER_CTRL, reg);
}

int tcpci_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return tcpc_write(port, TCPC_REG_MSG_HDR_INFO,
			  TCPC_REG_MSG_HDR_INFO_SET(data_role, power_role));
}

static int tcpm_alert_status(int port, int *alert)
{
	/* Read TCPC Alert register */
	return tcpc_read16(port, TCPC_REG_ALERT, alert);
}

int tcpci_tcpm_set_rx_enable(int port, int enable)
{
	/* If enable, then set RX detect for SOP and HRST */
	return tcpc_write(port, TCPC_REG_RX_DETECT,
			  enable ? TCPC_REG_RX_DETECT_SOP_HRST_MASK : 0);
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
int tcpci_tcpm_get_vbus_level(int port)
{
	return tcpc_vbus[port];
}
#endif

int tcpci_tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;

	rv = tcpc_read(port, TCPC_REG_RX_BYTE_CNT, &cnt);

	/* RX_BYTE_CNT includes 3 bytes for frame type and header */
	if (rv != EC_SUCCESS || cnt < 3) {
		rv = EC_ERROR_UNKNOWN;
		goto clear;
	}

	rv = tcpc_read16(port, TCPC_REG_RX_HDR, (int *)head);

	cnt = cnt - 3;
	if (rv == EC_SUCCESS && cnt > 0) {
		tcpc_lock(port, 1);
		rv = tcpc_xfer(port,
			       (uint8_t *)&reg, 1, (uint8_t *)payload,
			       cnt, I2C_XFER_SINGLE);
		tcpc_lock(port, 0);
	}

clear:
	/* Read complete, clear RX status alert bit */
	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_RX_STATUS);

	return rv;
}

int tcpci_tcpm_transmit(int port, enum tcpm_transmit_type type,
			uint16_t header, const uint32_t *data)
{
	int reg = TCPC_REG_TX_DATA;
	int rv, cnt = 4*PD_HEADER_CNT(header);

	/* TX_BYTE_CNT includes 2 bytes for message header */
	rv = tcpc_write(port, TCPC_REG_TX_BYTE_CNT, cnt + 2);

	rv |= tcpc_write16(port, TCPC_REG_TX_HDR, header);

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	if (cnt > 0) {
		tcpc_lock(port, 1);
		rv = tcpc_xfer(port,
			       (uint8_t *)&reg, 1, NULL, 0, I2C_XFER_START);
		rv |= tcpc_xfer(port,
				(uint8_t *)data, cnt, NULL, 0, I2C_XFER_STOP);
		tcpc_lock(port, 0);
	}

	/* If tcpc read fails, return error */
	if (rv)
		return rv;

	rv = tcpc_write(port, TCPC_REG_TRANSMIT, TCPC_REG_TRANSMIT_SET(type));

	return rv;
}

/* Returns true if TCPC has reset based on reading mask registers. */
static int register_mask_reset(int port)
{
	int mask;

	mask = 0;
	tcpc_read16(port, TCPC_REG_ALERT_MASK, &mask);
	if (mask == TCPC_REG_ALERT_MASK_ALL)
		return 1;

	mask = 0;
	tcpc_read(port, TCPC_REG_POWER_STATUS_MASK, &mask);
	if (mask == TCPC_REG_POWER_STATUS_MASK_ALL)
		return 1;

	return 0;
}

void tcpci_tcpc_alert(int port)
{
	int status;
	uint32_t pd_event = 0;

	/* Read the Alert register from the TCPC */
	tcpm_alert_status(port, &status);
	/*
	 * Check registers to see if we can tell that the TCPC has reset. If
	 * so, perform tcpc_init inline.
	 */
	if (register_mask_reset(port))
		pd_event |= PD_EVENT_TCPC_RESET;

	/*
	 * Clear alert status for everything except RX_STATUS, which shouldn't
	 * be cleared until we have successfully retrieved message.
	 */
	if (status & ~TCPC_REG_ALERT_RX_STATUS)
		tcpc_write16(port, TCPC_REG_ALERT,
			     status & ~TCPC_REG_ALERT_RX_STATUS);

	if (status & TCPC_REG_ALERT_CC_STATUS) {
		/* CC status changed, wake task */
		pd_event |= PD_EVENT_CC;
	}
	if (status & TCPC_REG_ALERT_POWER_STATUS) {
		int reg = 0;
		/* Read Power Status register */
		tcpci_tcpm_get_power_status(port, &reg);
		/* Update VBUS status */
		tcpc_vbus[port] = reg &
			TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
#if defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) && defined(CONFIG_USB_CHARGER)
		/* Update charge manager with new VBUS state */
		usb_charger_vbus_change(port, tcpc_vbus[port]);
		pd_event |= TASK_EVENT_WAKE;
#endif /* CONFIG_USB_PD_VBUS_DETECT_TCPC && CONFIG_USB_CHARGER */
	}
	if (status & TCPC_REG_ALERT_RX_STATUS) {
		/* message received */
		pd_event |= PD_EVENT_RX;
	}
	if (status & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		pd_event |= TASK_EVENT_WAKE;
	}
	if (status & TCPC_REG_ALERT_TX_COMPLETE) {
		/* transmit complete */
		pd_transmit_complete(port, status & TCPC_REG_ALERT_TX_SUCCESS ?
					   TCPC_TX_COMPLETE_SUCCESS :
					   TCPC_TX_COMPLETE_FAILED);
	}

	/*
	 * Wait until all possible TCPC accesses in this function are complete
	 * prior to setting events and/or waking the pd task. When the PD
	 * task is woken and runs (which will happen during I2C transactions in
	 * this function), the pd task may put the TCPC into low power mode and
	 * the next I2C transaction to the TCPC will cause it to wake again.
	 */
	if (pd_event)
		task_set_event(PD_PORT_TO_TASK_ID(port), pd_event, 0);
}

/*
 * For PS8751, this function will fail if the chip is in low power mode.
 * PS8751 has to be woken up by reading a random register first then wait for
 * 10ms.
 *
 * This code doesn't have the wake-up read to avoid 10ms delay. Instead, we
 * call this function immediately after the chip is reset or initialized
 * because it'll gurantee the chip is awake. Once it's called, the chip info
 * will be stored in cache, which can be accessed by tcpm_get_chip_info without
 * worrying about chip states.
 */
int tcpci_get_chip_info(int port, int renew,
			struct ec_response_pd_chip_info **chip_info)
{
	static struct ec_response_pd_chip_info info[CONFIG_USB_PD_PORT_COUNT];
	struct ec_response_pd_chip_info *i;
	int error;
	int val;

	if (port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_ERROR_INVAL;

	i = &info[port];

	/* If chip_info is NULL, chip info will be stored in cache and can be
	 * read later by another call. */
	if (chip_info)
		*chip_info = i;

	/* If already populated and renewal is not asked, return cache value */
	if (i->vendor_id && !renew)
		return EC_SUCCESS;

	error = tcpc_read16(port, TCPC_REG_VENDOR_ID, &val);
	if (error)
		return error;
	i->vendor_id = val;

	error = tcpc_read16(port, TCPC_REG_PRODUCT_ID, &val);
	if (error)
		return error;
	i->product_id = val;

	error = tcpc_read16(port, TCPC_REG_BCD_DEV, &val);
	if (error)
		return error;
	i->device_id = val;

	switch (i->vendor_id) {
#if  defined(CONFIG_USB_PD_TCPM_ANX3429) || \
	defined(CONFIG_USB_PD_TCPM_ANX740X) || \
	defined(CONFIG_USB_PD_TCPM_ANX741X)
	case ANX74XX_VENDOR_ID:
		error = anx74xx_tcpc_get_fw_version(port, &val);
		break;
#endif
#if defined(CONFIG_USB_PD_TCPM_PS8751) || defined(CONFIG_USB_PD_TCPM_PS8805)
	/* The PS8751 and PS8805 share the same vendor ID. */
	case PS8XXX_VENDOR_ID:
		error = ps8xxx_tcpc_get_fw_version(port, &val);
		break;
#endif
	default:
		/* Even if the chip doesn't implement get_fw_version, we
		 * return success.*/
		val = -1;
		error = EC_SUCCESS;
	}
	if (error)
		return error;
	/* This may vary chip to chip. For now everything fits in this format */
	i->fw_version_number = val;

	return EC_SUCCESS;
}

/*
 * On TCPC i2c failure, make 30 tries (at least 300ms) before giving up
 * in order to allow the TCPC time to boot / reset.
 */
#define TCPM_INIT_TRIES 30

int tcpci_tcpm_init(int port)
{
	int error;
	int power_status;
	int tries = TCPM_INIT_TRIES;

	while (1) {
		error = tcpc_read(port, TCPC_REG_POWER_STATUS, &power_status);
		/*
		 * If read succeeds and the uninitialized bit is clear, then
		 * initalization is complete, clear all alert bits and write
		 * the initial alert mask.
		 */
		if (!error && !(power_status & TCPC_REG_POWER_STATUS_UNINIT))
			break;
		else if (error && --tries == 0)
			return error;
		msleep(10);
	}

	tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
	/* Initialize power_status_mask */
	init_power_status_mask(port);
	/* Update VBUS status */
	tcpc_vbus[port] = power_status &
			TCPC_REG_POWER_STATUS_VBUS_PRES ? 1 : 0;
	error = init_alert_mask(port);
	if (error)
		return error;

	/* Read chip info here when we know the chip is awake. */
	tcpm_get_chip_info(port, 1, NULL);

	return EC_SUCCESS;
}

/*
 * Dissociate from the TCPC.
 */

int tcpci_tcpm_release(int port)
{
	int error;

	error = clear_alert_mask(port);
	if (error)
		return error;
	error = clear_power_status_mask(port);
	if (error)
		return error;
	/* Clear pending interrupts */
	error = tcpc_write16(port, TCPC_REG_ALERT, 0xffff);
	if (error)
		return error;

	return EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_TCPM_MUX

int tcpci_tcpm_mux_init(int i2c_addr)
{
	return EC_SUCCESS;
}

int tcpci_tcpm_mux_set(int i2c_port_addr, mux_state_t mux_state)
{
	int reg = 0;
	int rv;
#ifdef CONFIG_USB_PD_TCPM_TCPCI_MUX_ONLY
	int port = MUX_PORT(i2c_port_addr);
	int addr = MUX_ADDR(i2c_port_addr);
#else
	int port = tcpc_config[i2c_port_addr].i2c_host_port;
	int addr = tcpc_config[i2c_port_addr].i2c_slave_addr;
#endif
	rv = i2c_read8(port, addr, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	reg &= ~(TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK |
		 TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED);
	if (mux_state & MUX_USB_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	if (mux_state & MUX_DP_ENABLED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP;
	if (mux_state & MUX_POLARITY_INVERTED)
		reg |= TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;

	return i2c_write8(port, addr, TCPC_REG_CONFIG_STD_OUTPUT, reg);
}

/* Reads control register and updates mux_state accordingly */
int tcpci_tcpm_mux_get(int i2c_port_addr, mux_state_t *mux_state)
{
	int reg = 0;
	int rv;
#ifdef CONFIG_USB_PD_TCPM_TCPCI_MUX_ONLY
	int port = MUX_PORT(i2c_port_addr);
	int addr = MUX_ADDR(i2c_port_addr);
#else
	int port = tcpc_config[i2c_port_addr].i2c_host_port;
	int addr = tcpc_config[i2c_port_addr].i2c_slave_addr;
#endif

	*mux_state = 0;
	rv = i2c_read8(port, addr, TCPC_REG_CONFIG_STD_OUTPUT, &reg);
	if (rv != EC_SUCCESS)
		return rv;

	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB)
		*mux_state |= MUX_USB_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP)
		*mux_state |= MUX_DP_ENABLED;
	if (reg & TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED)
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}


const struct usb_mux_driver tcpci_tcpm_usb_mux_driver = {
	.init = tcpci_tcpm_mux_init,
	.set = tcpci_tcpm_mux_set,
	.get = tcpci_tcpm_mux_get,
};

#endif /* CONFIG_USB_PD_TCPM_MUX */

const struct tcpm_drv tcpci_tcpm_drv = {
	.init			= &tcpci_tcpm_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &tcpci_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message		= &tcpci_tcpm_get_message,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
};
