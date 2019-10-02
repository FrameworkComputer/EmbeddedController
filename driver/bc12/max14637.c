/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * MAX14637 USB BC 1.2 Charger Detector driver.
 *
 * NOTE: The driver assumes that CHG_AL_N and SW_OPEN are not connected,
 * therefore the value of CHG_DET indicates whether the source is NOT a
 * low-power standard downstream port (SDP).  In order to use higher currents,
 * the system will have to charge ramp.
 */

#include "max14637.h"
#include "cannonlake.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "power.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
/**
 * Returns true if the charger detect pin is activated.
 *
 * @parm cfg driver for chip to read the charger detect pin for.
 * @return 1 if charger detect is activated (high when active high or
 *	low with active low), otherwise 0.
 */
static int is_chg_det_activated(const struct max14637_config_t * const cfg)
{
	return !!gpio_get_level(cfg->chg_det_pin) ^
		!!(cfg->flags & MAX14637_FLAGS_CHG_DET_ACTIVE_LOW);
}
#endif

/**
 * Activates the Chip Enable GPIO based on the enabled value.
 *
 * @param cfg driver for chip that will set chip enable gpio.
 * @param enable 1 to activate gpio (high for active high and low for active
 *	low).
 */
static void activate_chip_enable(
	const struct max14637_config_t * const cfg, const int enable)
{
	gpio_set_level(
		cfg->chip_enable_pin,
		!!enable ^ !!(cfg->flags & MAX14637_FLAGS_ENABLE_ACTIVE_LOW));
}

/**
 * Perform BC1.2 detection and update charge manager.
 *
 * @param port: The Type-C port where VBUS is present.
 */
static void bc12_detect(const int port)
{
	const struct max14637_config_t * const cfg = &max14637_config[port];
	struct charge_port_info new_chg;

	/*
	 * Enable the IC to begin detection and connect switches if necessary.
	 */
	activate_chip_enable(cfg, 1);

	new_chg.voltage = USB_CHARGER_VOLTAGE_MV;
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	/*
	 * Apple or TomTom charger detection can take as long as 600ms.  Wait a
	 * little bit longer for margin.
	 */
	msleep(630);

	/*
	 * The driver assumes that CHG_AL_N and SW_OPEN are not connected,
	 * therefore an activated CHG_DET indicates whether the source is NOT a
	 * low-power standard downstream port (SDP). The system will have to
	 * ramp the current to determine the limit.
	 */
	new_chg.current = is_chg_det_activated(cfg) ? 2400 : 500;
#else
	/*
	 * If the board doesn't support charge ramping, then assume the lowest
	 * denominator; that is assume the charger detected is a weak dedicated
	 * charging port (DCP) which can only supply 500mA.
	 */
	new_chg.current = 500;
#endif /* !defined(CONFIG_CHARGE_RAMP_SW && CONFIG_CHARGE_RAMP_HW) */

	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, port, &new_chg);
}

/**
 * Turn off the MAX14637 detector.
 *
 * @param port: Which USB Type-C port's BC1.2 detector to turn off.
 */
static void power_down_ic(const int port)
{
	const struct max14637_config_t * const cfg = &max14637_config[port];

	/* Turn off the IC. */
	activate_chip_enable(cfg, 0);

	/* Let charge manager know there's no more charge available. */
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, port, NULL);
}

/**
 * If VBUS is present, determine the charger type, otherwise power down the IC.
 *
 * @param port: Which USB Type-C port to examine.
 */
static void detect_or_power_down_ic(const int port)
{
	int vbus_present;

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	vbus_present = tcpm_get_vbus_level(port);
#else
	vbus_present = pd_snk_is_vbus_provided(port);
#endif /* !defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) */

	if (vbus_present) {
#if defined(CONFIG_POWER_PP5000_CONTROL) && defined(HAS_TASK_CHIPSET)
		/* Turn on the 5V rail to allow the chip to be powered. */
		power_5v_enable(task_get_current(), 1);
#endif
		bc12_detect(port);
	} else {
		power_down_ic(port);
#if defined(CONFIG_POWER_PP5000_CONTROL) && defined(HAS_TASK_CHIPSET)
		/* Issue a request to turn off the rail. */
		power_5v_enable(task_get_current(), 0);
#endif
	}
}

void usb_charger_task(void *u)
{
	const int port = (intptr_t)u;
	uint32_t evt;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	detect_or_power_down_ic(port);

	while (1) {
		evt = task_wait_event(-1);

		if (evt & USB_CHG_EVENT_VBUS)
			detect_or_power_down_ic(port);
	}
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/*
	 * The MAX14637 automatically sets up the USB 2.0 high-speed switches.
	 */
}

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
int usb_charger_ramp_allowed(int supplier)
{
	/*
	 * Due to the limitations in the application of the MAX14637, we
	 * don't quite know exactly what we're plugged into.  Therefore,
	 * the supplier type will be CHARGE_SUPPLIER_OTHER.
	 */
	return supplier == CHARGE_SUPPLIER_OTHER;
}

int usb_charger_ramp_max(int supplier, int sup_curr)
{
	/* Use the current limit that was decided by the MAX14637. */
	if (supplier == CHARGE_SUPPLIER_OTHER)
		return sup_curr;
	else
		return 500;
}
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */
