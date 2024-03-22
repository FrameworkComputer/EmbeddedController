/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TODO: b/218904113: Convert to using Zephyr GPIOs */
#include "adlrvp_zephyr.h"
#include "battery.h"
#include "battery_fuel_gauge.h"
#include "bq25710.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/bb_retimer_public.h"
#include "extpower.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "intel_rvp_board_id.h"
#include "intelrvp.h"
#include "ioexpander.h"
#include "isl9241.h"
#include "power/icelake.h"
#include "sn5s330.h"
#include "system.h"
#include "task.h"
#include "tusb1064.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ##args)
#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ##args)

/* TCPC AIC GPIO Configuration */
const struct mecc_1_0_tcpc_aic_gpio_config_t mecc_1_0_tcpc_aic_gpios[] = {
	[TYPE_C_PORT_0] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p0)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p1)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p1)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p2)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p2)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p3)),
		.ppc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_ppc_alrt_p3)),
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(mecc_1_0_tcpc_aic_gpios) ==
	     CONFIG_USB_PD_PORT_MAX_COUNT);

/* Cache BB retimer power state */
static bool cache_bb_enable[CONFIG_USB_PD_PORT_MAX_COUNT];

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Port 0 & 1 and 2 & 3 share same line for over current indication */
#if defined(HAS_TASK_PD_C2)
	enum ioex_signal oc_signal = port < TYPE_C_PORT_2 ? IOEX_USB_C0_C1_OC :
							    IOEX_USB_C2_C3_OC;
#else
	enum ioex_signal oc_signal = IOEX_USB_C0_C1_OC;
#endif

	/* Overcurrent indication is active low signal */
	ioex_set_level(oc_signal, is_overcurrented ? 0 : 1);
}

__override int bb_retimer_power_enable(const struct usb_mux *me, bool enable)
{
	/*
	 * ADL-P-DDR5 RVP SKU has cascaded retimer topology.
	 * Ports with cascaded retimers share common load switch and reset pin
	 * hence no need to set the power state again if the 1st retimer's power
	 * status has already changed.
	 */
	if (cache_bb_enable[me->usb_port] == enable)
		return EC_SUCCESS;

	cache_bb_enable[me->usb_port] = enable;

	/* Handle retimer's power domain.*/
	if (enable) {
		ioex_set_level(bb_controls[me->usb_port].usb_ls_en_gpio, 1);

		/*
		 * minimum time from VCC to RESET_N de-assertion is 100us
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		msleep(1);
		ioex_set_level(bb_controls[me->usb_port].retimer_rst_gpio, 1);

		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		msleep(1);

	} else {
		ioex_set_level(bb_controls[me->usb_port].retimer_rst_gpio, 0);
		msleep(1);
		ioex_set_level(bb_controls[me->usb_port].usb_ls_en_gpio, 0);
	}
	return EC_SUCCESS;
}

static void board_connect_c0_sbu_deferred(void)
{
	int ccd_intr_level = gpio_get_level(GPIO_CCD_MODE_ODL);

	if (ccd_intr_level) {
		/* Default set the SBU lines to AUX mode on TCPC-AIC */
		ioex_set_level(IOEX_USB_C0_USB_MUX_CNTRL_1, 0);
		ioex_set_level(IOEX_USB_C0_USB_MUX_CNTRL_0, 0);
	} else {
		/* Set the SBU lines to CCD mode on TCPC-AIC */
		ioex_set_level(IOEX_USB_C0_USB_MUX_CNTRL_1, 1);
		ioex_set_level(IOEX_USB_C0_USB_MUX_CNTRL_0, 0);
	}
}
DECLARE_DEFERRED(board_connect_c0_sbu_deferred);

void board_connect_c0_sbu(enum gpio_signal s)
{
	hook_call_deferred(&board_connect_c0_sbu_deferred_data, 0);
}

static void enable_h1_irq(void)
{
	gpio_enable_interrupt(GPIO_CCD_MODE_ODL);
}
DECLARE_HOOK(HOOK_INIT, enable_h1_irq, HOOK_PRIO_LAST);

void set_charger_system_voltage(void)
{
	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/*
		 * As per b:196184163 configure the PPVAR_SYS depend
		 * on AC or AC+battery
		 */
		if (extpower_is_present() && battery_is_present()) {
			bq25710_set_min_system_voltage(
				CHARGER_SOLO, battery_get_info()->voltage_min);
		} else {
			bq25710_set_min_system_voltage(
				CHARGER_SOLO, battery_get_info()->voltage_max);
		}
		break;

	/* Add additional board SKUs */
	default:
		break;
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, set_charger_system_voltage, HOOK_PRIO_DEFAULT);

