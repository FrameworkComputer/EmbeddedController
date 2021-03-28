/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_AC_PRESENT			NAMED_GPIO(acok_od)
#define GPIO_AP_RST_L			NAMED_GPIO(ap_rst_l)
#define GPIO_AP_SUSPEND			NAMED_GPIO(ap_suspend)
#define GPIO_BATT_PRES_ODL		NAMED_GPIO(ec_batt_pres_odl)
#define GPIO_BOARD_VERSION1		NAMED_GPIO(brd_id0)
#define GPIO_BOARD_VERSION2		NAMED_GPIO(brd_id1)
#define GPIO_BOARD_VERSION3		NAMED_GPIO(brd_id2)
#define GPIO_CCD_MODE_ODL		NAMED_GPIO(ccd_mode_odl)
#define GPIO_DA9313_GPIO0		NAMED_GPIO(da9313_gpio0)
#define GPIO_DP_HOT_PLUG_DET		NAMED_GPIO(dp_hot_plug_det)
#define GPIO_DP_MUX_SEL			NAMED_GPIO(dp_mux_sel)
#define GPIO_DP_MUX_OE_L		NAMED_GPIO(dp_mux_oe_l)
#define GPIO_DEPRECATED_AP_RST_REQ	NAMED_GPIO(deprecated_ap_rst_req)
#define GPIO_ENTERING_RW		NAMED_GPIO(ec_entering_rw)
#define GPIO_EN_PP3300_A		NAMED_GPIO(en_pp3300_a)
#define GPIO_EN_PP5000			NAMED_GPIO(en_pp5000_a)
#define GPIO_EN_USB_A_5V		NAMED_GPIO(en_usb_a_5v)
#define GPIO_USB_C0_PD_INT_ODL		NAMED_GPIO(usb_c0_pd_int_odl)
#define GPIO_USB_C1_PD_INT_ODL		NAMED_GPIO(usb_c1_pd_int_odl)
#define GPIO_LID_OPEN			NAMED_GPIO(lid_open_ec)
#define GPIO_PMIC_KPD_PWR_ODL		NAMED_GPIO(pmic_kpd_pwr_odl)
#define GPIO_PMIC_RESIN_L		NAMED_GPIO(pm845_resin_l)
#define GPIO_POWER_BUTTON_L		NAMED_GPIO(ec_pwr_btn_odl)
#define GPIO_POWER_GOOD			NAMED_GPIO(power_good)
#define GPIO_PS_HOLD			NAMED_GPIO(ps_hold)
#define GPIO_QSIP_ON			NAMED_GPIO(qsip_on)
#define GPIO_SKU_ID0			NAMED_GPIO(sku_id0)
#define GPIO_SKU_ID1			NAMED_GPIO(sku_id1)
#define GPIO_SKU_ID2			NAMED_GPIO(sku_id2)
#define GPIO_SWITCHCAP_ON		NAMED_GPIO(switchcap_on)
#define GPIO_SWITCHCAP_ON_L		NAMED_GPIO(switchcap_on)
#define GPIO_SWITCHCAP_PG_INT_L		NAMED_GPIO(da9313_gpio0)
#define GPIO_USB_A0_OC_ODL		NAMED_GPIO(usb_a0_oc_odl)
#define GPIO_USB_C0_BC12_INT_L		NAMED_GPIO(usb_c0_bc12_int_l)
#define GPIO_USB_C1_BC12_INT_L		NAMED_GPIO(usb_c1_bc12_int_l)
#define GPIO_USB_C0_PD_RST_L		NAMED_GPIO(usb_c0_pd_rst_l)
#define GPIO_USB_C1_PD_RST_L		NAMED_GPIO(usb_c1_pd_rst_l)
#define GPIO_USB_C0_SWCTL_INT_ODL	NAMED_GPIO(usb_c0_swctl_int_odl)
#define GPIO_USB_C1_SWCTL_INT_ODL	NAMED_GPIO(usb_c1_swctl_int_odl)
#define GPIO_WARM_RESET_L		NAMED_GPIO(warm_reset_l)
#define GPIO_WP_L			NAMED_GPIO(ec_wp_odl)
#define GPIO_EC_INT_L			NAMED_GPIO(ec_int_l)
#define GPIO_KBD_KSO2			NAMED_GPIO(ec_kso_02_inv)
#define GPIO_ENABLE_BACKLIGHT		NAMED_GPIO(ec_bl_disable_l)
#define GPIO_VOLUME_DOWN_L		NAMED_GPIO(ec_voldn_btn_odl)
#define GPIO_VOLUME_UP_L		NAMED_GPIO(ec_volup_btn_odl)

