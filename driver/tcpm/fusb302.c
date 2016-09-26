/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Author: Gabe Noblesmith
 */

/* Type-C port manager for Fairchild's FUSB302 */

#include "console.h"
#include "fusb302.h"
#include "task.h"
#include "hooks.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

static struct fusb302_chip_state {
	int cc_polarity;
	int vconn_enabled;
	/* 1 = pulling up (DFP) 0 = pulling down (UFP) */
	int pulling_up;
	int rx_enable;
	int dfp_toggling_on;
	int previous_pull;
	int togdone_pullup_cc1;
	int togdone_pullup_cc2;
	int tx_hard_reset_req;
	struct mutex set_cc_lock;
	uint8_t mdac_vnc;
	uint8_t mdac_rd;
} state[CONFIG_USB_PD_PORT_COUNT];

/* bring the FUSB302 out of reset after Hard Reset signaling */
static void fusb302_pd_reset(int port)
{
	tcpc_write(port, TCPC_REG_RESET, TCPC_REG_RESET_PD_RESET);
}

static void fusb302_flush_rx_fifo(int port)
{
	/*
	 * other bits in the register _should_ be 0
	 * until the day we support other SOP* types...
	 * then we'll have to keep a shadow of what this register
	 * value should be so we don't clobber it here!
	 */
	tcpc_write(port, TCPC_REG_CONTROL1, TCPC_REG_CONTROL1_RX_FLUSH);
}

static void fusb302_flush_tx_fifo(int port)
{
	int reg;

	tcpc_read(port, TCPC_REG_CONTROL0, &reg);
	reg |= TCPC_REG_CONTROL0_TX_FLUSH;
	tcpc_write(port, TCPC_REG_CONTROL0, reg);
}

static void fusb302_auto_goodcrc_enable(int port, int enable)
{
	int reg;

	tcpc_read(port,	TCPC_REG_SWITCHES1, &reg);

	if (enable)
		reg |= TCPC_REG_SWITCHES1_AUTO_GCRC;
	else
		reg &= ~TCPC_REG_SWITCHES1_AUTO_GCRC;

	tcpc_write(port, TCPC_REG_SWITCHES1, reg);
}

/* Convert BC LVL values (in FUSB302) to Type-C CC Voltage Status */
static int convert_bc_lvl(int port, int bc_lvl)
{
	/* assume OPEN unless one of the following conditions is true... */
	int ret = TYPEC_CC_VOLT_OPEN;

	if (state[port].pulling_up) {
		if (bc_lvl == 0x00)
			ret = TYPEC_CC_VOLT_RA;
		else if (bc_lvl < 0x3)
			ret = TYPEC_CC_VOLT_RD;
	} else {
		if (bc_lvl == 0x1)
			ret = TYPEC_CC_VOLT_SNK_DEF;
		else if (bc_lvl == 0x2)
			ret = TYPEC_CC_VOLT_SNK_1_5;
		else if (bc_lvl == 0x3)
			ret = TYPEC_CC_VOLT_SNK_3_0;
	}

	return ret;
}

static int measure_cc_pin_source(int port, int cc_measure)
{
	int switches0_reg;
	int reg;
	int cc_lvl;

	/* Read status register */
	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);
	/* Save current value */
	switches0_reg = reg;
	/* Clear pull-up register settings and measure bits */
	reg &= ~(TCPC_REG_SWITCHES0_MEAS_CC1 | TCPC_REG_SWITCHES0_MEAS_CC2);
	/* Set desired pullup register bit */
	if (cc_measure == TCPC_REG_SWITCHES0_MEAS_CC1)
		reg |= TCPC_REG_SWITCHES0_CC1_PU_EN;
	else
		reg |= TCPC_REG_SWITCHES0_CC2_PU_EN;
	/* Set CC measure bit */
	reg |= cc_measure;

	/* Set measurement switch */
	tcpc_write(port, TCPC_REG_SWITCHES0, reg);

	/* Set MDAC for Open vs Rd/Ra comparison */
	tcpc_write(port, TCPC_REG_MEASURE, state[port].mdac_vnc);

	/* Wait on measurement */
	usleep(250);

	/* Read status register */
	tcpc_read(port, TCPC_REG_STATUS0, &reg);

	/* Assume open */
	cc_lvl = TYPEC_CC_VOLT_OPEN;

	/* CC level is below the 'no connect' threshold (vOpen) */
	if ((reg & TCPC_REG_STATUS0_COMP) == 0) {
		/* Set MDAC for Rd vs Ra comparison */
		tcpc_write(port, TCPC_REG_MEASURE, state[port].mdac_rd);

		/* Wait on measurement */
		usleep(250);

		/* Read status register */
		tcpc_read(port, TCPC_REG_STATUS0, &reg);

		cc_lvl = (reg & TCPC_REG_STATUS0_COMP) ? TYPEC_CC_VOLT_RD
						       : TYPEC_CC_VOLT_RA;
	}

	/* Restore SWITCHES0 register to its value prior */
	tcpc_write(port, TCPC_REG_SWITCHES0, switches0_reg);

	return cc_lvl;
}

