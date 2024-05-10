/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ANX7406 port manager */

#include "anx7406.h"
#include "assert.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, "ANX7406: " format, ##args)

const struct anx7406_i2c_addr anx7406_i2c_addrs_flags[] = {
	{ ANX7406_TCPC0_I2C_ADDR_FLAGS, ANX7406_TOP0_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC1_I2C_ADDR_FLAGS, ANX7406_TOP1_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC2_I2C_ADDR_FLAGS, ANX7406_TOP2_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC3_I2C_ADDR_FLAGS, ANX7406_TOP3_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC4_I2C_ADDR_FLAGS, ANX7406_TOP4_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC5_I2C_ADDR_FLAGS, ANX7406_TOP5_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC6_I2C_ADDR_FLAGS, ANX7406_TOP6_I2C_ADDR_FLAGS },
	{ ANX7406_TCPC7_I2C_ADDR_FLAGS, ANX7406_TOP7_I2C_ADDR_FLAGS },
};

static struct anx7406_i2c_addr i2c_peripheral[CONFIG_USB_PD_PORT_MAX_COUNT];

enum ec_error_list anx7406_set_gpio(int port, uint8_t gpio, bool value)
{
	if (gpio != 0) {
		CPRINTS("C%d: Setting GPIO%d not supported", port, gpio);
		return EC_ERROR_INVAL;
	}

	CPRINTS("C%d: Setting GPIO%u %s", port, gpio, value ? "high" : "low");

	return i2c_write8(tcpc_config[port].i2c_info.port,
			  i2c_peripheral[port].top_addr_flags,
			  ANX7406_REG_GPIO0,
			  value ? GPIO0_OUTPUT_HIGH : GPIO0_OUTPUT_LOW);
}

static int anx7406_set_hpd(int port, int hpd_lvl)
{
	int val;

	if (hpd_lvl) {
		CPRINTS("set hpd to HIGH");
		val = ANX7406_REG_HPD_OEN | HPD_DEGLITCH_TIME;
	} else {
		CPRINTS("set hpd to LOW");
		val = HPD_DEGLITCH_TIME;
	}

	return i2c_write8(tcpc_config[port].i2c_info.port,
			  i2c_peripheral[port].top_addr_flags,
			  ANX7406_REG_HPD_DEGLITCH_H, val);
}

