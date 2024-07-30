/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Awasuki configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/raa489000.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprints(CC_SYSTEM, format, ##args)

#define INT_RECHECK_US 5000

/* C0 interrupt line */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	schedule_deferred_pd_interrupt(0);
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

/* for G2176RB1U issue */
static void backlight_interrupt(enum gpio_signal s)
{
	gpio_set_level(GPIO_EC_PPVAR_BLPWR, 0);
	crec_usleep(10 * MSEC);
	gpio_set_level(GPIO_EC_PPVAR_BLPWR, 1);
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
	[ADC_TEMP_SENSOR_4] = { .name = "TEMP_SENSOR4",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH13 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

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
	},
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

void board_hibernate(void)
{
	/*
	 * Put all charger ICs present into low power mode before entering
	 * z-state.
	 */
	raa489000_hibernate(0, true);
}

__override void board_pulse_entering_rw(void)
{
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	crec_usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
}

void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal, TCPC C1 reset pin is not
	 * connected to the EC.
	 */
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
	if (port != 0 && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	CPRINTS("New chg p%d", port);

	/* for als function */
	if (!gpio_get_level(GPIO_DOOR_OPEN_EC) && als_enable_status())
		return EC_SUCCESS;

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		tcpc_write(0, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_LOW);
		raa489000_enable_asgate(0, false);
		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(0, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		return EC_ERROR_UNKNOWN;
	}

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
	[TEMP_SENSOR_1] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Aux",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
	[TEMP_SENSOR_4] = { .name = "USB",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_4 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(83),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(87),
		[EC_TEMP_THRESH_HALT] = 0,
	},
};

const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(84),
		[EC_TEMP_THRESH_HALT] = C_TO_K(87),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(81),
		[EC_TEMP_THRESH_HALT] = 0,
	},
};

const static struct ec_thermal_config thermal_aux = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(88),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(82),
		[EC_TEMP_THRESH_HALT] = 0,
	},
};

const static struct ec_thermal_config thermal_usb = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(82),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(79),
		[EC_TEMP_THRESH_HALT] = 0,
	},
};
struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_thermal(void)
{
	thermal_params[TEMP_SENSOR_1] = thermal_ambient;
	thermal_params[TEMP_SENSOR_2] = thermal_charger;
	thermal_params[TEMP_SENSOR_3] = thermal_aux;
	thermal_params[TEMP_SENSOR_4] = thermal_usb;
}

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
	gpio_enable_interrupt(GPIO_VBL_PD_OD);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	/* Initialize THERMAL */
	setup_thermal();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