/* Determine cc pin state for source when in manual detect mode */
static void detect_cc_pin_source_manual(int port, int *cc1_lvl, int *cc2_lvl)
{
	int cc1_measure = TCPC_REG_SWITCHES0_MEAS_CC1;
	int cc2_measure = TCPC_REG_SWITCHES0_MEAS_CC2;

	if (state[port].vconn_enabled) {
		/* If VCONN enabled, measure cc_pin that matches polarity */
		if (state[port].cc_polarity)
			*cc2_lvl = measure_cc_pin_source(port, cc2_measure);
		else
			*cc1_lvl = measure_cc_pin_source(port, cc1_measure);
	} else {
		/* If VCONN not enabled, measure both cc1 and cc2 */
		*cc1_lvl = measure_cc_pin_source(port, cc1_measure);
		*cc2_lvl = measure_cc_pin_source(port, cc2_measure);
	}

}

/* Determine cc pin state for sink */
static void detect_cc_pin_sink(int port, int *cc1, int *cc2)
{
	int reg;
	int orig_meas_cc1;
	int orig_meas_cc2;
	int bc_lvl_cc1;
	int bc_lvl_cc2;

	/*
	 * Measure CC1 first.
	 */
	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

	/* save original state to be returned to later... */
	if (reg & TCPC_REG_SWITCHES0_MEAS_CC1)
		orig_meas_cc1 = 1;
	else
		orig_meas_cc1 = 0;

	if (reg & TCPC_REG_SWITCHES0_MEAS_CC2)
		orig_meas_cc2 = 1;
	else
		orig_meas_cc2 = 0;


	/* Disable CC2 measurement switch, enable CC1 measurement switch */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;
	reg |= TCPC_REG_SWITCHES0_MEAS_CC1;

	tcpc_write(port, TCPC_REG_SWITCHES0, reg);

	/* CC1 is now being measured by FUSB302. */

	/* Wait on measurement */
	usleep(250);

	tcpc_read(port, TCPC_REG_STATUS0, &bc_lvl_cc1);

	/* mask away unwanted bits */
	bc_lvl_cc1 &= (TCPC_REG_STATUS0_BC_LVL0 | TCPC_REG_STATUS0_BC_LVL1);

	/*
	 * Measure CC2 next.
	 */

	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

	/* Disable CC1 measurement switch, enable CC2 measurement switch */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	reg |= TCPC_REG_SWITCHES0_MEAS_CC2;

	tcpc_write(port, TCPC_REG_SWITCHES0, reg);

	/* CC2 is now being measured by FUSB302. */

	/* Wait on measurement */
	usleep(250);

	tcpc_read(port, TCPC_REG_STATUS0, &bc_lvl_cc2);

	/* mask away unwanted bits */
	bc_lvl_cc2 &= (TCPC_REG_STATUS0_BC_LVL0 | TCPC_REG_STATUS0_BC_LVL1);

	*cc1 = convert_bc_lvl(port, bc_lvl_cc1);
	*cc2 = convert_bc_lvl(port, bc_lvl_cc2);

	/* return MEAS_CC1/2 switches to original state */
	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);
	if (orig_meas_cc1)
		reg |= TCPC_REG_SWITCHES0_MEAS_CC1;
	else
		reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	if (orig_meas_cc2)
		reg |= TCPC_REG_SWITCHES0_MEAS_CC2;
	else
		reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;

	tcpc_write(port, TCPC_REG_SWITCHES0, reg);
}