int anx7406_hpd_reset(const int port)
{
	int rv;

	CPRINTS("HPD reset");
	rv = i2c_write8(tcpc_config[port].i2c_info.port,
			i2c_peripheral[port].top_addr_flags,
			ANX7406_REG_HPD_CTRL_0, 0);
	if (rv) {
		CPRINTS("Clear HPD_MODE failed: %d", rv);
		return rv;
	}

	return anx7406_set_hpd(port, 0);
}

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_timestamp[CONFIG_USB_PD_PORT_MAX_COUNT];
void anx7406_update_hpd_status(const struct usb_mux *mux, mux_state_t mux_state)
{
	int port = mux->usb_port;
	int hpd_lvl = (mux_state & USB_PD_MUX_HPD_LVL) ? 1 : 0;
	int hpd_irq = (mux_state & USB_PD_MUX_HPD_IRQ) ? 1 : 0;
	int rv;

	/*
	 * All calls within this method need to update to a mux_read/write calls
	 * that use the secondary address. This is a non-trival change and no
	 * one is using the anx7406 as a mux only (and probably never will since
	 * it doesn't have a re-driver). If that changes, we need to update this
	 * code.
	 */
	ASSERT(!(mux->flags & USB_MUX_FLAG_NOT_TCPC));

	anx7406_set_hpd(port, hpd_lvl);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_timestamp[port])
			crec_usleep(hpd_timestamp[port] - now);

		/*
		 * For generate hardware HPD IRQ, need set bit
		 * ANX7406_REG_HPD_IRQ0 first, then clear it. This bit is not
		 * write clear.
		 */
		rv = i2c_write8(tcpc_config[port].i2c_info.port,
				i2c_peripheral[port].top_addr_flags,
				ANX7406_REG_HPD_CTRL_0, ANX7406_REG_HPD_IRQ0);
		rv |= i2c_write8(tcpc_config[port].i2c_info.port,
				 i2c_peripheral[port].top_addr_flags,
				 ANX7406_REG_HPD_CTRL_0, 0);
		if (rv)
			CPRINTS("Generate HPD IRQ failed: %d", rv);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_timestamp[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

static int anx7406_init(int port)
{
	int rv, i;

	CPRINTS("C%d: init", port);
	/*
	 * find corresponding anx7406 TOP address according to
	 * specified TCPC address
	 */
	for (i = 0; i < ARRAY_SIZE(anx7406_i2c_addrs_flags); i++) {
		if (I2C_STRIP_FLAGS(tcpc_config[port].i2c_info.addr_flags) ==
		    I2C_STRIP_FLAGS(
			    anx7406_i2c_addrs_flags[i].tcpc_addr_flags)) {
			i2c_peripheral[port].tcpc_addr_flags =
				anx7406_i2c_addrs_flags[i].tcpc_addr_flags;
			i2c_peripheral[port].top_addr_flags =
				anx7406_i2c_addrs_flags[i].top_addr_flags;
			break;
		}
	}
	if (!I2C_STRIP_FLAGS(i2c_peripheral[port].top_addr_flags)) {
		CPRINTS("C%d: 0x%x is invalid", port,
			i2c_peripheral[port].top_addr_flags);
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Set VBUS OCP
	 *
	 * This is retried in case the TCPC just woke up from LPM. If you add
	 * I2C above, you need to retry that instead.
	 */
	rv = tcpc_write(port, ANX7406_REG_VBUS_OCP, OCP_THRESHOLD);
	if (rv) {
		/* Failed but this is expected if the chip is in LPM. */
		CPRINTS("C%d: Retrying to set OCP", port);
		crec_msleep(5);
		rv = tcpc_write(port, ANX7406_REG_VBUS_OCP, OCP_THRESHOLD);
		if (rv)
			return rv;
	}

	rv = tcpc_update8(port, TCPC_REG_TCPC_CTRL,
			  TCPC_REG_TCPC_CTRL_DEBUG_ACC_CONTROL, MASK_SET);
	if (rv)
		return rv;

	/* Disable CAP write protect */
	rv = tcpc_update8(port, ANX7406_REG_TCPCCTRL, ANX7406_REG_CAP_WP,
			  MASK_CLR);
	/* Disable bleed discharge */
	rv |= tcpc_update16(port, TCPC_REG_DEV_CAP_1,
			    TCPC_REG_DEV_CAP_1_BLEED_DISCHARGE, MASK_CLR);
	CPRINTS("C%d: TCPC config disable bleed discharge", port);
	/* Enable CAP write protect */
	rv |= tcpc_update8(port, ANX7406_REG_TCPCCTRL, ANX7406_REG_CAP_WP,
			   MASK_SET);
	if (rv)
		return rv;

	rv = tcpc_update8(port, TCPC_REG_POWER_STATUS,
			  TCPC_REG_POWER_STATUS_UNINIT, MASK_CLR);
	if (rv)
		return rv;

	rv = tcpci_tcpm_init(port);
	if (rv)
		return rv;

	rv = board_anx7406_init(port);
	if (rv)
		return rv;

	/* Let sink_ctrl & source_ctrl GPIO pin controlled by TCPC */
	tcpc_write(port, ANX7406_REG_VBUS_SOURCE_CTRL, SOURCE_GPIO_OEN);
	tcpc_write(port, ANX7406_REG_VBUS_SINK_CTRL, SINK_GPIO_OEN);

	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE)) {
		rv = tcpc_update8(port, TCPC_REG_ROLE_CTRL,
				  TCPC_REG_ROLE_CTRL_DRP_MASK, MASK_SET);
		if (rv)
			return rv;
	}

	/*
	 * Specifically disable voltage alarms, as VBUS_VOLTAGE_ALARM_HI may
	 * trigger repeatedly despite being masked (b/153989733)
	 */
	rv = tcpc_update16(port, TCPC_REG_POWER_CTRL,
			   TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS, MASK_SET);
	if (rv)
		return rv;

	/* TCPC Filter set to 512uS */
	rv = tcpc_write(port, ANX7406_REG_TCPCFILTER, 0xFF);
	rv |= tcpc_update8(port, ANX7406_REG_TCPCCTRL,
			   ANX7406_REG_TCPCFILTERBIT8, MASK_SET);
	if (rv)
		CPRINTS("C%d: TCPC filter set failed: %d", port, rv);

	rv = anx7406_hpd_reset(port);

	CPRINTS("C%d: init success", port);
	return rv;
}

static int anx7406_release(int port)
{
	return EC_SUCCESS;
}

