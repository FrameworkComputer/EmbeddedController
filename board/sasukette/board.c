/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Sasukette configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/tusb544.h"
#include "driver/tcpm/raa489000.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	schedule_deferred_pd_interrupt(0);
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

static void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

static void c0_ccsbu_ovp_interrupt(enum gpio_signal s)
{
	cprints(CC_USBPD, "C0: CC OVP, SBU OVP, or thermal event");
	pd_handle_cc_overvoltage(0);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH15 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* BC 1.2 chips */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = { {
	.i2c_port = I2C_PORT_USB_C0,
	.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	.flags = PI3USB9201_ALWAYS_POWERED,
} };

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

/* Charger chips */
const struct charger_config_t chg_chips[] = { {
	.i2c_port = I2C_PORT_USB_C0,
	.i2c_addr_flags = ISL923X_ADDR_FLAGS,
	.drv = &isl923x_drv,
} };

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	}
};

/* USB Muxes */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = { {
	.mux =
		&(const struct usb_mux){
			.usb_port = 0,
			.i2c_port = I2C_PORT_USB_C0,
			.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
			.driver = &it5205_usb_mux_driver,
		},
} };

static uint32_t board_id;

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL))
		hook_call_deferred(&check_c0_line_data, 0);

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	/* modify AC DC prochot value */
	isl923x_set_ac_prochot(CHARGER_SOLO, 4096);
	isl923x_set_dc_prochot(CHARGER_SOLO, 6000);

	cbi_get_board_version(&board_id);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Put all charger ICs present into low power mode before entering
	 * z-state.
	 *
	 * b:186335659: In order to solve the power consumption problem of
	 * hibernate，HW solution is adopted after board id 3 to solve the
	 * problem that AC cannot wake up hibernate mode.
	 */
	if (board_id > 2)
		raa489000_hibernate(0, true);
	else
		raa489000_hibernate(0, false);
}

__override void board_pulse_entering_rw(void)
{
	/*
	 * On the ITE variants, the EC_ENTERING_RW signal was connected to a pin
	 * which is active high by default.  This causes Cr50 to think that the
	 * EC has jumped to its RW image even though this may not be the case.
	 * The pin is changed to GPIO_EC_ENTERING_RW2.
	 */
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 1);
	crec_usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
}

void board_reset_pd_mcu(void)
{
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Motherboard has a GPIO to turn on the 5V regulator, but the sub-board
	 * sets it through the charger GPIO.
	 */
	gpio_set_level(GPIO_EN_PP5000, !!enable);
	gpio_set_level(GPIO_EN_USB_A0_VBUS, !!enable);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2 detector IC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_0;
		}
	}

	return status;
}

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		if (tcpc_write(i, TCPC_REG_COMMAND,
			       TCPC_REG_COMMAND_SNK_CTRL_LOW))
			CPRINTS("p%d: sink path disable failed.", i);
		raa489000_enable_asgate(i, false);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (port < 0 || port > board_get_usb_pd_port_count())
		return;
	raa489000_set_output_current(port, rp);
}

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Cpu",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
