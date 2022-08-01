/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Shotzo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/charger/sm5803.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
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

#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

uint32_t board_version;

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

__override void board_process_pd_alert(int port)
{
	/*
	 * PD_INT task will process this alert, and that task is only needed on
	 * C1.
	 */
	if (port != 1)
		return;

	if (gpio_get_level(GPIO_USB_C1_INT_ODL))
		return;

	sm5803_handle_interrupt(port);
}

/* C0 interrupt line triggered by charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	sm5803_interrupt(0);
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

/* C1 interrupt line shared by TCPC and charger */
static void check_c1_line(void);
DECLARE_DEFERRED(check_c1_line);

static void notify_c1_chips(void)
{
	schedule_deferred_pd_interrupt(1);
}

static void check_c1_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips.
	 */
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
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
	[ADC_SUB_ANALOG] = { .name = "SUB_ANALOG",
			     .factor_mul = ADC_MAX_MVOLT,
			     .factor_div = ADC_READ_MAX + 1,
			     .shift = 0,
			     .channel = CHIP_ADC_CH13 },
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH15 },
	[ADC_TEMP_SENSOR_4] = { .name = "TEMP_SENSOR4",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH16 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	[CHARGER_PRIMARY] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SM5803_ADDR_CHARGER_FLAGS,
		.drv = &sm5803_drv,
	},
	[CHARGER_SECONDARY] = {
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = SM5803_ADDR_CHARGER_FLAGS,
		.drv = &sm5803_drv,
	},
};

/* TCPCs */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		.drv = &it83xx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_SUB_USB_C1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

/* USB Muxes */
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.usb_port = 0,
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
		.driver = &it5205_usb_mux_driver,
	},
	{
		.usb_port = 1,
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
};

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_INT_ODL);

	/* Store board version for use in determining charge limits */
	cbi_get_board_version(&board_version);

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL))
		hook_call_deferred(&check_c0_line_data, 0);
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL))
		hook_call_deferred(&check_c1_line_data, 0);

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);

	/* Charger on the MB will be outputting PROCHOT_ODL and OD CHG_DET */
	sm5803_configure_gpio0(CHARGER_PRIMARY, GPIO0_MODE_PROCHOT, 1);
	sm5803_configure_chg_det_od(CHARGER_PRIMARY, 1);

	if (board_get_charger_chip_count() > 1) {
		/* Charger on the sub-board will be a push-pull GPIO */
		sm5803_configure_gpio0(CHARGER_SECONDARY, GPIO0_MODE_OUTPUT, 0);
	}

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
	sm5803_disable_low_power_mode(CHARGER_PRIMARY);
	if (board_get_charger_chip_count() > 1)
		sm5803_disable_low_power_mode(CHARGER_SECONDARY);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);

static void board_suspend(void)
{
	sm5803_enable_low_power_mode(CHARGER_PRIMARY);
	if (board_get_charger_chip_count() > 1)
		sm5803_enable_low_power_mode(CHARGER_SECONDARY);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_shutdown(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_shutdown, HOOK_PRIO_DEFAULT);

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
	usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
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

	if (board_get_charger_chip_count() > 1) {
		if (sm5803_set_gpio0_level(1, !!enable))
			CPRINTUSB("Failed to %sable sub rails!",
				  enable ? "en" : "dis");
	}
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * TCPC 0 is embedded in the EC and processes interrupts in the chip
	 * code (it83xx/intc.c)
	 */

	uint16_t status = 0;
	int regval;

	/* Check whether TCPC 1 pulled the shared interrupt line */
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	int icl = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);

	/* Limit C1 on board version 0 to 2.0 A */
	if ((board_version == 0) && (port == 1))
		icl = MIN(icl, 2000);
	/*
	 * TODO(b/151955431): Characterize the input current limit in case a
	 * scaling needs to be applied here
	 */
	charge_set_input_current_limit(icl, charge_mv);
}

int board_set_active_charge_port(int port)
{
	int is_valid_port = (port >= 0 && port < board_get_usb_pd_port_count());

	if (!is_valid_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	if (port == CHARGE_PORT_NONE) {
		CPRINTUSB("Disabling all charge ports");

		sm5803_vbus_sink_enable(CHARGER_PRIMARY, 0);

		if (board_get_charger_chip_count() > 1)
			sm5803_vbus_sink_enable(CHARGER_SECONDARY, 0);

		return EC_SUCCESS;
	}

	CPRINTUSB("New chg p%d", port);

	/*
	 * Ensure other port is turned off, then enable new charge port
	 */
	if (port == 0) {
		if (board_get_charger_chip_count() > 1)
			sm5803_vbus_sink_enable(CHARGER_SECONDARY, 0);
		sm5803_vbus_sink_enable(CHARGER_PRIMARY, 1);

	} else {
		sm5803_vbus_sink_enable(CHARGER_PRIMARY, 0);
		sm5803_vbus_sink_enable(CHARGER_SECONDARY, 1);
	}

	return EC_SUCCESS;
}

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/* Vconn control is only for port 0 */
	if (port)
		return;

	if (cc_pin == USBPD_CC_PIN_1)
		gpio_set_level(GPIO_EN_USB_C0_CC1_VCONN, !!enabled);
	else
		gpio_set_level(GPIO_EN_USB_C0_CC2_VCONN, !!enabled);
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int current;

	if (port < 0 || port > board_get_usb_pd_port_count())
		return;

	current = (rp == TYPEC_RP_3A0) ? 3000 : 1500;

	charger_set_otg_current_voltage(port, current, 5000);
}

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

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
	[TEMP_SENSOR_3] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
	[TEMP_SENSOR_4] = { .name = "5V regular",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_4 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
