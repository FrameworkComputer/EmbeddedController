/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skyrim family-specific USB-C configuration */

#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x_public.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/anx7483_public.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp6.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* USB-A ports */
enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_A1, USBA_PORT_COUNT };

/* USB-C ports */
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

test_export_static void reset_nct38xx_port(int port);

void usbc_interrupt_init(void)
{
#if defined(CONFIG_PLATFORM_EC_USB_CHARGER)
	/* Enable BC 1.2 interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));
#endif

	/* Enable SBU fault interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_sbu_fault));
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

test_export_static void usb_fault_interrupt_init(void)
{
	/* Enable USB fault interrupts when we hit S5 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usb_fault_interrupt_init, HOOK_PRIO_DEFAULT);

test_export_static void usb_fault_interrupt_disable(void)
{
	/* Disable USB fault interrupts leaving S5 */
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_fault_interrupt_disable,
	     HOOK_PRIO_DEFAULT);

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int rv;

	if (port == CHARGE_PORT_NONE) {
		CPRINTSUSB("Disabling all charger ports");

		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			/*
			 * If this port had booted in dead battery mode, go
			 * ahead and reset it so EN_SNK responds properly.
			 */
			if (nct38xx_get_boot_type(i) ==
			    NCT38XX_BOOT_DEAD_BATTERY) {
				reset_nct38xx_port(i);
				pd_set_error_recovery(i);
			}

			/*
			 * Do not return early if one fails otherwise we can
			 * get into a boot loop assertion failure.
			 */
			if (ppc_vbus_sink_enable(i, 0))
				CPRINTSUSB("Disabling C%d as sink failed.", i);
		}

		return EC_SUCCESS;
	} else if (!is_valid_port) {
		return EC_ERROR_INVAL;
	}

	/*
	 * Check if we can reset any ports in dead battery mode
	 *
	 * The NCT3807 may continue to keep EN_SNK low on the dead battery port
	 * and allow a dangerous level of voltage to pass through to the initial
	 * charge port (see b/183660105).  We must reset the ports if we have
	 * sufficient battery to do so, which will bring EN_SNK back under
	 * normal control.
	 */
	rv = EC_SUCCESS;
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (nct38xx_get_boot_type(i) == NCT38XX_BOOT_DEAD_BATTERY) {
			CPRINTSUSB("Found dead battery on %d", i);
			/*
			 * If we have battery, get this port reset ASAP.
			 * This means temporarily rejecting charge manager
			 * sets to it.
			 */
			if (pd_is_battery_capable()) {
				reset_nct38xx_port(i);
				pd_set_error_recovery(i);

				if (port == i)
					rv = EC_ERROR_INVAL;
			} else if (port != i) {
				/*
				 * If other port is selected and in dead battery
				 * mode, reset this port.  Otherwise, reject
				 * change because we'll brown out.
				 */
				if (nct38xx_get_boot_type(port) ==
				    NCT38XX_BOOT_DEAD_BATTERY) {
					reset_nct38xx_port(i);
					pd_set_error_recovery(i);
				} else {
					rv = EC_ERROR_INVAL;
				}
			}
		}
	}

	if (rv != EC_SUCCESS)
		return rv;

	/* Check if the port is sourcing VBUS. */
	if (tcpm_get_src_ctrl(port)) {
		CPRINTSUSB("Skip enable C%d", port);
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

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = signal == IOEX_USB_C1_FAULT_ODL ? 1 : 0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}

static void usb_fault_alert(void)
{
	int out;

	out = gpio_pin_get_dt(
		      GPIO_DT_FROM_NODELABEL(gpio_usb_hub_fault_q_odl)) &&
	      gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ioex_usb_a0_fault_odl)) &&
	      gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ioex_usb_a1_fault_db_odl));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_fault_odl), out);

	if (out == 0)
		CPRINTSUSB("USB fault, alerting the SoC");
}
DECLARE_DEFERRED(usb_fault_alert);

void usb_fault_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&usb_fault_alert_data, 0);
}

void usb_pd_soc_interrupt(enum gpio_signal signal)
{
	/*
	 * This interrupt is unexpected with our use of the SoC mux, so just log
	 * it as a point of interest.
	 */
	CPRINTSUSB("SOC PD Interrupt");
}