static void configure_charger(void)
{
	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/* charger chip BQ25720 support */
		chg_chips[0].i2c_addr_flags = BQ25710_SMBUS_ADDR1_FLAGS;
		chg_chips[0].drv = &bq25710_drv;
		set_charger_system_voltage();
		break;

	/* Add additional board SKUs */
	default:
		break;
	}
}

static void configure_retimer_usbmux(void)
{
	struct usb_mux *mux;

	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/* enable TUSB1044RNQR redriver on Port0  */
		mux = USB_MUX_POINTER(DT_NODELABEL(usb_mux_chain_0), 0);
		mux->i2c_addr_flags = TUSB1064_I2C_ADDR14_FLAGS;
		mux->driver = &tusb1064_usb_mux_driver;
		mux->hpd_update = tusb1044_hpd_update;

#if defined(HAS_TASK_PD_C1)
		mux = USB_MUX_POINTER(DT_NODELABEL(usb_mux_chain_1), 0);
		mux->driver = NULL;
		mux->hpd_update = NULL;
#endif
		break;

	case ADLP_LP5_T4_RVP_SKU_BOARD_ID:
		/* No retimer on Port-2 */
#if defined(HAS_TASK_PD_C2)
		mux = USB_MUX_POINTER(DT_NODELABEL(usb_mux_chain_2), 0);
		mux->driver = NULL;
#endif
		break;

	case ADLP_DDR5_RVP_SKU_BOARD_ID:
		/*
		 * ADL-P-DDR5 RVP has dual BB-retimers for port0 & port1.
		 * Change the default usb mux config on runtime to support
		 * dual retimer topology.
		 */
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_0);
#if defined(HAS_TASK_PD_C1)
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_alt_chain_1);
#endif
		break;

		/* Add additional board SKUs */

	default:
		break;
	}
}

__override int board_get_default_battery_type(void)
{
	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLM_LP4_RVP1_SKU_BOARD_ID:
	case ADLM_LP5_RVP2_SKU_BOARD_ID:
	case ADLM_LP5_RVP3_SKU_BOARD_ID:
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/* configure Battery to 2S based */
		return BATTERY_TYPE(DT_ALIAS(getac_2s));
	default:
		/* configure Battery to 3S based */
		return BATTERY_TYPE(DT_ALIAS(getac_3s));
	}
}

/******************************************************************************/
/* PWROK signal configuration */
/*
 * On ADLRVP, SYS_PWROK_EC is an output controlled by EC and uses ALL_SYS_PWRGD
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
	/* Cache the board ID */
	static int adlrvp_board_id;

	int i;
	int rv = EC_ERROR_UNKNOWN;

	int fab_id, board_id, bom_id;

	/* Board ID is already read */
	if (adlrvp_board_id)
		return adlrvp_board_id;

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

	/* retrun -1 if failed to read board id */
	if (rv < 0)
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

	adlrvp_board_id = board_id | (fab_id << 8);
	return adlrvp_board_id;
}

__override bool board_is_tbt_usb4_port(int port)
{
	bool tbt_usb4 = true;

	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/* No retimer on both ports */
		tbt_usb4 = false;
		break;

	case ADLP_LP5_T4_RVP_SKU_BOARD_ID:
		/* No retimer on Port-2 hence no platform level AUX & LSx mux */
#if defined(HAS_TASK_PD_C2)
		if (port == TYPE_C_PORT_2)
			tbt_usb4 = false;
#endif
		break;

	/* Add additional board SKUs */
	default:
		break;
	}

	return tbt_usb4;
}

static int board_pre_task_peripheral_init(void)
{
	/* Initialized IOEX-0 to access IOEX-GPIOs needed pre-task */
	ioex_init(IOEX_C0_PCA9675);

	/* Make sure SBU are routed to CCD or AUX based on CCD status at init */
	board_connect_c0_sbu_deferred();

	/* Reconfigure board specific charger drivers */
	configure_charger();

	/* Configure board specific retimer & mux */
	configure_retimer_usbmux();

	return 0;
}
SYS_INIT(board_pre_task_peripheral_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);