static int anx7406_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	int rv;

	if (polarity_rm_dts(polarity))
		rv = tcpc_write(port, ANX7406_REG_VCONN_CTRL,
				VCONN_PWR_CTRL_SEL | VCONN_CC1_PWR_ENABLE);
	else
		rv = tcpc_write(port, ANX7406_REG_VCONN_CTRL,
				VCONN_PWR_CTRL_SEL | VCONN_CC2_PWR_ENABLE);

	rv |= anx7406_set_aux(port, polarity);

	if (rv)
		CPRINTS("Update VCONN power failed: %d, polarity: %d", rv,
			polarity);

	return tcpci_tcpm_set_polarity(port, polarity);
}

static int anx7406_m1_config(int port, int slave, int offset)
{
	int rv;

	/* Configure external I2C slave address */
	rv = i2c_write8(tcpc_config[port].i2c_info.port,
			i2c_peripheral[port].top_addr_flags, EXT_I2C1_ADDR,
			slave);
	/* Configure external I2C slave offset */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags, EXT_I2C1_OFFSET,
			 offset);
	/* Configure external I2C transfer byte count */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags,
			 EXT_I2C1_ACCESS_DATA_BYTE_CNT, 1);
	/* Clear DATA buffer */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags,
			 EXT_I2C1_ACCESS_CTRL, I2C1_DATA_CLR);
	/* Clear release */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags,
			 EXT_I2C1_ACCESS_CTRL, 0);

	return rv;
}

int anx7406_m1_read(int port, int slave, int offset)
{
	int rv, val;

	rv = anx7406_m1_config(port, slave, offset);

	/* Send I2C read command */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags, EXT_I2C1_CTRL,
			 I2C1_CMD_READ | I2C1_SPEED_100K);
	if (rv) {
		CPRINTS("initial cisco I2C master failed!");
		return rv;
	}

	crec_usleep(1000);

	/* Read I2C data out */
	rv = i2c_read8(tcpc_config[port].i2c_info.port,
		       i2c_peripheral[port].top_addr_flags,
		       EXT_I2C1_ACCESS_DATA, &val);
	if (rv) {
		CPRINTS("read cisco register failed!");
		return rv;
	}

	return val;
}

static int anx7406_m1_write(int port, int slave, int offset, int data)
{
	int rv;

	/* Configure external I2C slave address */
	rv = anx7406_m1_config(port, slave, offset);

	/* Configure I2C data */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags,
			 EXT_I2C1_ACCESS_DATA, data);
	/* Send I2C write command */
	rv |= i2c_write8(tcpc_config[port].i2c_info.port,
			 i2c_peripheral[port].top_addr_flags, EXT_I2C1_CTRL,
			 I2C1_CMD_WRITE | I2C1_SPEED_100K);
	if (rv) {
		CPRINTS("write data to cisco register failed!");
		return rv;
	}

	return 0;
}

int anx7406_set_aux(int port, int flip)
{
	int rv;

	CPRINTS("Set SBU %s flip", flip ? "" : "un");
	/* Configure SBU orientation */
	rv = anx7406_m1_write(port, I2C1_CISCO_SLAVE, I2C1_CISCO_LOCAL_REG,
			      SELECT_SBU_1_2);
	if (rv) {
		CPRINTS("Config CISCO_LOCAL_REG register failed");
		return rv;
	}

	/* Disable pull up/down */
	rv = anx7406_m1_write(port, I2C1_CISCO_SLAVE, I2C1_CISCO_CTRL_3,
			      flip ? AUX_FLIP_EN : 0);
	if (rv) {
		CPRINTS("Config CISCO_CTRL_3 register failed");
		return rv;
	}

	/* Disable pull up/down */
	rv = anx7406_m1_write(port, I2C1_CISCO_SLAVE, I2C1_CISCO_CTRL_1,
			      VBUS_PROTECT_750MA | AUX_PULL_DISABLE);
	if (rv) {
		CPRINTS("Config CISCO_CTRL_1 register failed");
		return rv;
	}

	return 0;
}

/*
 * ANX7406 is a TCPCI compatible port controller, with some caveats.
 * It seems to require both CC lines to be set always, instead of just
 * one at a time, according to TCPCI spec.  Thus, now that the TCPCI
 * driver more closely follows the spec, this driver requires
 * overrides for set_cc and set_polarity.
 */
const struct tcpm_drv anx7406_tcpm_drv = {
	.init = &anx7406_init,
	.release = &anx7406_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &tcpci_tcpm_set_cc,
	.set_polarity = &anx7406_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &tcpci_tcpm_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_PPC
	.set_snk_ctrl = &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &tcpci_enter_low_power_mode,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
#ifdef CONFIG_CMD_TCPC_DUMP
	.dump_registers = &tcpc_dump_std_registers,
#endif
};
