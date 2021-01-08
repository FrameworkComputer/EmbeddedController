/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP board-specific common configuration */

#include "bb_retimer.h"
#include "charger.h"
#include "common.h"
#include "hooks.h"
#include "isl9241.h"
#include "pca9675.h"
#include "power/icelake.h"
#include "sn5s330.h"
#include "system.h"
#include "task.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ## args)

/* Mutex for BB retimer shared NVM access */
static struct mutex bb_nvm_mutex;

/* TCPC AIC GPIO Configuration */
const struct tcpc_aic_gpio_config_t tcpc_aic_gpios[] = {
	[TYPE_C_PORT_0] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P0,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P0,
		.ppc_intr_handler = sn5s330_interrupt,
	},
	[TYPE_C_PORT_1] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P1,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P1,
		.ppc_intr_handler = sn5s330_interrupt,
	},
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
BUILD_ASSERT(ARRAY_SIZE(tcpc_aic_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB-C PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[TYPE_C_PORT_0] = {
		.i2c_port = I2C_PORT_TYPEC_0,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
	[TYPE_C_PORT_1] = {
		.i2c_port = I2C_PORT_TYPEC_1,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv
	},
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
struct usb_mux usbc0_tcss_usb_mux = {
	.usb_port = TYPE_C_PORT_0,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};
struct usb_mux usbc1_tcss_usb_mux = {
	.usb_port = TYPE_C_PORT_1,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};
#if defined(HAS_TASK_PD_C2)
struct usb_mux usbc2_tcss_usb_mux = {
	.usb_port = TYPE_C_PORT_2,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};
#endif
#if defined(HAS_TASK_PD_C3)
struct usb_mux usbc3_tcss_usb_mux = {
	.usb_port = TYPE_C_PORT_3,
	.driver = &virtual_usb_mux_driver,
	.hpd_update = &virtual_hpd_update,
};
#endif

/* USB muxes Configuration */
const struct usb_mux usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.usb_port = TYPE_C_PORT_0,
		.next_mux = &usbc0_tcss_usb_mux,
		.driver = &bb_usb_retimer,
		.i2c_port = I2C_PORT_TYPEC_0,
		.i2c_addr_flags = I2C_PORT0_BB_RETIMER_ADDR,
	},
	[TYPE_C_PORT_1] = {
		.usb_port = TYPE_C_PORT_1,
		.next_mux = &usbc1_tcss_usb_mux,
		.driver = &bb_usb_retimer,
		.i2c_port = I2C_PORT_TYPEC_1,
		.i2c_addr_flags = I2C_PORT1_BB_RETIMER_ADDR,
	},
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.usb_port = TYPE_C_PORT_2,
		.next_mux = &usbc2_tcss_usb_mux,
		.driver = &bb_usb_retimer,
		.i2c_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_PORT2_BB_RETIMER_ADDR,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.usb_port = TYPE_C_PORT_3,
		.next_mux = &usbc3_tcss_usb_mux,
		.driver = &bb_usb_retimer,
		.i2c_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_PORT3_BB_RETIMER_ADDR,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Each TCPC have corresponding IO expander */
const struct pca9675_ioexpander pca9675_iox[] = {
	[TYPE_C_PORT_0] = {
		.i2c_host_port = I2C_PORT_TYPEC_0,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
	[TYPE_C_PORT_1] = {
		.i2c_host_port = I2C_PORT_TYPEC_1,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
#if defined(HAS_TASK_PD_C2)
	[TYPE_C_PORT_2] = {
		.i2c_host_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
#endif
#if defined(HAS_TASK_PD_C3)
	[TYPE_C_PORT_3] = {
		.i2c_host_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(pca9675_iox) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Charger Chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Port 0 & 1 and 2 & 3 share same line for over current indication */
	/* If PD_C2 task is defined, PD_C3 task is assumed to be defined. */
#if defined(HAS_TASK_PD_C2)
	int ioex = port < TYPE_C_PORT_2 ?
			TYPE_C_PORT_1 : TYPE_C_PORT_3;
#else
	int ioex = TYPE_C_PORT_1;
#endif

	if (is_overcurrented)
		pca9675_update_pins(ioex, TCPC_AIC_IOE_OC, 0);
	else
		pca9675_update_pins(ioex, 0, TCPC_AIC_IOE_OC);
}

__override void bb_retimer_power_handle(const struct usb_mux *me, int on_off)
{
	/* Handle retimer's power domain.*/
	if (on_off) {
		/*
		 * BB retimer NVM can be shared between multiple ports, hence
		 * lock enabling the retimer until the current retimer request
		 * is complete.
		 */
		mutex_lock(&bb_nvm_mutex);

		pca9675_update_pins(me->usb_port,
				TCPC_AIC_IOE_BB_RETIMER_LS_EN, 0);

		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		msleep(1);
		pca9675_update_pins(me->usb_port,
				TCPC_AIC_IOE_BB_RETIMER_RST, 0);

		/* Allow 20ms time for the retimer to be initialized. */
		msleep(20);

		mutex_unlock(&bb_nvm_mutex);
	} else {
		pca9675_update_pins(me->usb_port,
				0, TCPC_AIC_IOE_BB_RETIMER_RST);
		msleep(1);
		pca9675_update_pins(me->usb_port,
				0, TCPC_AIC_IOE_BB_RETIMER_LS_EN);
	}
}

static void board_connect_c0_sbu_deferred(void)
{
	int ccd_intr_level = gpio_get_level(GPIO_CCD_MODE_ODL);

	if (ccd_intr_level) {
		/* Default set the SBU lines to AUX mode on TCPC-AIC */
		pca9675_update_pins(TYPE_C_PORT_0, 0,
			TCPC_AIC_IOE_USB_MUX_CNTRL_1 |
			TCPC_AIC_IOE_USB_MUX_CNTRL_0);
	} else {
		/* Set the SBU lines to CCD mode on TCPC-AIC */
		pca9675_update_pins(TYPE_C_PORT_0,
			TCPC_AIC_IOE_USB_MUX_CNTRL_1,
			TCPC_AIC_IOE_USB_MUX_CNTRL_0);
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

static void tcpc_aic_init(void)
{
	int i;

	/* Initialize the IOEXPANDER on TCPC-AIC */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		pca9675_init(i);

	/* Default set the SBU lines to AUX mode on both the TCPC-AIC */
	board_connect_c0_sbu_deferred();

#if defined(HAS_TASK_PD_C2)
	 /* Only TCPC-0 can do CCD or BSSB, Default set SBU lines to AUX */
	pca9675_update_pins(TYPE_C_PORT_2, 0,
		TCPC_AIC_IOE_USB_MUX_CNTRL_1 | TCPC_AIC_IOE_USB_MUX_CNTRL_0);
#endif
}
DECLARE_HOOK(HOOK_INIT, tcpc_aic_init, HOOK_PRIO_INIT_PCA9675);

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
int board_get_version(void)
{
	int port0, port1;
	int fab_id, board_id, bom_id;

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

	return board_id | (fab_id << 8);
}
