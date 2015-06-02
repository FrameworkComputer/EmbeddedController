/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C port manager */

#include "i2c.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include "console.h"


/* Convert port number to tcpc i2c address */
#define I2C_ADDR_TCPC(p) (CONFIG_TCPC_I2C_BASE_ADDR + 2*(p))

static int tcpc_polarity, tcpc_vconn;

int tcpm_get_cc(int port, int *cc1, int *cc2)
{
	int status;
	int rv;

	rv = i2c_read16(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			TCPC_REG_CC1_STATUS, &status);

	/* If i2c read fails, return error */
	if (rv)
		return rv;

	*cc1 = TCPC_REG_CC_STATUS_VOLT(status & 0xff);
	*cc2 = TCPC_REG_CC_STATUS_VOLT((status >> 8) & 0xff);

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

int tcpm_alert_status(int port, int alert_reg, uint8_t *alert)
{
	return i2c_read8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			 alert_reg, (int *)alert);
}

int tcpm_set_rx_enable(int port, int enable)
{
	/* If enable, then set RX detect for SOP and HRST */
	return i2c_write8(I2C_PORT_TCPC, I2C_ADDR_TCPC(port),
			  TCPC_REG_RX_DETECT,
			  enable ? TCPC_REG_RX_DETECT_SOP_HRST_MASK : 0);
}

int tcpm_get_message(int port, uint32_t *payload, int *head)
{
	int rv, cnt, reg = TCPC_REG_RX_DATA;

	/* TODO: need to first read TCPC_REG_RX_STATUS to check if SOP */

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
