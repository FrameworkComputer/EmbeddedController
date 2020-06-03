/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "anx7447.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP)

/* Used to fake VBUS presence since no GPIO is available to read VBUS */
static int vbus_present;

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000, 3000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 900, PDO_FIXED_FLAGS),
		PDO_BATT(5000, 21000, 30000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

#if defined(CONFIG_USB_PD_TCPM_MUX) && defined(CONFIG_USB_PD_TCPM_ANX7447)
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.driver    = &anx7447_usb_mux_driver,
	},
};
#endif

#ifdef CONFIG_USB_PD_TCPM_ANX7447
int pd_set_power_supply_ready(int port)
{
	/* Disable charging */
	anx7447_board_charging_enable(port, 0);

	/* Provide VBUS */
	gpio_set_level(GPIO_VBUS_PMIC_CTRL, 1);
	anx7447_set_power_supply_ready(port);

	/* notify host of power info change */

	CPRINTS("Enable VBUS, port%d", port);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	anx7447_power_supply_reset(port);
	gpio_set_level(GPIO_VBUS_PMIC_CTRL, 0);
	CPRINTS("Disable VBUS, port%d", port);

	/* Enable charging */
	anx7447_board_charging_enable(port, 1);
}
#else
int pd_set_power_supply_ready(int port)
{
	/* Turn on the "up" LED when we output VBUS */
	gpio_set_level(GPIO_LED_U, 1);
	CPRINTS("Power supply ready/%d", port);
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Turn off the "up" LED when we shutdown VBUS */
	gpio_set_level(GPIO_LED_U, 0);
	/* Disable VBUS */
	CPRINTS("Disable VBUS", port);
}
#endif /* CONFIG_USB_PD_TCPM_ANX7447 */

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	CPRINTS("USBPD current limit port %d max %d mA %d mV",
		port, max_ma, supply_voltage);
	/* do some LED coding of the power we can sink */
	if (max_ma) {
		if (supply_voltage > 6500)
			gpio_set_level(GPIO_LED_R, 1);
		else
			gpio_set_level(GPIO_LED_L, 1);
	} else {
		gpio_set_level(GPIO_LED_L, 0);
		gpio_set_level(GPIO_LED_R, 0);
	}
}

__override void typec_set_input_current_limit(int port, uint32_t max_ma,
					      uint32_t supply_voltage)
{
	CPRINTS("TYPEC current limit port %d max %d mA %d mV",
		port, max_ma, supply_voltage);
	gpio_set_level(GPIO_LED_R, !!max_ma);
}

void button_event(enum gpio_signal signal)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);
}

static int command_vbus_toggle(int argc, char **argv)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(vbus, command_vbus_toggle,
			"",
			"Toggle VBUS detected");

int pd_snk_is_vbus_provided(int port)
{
	return vbus_present;
}

__override int pd_check_data_swap(int port,
				  enum pd_data_role data_role)
{
	/* Always allow data swap */
	return 1;
}

#ifdef CONFIG_USBC_VCONN_SWAP
int pd_check_vconn_swap(int port)
{
	/*
	 * Allow vconn swap as long as we are acting as a dual role device,
	 * otherwise assume our role is fixed (not in S0 or console command
	 * to fix our role).
	 */
	return pd_get_dual_role(port) == PD_DRP_TOGGLE_ON;
}
#endif

__override void pd_check_pr_role(int port,
				 enum pd_power_role pr_role,
				 int flags)
{
}

__override void pd_check_dr_role(int port,
				 enum pd_data_role dr_role,
				 int flags)
{
}
/* ----------------- Vendor Defined Messages ------------------ */
const uint32_t vdo_idh = VDO_IDH(1, /* data caps as USB host */
				 0, /* data caps as USB device */
				 IDH_PTYPE_PERIPH,
				 0, /* supports alt modes */
				 0x0000);

const uint32_t vdo_product = VDO_PRODUCT(0x0000, 0x0000);

static int svdm_response_identity(int port, uint32_t *payload)
{
	payload[VDO_I(IDH)] = vdo_idh;
	payload[VDO_I(CSTAT)] = VDO_CSTAT(0);
	payload[VDO_I(PRODUCT)] = vdo_product;
	return VDO_I(PRODUCT) + 1;
}

__override const struct svdm_response svdm_rsp = {
	.identity = &svdm_response_identity,
	.svids = NULL,
	.modes = NULL,
};

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
__override void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	/* board_set_usb_mux(port, USB_PD_MUX_NONE, pd_get_polarity(port)); */
}

__override int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPC_TX_SOP, USB_SID_DISPLAYPORT);
	int pin_mode = pd_dfp_dp_get_pin_mode(port, dp_status[port]);
#if defined(CONFIG_USB_PD_TCPM_MUX) && defined(CONFIG_USB_PD_TCPM_ANX7447)
	const struct usb_mux *mux = &usb_muxes[port];
#endif

#ifdef CONFIG_USB_PD_TCPM_ANX7447
	mux_state_t mux_state = USB_PD_MUX_NONE;
	if (pd_get_polarity(port))
		mux_state |= USB_PD_MUX_POLARITY_INVERTED;
#endif

	CPRINTS("pin_mode = %d", pin_mode);
	if (!pin_mode)
		return 0;

#if defined(CONFIG_USB_PD_TCPM_MUX) && defined(CONFIG_USB_PD_TCPM_ANX7447)
	switch (pin_mode) {
	case MODE_DP_PIN_A:
	case MODE_DP_PIN_C:
	case MODE_DP_PIN_E:
		mux_state |= USB_PD_MUX_DP_ENABLED;
		mux->driver->set(mux, mux_state);
		break;
	case MODE_DP_PIN_B:
	case MODE_DP_PIN_D:
	case MODE_DP_PIN_F:
		mux_state |= USB_PD_MUX_DOCK;
		mux->driver->set(mux, mux_state);
		break;
	}
#endif

	/*
	 * board_set_usb_mux(port, USB_PD_MUX_DP_ENABLED,
	 * pd_get_polarity(port));
	 */
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode, /* pin mode */
				1,             /* DPv1.3 signaling */
				2);            /* UFP connected */
	return 2;
}

__override void svdm_dp_post_config(int port)
{
	const struct usb_mux *mux = &usb_muxes[port];

	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

	if (IS_ENABLED(CONFIG_USB_PD_TCPM_ANX7447))
		anx7447_tcpc_update_hpd_status(mux, 1, 0);
}

__override int svdm_dp_attention(int port, uint32_t *payload)
{
#ifdef CONFIG_USB_PD_TCPM_ANX7447
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
	const struct usb_mux *mux = &usb_muxes[port];

	CPRINTS("Attention: 0x%x", payload[1]);
	anx7447_tcpc_update_hpd_status(mux, lvl, irq);
#endif
	dp_status[port] = payload[1];

	/* ack */
	return 1;
}

__override void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#ifdef CONFIG_USB_PD_TCPM_ANX7447
	anx7447_tcpc_clear_hpd_status(port);
#endif
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