/* Parse header bytes for the size of packet */
static int get_num_bytes(uint16_t header)
{
	int rv;

	/* Grab the Number of Data Objects field.*/
	rv = PD_HEADER_CNT(header);

	/* Multiply by four to go from 32-bit words -> bytes */
	rv *= 4;

	/* Plus 2 for header */
	rv += 2;

	return rv;
}

static int fusb302_send_message(int port, uint16_t header, const uint32_t *data,
				 uint8_t *buf, int buf_pos)
{
	int rv;
	int reg;
	int len;

	len = get_num_bytes(header);

	/*
	 * packsym tells the TXFIFO that the next X bytes are payload,
	 * and should not be interpreted as special tokens.
	 * The 5 LSBs represent X, the number of bytes.
	 */
	reg = FUSB302_TKN_PACKSYM;
	reg |= (len & 0x1F);

	buf[buf_pos++] = reg;

	/* write in the header */
	reg = header;
	buf[buf_pos++] = reg & 0xFF;

	reg >>= 8;
	buf[buf_pos++] = reg & 0xFF;

	/* header is done, subtract from length to make this for-loop simpler */
	len -= 2;

	/* write data objects, if present */
	memcpy(&buf[buf_pos], data, len);
	buf_pos += len;

	/* put in the CRC */
	buf[buf_pos++] = FUSB302_TKN_JAMCRC;

	/* put in EOP */
	buf[buf_pos++] = FUSB302_TKN_EOP;

	/* Turn transmitter off after sending message */
	buf[buf_pos++] = FUSB302_TKN_TXOFF;

	/* Start transmission */
	reg = FUSB302_TKN_TXON;
	buf[buf_pos++] = FUSB302_TKN_TXON;

	/* burst write for speed! */
	tcpc_lock(port, 1);
	rv = tcpc_xfer(port, buf, buf_pos, 0, 0, I2C_XFER_SINGLE);
	tcpc_lock(port, 0);

	return rv;
}

static int fusb302_tcpm_select_rp_value(int port, int rp)
{
	int reg;
	int rv;
	uint8_t vnc, rd;

	rv = tcpc_read(port, TCPC_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	/* Set the current source for Rp value */
	reg &= ~TCPC_REG_CONTROL0_HOST_CUR_MASK;
	switch (rp) {
	case TYPEC_RP_1A5:
		reg |= TCPC_REG_CONTROL0_HOST_CUR_1A5;
		vnc = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_1_5_VNC_MV);
		rd = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_1_5_RD_THRESH_MV);
		break;
	case TYPEC_RP_3A0:
		reg |= TCPC_REG_CONTROL0_HOST_CUR_3A0;
		vnc = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_3_0_VNC_MV);
		rd = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_3_0_RD_THRESH_MV);
		break;
	case TYPEC_RP_USB:
	default:
		reg |= TCPC_REG_CONTROL0_HOST_CUR_USB;
		vnc = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_DEF_VNC_MV);
		rd = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_DEF_RD_THRESH_MV);
	}
	state[port].mdac_vnc = vnc;
	state[port].mdac_rd = rd;
	return tcpc_write(port, TCPC_REG_CONTROL0, reg);
}

