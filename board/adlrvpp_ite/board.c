/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-ITE board-specific configuration */

#include "bb_retimer.h"
#include "button.h"
#include "common.h"
#include "charger.h"
#include "fan.h"
#include "fusb302.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "icelake.h"
#include "isl9241.h"
#include "it83xx_pd.h"
#include "lid_switch.h"
#include "pca9675.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "sn5s330.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tablet_mode.h"
#include "uart.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ## args)

/*
 * TCPC AIC used on all the ports are identical expect the I2C lines which
 * are on the respective TCPC port's EC I2C line. Hence, I2C address and
 * the GPIOs to control the retimers are also same for all the ports.
 */
#define TCPC_AIC_IOE_BB_RETIMER_RST	PCA9675_IO_P00
#define TCPC_AIC_IOE_BB_RETIMER_LS_EN	PCA9675_IO_P01
#define TCPC_AIC_IOE_USB_MUX_CNTRL_1	PCA9675_IO_P04
#define TCPC_AIC_IOE_USB_MUX_CNTRL_0	PCA9675_IO_P05
#define TCPC_AIC_IOE_OC			PCA9675_IO_P10

#define TCPC_AIC_IOE_DIRECTION (PCA9675_DEFAULT_IO_DIRECTION & \
	~(TCPC_AIC_IOE_BB_RETIMER_RST | TCPC_AIC_IOE_BB_RETIMER_LS_EN | \
	TCPC_AIC_IOE_USB_MUX_CNTRL_1 | TCPC_AIC_IOE_USB_MUX_CNTRL_0 | \
	TCPC_AIC_IOE_OC))

/* Mutex for BB retimer shared NVM access */
static struct mutex bb_nvm_mutex;

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	[I2C_CHAN_FLASH] = {
		.name = "ec_flash",
		.port = IT83XX_I2C_CH_A,
		.kbps = 100,
		.scl = GPIO_EC_I2C_PROG_SCL,
		.sda = GPIO_EC_I2C_PROG_SDA,
	},
	[I2C_CHAN_BATT_CHG] = {
		.name = "batt_chg",
		.port = IT83XX_I2C_CH_B,
		.kbps = 100,
		.scl = GPIO_SMB_BS_CLK,
		.sda = GPIO_SMB_BS_DATA,
	},
	[I2C_CHAN_TYPEC_0] = {
		.name = "typec_0",
		.port = IT83XX_I2C_CH_C,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P0,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P0,
	},
	[I2C_CHAN_TYPEC_1] = {
		.name = "typec_1",
		.port = IT83XX_I2C_CH_F,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P2,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P2,
	},
	[I2C_CHAN_TYPEC_2] = {
		.name = "typec_2",
		.port = IT83XX_I2C_CH_E,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P1,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P1,
	},
	[I2C_CHAN_TYPEC_3] = {
		.name = "typec_3",
		.port = IT83XX_I2C_CH_D,
		.kbps = 400,
		.scl = GPIO_USBC_TCPC_I2C_CLK_P3,
		.sda = GPIO_USBC_TCPC_I2C_DATA_P3,
	},
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_CHAN_COUNT);
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

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
	[TYPE_C_PORT_2] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P2,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P2,
		.ppc_intr_handler = sn5s330_interrupt,
	},
	[TYPE_C_PORT_3] = {
		.tcpc_alert = GPIO_USBC_TCPC_ALRT_P3,
		.ppc_alert = GPIO_USBC_TCPC_PPC_ALRT_P3,
		.ppc_intr_handler = sn5s330_interrupt,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_aic_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB-C TCPC Configuration */
const struct tcpc_config_t tcpc_config[] = {
	[TYPE_C_PORT_0] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
#ifdef CONFIG_INTEL_VIRTUAL_MUX
		.usb23 = TYPE_C_PORT_0_USB2_NUM | (TYPE_C_PORT_0_USB3_NUM << 4),
#endif
	},
	[TYPE_C_PORT_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_1,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
#ifdef CONFIG_INTEL_VIRTUAL_MUX
		.usb23 = TYPE_C_PORT_1_USB2_NUM | (TYPE_C_PORT_1_USB3_NUM << 4),
#endif
	},
	[TYPE_C_PORT_2] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_2,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
#ifdef CONFIG_INTEL_VIRTUAL_MUX
		.usb23 = TYPE_C_PORT_2_USB2_NUM | (TYPE_C_PORT_2_USB3_NUM << 4),
#endif
	},
	[TYPE_C_PORT_3] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TYPEC_3,
			.addr_flags = I2C_ADDR_FUSB302_TCPC_AIC,
		},
		.drv = &fusb302_tcpm_drv,
