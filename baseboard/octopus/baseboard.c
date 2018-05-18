/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Octopus family-specific configuration */

#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/bc12/bq24392.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "power.h"
#include "system.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	{GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED"},
#endif
	{GPIO_PCH_SLP_S3_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
	{GPIO_SUSPWRDNACK,    POWER_SIGNAL_ACTIVE_HIGH,
	 "SUSPWRDNACK_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "ALL_SYS_PGOOD"},
	{GPIO_RSMRST_L_PGOOD, POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L"},
	{GPIO_PP3300_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP3300_PG"},
	{GPIO_PP5000_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP5000_PG"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

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
/* USB-A Configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
#if USB_PORT_COUNT == 1
	GPIO_EN_USB_A_5V,
	/* TODO(b/74388692): Add second port control after hardware fix. */
#else
	GPIO_EN_USB_A0_5V,
	GPIO_EN_USB_A1_5V,
#endif
};

/******************************************************************************/
/* BC 1.2 chip Configuration */
const struct bq24392_config_t bq24392_config[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	{
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* Chipset callbacks/hooks */

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
	/* Enable 5.0V and 3.3V rails, and wait for Power Good */
	power_5v_enable(task_get_current(), 1);

	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP5000_PG) ||
	       !gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}

/* Called on AP S5 -> S3 transition */
static void baseboard_chipset_startup(void)
{
	/* Enable Trackpad in S3+, so it can be an AP wake source. */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, baseboard_chipset_startup,
	     HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void baseboard_chipset_resume(void)
{
	/*
	 * GPIO_ENABLE_BACKLIGHT is AND'ed with SOC_EDP_BKLTEN from the SoC and
	 * LID_OPEN connection in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void baseboard_chipset_suspend(void)
{
	/*
	 * GPIO_ENABLE_BACKLIGHT is AND'ed with SOC_EDP_BKLTEN from the SoC and
	 * LID_OPEN connection in hardware.
	 */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void baseboard_chipset_shutdown(void)
{
	/* Disable Trackpad in S5- to save power; not a low power wake source */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, baseboard_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);

/* Called by APL power state machine when transitioning to G3. */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/* Disable 5.0V and 3.3V rails, and wait until they power down. */
	power_5v_enable(task_get_current(), 0);

	/*
	 * Shutdown the 3.3V rail and wait for it to go down. We cannot wait
	 * for the 5V rail since other tasks may be using it.
	 */
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP3300_PG))
		;
}

/******************************************************************************/
/* Power Delivery and charing functions */

void baseboard_tcpc_init(void)
{
	int count = 0;
	int port;

	/* Wait for disconnected battery to wake up */
	while (battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED) {
		usleep(100 * MSEC);
		/* Give up waiting after more than 1 second */
		if (++count > 10) {
			ccprintf("Battery still disconnected > 1 second!\n");
			break;
		}
	}

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_COUNT);
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