static int fusb302_tcpm_init(int port)
{
	int reg;

	/* set default */
	state[port].cc_polarity = -1;

	state[port].previous_pull = TYPEC_CC_RD;
	/* set the voltage threshold for no connect detection (vOpen) */
	state[port].mdac_vnc = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_DEF_VNC_MV);
	/* set the voltage threshold for Rd vs Ra detection */
	state[port].mdac_rd = TCPC_REG_MEASURE_MDAC_MV(PD_SRC_DEF_RD_THRESH_MV);

	/* all other variables assumed to default to 0 */

	/* Restore default settings */
	tcpc_write(port, TCPC_REG_RESET, TCPC_REG_RESET_SW_RESET);

	/* Turn on retries and set number of retries */
	tcpc_read(port, TCPC_REG_CONTROL3, &reg);
	reg |= TCPC_REG_CONTROL3_AUTO_RETRY;
	reg |= (PD_RETRY_COUNT & 0x3) <<
		TCPC_REG_CONTROL3_N_RETRIES_POS;
	tcpc_write(port, TCPC_REG_CONTROL3, reg);

	/* Create interrupt masks */
	reg = 0xFF;
	/* CC level changes */
	reg &= ~TCPC_REG_MASK_BC_LVL;
	/* collisions */
	reg &= ~TCPC_REG_MASK_COLLISION;
	/* misc alert */
	reg &= ~TCPC_REG_MASK_ALERT;
	tcpc_write(port, TCPC_REG_MASK, reg);

	reg = 0xFF;
	/* when all pd message retries fail... */
	reg &= ~TCPC_REG_MASKA_RETRYFAIL;
	/* when fusb302 send a hard reset. */
	reg &= ~TCPC_REG_MASKA_HARDSENT;
	/* when fusb302 receives GoodCRC ack for a pd message */
	reg &= ~TCPC_REG_MASKA_TX_SUCCESS;
	/* when fusb302 receives a hard reset */
	reg &= ~TCPC_REG_MASKA_HARDRESET;
	tcpc_write(port, TCPC_REG_MASKA, reg);

	reg = 0xFF;
	/* when fusb302 sends GoodCRC to ack a pd message */
	reg &= ~TCPC_REG_MASKB_GCRCSENT;
	tcpc_write(port, TCPC_REG_MASKB, reg);

	/* Interrupt Enable */
	tcpc_read(port, TCPC_REG_CONTROL0, &reg);
	reg &= ~TCPC_REG_CONTROL0_INT_MASK;
	tcpc_write(port, TCPC_REG_CONTROL0, reg);

	/* Set VCONN switch defaults */
	tcpm_set_polarity(port, 0);
	tcpm_set_vconn(port, 0);

	/* Turn on the power! */
	/* TODO: Reduce power consumption */
	tcpc_write(port, TCPC_REG_POWER, TCPC_REG_POWER_PWR_ALL);

	return 0;
}

static int fusb302_tcpm_get_cc(int port, int *cc1, int *cc2)
{
	/*
	 * can't measure while doing DFP toggling -
	 * FUSB302 takes control of the switches.
	 * During this time, tell software that CCs are open -
	 * at least until we get the TOGDONE interrupt...
	 * which signals that the hardware found something.
	 */
	if (state[port].dfp_toggling_on) {
		*cc1 = TYPEC_CC_VOLT_OPEN;
		*cc2 = TYPEC_CC_VOLT_OPEN;
		return 0;
	}

	if (state[port].pulling_up) {
		/* Source mode? */
		detect_cc_pin_source_manual(port, cc1, cc2);
	} else {
		/* Sink mode? */
		detect_cc_pin_sink(port, cc1, cc2);
	}

	return 0;
}

static int fusb302_tcpm_set_cc(int port, int pull)
{
	int reg;

	/*
	 * Ensure we aren't in the process of changing CC from the alert
	 * handler, then cancel any pending toggle-triggered CC change.
	 */
	mutex_lock(&state[port].set_cc_lock);
	state[port].dfp_toggling_on = 0;
	mutex_unlock(&state[port].set_cc_lock);

	state[port].previous_pull = pull;

	/* NOTE: FUSB302 toggles a single pull-up between CC1 and CC2 */
	/* NOTE: FUSB302 Does not support Ra. */
	switch (pull) {
	case TYPEC_CC_RP:
		/* enable the pull-up we know to be necessary */
		tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

		reg &= ~(TCPC_REG_SWITCHES0_CC2_PU_EN |
			 TCPC_REG_SWITCHES0_CC1_PU_EN |
			 TCPC_REG_SWITCHES0_CC1_PD_EN |
			 TCPC_REG_SWITCHES0_CC2_PD_EN |
			 TCPC_REG_SWITCHES0_VCONN_CC1 |
			 TCPC_REG_SWITCHES0_VCONN_CC2);

		reg |= TCPC_REG_SWITCHES0_CC1_PU_EN |
			TCPC_REG_SWITCHES0_CC2_PU_EN;

		if (state[port].vconn_enabled)
			reg |= state[port].togdone_pullup_cc1 ?
			       TCPC_REG_SWITCHES0_VCONN_CC2 :
			       TCPC_REG_SWITCHES0_VCONN_CC1;

		tcpc_write(port, TCPC_REG_SWITCHES0, reg);

		state[port].pulling_up = 1;
		state[port].dfp_toggling_on = 0;
		break;
	case TYPEC_CC_RD:
		/* Enable UFP Mode */

		/* turn off toggle */
		tcpc_read(port, TCPC_REG_CONTROL2, &reg);
		reg &= ~TCPC_REG_CONTROL2_TOGGLE;
		tcpc_write(port, TCPC_REG_CONTROL2, reg);

		/* enable pull-downs, disable pullups */
		tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

		reg &= ~(TCPC_REG_SWITCHES0_CC2_PU_EN);
		reg &= ~(TCPC_REG_SWITCHES0_CC1_PU_EN);
		reg |= (TCPC_REG_SWITCHES0_CC1_PD_EN);
		reg |= (TCPC_REG_SWITCHES0_CC2_PD_EN);
		tcpc_write(port, TCPC_REG_SWITCHES0, reg);

		state[port].pulling_up = 0;
		state[port].dfp_toggling_on = 0;
		break;
	case TYPEC_CC_OPEN:
		/* Disable toggling */
		tcpc_read(port, TCPC_REG_CONTROL2, &reg);
		reg &= ~TCPC_REG_CONTROL2_TOGGLE;
		tcpc_write(port, TCPC_REG_CONTROL2, reg);

		/* Ensure manual switches are opened */
		tcpc_read(port, TCPC_REG_SWITCHES0, &reg);
		reg &= ~TCPC_REG_SWITCHES0_CC1_PU_EN;
		reg &= ~TCPC_REG_SWITCHES0_CC2_PU_EN;
		reg &= ~TCPC_REG_SWITCHES0_CC1_PD_EN;
		reg &= ~TCPC_REG_SWITCHES0_CC2_PD_EN;
		tcpc_write(port, TCPC_REG_SWITCHES0, reg);

		state[port].pulling_up = 0;
		state[port].dfp_toggling_on = 0;
		break;
	default:
		/* Unsupported... */
		return EC_ERROR_UNIMPLEMENTED;
	}
	return 0;
}

