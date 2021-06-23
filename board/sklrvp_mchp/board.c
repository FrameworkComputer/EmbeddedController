/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Microchip Evaluation Board(EVB) with
 * MEC1521H 144-pin processor card.
 * EVB connected to Intel eSPI host chipset.
 */

/* #include "bb_retimer.h" */
#include "button.h" /* */
#include "charger.h" /* */
#include "chipset.h"
#include "driver/charger/isl9241.h" /* */
#include "espi.h"
#include "extpower.h"
#include "driver/tcpm/fusb307.h" /* */
#include "driver/tcpm/tcpci.h"
#include "gpio_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "espi.h"
#include "lpc_chip.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power/skylake.h"
#include "power_button.h"
#include "spi.h"
#include "spi_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_mux.h" /* */
#include "usb_pd.h"
#include "usb_pd_tcpm.h" /* */
#include "util.h"
#include "battery_smart.h"

#include "gpio_list.h"

#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/*
 * NOTE: MCHP EVB + SKL RVP3 does not use BD99992 PMIC.
 * RVP3 PMIC controlled by RVP3 logic.
 */
#define I2C_ADDR_BD99992_FLAGS	0x30

/* TCPC table of GPIO pins */
const struct tcpc_gpio_config_t tcpc_gpios[] = {
	[TYPE_C_PORT_0] = {
		.vbus = {
			.pin = GPIO_USB_C0_VBUS_INT,
			.pin_pol = 1,
		},
		.src = {
			.pin = GPIO_USB_C0_SRC_EN,
			.pin_pol = 1,
		},
		.snk = {
			.pin = GPIO_USB_C0_SNK_EN_L,
			.pin_pol = 0,
		},
		.src_ilim = {
			.pin = GPIO_USB_C0_SRC_HI_ILIM,
			.pin_pol = 1,
		},
	},
	[TYPE_C_PORT_1] = {
		.vbus = {
			.pin = GPIO_USB_C1_VBUS_INT,
			.pin_pol = 1,
		},
		.src = {
			.pin = GPIO_USB_C1_SRC_EN,
			.pin_pol = 1,
		},
		.snk = {
			.pin = GPIO_USB_C1_SNK_EN_L,
			.pin_pol = 0,
		},
		.src_ilim = {
			.pin = GPIO_USB_C1_SRC_HI_ILIM,
			.pin_pol = 1,
		},
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[] = {
	[TYPE_C_PORT_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = MCHP_I2C_PORT0,
			.addr_flags = FUSB307_I2C_ADDR_FLAGS,
		},
		.drv = &fusb307_tcpm_drv,
	},
	[TYPE_C_PORT_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = MCHP_I2C_PORT2,
			.addr_flags = FUSB307_I2C_ADDR_FLAGS,
		},
		.drv = &fusb307_tcpm_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(tcpc_config) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* USB MUX Configuration */
const struct usb_mux usb_muxes[] = {
	[TYPE_C_PORT_0] = {
		.usb_port = TYPE_C_PORT_0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
	[TYPE_C_PORT_1] = {
		.usb_port = TYPE_C_PORT_1,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	/*
	 * Port-80 Display, Charger, Battery, IO-expanders, EEPROM,
	 * IMVP9, AUX-rail, power-monitor.
	 */
	[I2C_CHAN_BATT_CHG] = {
		.name = "batt_chg",
		.port = MCHP_I2C_PORT4,
		.kbps = 100,
		.scl = GPIO_SMB04_SCL,
		.sda = GPIO_SMB04_SDA,
	},
	/* other I2C devices */
	[I2C_CHAN_MISC] = {
		.name = "misc",
		.port = MCHP_I2C_PORT5,
		.kbps = 100,
		.scl = GPIO_SMB05_SCL,
		.sda = GPIO_SMB05_SDA,
	},
	[I2C_CHAN_TCPC_0] = {
		.name = "tcpci0",
		.port = MCHP_I2C_PORT0,
		.kbps = 100,
		.scl = GPIO_SMB00_SCL,
		.sda = GPIO_SMB00_SDA,
	},
	[I2C_CHAN_TCPC_1] = {
		.name = "tcpci1",
		.port = MCHP_I2C_PORT2,
		.kbps = 100,
		.scl = GPIO_SMB02_SCL,
		.sda = GPIO_SMB02_SDA,
	}
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ports) == I2C_CHAN_COUNT);
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Charger Chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};

static void sklrvp_init(void)
{
	int extpwr = extpower_is_present();

	/* Provide AC status to the PCH */
	CPRINTS("Set PCH_ACOK = %d", extpwr);
	gpio_set_level(GPIO_PCH_ACOK, extpwr);
}
DECLARE_HOOK(HOOK_INIT, sklrvp_init, HOOK_PRIO_DEFAULT);

static void sklrvp_interrupt_init(void)
{
	/* Enable ALL_SYS_PWRGD interrupt  */
	CPUTS("IEN ALL_SYS_PWRGD");
	gpio_enable_interrupt(GPIO_ALL_SYS_PWRGD);
}
DECLARE_HOOK(HOOK_INIT, sklrvp_interrupt_init, HOOK_PRIO_DEFAULT);

/* Will this work for SKL-RVP */
/******************************************************************************/
/* PWROK signal configuration */
/*
 * SKL with MCHP EVB uses EC to handle ALL_SYS_PWRGD signal.
 * MEC170x/MEC152x connected to SKL/KBL RVP3 reference board
 * is required to monitor ALL_SYS_PWRGD and drive SYS_RESET_L
 * after a 10 to 100 ms delay.
 */
#ifdef CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD

static void board_all_sys_pwrgd(void)
{
	int allsys_in = gpio_get_level(GPIO_ALL_SYS_PWRGD);
	int allsys_out = gpio_get_level(GPIO_SYS_RESET_L);

	if (allsys_in == allsys_out)
		return;

	CPRINTS("ALL_SYS_PWRGD=%d SYS_RESET_L=%d", allsys_in, allsys_out);

	/*
	 * Wait at least 10 ms between power signals going high
	 */
	if (allsys_in)
		msleep(100);

	if (!allsys_out) {
		gpio_set_level(GPIO_SYS_RESET_L, allsys_in);
		/* Force fan on for kabylake RVP */
		gpio_set_level(GPIO_EC_FAN1_PWM, 1);
		CPRINTS("Set SYS_RESET_L = %d", allsys_in);
	}
}
DECLARE_DEFERRED(board_all_sys_pwrgd);

void board_all_sys_pwrgd_interrupt(enum gpio_signal signal)
{
	CPUTS("ISR ALL_SYS_PWRGD");
	hook_call_deferred(&board_all_sys_pwrgd_data, 0);
}
#endif /* #ifdef CONFIG_BOARD_HAS_ALL_SYS_PWRGD */

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

#ifdef CONFIG_BOARD_PRE_INIT
/*
 * Used to enable JTAG debug during development.
 * NOTE: UART2_TX on the same pin as SWV(JTAG_TDO).
 * If UART2 is used for EC console you cannot enable SWV.
 * For no SWV change mode to MCHP_JTAG_MODE_SWD.
 * For low power idle testing enable GPIO060 as function 2(48MHZ_OUT)
 * to check PLL is turning off in heavy sleep. Note, do not put GPIO060
 * in gpio.inc
 * GPIO060 is port 1 bit[16].
 */
void board_config_pre_init(void)
{
#ifdef CONFIG_CHIPSET_DEBUG
	MCHP_EC_JTAG_EN = MCHP_JTAG_ENABLE + MCHP_JTAG_MODE_SWD;
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_MCHP_48MHZ_OUT)
	gpio_set_alternate_function(1, 0x10000, 2);
#endif
}
#endif /* #ifdef CONFIG_BOARD_PRE_INIT */


/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, 0);
}

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ QMSPI0_PORT, 4, GPIO_QMSPI_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/*
 * Enable or disable input devices,
 * based upon chipset state and tablet mode
 */
static void enable_input_devices(void)
{
	int kb_enable = 1;
	int tp_enable = 1;

	/* Disable both TP and KB in tablet mode */
	if (!gpio_get_level(GPIO_TABLET_MODE_L))
		kb_enable = tp_enable = 0;
	/* Disable TP if chipset is off */
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		tp_enable = 0;

	keyboard_scan_enable(kb_enable, KB_SCAN_DISABLE_LID_ANGLE);
	gpio_set_level(GPIO_ENABLE_TOUCHPAD, tp_enable);
}

#ifdef CONFIG_USBC_VCONN
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
#ifndef CONFIG_USBC_PPC_VCONN
	/*
	 * TODO: MCHP EC does not have built-in TCPC. Does external
	 * I2C based TCPC need this?
	 */
#endif
}
#endif