#ifdef CONFIG_INTEL_VIRTUAL_MUX
		.usb23 = TYPE_C_PORT_3_USB2_NUM | (TYPE_C_PORT_3_USB3_NUM << 4),
#endif
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

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
	[TYPE_C_PORT_2] = {
		.i2c_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
	[TYPE_C_PORT_3] = {
		.i2c_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_ADDR_SN5S330_TCPC_AIC_PPC,
		.drv = &sn5s330_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ppc_chips) == CONFIG_USB_PD_PORT_MAX_COUNT);
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB-C retimer Configuration */
struct usb_mux usbc0_retimer = {
	.usb_port = TYPE_C_PORT_0,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT_TYPEC_0,
	.i2c_addr_flags = I2C_PORT0_BB_RETIMER_ADDR,
};
struct usb_mux usbc1_retimer = {
	.usb_port = TYPE_C_PORT_1,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT_TYPEC_1,
	.i2c_addr_flags = I2C_PORT1_BB_RETIMER_ADDR,
};
struct usb_mux usbc2_retimer = {
	.usb_port = TYPE_C_PORT_2,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT_TYPEC_2,
	.i2c_addr_flags = I2C_PORT2_BB_RETIMER_ADDR,
};
struct usb_mux usbc3_retimer = {
	.usb_port = TYPE_C_PORT_3,
	.driver = &bb_usb_retimer,
	.i2c_port = I2C_PORT_TYPEC_3,
	.i2c_addr_flags = I2C_PORT3_BB_RETIMER_ADDR,
};

/* USB muxes Configuration */
const struct usb_mux usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.usb_port = TYPE_C_PORT_0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc0_retimer,
	},
	[TYPE_C_PORT_1] = {
		.usb_port = TYPE_C_PORT_1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc1_retimer,
	},
	[TYPE_C_PORT_2] = {
		.usb_port = TYPE_C_PORT_2,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc2_retimer,
	},
	[TYPE_C_PORT_3] = {
		.usb_port = TYPE_C_PORT_3,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
		.next_mux = &usbc3_retimer,
	},
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
	[TYPE_C_PORT_2] = {
		.i2c_host_port = I2C_PORT_TYPEC_2,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
	[TYPE_C_PORT_3] = {
		.i2c_host_port = I2C_PORT_TYPEC_3,
		.i2c_addr_flags = I2C_ADDR_PCA9675_TCPC_AIC_IOEX,
		.io_direction = TCPC_AIC_IOE_DIRECTION,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

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
	int ioex = port < TYPE_C_PORT_2 ?
			TYPE_C_PORT_1 : TYPE_C_PORT_3;

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

	 /* Only TCPC-0 can do CCD or BSSB, Default set SBU lines to AUX */
	pca9675_update_pins(TYPE_C_PORT_2, 0,
		TCPC_AIC_IOE_USB_MUX_CNTRL_1 | TCPC_AIC_IOE_USB_MUX_CNTRL_0);
}
DECLARE_HOOK(HOOK_INIT, tcpc_aic_init, HOOK_PRIO_INIT_PCA9675);

/******************************************************************************/
/* PWROK signal configuration */
/*
 * On ADLRVP the ALL_SYS_PWRGD, VCCST_PWRGD, PCH_PWROK, and SYS_PWROK
 * signals are handled by the board. No EC control needed.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {};
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
