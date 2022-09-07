/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skyrim family-specific USB-C configuration */

#include <zephyr/drivers/gpio.h>

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "battery_fuel_gauge.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state_v2.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl9241.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/anx7483_public.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp6.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "ioexpander.h"
#include "power.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* USB-A ports */
enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_A1, USBA_PORT_COUNT };

/* USB-C ports */
enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C1, USBC_PORT_COUNT };
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

static void reset_nct38xx_port(int port);

static void usbc_interrupt_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_ppc));

	/* Enable TCPC interrupts. */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_tcpc));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_tcpc));

	/* Enable BC 1.2 interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_bc12));

	/* Enable SBU fault interrupts */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_sbu_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c1_sbu_fault));
}
DECLARE_HOOK(HOOK_INIT, usbc_interrupt_init, HOOK_PRIO_POST_I2C);

static void usb_fault_interrupt_init(void)
{
	/* Enable USB fault interrupts when we hit S5 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usb_fault_interrupt_init, HOOK_PRIO_DEFAULT);

static void usb_fault_interrupt_disable(void)
{
	/* Disable USB fault interrupts leaving S5 */
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_hub_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a0_fault));
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_a1_fault));
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_fault_interrupt_disable,
	     HOOK_PRIO_DEFAULT);

/*
 * USB C0 (general) and C1 (just ANX DB) use IOEX pins to
 * indicate flipped polarity to a protection switch.
 */
static int ioex_set_flip(int port, mux_state_t mux_state)
{
	if (port == USBC_PORT_C0) {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c0_sbu_flip),
				0);
	} else {
		if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				1);
		else
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ioex_usb_c1_sbu_flip),
				0);
	}

	return EC_SUCCESS;
}

int baseboard_anx7483_c0_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	return anx7483_set_default_tuning(me, mux_state);
}

int baseboard_anx7483_c1_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	bool flipped = mux_state & USB_PD_MUX_POLARITY_INVERTED;

	/* Set the SBU polarity mux */
	RETURN_ERROR(ioex_set_flip(me->usb_port, mux_state));

	/* Remove flipped from the state for easier compraisons */
	mux_state = mux_state & ~USB_PD_MUX_POLARITY_INVERTED;

	RETURN_ERROR(anx7483_set_default_tuning(me, mux_state));

	if (mux_state == USB_PD_MUX_USB_ENABLED) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
					    ANX7483_EQ_SETTING_12_5DB));
	} else if (mux_state == USB_PD_MUX_DP_ENABLED) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
					    ANX7483_EQ_SETTING_12_5DB));
	} else if (mux_state == USB_PD_MUX_DOCK && !flipped) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX2,
					    ANX7483_EQ_SETTING_12_5DB));
	} else if (mux_state == USB_PD_MUX_DOCK && flipped) {
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_URX2,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_UTX1,
					    ANX7483_EQ_SETTING_12_5DB));
		RETURN_ERROR(anx7483_set_eq(me, ANX7483_PIN_DRX2,
					    ANX7483_EQ_SETTING_12_5DB));
	}

	return EC_SUCCESS;
}

struct usb_mux_chain usbc0_anx7483 = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_TCPC0,
			.i2c_addr_flags = ANX7483_I2C_ADDR0_FLAGS,
			.driver = &anx7483_usb_retimer_driver,
			.board_set = &baseboard_anx7483_c0_mux_set,
		},
};

__overridable int board_c1_ps8818_mux_set(const struct usb_mux *me,
					  mux_state_t mux_state)
{
	CPRINTSUSB("C1: PS8818 mux using default tuning");

	/* Once a DP connection is established, we need to set IN_HPD */
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return 0;
}

struct usb_mux_chain usbc1_ps8818 = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_TCPC1,
			.flags = USB_MUX_FLAG_RESETS_IN_G3,
			.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
			.driver = &ps8818_usb_retimer_driver,
			.board_set = &board_c1_ps8818_mux_set,
		},
};

struct usb_mux_chain usbc1_anx7483 = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_TCPC1,
			.i2c_addr_flags = ANX7483_I2C_ADDR0_FLAGS,
			.driver = &anx7483_usb_retimer_driver,
			.board_set = &baseboard_anx7483_c1_mux_set,
		},
};

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_MUX,
			.i2c_addr_flags = AMD_FP6_C0_MUX_I2C_ADDR,
			.driver = &amd_fp6_usb_mux_driver,
		},
		.next = &usbc0_anx7483,
	},
	[USBC_PORT_C1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C1,
			.i2c_port = I2C_PORT_USB_MUX,
			.i2c_addr_flags = AMD_FP6_C4_MUX_I2C_ADDR,
			.driver = &amd_fp6_usb_mux_driver,
		},
		/* .next = filled in by setup_mux based on fw_config */
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == CONFIG_USB_PD_PORT_MAX_COUNT);

