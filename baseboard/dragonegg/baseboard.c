/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg family-specific configuration */
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "chipset.h"
#include "console.h"
#include "driver/bc12/max14637.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/sn5s330.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/tcpm/tusb422.h"
#include "espi.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "power.h"
#include "timer.h"
#include "util.h"
#include "tcpci.h"
#include "usbc_ppc.h"
#include "util.h"

#define USB_PD_PORT_ITE_0	0
#define USB_PD_PORT_ITE_1	1
#define USB_PD_PORT_TUSB422_2	2

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map configuration */
/* TODO(b/111125177): Increase these speeds to 400 kHz and verify operation */
const struct i2c_port_t i2c_ports[] = {
	{"eeprom", IT83XX_I2C_CH_A, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"sensor", IT83XX_I2C_CH_B, 100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"usbc12",  IT83XX_I2C_CH_C, 100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"usbc0",  IT83XX_I2C_CH_E, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"power",  IT83XX_I2C_CH_F, 100, GPIO_I2C5_SCL, GPIO_I2C5_SDA}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* Chipset callbacks/hooks */

/* Called on AP S5 -> S3 transition */
static void baseboard_chipset_startup(void)
{
	/* TODD(b/111121615): Need to fill out this hook */
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, baseboard_chipset_startup,
	     HOOK_PRIO_DEFAULT);

/* Called on AP S0iX -> S0 transition */
static void baseboard_chipset_resume(void)
{
	/* TODD(b/111121615): Need to fill out this hook */
	/* Enable display backlight. */
	gpio_set_level(GPIO_EDP_BKTLEN_OD, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S0iX transition */
static void baseboard_chipset_suspend(void)
{
	/* TODD(b/111121615): Need to fill out this hook */
	/* Enable display backlight. */
	gpio_set_level(GPIO_EDP_BKTLEN_OD, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void baseboard_chipset_shutdown(void)
{
	/* TODD(b/111121615): Need to fill out this hook */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, baseboard_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	int timeout_ms = 20;
	/*
	 * Disable the TCPC power rail and the PP5000 rail before going into
	 * hibernate. Note, these 2 rails are powered up as the default state in
	 * gpio.inc.
	 */
	gpio_set_level(GPIO_EN_PP5000, 0);
	/* Wait for PP5000 to drop before disabling PP3300_TCPC */
	while (gpio_get_level(GPIO_PP5000_PG_OD) && timeout_ms > 0) {
		msleep(1);
		timeout_ms--;
	}
	if (!timeout_ms)
		CPRINTS("PP5000_PG didn't go low after 20 msec");
	gpio_set_level(GPIO_EN_PP3300_TCPC, 0);
}
/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},

	[USB_PD_PORT_ITE_1] = {
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it83xx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},

	[USB_PD_PORT_TUSB422_2] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USBC1C2,
			.addr_flags = TUSB422_I2C_ADDR_FLAGS,
		},
		.drv = &tusb422_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.i2c_port = I2C_PORT_USBC0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv
	},

	[USB_PD_PORT_ITE_1] = {
		.i2c_port = I2C_PORT_USBC1C2,
		.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
		.drv = &syv682x_drv
	},

	[USB_PD_PORT_TUSB422_2] = {
		.i2c_port = I2C_PORT_USBC1C2,
		.i2c_addr_flags = NX20P3481_ADDR2_FLAGS,
		.drv = &nx20p348x_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ITE_0] = {
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},

	[USB_PD_PORT_ITE_1] = {
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},

	[USB_PD_PORT_TUSB422_2] = {
		.port_addr = 0,
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
};

/******************************************************************************/
/* BC 1.2 chip Configuration */
const struct max14637_config_t max14637_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON_ODL,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_MAX,
		.flags = MAX14637_FLAGS_ENABLE_ACTIVE_LOW,
	},
	{
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON_ODL,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_MAX,
		.flags = MAX14637_FLAGS_ENABLE_ACTIVE_LOW,
	},
	{
		.chip_enable_pin = GPIO_USB_C2_BC12_VBUS_ON_ODL,
		.chg_det_pin = GPIO_USB_C2_BC12_CHG_MAX,
		.flags = MAX14637_FLAGS_ENABLE_ACTIVE_LOW,
	},
};

/* Power Delivery and charging functions */

void baseboard_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPPC_INT_L);
	gpio_enable_interrupt(GPIO_USB_C2_TCPPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C2_TCPC_INT_ODL);

}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	/*
	 * Since C0/C1 TCPC are embedded within EC, we don't need the PDCMD
	 * tasks.The (embedded) TCPC status since chip driver code will
	 * handles its own interrupts and forward the correct events to
	 * the PD_C0 task. See it83xx/intc.c
	 */

	if (!gpio_get_level(GPIO_USB_C2_TCPC_INT_ODL))
		status = PD_STATUS_TCPC_ALERT_2;

	return status;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/*
	 * C0 & C1: The internal TCPC on ITE EC does not have a reset signal,
	 * but it will get reset when the EC gets reset.
	 */
}
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/*
	 * We ignore the cc_pin because the polarity should already be set
	 * correctly in the PPC driver via the pd state machine.
	 */
	if (ppc_set_vconn(port, enabled) != EC_SUCCESS)
		cprints(CC_USBPD, "C%d: Failed %sabling vconn",
			port, enabled ? "en" : "dis");
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTFUSB("Skip enable C%d", port);
		return EC_ERROR_INVAL;
	}

	CPRINTSUSB("New charge port: C%d", port);

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTSUSB("C%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTSUSB("C%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}