#ifdef CONFIG_CHARGER_ISL9241
/* Round up 3250 max current to multiple of 128mA for ISL9241 AC prochot. */
static void charger_prochot_init_isl9241(void)
{
	isl9241_set_ac_prochot(CHARGER_SOLO, CONFIG_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, charger_prochot_init_isl9241, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_CHARGER_ISL9241 */

#ifdef CONFIG_CHARGER_ISL9238
static void charger_prochot_init_isl9238(void)
{
	isl923x_set_ac_prochot(CHARGER_SOLO, CONFIG_AC_PROCHOT_CURRENT_MA);
	isl923x_set_dc_prochot(CHARGER_SOLO, CONFIG_DC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, charger_prochot_init_isl9238, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_CHARGER_ISL9238 */

test_export_static void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;
	const struct device *ioex_port0, *ioex_port1;
	int rv;

	/* The maximum pin numbers of the NCT38xx IO expander port is 8 */
	gpio_flags_t saved_port0_flags[8] = { 0 };
	gpio_flags_t saved_port1_flags[8] = { 0 };

	if (port == USBC_PORT_C0) {
		reset_gpio_l = &tcpc_config[0].rst_gpio;
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	} else if (port == USBC_PORT_C1) {
		reset_gpio_l = &tcpc_config[1].rst_gpio;
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));
	} else {
		/* Invalid port: do nothing */
		return;
	}
	gpio_save_port_config(ioex_port0, saved_port0_flags,
			      ARRAY_SIZE(saved_port0_flags));
	gpio_save_port_config(ioex_port1, saved_port1_flags,
			      ARRAY_SIZE(saved_port1_flags));

	gpio_pin_set_dt(reset_gpio_l, 1);
	crec_msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 0);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		crec_msleep(NCT3807_RESET_POST_DELAY_MS);

	/* Re-enable the IO expander pins */
	gpio_restore_port_config(ioex_port0, saved_port0_flags,
				 ARRAY_SIZE(saved_port0_flags));
	gpio_restore_port_config(ioex_port1, saved_port1_flags,
				 ARRAY_SIZE(saved_port1_flags));

	if (power_get_state() == POWER_S0) {
		/* If we transitioned to S0 during the reset then the restore
		 * may set the vbus enable pin low. Ensure the A port is
		 * always powered in S0.
		 */
		rv = usb_charge_set_mode(port, USB_CHARGE_MODE_ENABLED,
					 USB_ALLOW_SUSPEND_CHARGE);
		if (rv)
			CPRINTSUSB("S0 TCPC enable failure on port %d(%d)",
				   port, rv);
	}
}

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	reset_nct38xx_port(USBC_PORT_C1);
}

#if defined(CONFIG_PLATFORM_EC_USB_CHARGER)
void bc12_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_BC12_INT_ODL:
		usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
		break;

	default:
		break;
	}
}

/**
 * Return if VBUS is sagging too low
 *
 * For legacy BC1.2 charging with CONFIG_CHARGE_RAMP_SW, ramp up input current
 * until voltage drops to 4.5V. Don't go lower than this to be kind to the
 * charger (see b/67964166).
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage = 0;
	int rv;

	rv = charger_get_vbus_voltage(port, &voltage);

	if (rv) {
		CPRINTSUSB("%s rv=%d", __func__, rv);
		return 0;
	}

	/*
	 * b/168569046: The ISL9241 sometimes incorrectly reports 0 for unknown
	 * reason, causing ramp to stop at 0.5A. Workaround this by ignoring 0.
	 * This partly defeats the point of ramping, but will still catch
	 * VBUS below 4.5V and above 0V.
	 */
	if (voltage == 0) {
		CPRINTSUSB("%s vbus=0", __func__);
		return 0;
	}

	if (voltage < SKYRIM_BC12_MIN_VOLTAGE)
		CPRINTSUSB("%s vbus=%d", __func__, voltage);

	return voltage < SKYRIM_BC12_MIN_VOLTAGE;
}
#endif

#define SAFE_RESET_VBUS_DELAY_MS 900
void board_hibernate(void)
{
	int port;
	enum ec_error_list ret;

	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851, b/143778351, b/147007265)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE) {
		pd_request_source_voltage(port, SKYRIM_SAFE_RESET_VBUS_MV);

		/* Give PD task and PPC chip time to get to 5V */
		crec_msleep(SAFE_RESET_VBUS_DELAY_MS);
	}

	/* Try to put our battery fuel gauge into sleep mode */
	ret = battery_sleep_fuel_gauge();
	if ((ret != EC_SUCCESS) && (ret != EC_ERROR_UNIMPLEMENTED))
		cprints(CC_SYSTEM, "Failed to send battery sleep command");
}