static void setup_mux(void)
{
	uint32_t val;

	if (cros_cbi_get_fw_config(FW_IO_DB, &val) != 0)
		CPRINTSUSB("Error finding FW_DB_IO in CBI FW_CONFIG");
	/* Val will have our dts default on error, so continue setup */

	if (val == FW_IO_DB_PS8811_PS8818) {
		CPRINTSUSB("C1: Setting PS8818 mux");
		usb_muxes[USBC_PORT_C1].next = &usbc1_ps8818;
	} else if (val == FW_IO_DB_NONE_ANX7483) {
		CPRINTSUSB("C1: Setting ANX7483 mux");
		usb_muxes[USBC_PORT_C1].next = &usbc1_anx7483;
	} else {
		CPRINTSUSB("Unexpected DB_IO board: %d", val);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);

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

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
int board_aoz1380_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv = EC_SUCCESS;

	rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

void sbu_fault_interrupt(enum gpio_signal signal)
{
	int port = signal == IOEX_USB_C1_FAULT_ODL ? 1 : 0;

	CPRINTSUSB("C%d: SBU fault", port);
	pd_handle_overcurrent(port);
}

void usb_fault_interrupt(enum gpio_signal signal)
{
	int out;

	CPRINTSUSB("USB fault(%d), alerting the SoC", signal);
	out = gpio_pin_get_dt(
		      GPIO_DT_FROM_NODELABEL(gpio_usb_hub_fault_q_odl)) &&
	      gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ioex_usb_a0_fault_odl)) &&
	      gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ioex_usb_a1_fault_db_odl));

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_fault_odl), out);
}

void usb_pd_soc_interrupt(enum gpio_signal signal)
{
	/*
	 * This interrupt is unexpected with our use of the SoC mux, so just log
	 * it as a point of interest.
	 */
	CPRINTSUSB("SOC PD Interrupt");
}

/* Round up 3250 max current to multiple of 128mA for ISL9241 AC prochot. */
#define SKYRIM_AC_PROCHOT_CURRENT_MA 3328
static void set_ac_prochot(void)
{
	isl9241_set_ac_prochot(CHARGER_SOLO, SKYRIM_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, set_ac_prochot, HOOK_PRIO_DEFAULT);

void tcpc_alert_event(enum gpio_signal signal)
{
	int port;

	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

static void reset_nct38xx_port(int port)
{
	const struct gpio_dt_spec *reset_gpio_l;
	const struct device *ioex_port0, *ioex_port1;

	/* TODO(b/225189538): Save and restore ioex signals */
	if (port == USBC_PORT_C0) {
		reset_gpio_l = GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_rst_l);
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c0_port1));
	} else if (port == USBC_PORT_C1) {
		reset_gpio_l = GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_rst_l);
		ioex_port0 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port0));
		ioex_port1 = DEVICE_DT_GET(DT_NODELABEL(ioex_c1_port1));
	} else {
		/* Invalid port: do nothing */
		return;
	}

	gpio_pin_set_dt(reset_gpio_l, 0);
	msleep(NCT38XX_RESET_HOLD_DELAY_MS);
	gpio_pin_set_dt(reset_gpio_l, 1);
	nct38xx_reset_notify(port);
	if (NCT3807_RESET_POST_DELAY_MS != 0)
		msleep(NCT3807_RESET_POST_DELAY_MS);

	/* Re-enable the IO expander pins */
	gpio_reset_port(ioex_port0);
	gpio_reset_port(ioex_port1);
}

void board_reset_pd_mcu(void)
{
	/* Reset TCPC0 */
	reset_nct38xx_port(USBC_PORT_C0);

	/* Reset TCPC1 */
	reset_nct38xx_port(USBC_PORT_C1);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_usb_c0_tcpc_int_odl))) {
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			    gpio_usb_c0_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_pin_get_dt(
		    GPIO_DT_FROM_NODELABEL(gpio_usb_c1_tcpc_int_odl))) {
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
			    gpio_usb_c1_tcpc_rst_l)) != 0)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

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
#define BC12_MIN_VOLTAGE 4500
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

	if (voltage < BC12_MIN_VOLTAGE)
		CPRINTSUSB("%s vbus=%d", __func__, voltage);

	return voltage < BC12_MIN_VOLTAGE;
}

#define SAFE_RESET_VBUS_DELAY_MS 900
#define SAFE_RESET_VBUS_MV 5000
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
		pd_request_source_voltage(port, SAFE_RESET_VBUS_MV);

		/* Give PD task and PPC chip time to get to 5V */
		msleep(SAFE_RESET_VBUS_DELAY_MS);
	}

	/* Try to put our battery fuel gauge into sleep mode */
	ret = battery_sleep_fuel_gauge();
	if ((ret != EC_SUCCESS) && (ret != EC_ERROR_UNIMPLEMENTED))
		cprints(CC_SYSTEM, "Failed to send battery sleep command");
}