static int fusb302_tcpm_set_polarity(int port, int polarity)
{
	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	int reg;

	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

	/* clear VCONN switch bits */
	reg &= ~TCPC_REG_SWITCHES0_VCONN_CC1;
	reg &= ~TCPC_REG_SWITCHES0_VCONN_CC2;

	if (state[port].vconn_enabled) {
		/* set VCONN switch to be non-CC line */
		if (polarity)
			reg |= TCPC_REG_SWITCHES0_VCONN_CC1;
		else
			reg |= TCPC_REG_SWITCHES0_VCONN_CC2;
	}

	/* clear meas_cc bits (RX line select) */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;

	/* set rx polarity */
	if (polarity)
		reg |= TCPC_REG_SWITCHES0_MEAS_CC2;
	else
		reg |= TCPC_REG_SWITCHES0_MEAS_CC1;

	tcpc_write(port, TCPC_REG_SWITCHES0, reg);

	tcpc_read(port, TCPC_REG_SWITCHES1, &reg);

	/* clear tx_cc bits */
	reg &= ~TCPC_REG_SWITCHES1_TXCC1_EN;
	reg &= ~TCPC_REG_SWITCHES1_TXCC2_EN;

	/* set tx polarity */
	if (polarity)
		reg |= TCPC_REG_SWITCHES1_TXCC2_EN;
	else
		reg |= TCPC_REG_SWITCHES1_TXCC1_EN;

	tcpc_write(port, TCPC_REG_SWITCHES1, reg);

	/* Save the polarity for later */
	state[port].cc_polarity = polarity;

	return 0;
}

static int fusb302_tcpm_set_vconn(int port, int enable)
{
	/*
	 * FUSB302 does not have dedicated VCONN Enable switch.
	 * We'll get through this by disabling both of the
	 * VCONN - CC* switches to disable, and enabling the
	 * saved polarity when enabling.
	 * Therefore at startup, tcpm_set_polarity should be called first,
	 * or else live with the default put into tcpm_init.
	 */
	int reg;

	/* save enable state for later use */
	state[port].vconn_enabled = enable;

	if (enable) {
		/* set to saved polarity */
		tcpm_set_polarity(port, state[port].cc_polarity);
	} else {

		tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

		/* clear VCONN switch bits */
		reg &= ~TCPC_REG_SWITCHES0_VCONN_CC1;
		reg &= ~TCPC_REG_SWITCHES0_VCONN_CC2;

		tcpc_write(port, TCPC_REG_SWITCHES0, reg);
	}

	return 0;
}

