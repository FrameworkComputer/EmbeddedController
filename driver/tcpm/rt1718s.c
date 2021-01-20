/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * RT1718S TCPC Driver
 */

#include "console.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "stdint.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define RT1718S_SW_RESET_DELAY_MS 2

/* i2c_write function which won't wake TCPC from low power mode. */
int rt1718s_write8(int port, int reg, int val)
{
	if (reg > 0xFF) {
		return i2c_write_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, 1);
	}
	return tcpc_write(port, reg, val);
}

int rt1718s_read8(int port, int reg, int *val)
{
	if (reg > 0xFF) {
		return i2c_read_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, 1);
	}
	return tcpc_read(port, reg, val);
}

int rt1718s_update_bits8(int port, int reg, int mask, int val)
{
	int reg_val;

	if (mask == 0xFF)
		return rt1718s_write8(port, reg, val);

	RETURN_ERROR(rt1718s_read8(port, reg, &reg_val));

	reg_val &= (~mask);
	reg_val |= (mask & val);
	return rt1718s_write8(port, reg, reg_val);
}

static int rt1718s_sw_reset(int port)
{
	int rv;

	rv = rt1718s_update_bits8(port, RT1718S_SYS_CTRL3,
		RT1718S_SWRESET_MASK, 0xFF);

	msleep(RT1718S_SW_RESET_DELAY_MS);

	return rv;
}

static int rt1718s_init(int port)
{
	static bool need_sw_reset = true;

	if (!system_jumped_late() && need_sw_reset) {
		RETURN_ERROR(rt1718s_sw_reset(port));
		need_sw_reset = false;
	}

	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC))
		/* Set vbus frs low unmasked, Rx frs unmasked */
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT_MASK1,
					RT1718S_RT_MASK1_M_VBUS_FRS_LOW |
					RT1718S_RT_MASK1_M_RX_FRS,
					0xFF));


	/* Disable FOD function */
	RETURN_ERROR(rt1718s_update_bits8(port, 0xCF, 0x40, 0x00));

	/* Tcpc connect invalid disabled. Exit shipping mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL1,
				RT1718S_SYS_CTRL1_TCPC_CONN_INVALID, 0x00));
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL1,
				RT1718S_SYS_CTRL1_SHIPPING_OFF, 0xFF));

	/* Clear alert and fault */
	RETURN_ERROR(rt1718s_write8(port, TCPC_REG_FAULT_STATUS, 0xFF));
	RETURN_ERROR(tcpc_write16(port, TCPC_REG_ALERT, 0xFFFF));

	RETURN_ERROR(tcpci_tcpm_init(port));

	RETURN_ERROR(board_rt1718s_init(port));

	return EC_SUCCESS;
}

__overridable int board_rt1718s_init(int port)
{
	return EC_SUCCESS;
}

/* RT1718S is a TCPCI compatible port controller */
const struct tcpm_drv rt1718s_tcpm_drv = {
	.init			= &rt1718s_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable	= &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &tcpci_enter_low_power_mode,
#endif
};
