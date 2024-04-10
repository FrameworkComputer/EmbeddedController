/* Copyright 2017 The ChromiumOS Authors
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

#include "builtin/assert.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "max14637.h"
#include "power.h"
#include "power/cannonlake.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
/**
 * Returns true if the charger detect pin is activated.
 *
 * @parm cfg driver for chip to read the charger detect pin for.
 * @return 1 if charger detect is activated (high when active high or
 *	low with active low), otherwise 0.
 */
static int is_chg_det_activated(const struct max14637_config_t *const cfg)
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
static void activate_chip_enable(const struct max14637_config_t *const cfg,
				 const int enable)
{
	gpio_set_level(cfg->chip_enable_pin,
		       !!enable ^ !!(cfg->flags &
				     MAX14637_FLAGS_ENABLE_ACTIVE_LOW));
}

/**
 * Update BC1.2 detected status to charge manager.
 *
 * @param port: The Type-C port where VBUS is present.
 */
static void update_bc12_status_to_charger_manager(const int port)
{
	const struct max14637_config_t *const cfg = &max14637_config[port];
	struct charge_port_info new_chg;

	new_chg.voltage = USB_CHARGER_VOLTAGE_MV;
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	/*
	 * The driver assumes that CHG_AL_N and SW_OPEN are not connected,
	 * therefore an activated CHG_DET indicates whether the source is NOT a
	 * low-power standard downstream port (SDP). The system will have to
	 * ramp the current to determine the limit.  The Type-C spec prohibits
	 * proprietary methods now, therefore 1500mA is the max.
	 */
	new_chg.current = is_chg_det_activated(cfg) ? USB_CHARGER_MAX_CURR_MA :
						      500;
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
 * Perform BC1.2 detection.
 *
 * @param port: The Type-C port where VBUS is present.
 */
static void bc12_detect(const int port)
{
	const struct max14637_config_t *const cfg = &max14637_config[port];

	/*
	 * Enable the IC to begin detection and connect switches if
	 * necessary. This is only necessary if the port power role is a
	 * sink. If the power role is a source then just keep the max14637
	 * powered on so that data switches are close. Note that the gpio enable
	 * for this chip is active by default. In order to trigger bc1.2
	 * detection, the chip enable must be driven low, then high again so the
	 * chip will start bc1.2 client side detection. Add a 100 msec delay to
	 * avoid collision with a device that might be doing bc1.2 client side
	 * detection.
	 */
	crec_msleep(100);
	activate_chip_enable(cfg, 0);
	crec_msleep(CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS);
	activate_chip_enable(cfg, 1);

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	/*
	 * Apple or TomTom charger detection can take as long as 600ms.  Wait a
	 * little bit longer for margin.
	 */
	crec_msleep(630);
#endif /* !defined(CONFIG_CHARGE_RAMP_SW && CONFIG_CHARGE_RAMP_HW) */
}

/**
 * If VBUS is present and port power role is sink, then trigger bc1.2 client
 * detection. If VBUS is not present then update charge manager. Note that both
 * chip_enable and VBUS must be active for the IC to be powered up. Chip enable
 * is kept enabled by default so that bc1.2 client detection is not triggered
 * when the port power role is source.
 *
 * @param port: Which USB Type-C port to examine.
 */
static void detect_or_power_down_ic(const int port)
{
	int vbus_present;

#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	vbus_present = tcpm_check_vbus_level(port, VBUS_PRESENT);
#else
	vbus_present = pd_snk_is_vbus_provided(port);
#endif /* !defined(CONFIG_USB_PD_VBUS_DETECT_TCPC) */

	if (vbus_present) {
#if defined(CONFIG_POWER_PP5000_CONTROL) && defined(CONFIG_AP_POWER_CONTROL)
		/* Turn on the 5V rail to allow the chip to be powered. */
		power_5v_enable(task_get_current(), 1);
#endif
		if (pd_get_power_role(port) == PD_ROLE_SINK) {
			bc12_detect(port);
			update_bc12_status_to_charger_manager(port);
		}
	} else {
		/* Let charge manager know there's no more charge available. */
		charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, port, NULL);
		/*
		 * If latest attached charger is PD Adapter then it would be
		 * detected as DCP and data switch of USB2.0 would be open which
		 * prevents USB 2.0 data path from working later. As a result,
		 * bc12_detect() is called again here and SCP would be detected
		 * due to D+/D- are NC (open) if nothing is attached then data
		 * switch of USB2.0 can be kept close from now on.
		 */
		bc12_detect(port);
#if defined(CONFIG_POWER_PP5000_CONTROL) && defined(CONFIG_AP_POWER_CONTROL)
		/* Issue a request to turn off the rail. */
		power_5v_enable(task_get_current(), 0);
#endif
	}
}

static void max14637_usb_charger_task_init(const int port)
{
	const struct max14637_config_t *const cfg = &max14637_config[port];

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/*
	 * Have chip enable active as default state so data switches are closed
	 * and bc1.2 client side detection is not activated when the port power
	 * role is a source.
	 */
	activate_chip_enable(cfg, 1);
	/* Check whether bc1.2 client mode detection needs to be triggered */
	detect_or_power_down_ic(port);
}

static void max14637_usb_charger_task_event(const int port, uint32_t evt)
{
	if (evt & USB_CHG_EVENT_VBUS)
		detect_or_power_down_ic(port);
}

#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
static int max14637_ramp_allowed(int supplier)
{
	/*
	 * Due to the limitations in the application of the MAX14637, we
	 * don't quite know exactly what we're plugged into.  Therefore,
	 * the supplier type will be CHARGE_SUPPLIER_OTHER.
	 */
	return supplier == CHARGE_SUPPLIER_OTHER;
}

static int max14637_ramp_max(int supplier, int sup_curr)
{
	/* Use the current limit that was decided by the MAX14637. */
	if (supplier == CHARGE_SUPPLIER_OTHER)
		return sup_curr;
	else
		return 500;
}
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */

/* Called on AP S5 -> S3  and S3/S0iX -> S0 transition */
static void bc12_chipset_startup(void)
{
	int port;

	/*
	 * For each port, trigger a new USB_CHG_EVENT_VBUS event to handle cases
	 * where there was no change in VBUS following an AP resume/startup
	 * event. If a legacy charger is connected to the port, then VBUS will
	 * not drop even during the USB PD hard reset.
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
		usb_charger_task_set_event(port, USB_CHG_EVENT_VBUS);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, bc12_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, bc12_chipset_startup, HOOK_PRIO_DEFAULT);

const struct bc12_drv max14637_drv = {
	.usb_charger_task_init = max14637_usb_charger_task_init,
	.usb_charger_task_event = max14637_usb_charger_task_event,
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	.ramp_allowed = max14637_ramp_allowed,
	.ramp_max = max14637_ramp_max,
#endif /* CONFIG_CHARGE_RAMP_SW || CONFIG_CHARGE_RAMP_HW */
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &max14637_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