static int fusb302_tcpm_set_msg_header(int port, int power_role, int data_role)
{
	int reg;

	tcpc_read(port, TCPC_REG_SWITCHES1, &reg);

	reg &= ~TCPC_REG_SWITCHES1_POWERROLE;
	reg &= ~TCPC_REG_SWITCHES1_DATAROLE;

	if (power_role)
		reg |= TCPC_REG_SWITCHES1_POWERROLE;
	if (data_role)
		reg |= TCPC_REG_SWITCHES1_DATAROLE;

	tcpc_write(port, TCPC_REG_SWITCHES1, reg);

	return 0;
}

static int fusb302_tcpm_set_rx_enable(int port, int enable)
{
	int reg;

	state[port].rx_enable = enable;

	/* Get current switch state */
	tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

	/* Clear CC1/CC2 measure bits */
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC1;
	reg &= ~TCPC_REG_SWITCHES0_MEAS_CC2;

	if (enable) {
		switch (state[port].cc_polarity) {
		/* if CC polarity hasnt been determined, can't enable */
		case -1:
			return EC_ERROR_UNKNOWN;
		case 0:
			reg |= TCPC_REG_SWITCHES0_MEAS_CC1;
			break;
		case 1:
			reg |= TCPC_REG_SWITCHES0_MEAS_CC2;
			break;
		default:
			/* "shouldn't get here" */
			return EC_ERROR_UNKNOWN;
		}
		tcpc_write(port, TCPC_REG_SWITCHES0, reg);

		/* flush rx fifo in case messages have been coming our way */
		fusb302_flush_rx_fifo(port);


	} else {
		/*
		 * bit of a hack here.
		 * when this function is called to disable rx (enable=0)
		 * using it as an indication of detach (gulp!)
		 * to reset our knowledge of where
		 * the toggle state machine landed.
		 */
		state[port].togdone_pullup_cc1 = 0;
		state[port].togdone_pullup_cc2 = 0;

		tcpm_set_cc(port, state[port].previous_pull);

		tcpc_write(port, TCPC_REG_SWITCHES0, reg);
	}

	fusb302_auto_goodcrc_enable(port, enable);

	return 0;
}

static int fusb302_tcpm_get_message(int port, uint32_t *payload, int *head)
{
	/*
	 * this is the buffer that will get the burst-read data
	 * from the fusb302.
	 *
	 * it's re-used in a couple different spots, the worst of which
	 * is the PD packet (not header) and CRC.
	 * maximum size necessary = 28 + 4 = 32
	 */
	uint8_t buf[32];
	int rv = 0;
	int len;

	/* NOTE: Assuming enough memory has been allocated for payload. */

	/*
	 * PART 1 OF BURST READ: Write in register address.
	 * Issue a START, no STOP.
	 */
	tcpc_lock(port, 1);
	buf[0] = TCPC_REG_FIFOS;
	rv |= tcpc_xfer(port, buf, 1, 0, 0, I2C_XFER_START);

	/*
	 * PART 2 OF BURST READ: Read up to the header.
	 * Issue a repeated START, no STOP.
	 * only grab three bytes so we can get the header
	 * and determine how many more bytes we need to read.
	 */
	rv |= tcpc_xfer(port, 0, 0, buf, 3, I2C_XFER_START);

	/* Grab the header */
	*head = (buf[1] & 0xFF);
	*head |= ((buf[2] << 8) & 0xFF00);

	/* figure out packet length, subtract header bytes */
	len = get_num_bytes(*head) - 2;

	/*
	 * PART 3 OF BURST READ: Read everything else.
	 * No START, but do issue a STOP at the end.
	 * add 4 to len to read CRC out
	 */
	rv |= tcpc_xfer(port, 0, 0, buf, len+4, I2C_XFER_STOP);

	tcpc_lock(port, 0);

	/* return the data */
	memcpy(payload, buf, len);

	return rv;
}