/*
 * Set EC_CROS_GPIO_INTERRUPTS to a space-separated list of GPIO_INT items.
 *
 * Each GPIO_INT requires three parameters:
 *   gpio_signal - The enum gpio_signal for the interrupt gpio
 *   interrupt_flags - The interrupt-related flags (e.g. GPIO_INT_EDGE_BOTH)
 *   handler - The platform/ec interrupt handler.
 *
 * Ensure that this files includes all necessary headers to declare all
 * referenced handler functions.
 *
 * For example, one could use the follow definition:
 * #define EC_CROS_GPIO_INTERRUPTS \
 *   GPIO_INT(NAMED_GPIO(h1_ec_pwr_btn_odl), GPIO_INT_EDGE_BOTH, button_print)
 */
#define EC_CROS_GPIO_INTERRUPTS                                               \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt)     \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)            \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                     \
		 power_button_interrupt)                                      \
	GPIO_INT(GPIO_VOLUME_DOWN_L, GPIO_INT_EDGE_BOTH, button_interrupt)    \
	GPIO_INT(GPIO_VOLUME_UP_L, GPIO_INT_EDGE_BOTH, button_interrupt)      \
	GPIO_INT(GPIO_SWITCHCAP_PG_INT_L, GPIO_INT_FALLING, ln9310_interrupt) \
	GPIO_INT(GPIO_AP_RST_L, GPIO_INT_EDGE_BOTH, power_signal_interrupt)   \
	GPIO_INT(GPIO_AP_SUSPEND, GPIO_INT_EDGE_BOTH, power_signal_interrupt) \
	GPIO_INT(GPIO_DEPRECATED_AP_RST_REQ, GPIO_INT_EDGE_BOTH,              \
		 power_signal_interrupt)                                      \
	GPIO_INT(GPIO_POWER_GOOD, GPIO_INT_EDGE_BOTH, power_signal_interrupt) \
	GPIO_INT(GPIO_PS_HOLD, GPIO_INT_EDGE_BOTH, power_signal_interrupt)    \
	GPIO_INT(GPIO_WARM_RESET_L, GPIO_INT_EDGE_BOTH,                       \
		 power_signal_interrupt)                                      \
	GPIO_INT(GPIO_USB_C0_PD_INT_ODL, GPIO_INT_FALLING, tcpc_alert_event)  \
	GPIO_INT(GPIO_USB_C1_PD_INT_ODL, GPIO_INT_FALLING, tcpc_alert_event)  \
	GPIO_INT(GPIO_USB_C0_SWCTL_INT_ODL, GPIO_INT_FALLING, ppc_interrupt)  \
	GPIO_INT(GPIO_USB_C1_SWCTL_INT_ODL, GPIO_INT_FALLING, ppc_interrupt)  \
	GPIO_INT(GPIO_USB_C0_BC12_INT_L, GPIO_INT_FALLING, usb0_evt)          \
	GPIO_INT(GPIO_USB_C1_BC12_INT_L, GPIO_INT_FALLING, usb1_evt)          \
	GPIO_INT(GPIO_USB_A0_OC_ODL, GPIO_INT_BOTH, usba_oc_interrupt)        \
	GPIO_INT(GPIO_CCD_MODE_ODL, GPIO_INT_FALLING, board_connect_c0_sbu)


#endif /* __ZEPHYR_GPIO_MAP_H */
