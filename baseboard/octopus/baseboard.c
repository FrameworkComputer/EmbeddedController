/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Octopus family-specific configuration */

#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/bc12/max14637.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/nx20p348x.h"
#include "gpio.h"
#include "hooks.h"
#ifdef VARIANT_OCTOPUS_EC_ITE8320
#include "intc.h"
#endif
#include "keyboard_scan.h"
#include "power.h"
#include "system.h"
#include "task.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/******************************************************************************/
/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
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
#ifndef CONFIG_KEYBOARD_KEYPAD
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
#else
		0x1c, 0xfe, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfe, 0xff, 0xff, 0xff, /* full set */
#endif
	},
};

/******************************************************************************/
/* USB-A Configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_5V,
	GPIO_EN_USB_A1_5V,
};

/******************************************************************************/
/* BC 1.2 chip Configuration */
const struct max14637_config_t max14637_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	{
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* Charger Chip Configuration */
#ifdef VARIANT_OCTOPUS_CHARGER_ISL9238
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
#endif

/******************************************************************************/
/* Chipset callbacks/hooks */

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
#ifdef IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
	/*
	 * Since we disable eSPI module for IT8320 part when system goes into G3
	 * state, so we need to enable it at system startup.
	 */
	espi_enable_pad(1);
#endif

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
	/* Enable the keyboard backlight */
	gpio_set_level(GPIO_KB_BL_PWR_EN, 1);
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
	/* Disable the keyboard backlight */
	gpio_set_level(GPIO_KB_BL_PWR_EN, 0);
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
#ifdef VARIANT_OCTOPUS_EC_ITE8320
	/*
	 * We want the processor to be reset before dropping the PP3300_A rail
	 * below, otherwise the PP3300_LDO and PP3300_EC rails can be overloaded
	 */
	if (gpio_get_level(GPIO_PCH_SLP_S4_L)) {
		/* assert RSMRST to PCH */
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);
		/* Wait SLP_S4 goes low; would rather watchdog than continue */
		while (gpio_get_level(GPIO_PCH_SLP_S4_L))
			;
	}
#endif

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

#ifdef IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
	/*
	 * The IT8320 part doesn't go into its lowest power state in idle task
	 * when the eSPI module is on and CS# is asserted, so we need to
	 * manually disable it.
	 */
	espi_enable_pad(0);
#endif
}

int board_is_i2c_port_powered(int port)
{
	if (port != I2C_PORT_SENSOR)
		return 1;

	/* Sensor rails are off in S5/G3 */
	return chipset_in_state(CHIPSET_STATE_ANY_OFF) ? 0 : 1;
}

/******************************************************************************/
/* Power Delivery and charing functions */

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
enum adc_channel board_get_vbus_adc(int port)
{
	if (port == 0)
		return ADC_VBUS_C0;
	if (port == 1)
		return ADC_VBUS_C1;
	CPRINTSUSB("Unknown vbus adc port id: %d", port);
	return ADC_VBUS_C0;
}
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */

void baseboard_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < board_get_usb_pd_port_count(); ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
/* Called after the cbi_init (via +2) */
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0;
		     (i < ppc_cnt) && (i < board_get_usb_pd_port_count());
		     i++) {
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
	for (i = 0; (i < ppc_cnt) && (i < board_get_usb_pd_port_count()); i++) {
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

void board_hibernate(void)
{
	int port;

	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 *
	 * If board_hibernate() is called from within chipset task, then
	 * chipset_do_shutdown needs to be called directly since
	 * chipset_force_shutdown basically just sets wake event for chipset
	 * task. But that will not help since chipset task is in board_hibernate
	 * and never returns back to the power state machine to take down power
	 * rails.
	 */
#ifdef HAS_TASK_CHIPSET
	if (task_get_current() == TASK_ID_CHIPSET)
		chipset_do_shutdown();
	else
#endif
		chipset_force_shutdown(CHIPSET_SHUTDOWN_BOARD_CUSTOM);

#ifdef CONFIG_USBC_PPC_NX20P3483
	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE)
		pd_request_source_voltage(port, NX20P348X_SAFE_RESET_VBUS_MV);
#endif

	/*
	 * If Vbus isn't already on this port, then we need to put the PPC into
	 * low power mode or open the SNK FET based on which signals wake up
	 * the EC from hibernate.
	 */
	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		if (!pd_is_vbus_present(port)) {
#ifdef VARIANT_OCTOPUS_EC_ITE8320
			/*
			 * ITE variant uses the PPC interrupts instead of
			 * AC_PRESENT to wake up, so we do not need to enable
			 * the SNK FETS.
			 */
			ppc_enter_low_power_mode(port);
#else
			/*
			 * Open the SNK path to allow AC to pass through to the
			 * charger when connected. This is need if the TCPC/PPC
			 * rails do not go away and AC_PRESENT wakes up the EC
			 * (b/79173959).
			 */
			ppc_vbus_sink_enable(port, 1);
#endif
		}
	}

	/*
	 * Delay allows AP power state machine to settle down along
	 * with any PD contract renegotiation, and tcpm to put TCPC into low
	 * power mode if required.
	 */
	crec_msleep(1500);
}
