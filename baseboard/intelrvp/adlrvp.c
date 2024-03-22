/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP board-specific common configuration */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "bq25710.h"
#include "charger.h"
#include "common.h"
#include "driver/retimer/bb_retimer_public.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "isl9241.h"
#include "pca9675.h"
#include "power/icelake.h"
#include "sn5s330.h"
#include "system.h"
#include "task.h"
#include "tusb1064.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ##args)

/* TCPC AIC GPIO Configuration */
const struct mecc_1_0_tcpc_aic_gpio_config_t mecc_1_0_tcpc_aic_gpios[] = {
	[TYPE_C_PORT_0] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P0,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P0,
		.ppc_intr_handler = sn5s330_interrupt,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P1,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P1,
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P2,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P2,
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P3,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P3,
		.ppc_intr_handler = sn5s330_interrupt,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(mecc_1_0_tcpc_aic_gpios) ==
	     CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB-C PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[TYPE_C_PORT_0] = {
		.i2c_port = I2C_PORT_TYPEC_0,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.i2c_port = I2C_PORT_TYPEC_1,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.i2c_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.i2c_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == CONFIG_USB_PD_PORT_MAX_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB-C retimer Configuration */
struct usb_mux bb_retimer0_usb_mux = {
	.usb_port = TYPE_C_PORT_0,
	.driver = &bb_usb_retimer,
	.hpd_update = bb_retimer_hpd_update,
	.i2c_port = I2C_PORT_TYPEC_0,
	.i2c_addr_flags = I2C_PORT0_BB_RETIMER_ADDR,
};
struct usb_mux_chain usbc0_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_0,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
#if defined(HAS_TASK_PD_C1)
struct usb_mux bb_retimer1_usb_mux = {
	.usb_port = TYPE_C_PORT_1,
	.driver = &bb_usb_retimer,
	.hpd_update = bb_retimer_hpd_update,
	.i2c_port = I2C_PORT_TYPEC_1,
	.i2c_addr_flags = I2C_PORT1_BB_RETIMER_ADDR,
};
struct usb_mux_chain usbc1_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_1,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
#endif
#if defined(HAS_TASK_PD_C2)
struct usb_mux bb_retimer2_usb_mux = {
	.usb_port = TYPE_C_PORT_2,
	.driver = &bb_usb_retimer,
	.hpd_update = bb_retimer_hpd_update,
	.i2c_port = I2C_PORT_TYPEC_2,
	.i2c_addr_flags = I2C_PORT2_BB_RETIMER_ADDR,
};
struct usb_mux_chain usbc2_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_2,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
#endif
#if defined(HAS_TASK_PD_C3)
struct usb_mux_chain usbc3_tcss_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_3,
			.driver = &virtual_usb_mux_driver,
			.hpd_update = &virtual_hpd_update,
		},
};
#endif

/* USB muxes Configuration */
struct usb_mux_chain usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.mux = &bb_retimer0_usb_mux,
		.next = &usbc0_tcss_usb_mux,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.mux = &bb_retimer1_usb_mux,
		.next = &usbc1_tcss_usb_mux,
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.mux = &bb_retimer2_usb_mux,
		.next = &usbc2_tcss_usb_mux,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.mux = &(const struct usb_mux) {
			.usb_port = TYPE_C_PORT_3,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_TYPEC_3,
			.i2c_addr_flags = I2C_PORT3_BB_RETIMER_ADDR,
		},
		.next = &usbc3_tcss_usb_mux,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB Mux Configuration for Soc side BB-Retimers for Dual retimer config */
struct usb_mux_chain soc_side_bb_retimer0_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_0,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_TYPEC_0,
			.i2c_addr_flags = I2C_PORT0_BB_RETIMER_SOC_ADDR,
		},
	.next = &usbc0_tcss_usb_mux,
};

