/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/ccgxxf.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "intelrvp.h"
#include "ioexpander.h"
#include "isl9241.h"
#include "sn5s330.h"
#include "tusb1064.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"
#include "usbc_ppc.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/*******************************************************************/
/* USB-C Configuration Start */

/* PPC */
#define I2C_ADDR_SN5S330_P0 0x40
#define I2C_ADDR_SN5S330_P1 0x41

#define MTLP_DDR5_RVP_SKU_BOARD_ID 0x01
#define MTLP_LP5_RVP_SKU_BOARD_ID 0x02
#define MTL_RVP_BOARD_ID(id) ((id) & 0x3F)

/* IOEX ports */
enum ioex_port {
	IOEX_KBD = 0,
#if defined(HAS_TASK_PD_C2)
	IOEX_C2_CCGXXF,
#endif
	IOEX_COUNT
};

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
#if defined(HAS_TASK_PD_C2)
	USBC_PORT_C2,
	USBC_PORT_C3,
#endif
	USBC_PORT_COUNT
};
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

/* TCPC AIC GPIO Configuration */
const struct mecc_1_1_tcpc_aic_gpio_config_t mecc_1_1_tcpc_aic_gpios[] = {
	[USBC_PORT_C0] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
	},
	[USBC_PORT_C1] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
	},
#if defined(HAS_TASK_PD_C2)
	[USBC_PORT_C2] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p2)),
	},
	[USBC_PORT_C3] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p3)),
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(mecc_1_1_tcpc_aic_gpios) ==
	     CONFIG_USB_PD_PORT_MAX_COUNT);

static void board_connect_c0_sbu_deferred(void)
{
	enum pd_power_role prole;

	if (gpio_get_level(GPIO_CCD_MODE_ODL)) {
		CPRINTS("Default AUX line connected");
		/* Default set the SBU lines to AUX mode */
		ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_1, 0);
		ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_0, 1);
	} else {
		prole = pd_get_power_role(USBC_PORT_C0);
		CPRINTS("%s debug device is attached",
			prole == PD_ROLE_SINK ? "Servo V4C/SuzyQ" : "Intel");

		if (prole == PD_ROLE_SINK) {
			/* Set the SBU lines to Google CCD mode */
			ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_1, 1);
			ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_0, 1);
		} else {
			/* Set the SBU lines to Intel CCD mode */
			ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_1, 0);
			ioex_set_level(IOEX_USB_C0_MUX_SBU_SEL_0, 0);
		}
	}
}
DECLARE_DEFERRED(board_connect_c0_sbu_deferred);

void board_reset_pd_mcu(void)
{
	/* Reset NCT38XX TCPC */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_c1_tcpc_rst_odl), 0);
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_c1_tcpc_rst_odl), 1);
	nct38xx_reset_notify(0);
	nct38xx_reset_notify(1);

	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);
	}

	/* NCT38XX chip uses gpio ioex */
	gpio_reset_port(DEVICE_DT_GET(DT_NODELABEL(ioex_c0)));
	gpio_reset_port(DEVICE_DT_GET(DT_NODELABEL(ioex_c1)));

#if defined(HAS_TASK_PD_C2)
	/* Reset the ccgxxf ports only resetting 1 is required */
	ccgxxf_reset(USBC_PORT_C2);

	/* CCGXXF has ioex on port 2 */
	ioex_init(IOEX_C2_CCGXXF);
#endif
}

void board_connect_c0_sbu(enum gpio_signal signal)
{
	hook_call_deferred(&board_connect_c0_sbu_deferred_data, 0);
}

static void board_int_init(void)
{
	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_c1_tcpc));
#if defined(HAS_TASK_PD_C2)
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c2_tcpc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c3_tcpc));
#endif

	/* Enable CCD Mode interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ccd_mode));
}

static void configure_retimer_usbmux(void)
{
	switch (MTL_RVP_BOARD_ID(board_get_version())) {
	case MTLP_LP5_RVP_SKU_BOARD_ID:
		/* No retimer on Port 0 */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_0);
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_1);
#if defined(HAS_TASK_PD_C2)
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_2);
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_3);
#endif
		break;
		/* Add additional board SKUs */
	default:
		break;
	}
}

__override bool board_is_tbt_usb4_port(int port)
{
	bool tbt_usb4 = true;

	switch (MTL_RVP_BOARD_ID(board_get_version())) {
	case MTLP_LP5_RVP_SKU_BOARD_ID:
		/* No retimer on port 0; and port 1 is not available */
		if ((port == USBC_PORT_C0) || (port == USBC_PORT_C1))
			tbt_usb4 = false;
		break;
	default:
		break;
	}
	return tbt_usb4;
}

__override enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	enum tbt_compat_cable_speed max_speed = TBT_SS_TBT_GEN3;

	switch (MTL_RVP_BOARD_ID(board_get_version())) {
	case MTLP_LP5_RVP_SKU_BOARD_ID:
		if (port == USBC_PORT_C2)
			max_speed = TBT_SS_U32_GEN1_GEN2;
		break;
	default:
		break;
	}

	return max_speed;
}

static int board_pre_task_typec_peripheral_init(void)
{
	/* Only reset tcpc/pd if not sysjump */
	if (!system_jumped_late()) {
		/* Initialize tcpc and all ioex */
		board_reset_pd_mcu();
	}

	/* Initialize all interrupts */
	board_int_init();

	/* Make sure SBU are routed to CCD or AUX based on CCD status at init */
	board_connect_c0_sbu_deferred();

	/* Configure board specific retimer & mux */
	configure_retimer_usbmux();

	return 0;
}
SYS_INIT(board_pre_task_typec_peripheral_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);

/*
 * Since MTLRVP has both PPC and TCPC ports override to check if the port
 * is a PPC or non PPC port
 */
__override bool pd_check_vbus_level(int port, enum vbus_level level)
{
	if (!board_port_has_ppc(port)) {
		return tcpm_check_vbus_level(port, level);
	} else if (level == VBUS_PRESENT) {
		return pd_snk_is_vbus_provided(port);
	} else {
		return !pd_snk_is_vbus_provided(port);
	}
}

__override bool board_port_has_ppc(int port)
{
	bool ppc_port;

	switch (port) {
	case USBC_PORT_C0:
	case USBC_PORT_C1:
		ppc_port = true;
		break;
	default:
		ppc_port = false;
		break;
	}

	return ppc_port;
}
