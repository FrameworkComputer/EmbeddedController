/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

#include "i2c.h"
#include "task.h"
#include "tcpci.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "util.h"

/* Convert port number to tcpc i2c address */
#define I2C_ADDR_TCPC(p) (CONFIG_TCPC_I2C_BASE_ADDR + 2*(p))

static int tcpc_polarity, tcpc_vconn;

static int init_alert_mask(int port)
{
	uint16_t mask;
	int rv;

	/*
	 * Create mask of alert events that will cause the TCPC to
	 * signal the TCPM via the Alert# gpio line.
	 */
	mask = TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED |
		TCPC_REG_ALERT_TX_DISCARDED | TCPC_REG_ALERT_RX_STATUS |
		TCPC_REG_ALERT_RX_HARD_RST | TCPC_REG_ALERT_CC_STATUS;
	/* Set the alert mask in TCPC */
	rv = tcpm_alert_mask_set(port, mask);

	return rv;
}

int tcpm_init(int port)
{
	int rv, vid = 0;

	while (1) {
		rv = i2c_read16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
				TCPC_REG_VENDOR_ID, &vid);
		/*
		 * If i2c succeeds and VID is non-zero, then initialization
		 * is complete
		 */
		if (rv == EC_SUCCESS && vid)
			return init_alert_mask(port);
		msleep(10);
	}
}

int tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int status;
	int rv;

	rv = i2c_read8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
		       TCPC_REG_CC_STATUS, &status);

	/* If i2c read fails, return error */
	if (rv)
		return rv;

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

int tcpm_set_cc(int port, int pull)
{
	/*
	 * Set manual control of Rp/Rd, and set both CC lines to the same
	 * pull.
	 */
	/* TODO: set desired Rp strength */
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(0, 0, pull, pull));
}

int tcpm_set_polarity(int port, int polarity)
{
	/* Write new polarity, leave vconn enable flag untouched */
	tcpc_polarity = polarity;
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_POWER_CTRL,
			  TCPC_REG_POWER_CTRL_SET(tcpc_polarity, tcpc_vconn));
}

int tcpm_set_vconn(int port, int enable)
{
	/* Write new vconn enable flag, leave polarity untouched */
	tcpc_vconn = enable;
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_POWER_CTRL,
			  TCPC_REG_POWER_CTRL_SET(tcpc_polarity, tcpc_vconn));
}

int tcpm_set_msg_header(int port, int power_role, int data_role)
{
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_MSG_HDR_INFO,
			  TCPC_REG_MSG_HDR_INFO_SET(data_role, power_role));
}

int tcpm_alert_status(int port, int *alert)
{
	int rv;
	/* Read TCPC Alert register */
	rv = i2c_read16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			TCPC_REG_ALERT, alert);
	/*
	 * The PD protocol layer will process all alert bits
	 * returned by this function. Therefore, these bits
	 * can now be cleared from the TCPC register.
	 */
	i2c_write16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
		    TCPC_REG_ALERT, *alert);
	return rv;
}

int tcpm_set_rx_enable(int port, int enable)
{
	/* If enable, then set RX detect for SOP and HRST */
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_RX_DETECT,
			  enable ? TCPC_REG_RX_DETECT_SOP_HRST_MASK : 0);
}

int tcpm_alert_mask_set(int port, uint16_t mask)
{
	int rv;
	/* write to the Alert Mask register */
	rv = i2c_write16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			 TCPC_REG_ALERT_MASK, mask);

	if (rv)
		return rv;

	return rv;
}

int tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;

	rv = i2c_read8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
		       TCPC_REG_RX_BYTE_CNT, &cnt);

	rv |= i2c_read16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			 TCPC_REG_RX_HDR, (int *)head);

	/* If i2c read fails, return error */
	if (rv)
		return rv;

	if (cnt > 0) {
		i2c_lock(I2C_PORT_TCPC, 1);
		rv = i2c_xfer(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			      (uint8_t *)&reg, 1, (uint8_t *)payload,
			      cnt, I2C_XFER_SINGLE);
		i2c_lock(I2C_PORT_TCPC, 0);
	}

	/* TODO: need to write to alert reg to clear status */

	return rv;
}

int tcpm_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data)
{
	int reg = TCPC_REG_TX_DATA;
	int rv, cnt = 4*PD_HEADER_CNT(header);

	rv = i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			TCPC_REG_TX_BYTE_CNT, cnt);

	rv |= i2c_write16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_TX_HDR, header);

	/* If i2c read fails, return error */
	if (rv)
		return rv;

	if (cnt > 0) {
		i2c_lock(I2C_PORT_TCPC, 1);
		rv = i2c_xfer(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			      (uint8_t *)&reg, 1, NULL, 0, I2C_XFER_START);
		rv |= i2c_xfer(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			       (uint8_t *)data, cnt, NULL, 0, I2C_XFER_STOP);
		i2c_lock(I2C_PORT_TCPC, 0);
	}

	/* If i2c read fails, return error */
	if (rv)
		return rv;

	rv = i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			TCPC_REG_TRANSMIT, TCPC_REG_TRANSMIT_SET(type));

	return rv;
}

void tcpc_alert(int port)
{
	int status;

	/* Read the Alert register from the TCPC */
	tcpm_alert_status(port, &status);

	if (status & TCPC_REG_ALERT_CC_STATUS) {
		/* CC status changed, wake task */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
	}
	if (status & TCPC_REG_ALERT_RX_STATUS) {
		/* message received */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_RX, 0);
	}
	if (status & TCPC_REG_ALERT_RX_HARD_RST) {
		/* hard reset received */
		pd_execute_hard_reset(port);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
	if (status & TCPC_REG_ALERT_TX_COMPLETE) {
		/* transmit complete */
		pd_transmit_complete(port, status & TCPC_REG_ALERT_TX_SUCCESS ?
					   TCPC_TX_COMPLETE_SUCCESS :
					   TCPC_TX_COMPLETE_FAILED);
	}
}