static int fusb302_tcpm_transmit(int port, enum tcpm_transmit_type type,
				 uint16_t header, const uint32_t *data)
{
	/*
	 * this is the buffer that will be burst-written into the fusb302
	 * maximum size necessary =
	 * 1: FIFO register address
	 * 4: SOP* tokens
	 * 1: Token that signifies "next X bytes are not tokens"
	 * 30: 2 for header and up to 7*4 = 28 for rest of message
	 * 1: "Insert CRC" Token
	 * 1: EOP Token
	 * 1: "Turn transmitter off" token
	 * 1: "Star Transmission" Command
	 * -
	 * 40: 40 bytes worst-case
	 */
	uint8_t buf[40];
	int buf_pos = 0;

	int reg;

	/* Flush the TXFIFO */
	fusb302_flush_tx_fifo(port);

	switch (type) {
	case TCPC_TX_SOP:

		/* put register address first for of burst tcpc write */
		buf[buf_pos++] = TCPC_REG_FIFOS;

		/* Write the SOP Ordered Set into TX FIFO */
		buf[buf_pos++] = FUSB302_TKN_SYNC1;
		buf[buf_pos++] = FUSB302_TKN_SYNC1;
		buf[buf_pos++] = FUSB302_TKN_SYNC1;
		buf[buf_pos++] = FUSB302_TKN_SYNC2;

		return fusb302_send_message(port, header, data, buf, buf_pos);
	case TCPC_TX_HARD_RESET:
		state[port].tx_hard_reset_req = 1;

		/* Simply hit the SEND_HARD_RESET bit */
		tcpc_read(port, TCPC_REG_CONTROL3, &reg);
		reg |= TCPC_REG_CONTROL3_SEND_HARDRESET;
		tcpc_write(port, TCPC_REG_CONTROL3, reg);

		break;
	case TCPC_TX_BIST_MODE_2:
		/* Simply hit the BIST_MODE2 bit */
		tcpc_read(port, TCPC_REG_CONTROL1, &reg);
		reg |= TCPC_REG_CONTROL1_BIST_MODE2;
		tcpc_write(port, TCPC_REG_CONTROL1, reg);
		break;
	default:
		return EC_ERROR_UNIMPLEMENTED;
	}

	return 0;
}

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
static int fusb302_tcpm_get_vbus_level(int port)
{
	int reg;

	/* Read status register */
	tcpc_read(port, TCPC_REG_STATUS0, &reg);

	return (reg & TCPC_REG_STATUS0_VBUSOK) ? 1 : 0;
}
#endif