#if defined(HAS_TASK_PD_C1)
struct usb_mux_chain soc_side_bb_retimer1_usb_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = TYPE_C_PORT_1,
			.driver = &bb_usb_retimer,
			.hpd_update = bb_retimer_hpd_update,
			.i2c_port = I2C_PORT_TYPEC_1,
			.i2c_addr_flags = I2C_PORT1_BB_RETIMER_SOC_ADDR,
		},
	.next = &usbc1_tcss_usb_mux,
};
#endif

const struct bb_usb_control bb_controls[] = {
	[TYPE_C_PORT_0] = {
		.retimer_rst_gpio = (enum gpio_signal)
			IOEX_USB_C0_BB_RETIMER_RST,
		.usb_ls_en_gpio = (enum gpio_signal)
			IOEX_USB_C0_BB_RETIMER_LS_EN,
	},
#if defined(HAS_TASK_PD_C1)
	[TYPE_C_PORT_1] = {
		.retimer_rst_gpio = (enum gpio_signal)
			IOEX_USB_C1_BB_RETIMER_RST,
		.usb_ls_en_gpio = (enum gpio_signal)
			IOEX_USB_C1_BB_RETIMER_LS_EN,
	},
#endif
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.retimer_rst_gpio = (enum gpio_signal)
			IOEX_USB_C2_BB_RETIMER_RST,
		.usb_ls_en_gpio = (enum gpio_signal)
			IOEX_USB_C2_BB_RETIMER_LS_EN,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.retimer_rst_gpio = (enum gpio_signal)
			IOEX_USB_C3_BB_RETIMER_RST,
		.usb_ls_en_gpio = (enum gpio_signal)
			IOEX_USB_C3_BB_RETIMER_LS_EN,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(bb_controls) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Cache BB retimer power state */
static bool cache_bb_enable[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Each TCPC have corresponding IO expander and are available in pair */
struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_PCA9675] = {
		.i2c_host_port = I2C_PORT_TYPEC_0,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.drv = &pca9675_ioexpander_drv,
	},
	[IOEX_C1_PCA9675] = {
		.i2c_host_port = I2C_PORT_TYPEC_1,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.drv = &pca9675_ioexpander_drv,
	},
#if defined(HAS_TASK_PD_C2)
	[IOEX_C2_PCA9675] = {
		.i2c_host_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.drv = &pca9675_ioexpander_drv,
	},
	[IOEX_C3_PCA9675] = {
		.i2c_host_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.drv = &pca9675_ioexpander_drv,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

/* Charger Chips */
struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

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
		ioex_set_level((enum ioex_signal)bb_controls[me->usb_port]
				       .usb_ls_en_gpio,
			       1);

		/*
		 * minimum time from VCC to RESET_N de-assertion is 100us
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		msleep(1);
		ioex_set_level((enum ioex_signal)bb_controls[me->usb_port]
				       .retimer_rst_gpio,
			       1);

		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		msleep(1);

	} else {
		ioex_set_level((enum ioex_signal)bb_controls[me->usb_port]
				       .retimer_rst_gpio,
			       0);
		msleep(1);
		ioex_set_level((enum ioex_signal)bb_controls[me->usb_port]
				       .usb_ls_en_gpio,
			       0);
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
	switch (ADL_RVP_BOARD_ID(board_get_version())) {
	case ADLN_LP5_ERB_SKU_BOARD_ID:
	case ADLN_LP5_RVP_SKU_BOARD_ID:
		/* enable TUSB1044RNQR redriver on Port0  */
		bb_retimer0_usb_mux.i2c_addr_flags = TUSB1064_I2C_ADDR14_FLAGS;
		bb_retimer0_usb_mux.driver = &tusb1064_usb_mux_driver;
		bb_retimer0_usb_mux.hpd_update = tusb1044_hpd_update;

#if defined(HAS_TASK_PD_C1)
		bb_retimer1_usb_mux.driver = NULL;
		bb_retimer1_usb_mux.hpd_update = NULL;
#endif
		break;

	case ADLP_LP5_T4_RVP_SKU_BOARD_ID:
		/* No retimer on Port-2 */
#if defined(HAS_TASK_PD_C2)
		bb_retimer2_usb_mux.driver = NULL;
#endif
		break;

	case ADLP_DDR5_RVP_SKU_BOARD_ID:
		/*
		 * ADL-P-DDR5 RVP has dual BB-retimers for port0 & port1.
		 * Change the default usb mux config on runtime to support
		 * dual retimer topology.
		 */
		usb_muxes[TYPE_C_PORT_0].next = &soc_side_bb_retimer0_usb_mux;
#if defined(HAS_TASK_PD_C1)
		usb_muxes[TYPE_C_PORT_1].next = &soc_side_bb_retimer1_usb_mux;
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
		return BATTERY_GETAC_SMP_HHP_408_2S;
	default:
		/* configure Battery to 3S based */
		return BATTERY_GETAC_SMP_HHP_408_3S;
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
		.gpio = GPIO_SYS_PWROK_EC,
		.delay_ms = 3,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	{
		.gpio = GPIO_SYS_PWROK_EC,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_assert_list);

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
__override int board_get_version(void)
{
	/* Cache the ADLRVP board ID */
	static int adlrvp_board_id;

	int port0, port1;
	int fab_id, board_id, bom_id;

	/* Board ID is already read */
	if (adlrvp_board_id)
		return adlrvp_board_id;

	if (ioexpander_read_intelrvp_version(&port0, &port1))
		return -1;
	/*
	 * Port0: bit 0   - BOM ID(2)
	 *        bit 2:1 - FAB ID(1:0) + 1
	 * Port1: bit 7:6 - BOM ID(1:0)
	 *        bit 5:0 - BOARD ID(5:0)
	 */
	bom_id = ((port1 & 0xC0) >> 6) | ((port0 & 0x01) << 2);
	fab_id = ((port0 & 0x06) >> 1) + 1;
	board_id = port1 & 0x3F;

	CPRINTS("BID:0x%x, FID:0x%x, BOM:0x%x", board_id, fab_id, bom_id);

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

__override void board_pre_task_i2c_peripheral_init(void)
{
	/* Initialized IOEX-0 to access IOEX-GPIOs needed pre-task */
	ioex_init(IOEX_C0_PCA9675);

	/* Make sure SBU are routed to CCD or AUX based on CCD status at init */
	board_connect_c0_sbu_deferred();

	/* Reconfigure board specific charger drivers */
	configure_charger();

	/* Configure board specific retimer & mux */
	configure_retimer_usbmux();
}

/*
 * ADL RVP has both ITE and FUSB based TCPC chips. By default, the PD
 * state of a non-attached port remains in PD_DRP_TOGGLE_ON in active
 * state. Also, FUSB TCPC chip does not support 'dual role auto toggle'
 * which contradicts the default set S0 state of PD_DRP_TOGGLE_ON,
 * while ITE  based TCPC can support dual role auto toggle. The
 * default PD_DRP_TOGGLE_ON state in Active state doesnot allow TCPC
 * ports to enter Low power mode. To fix the issue, added board specific
 * code to remove the default DRP state - PD_DRP_TOGGLE_ON in S0. Also,
 * eventhough 'dual role auto toggle' is not supported by FUSB, the ports
 * supports both source and sink. Hence, setting the default DRP state
 * as PD_DRP_FORCE_SOURCE in S0, would be the ideal board based solution to
 * support for both source and sink devices for this RVP.
 * Note:For ITE based TCPC, low power mode entry does makes no
 * difference, as it is controlled by ITE TCPC clk in deep sleepmode.
 */
__override enum pd_dual_role_states pd_get_drp_state_in_s0(void)
{
	return PD_DRP_FORCE_SOURCE;
}
