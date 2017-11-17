/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * BQ24392 USB BC 1.2 Charger Detector driver.
 *
 * NOTE: The driver assumes that CHG_AL_N and SW_OPEN are not connected,
 * therefore the value of CHG_DET indicates whether the source is NOT a
 * low-power standard downstream port (SDP).  In order to use higher currents,
 * the system will have to charge ramp.
 */

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

struct bq24392_pins {
	enum gpio_signal chip_enable_l;
	enum gpio_signal chg_det;
};

static const struct bq24392_pins pin_tbl[] = {
	{ GPIO_USB_C0_BC12_VBUS_ON_L, GPIO_USB_C0_BC12_CHG_DET },
#ifdef HAS_TASK_USB_CHG_P1
	{ GPIO_USB_C1_BC12_VBUS_ON_L, GPIO_USB_C1_BC12_CHG_DET },
#endif
#ifdef HAS_TASK_USB_CHG_P2
	{ GPIO_USB_C2_BC12_VBUS_ON_L, GPIO_USB_C2_BC12_CHG_DET },
#endif
};

/**
 * Perform BC1.2 detection and update charge manager.
 *
 * @param port: The Type-C port where VBUS is present.
 */
static void bc12_detect(const int port)
{
	struct charge_port_info new_chg;

	/*
	 * Enable the IC to begin detection and connect switches if
	 * necessary.  Note, the value is 0 because the enable is active low.
	 */
	gpio_set_level(pin_tbl[port].chip_enable_l, 0);

	new_chg.voltage = USB_CHARGER_VOLTAGE_MV;
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	/*
	 * Apple or TomTom charger detection can take as long as 600ms.  Wait a
	 * little bit longer for margin.
	 */
	msleep(630);

	/*
	 * The driver assumes that CHG_AL_N and SW_OPEN are not connected,
	 * therefore the value of CHG_DET indicates whether the source is NOT a
	 * low-power standard downstream port (SDP).  The system will have to
	 * ramp the current to determine the limit.
	 */
	new_chg.current = gpio_get_level(pin_tbl[port].chg_det) ? 2400 : 500;
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
 * Turn off the BQ24392 detector.
 *
 * @param port: Which USB Type-C port's BC1.2 detector to turn off.
 */
static void power_down_ic(const int port)
{
	struct charge_port_info no_chg = { 0 };

	/* Turn off the IC. */
	gpio_set_level(pin_tbl[port].chip_enable_l, 1);

	/* Let charge manager know there's no more charge available. */
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, port, &no_chg);
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
		/* Turn on the 5V rail to allow the chip to be powered. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else
		gpio_set_level(GPIO_EN_PP5000, 1);
#endif
		bc12_detect(port);
	} else {
		power_down_ic(port);
#ifdef CONFIG_POWER_PP5000_CONTROL
		/* Issue a request to turn off the rail. */
		power_5v_enable(task_get_current(), 0);
#else
		gpio_set_level(GPIO_EN_PP5000, 0);
#endif
	}
}

void usb_charger_task(void *u)
{
	const int port = (intptr_t)u;
	uint32_t evt;

	ASSERT(port >= 0 && port <= 2);

	detect_or_power_down_ic(port);

	while (1) {
		evt = task_wait_event(-1);

		if (evt & USB_CHG_EVENT_VBUS)
			detect_or_power_down_ic(port);
	}
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/* The BQ24392 automatically sets up the USB 2.0 high-speed switches. */
}

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
int usb_charger_ramp_allowed(int supplier)
{
	/*
	 * Due to the limitations in the application of the BQ24392, we
	 * don't quite know exactly what we're plugged into.  Therefore,
	 * the supplier type will be CHARGE_SUPPLIER_OTHER.
	 */
	return supplier == CHARGE_SUPPLIER_OTHER;
}

int usb_charger_ramp_max(int supplier, int sup_curr)
{
	/* Use the current limit that was decided by the BQ24392. */
	if (supplier == CHARGE_SUPPLIER_OTHER)
		return sup_curr;
	else
		return 500;
}
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */
