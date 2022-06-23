/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/bb_retimer_public.h"
#include "driver/tcpm/ccgxxf.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/tcpm/tcpci.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "intelrvp.h"
#include "intel_rvp_board_id.h"
#include "ioexpander.h"
#include "isl9241.h"
#include "keyboard_raw.h"
#include "power/meteorlake.h"
#include "sn5s330.h"
#include "system.h"
#include "task.h"
#include "tusb1064.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ##args)
#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ##args)

/*******************************************************************/
/* USB-C Configuration Start */

/* PPC */
#define I2C_ADDR_SN5S330_P0 0x40
#define I2C_ADDR_SN5S330_P1 0x41

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

/* USB-C PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_TYPEC_AIC_1,
		.i2c_addr_flags = I2C_ADDR_SN5S330_P0,
		.drv = &sn5s330_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_port = I2C_PORT_TYPEC_AIC_1,
		.i2c_addr_flags = I2C_ADDR_SN5S330_P1,
		.drv = &sn5s330_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* TCPC AIC GPIO Configuration */
const struct tcpc_aic_gpio_config_t tcpc_aic_gpios[] = {
	[USBC_PORT_C0] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p0)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
	[USBC_PORT_C1] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p1)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
#if defined(HAS_TASK_PD_C2)
	[USBC_PORT_C2] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p2)),
		/* No PPC alert for CCGXXF */
	},
	[USBC_PORT_C3] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p3)),
		/* No PPC alert for CCGXXF */
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_aic_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);

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

void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * TODO: Meteorlake PCH does not use Physical GPIO for over current
	 * error, hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
}

void board_reset_pd_mcu(void)
{
	/* Reset NCT38XX TCPC */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_c1_tcpc_rst_odl), 0);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(usb_c0_c1_tcpc_rst_odl), 1);
	nct38xx_reset_notify(0);
	nct38xx_reset_notify(1);

	if (NCT3807_RESET_POST_DELAY_MS != 0) {
		msleep(NCT3807_RESET_POST_DELAY_MS);
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

/******************************************************************************/
/* KSO mapping for discrete keyboard */
__override const uint8_t it8801_kso_mapping[] = {
	0, 1, 20, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16,
};
BUILD_ASSERT(ARRAY_SIZE(it8801_kso_mapping) == KEYBOARD_COLS_MAX);

/* PWROK signal configuration */
/*
 * On MTLRVP, SYS_PWROK_EC is an output controlled by EC and uses ALL_SYS_PWRGD
 * as input.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_PCH_SYS_PWROK,
		.delay_ms = 3,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	{
		.gpio = GPIO_PCH_SYS_PWROK,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
__override int board_get_version(void)
{
	/* Cache the MTLRVP board ID */
	static int mtlrvp_board_id;

	int i;
	int rv = EC_ERROR_UNKNOWN;
	int fab_id, board_id, bom_id;

	/* Board ID is already read */
	if (mtlrvp_board_id)
		return mtlrvp_board_id;

	/*
	 * IOExpander that has Board ID information is on DSW-VAL rail on
	 * ADL RVP. On cold boot cycles, DSW-VAL rail is taking time to settle.
	 * This loop retries to ensure rail is settled and read is successful
	 */
	for (i = 0; i < RVP_VERSION_READ_RETRY_CNT; i++) {
		rv = gpio_pin_get_dt(&bom_id_config[0]);

		if (rv >= 0)
			break;

		k_msleep(1);
	}

	/* return -1 if failed to read board id */
	if (rv)
		return -1;

	/*
	 * BOM ID [2]   : IOEX[0]
	 * BOM ID [1:0] : IOEX[15:14]
	 */
	bom_id = gpio_pin_get_dt(&bom_id_config[0]) << 2;
	bom_id |= gpio_pin_get_dt(&bom_id_config[1]) << 1;
	bom_id |= gpio_pin_get_dt(&bom_id_config[2]);
	/*
	 * FAB ID [1:0] : IOEX[2:1] + 1
	 */
	fab_id = gpio_pin_get_dt(&fab_id_config[0]) << 1;
	fab_id |= gpio_pin_get_dt(&fab_id_config[1]);
	fab_id += 1;

	/*
	 * BOARD ID[5:0] : IOEX[13:8]
	 */
	board_id = gpio_pin_get_dt(&board_id_config[0]) << 5;
	board_id |= gpio_pin_get_dt(&board_id_config[1]) << 4;
	board_id |= gpio_pin_get_dt(&board_id_config[2]) << 3;
	board_id |= gpio_pin_get_dt(&board_id_config[3]) << 2;
	board_id |= gpio_pin_get_dt(&board_id_config[4]) << 1;
	board_id |= gpio_pin_get_dt(&board_id_config[5]);

	CPRINTF("BID:0x%x, FID:0x%x, BOM:0x%x", board_id, fab_id, bom_id);

	mtlrvp_board_id = board_id | (fab_id << 8);
	return mtlrvp_board_id;
}

static void board_int_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_c1_tcpc));
#if defined(HAS_TASK_PD_C2)
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c2_tcpc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c3_tcpc));
#endif

	/* Enable CCD Mode interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ccd_mode));
}

static int board_pre_task_peripheral_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	/* Only reset tcpc/pd if not sysjump */
	if (!system_jumped_late()) {
		/* Initialize tcpc and all ioex */
		board_reset_pd_mcu();
	}

	/* Initialize all interrupts */
	board_int_init();

	/* Make sure SBU are routed to CCD or AUX based on CCD status at init */
	board_connect_c0_sbu_deferred();

	return 0;
}
SYS_INIT(board_pre_task_peripheral_init, APPLICATION,
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