void fusb302_tcpc_alert(int port)
{
	/* interrupt has been received */
	int interrupt;
	int interrupta;
	int interruptb;
	int reg;
	int toggle_answer;
	int head;
	uint32_t payload[7];

	/* reading interrupt registers clears them */

	tcpc_read(port, TCPC_REG_INTERRUPT, &interrupt);
	tcpc_read(port, TCPC_REG_INTERRUPTA, &interrupta);
	tcpc_read(port, TCPC_REG_INTERRUPTB, &interruptb);

	if (interrupt & TCPC_REG_INTERRUPT_BC_LVL) {
		/* CC Status change */
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
	}

	if (interrupt & TCPC_REG_INTERRUPT_COLLISION) {
		/* packet sending collided */
		state[port].tx_hard_reset_req = 0;
		pd_transmit_complete(port, TCPC_TX_COMPLETE_FAILED);
	}

	if (interrupta & TCPC_REG_INTERRUPTA_TX_SUCCESS) {
		/*
		 * Sent packet was acknowledged with a GoodCRC,
		 * so remove GoodCRC message from FIFO.
		 */
		tcpm_get_message(port, payload, &head);

		pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);
	}

	if (interrupta & TCPC_REG_INTERRUPTA_TOGDONE) {
		/* Don't allow other tasks to change CC PUs */
		mutex_lock(&state[port].set_cc_lock);
		/* If our toggle request is obsolete then we're done */
		if (state[port].dfp_toggling_on) {
			/* read what 302 settled on for an answer...*/
			tcpc_read(port, TCPC_REG_STATUS1A, &reg);
			reg = reg >> TCPC_REG_STATUS1A_TOGSS_POS;
			reg = reg & TCPC_REG_STATUS1A_TOGSS_MASK;

			toggle_answer = reg;

			/* Turn off toggle so we can take over the switches */
			tcpc_read(port, TCPC_REG_CONTROL2, &reg);
			reg &= ~TCPC_REG_CONTROL2_TOGGLE;
			tcpc_write(port, TCPC_REG_CONTROL2, reg);

			switch (toggle_answer) {
			case TCPC_REG_STATUS1A_TOGSS_SRC1:
				state[port].togdone_pullup_cc1 = 1;
				state[port].togdone_pullup_cc2 = 0;
				break;
			case TCPC_REG_STATUS1A_TOGSS_SRC2:
				state[port].togdone_pullup_cc1 = 0;
				state[port].togdone_pullup_cc2 = 1;
				break;
			case TCPC_REG_STATUS1A_TOGSS_SNK1:
			case TCPC_REG_STATUS1A_TOGSS_SNK2:
			case TCPC_REG_STATUS1A_TOGSS_AA:
				state[port].togdone_pullup_cc1 = 0;
				state[port].togdone_pullup_cc2 = 0;
				break;
			default:
				/* TODO: should never get here, but? */
				ASSERT(0);
				break;
			}

			/* enable the pull-up we know to be necessary */
			tcpc_read(port, TCPC_REG_SWITCHES0, &reg);

			reg &= ~(TCPC_REG_SWITCHES0_CC2_PU_EN |
				 TCPC_REG_SWITCHES0_CC1_PU_EN |
				 TCPC_REG_SWITCHES0_CC1_PD_EN |
				 TCPC_REG_SWITCHES0_CC2_PD_EN |
				 TCPC_REG_SWITCHES0_VCONN_CC1 |
				 TCPC_REG_SWITCHES0_VCONN_CC2);

			reg |= TCPC_REG_SWITCHES0_CC1_PU_EN |
			       TCPC_REG_SWITCHES0_CC2_PU_EN;

			if (state[port].vconn_enabled)
				reg |= state[port].togdone_pullup_cc1 ?
				       TCPC_REG_SWITCHES0_VCONN_CC2 :
				       TCPC_REG_SWITCHES0_VCONN_CC1;

			tcpc_write(port, TCPC_REG_SWITCHES0, reg);
			/* toggle done */
			state[port].dfp_toggling_on = 0;
		}

		mutex_unlock(&state[port].set_cc_lock);
	}

	if (interrupta & TCPC_REG_INTERRUPTA_RETRYFAIL) {
		/* all retries have failed to get a GoodCRC */
		pd_transmit_complete(port, TCPC_TX_COMPLETE_FAILED);
	}

	if (interrupta & TCPC_REG_INTERRUPTA_HARDSENT) {
		/* hard reset has been sent */

		if (state[port].tx_hard_reset_req) {
			state[port].tx_hard_reset_req = 0;
			/* bring FUSB302 out of reset */
			fusb302_pd_reset(port);

			pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);
		}
	}

	if (interrupta & TCPC_REG_INTERRUPTA_HARDRESET) {
		/* hard reset has been received */

		/* bring FUSB302 out of reset */
		fusb302_pd_reset(port);

		pd_execute_hard_reset(port);

		task_wake(PD_PORT_TO_TASK_ID(port));
	}

	if (interruptb & TCPC_REG_INTERRUPTB_GCRCSENT) {
		/* Packet received and GoodCRC sent */
		/* (this interrupt fires after the GoodCRC finishes) */
		if (state[port].rx_enable) {
			task_set_event(PD_PORT_TO_TASK_ID(port),
					PD_EVENT_RX, 0);
		} else {
			/* flush rx fifo if rx isn't enabled */
			fusb302_flush_rx_fifo(port);
		}
	}

}

void tcpm_set_bist_test_data(int port)
{
	int reg;

	/* Read control3 register */
	tcpc_read(port, TCPC_REG_CONTROL3, &reg);

	/* Set the BIST_TMODE bit (Clears on Hard Reset) */
	reg |= TCPC_REG_CONTROL3_BIST_TMODE;

	/* Write the updated value */
	tcpc_write(port, TCPC_REG_CONTROL3, reg);
}

const struct tcpm_drv fusb302_tcpm_drv = {
	.init			= &fusb302_tcpm_init,
	.get_cc			= &fusb302_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.get_vbus_level		= &fusb302_tcpm_get_vbus_level,
#endif
	.select_rp_value	= &fusb302_tcpm_select_rp_value,
	.set_cc			= &fusb302_tcpm_set_cc,
	.set_polarity		= &fusb302_tcpm_set_polarity,
	.set_vconn		= &fusb302_tcpm_set_vconn,
	.set_msg_header		= &fusb302_tcpm_set_msg_header,
	.set_rx_enable		= &fusb302_tcpm_set_rx_enable,
	.get_message		= &fusb302_tcpm_get_message,
	.transmit		= &fusb302_tcpm_transmit,
	.tcpc_alert		= &fusb302_tcpc_alert,
};
