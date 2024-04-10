/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* reef_it8320 board-specific configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/charger/bd9995x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/tcpm.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define IN_ALL_SYS_PG POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_PGOOD_PP3300 POWER_SIGNAL_MASK(X86_PGOOD_PP3300)
#define IN_PGOOD_PP5000 POWER_SIGNAL_MASK(X86_PGOOD_PP5000)

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{ "CHARGER", 3000, 1024, 0, CHIP_ADC_CH1 }, /* GPI1 */
	{ "AMBIENT", 3000, 1024, 0, CHIP_ADC_CH2 }, /* GPI2 */
	{ "BRD_ID", 3000, 1024, 0, CHIP_ADC_CH3 }, /* GPI3 */
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct i2c_port_t i2c_ports[] = {
	{ .name = "mux",
	  .port = IT83XX_I2C_CH_C,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_C_SCL,
	  .sda = GPIO_EC_I2C_C_SDA },
	{ .name = "batt",
	  .port = IT83XX_I2C_CH_E,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C_E_SCL,
	  .sda = GPIO_EC_I2C_E_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{ .bus_type = EC_BUS_TYPE_EMBEDDED, .drv = &it83xx_tcpm_drv },
	{ .bus_type = EC_BUS_TYPE_EMBEDDED, .drv = &it83xx_tcpm_drv },
};

void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	int cc1_enabled = 0, cc2_enabled = 0;

	if (cc_pin != USBPD_CC_PIN_1)
		cc2_enabled = enabled;
	else
		cc1_enabled = enabled;

	if (port) {
		gpio_set_level(GPIO_USB_C1_CC2_VCONN_EN, cc2_enabled);
		gpio_set_level(GPIO_USB_C1_CC1_VCONN_EN, cc1_enabled);
	} else {
		gpio_set_level(GPIO_USB_C0_CC2_VCONN_EN, !cc2_enabled);
		gpio_set_level(GPIO_USB_C0_CC1_VCONN_EN, !cc1_enabled);
	}
}

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

static void it83xx_tcpc_update_hpd_status(const struct usb_mux *me,
					  mux_state_t mux_state,
					  bool *ack_required)
{
	int hpd_lvl = (mux_state & USB_PD_MUX_HPD_LVL) ? 1 : 0;
	int hpd_irq = (mux_state & USB_PD_MUX_HPD_IRQ) ? 1 : 0;
	enum gpio_signal gpio = me->usb_port ? GPIO_USB_C1_HPD_1P8_ODL :
					       GPIO_USB_C0_HPD_1P8_ODL;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	hpd_lvl = !hpd_lvl;

	gpio_set_level(gpio, hpd_lvl);
	if (hpd_irq) {
		gpio_set_level(gpio, 1);
		crec_msleep(1);
		gpio_set_level(gpio, hpd_lvl);
	}
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
				.hpd_update = &it83xx_tcpc_update_hpd_status,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = 0x10,
				.driver = &ps8740_usb_mux_driver,
				.hpd_update = &it83xx_tcpc_update_hpd_status,
			},
	},
};

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
};

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = { .name = "Battery",
				  .type = TEMP_SENSOR_TYPE_BATTERY,
				  .read = charge_get_battery_temp,
				  .idx = 0 },
	[TEMP_SENSOR_AMBIENT] = { .name = "Ambient",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_51k1_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_AMB },
	[TEMP_SENSOR_CHARGER] = { .name = "Charger",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_13k7_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = BD9995X_ADDR_FLAGS,
		.drv = &bd9995x_drv,
	},
};

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
	/*
	 * No need to re-init PMIC since settings are sticky across sysjump.
	 * However, be sure to check that PMIC is already enabled. If it is
	 * then there's no need to re-sequence the PMIC.
	 */
	if (system_jumped_to_this_image() && gpio_get_level(GPIO_PMIC_EN))
		return;

	/* Enable PP5000 before PP3300 due to NFC: chrome-os-partner:50807 */
	gpio_set_level(GPIO_EN_PP5000, 1);
	while (!gpio_get_level(GPIO_PP5000_PG))
		;

	/*
	 * To prevent SLP glitches, PMIC_EN (V5A_EN) should be enabled
	 * at the same time as PP3300 (chrome-os-partner:51323).
	 */
	/* Enable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}

static void board_set_tablet_mode(void)
{
	/*
	 * Always report device isn't in tablet mode because
	 * our id is clamshell and no TABLET_MODE_L pin
	 */
	tablet_set_mode(0, TABLET_TRIGGER_LID);
}

/* Initialize board. */
static void board_init(void)
{
	board_set_tablet_mode();
	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_L);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_INIT_I2C + 1);

int pd_snk_is_vbus_provided(int port)
{
	if (port != 0 && port != 1)
		panic("Invalid charge port\n");

	return bd9995x_is_vbus_provided(port);
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	enum bd9995x_charge_port bd9995x_port = 0;
	int bd9995x_port_select = 1;

	switch (charge_port) {
	case 0:
	case 1:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		bd9995x_port = charge_port;
		break;
	case CHARGE_PORT_NONE:
		bd9995x_port_select = 0;
		bd9995x_port = BD9995X_CHARGE_PORT_BOTH;

		/*
		 * To avoid inrush current from the external charger, enable
		 * discharge on AC till the new charger is detected and
		 * charge detect delay has passed.
		 */
		if (charge_get_percent() > 2)
			charger_discharge_on_ac(1);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	CPRINTS("New chg p%d", charge_port);

	return bd9995x_select_input_port(bd9995x_port, bd9995x_port_select);
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/* Enable charging trigger by BC1.2 detection */
	int bc12_enable = (supplier == CHARGE_SUPPLIER_BC12_CDP ||
			   supplier == CHARGE_SUPPLIER_BC12_DCP ||
			   supplier == CHARGE_SUPPLIER_BC12_SDP ||
			   supplier == CHARGE_SUPPLIER_OTHER);

	if (bd9995x_bc12_enable_charging(port, bc12_enable))
		return;

	charge_set_input_current_limit(charge_ma, charge_mv);
}

/**
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	return voltage < BD9995X_BC12_MIN_VOLTAGE;
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	/* Enable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 1);

	/* Enable Trackpad */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 0);

	/* Disable Trackpad */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 1);

	/* FIXME(dhendrix): Drive USB_PD_RST_ODL low to prevent
	 * leakage? (see comment in schematic)
	 */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* FIXME(dhendrix): Add CHIPSET_RESUME and CHIPSET_SUSPEND
 * hooks to enable/disable sensors?
 */
/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * FIXME(dhendrix): Weak symbol hack until we can get a better solution for
 * both Amenia and Reef.
 */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/*Disable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP3300_PG))
		;

	/*Disable 5V rail */
	gpio_set_level(GPIO_EN_PP5000, 0);
	while (gpio_get_level(GPIO_PP5000_PG))
		;
}

void board_reset_pd_mcu(void)
{
	/* do nothing */
}

void board_hibernate_late(void)
{
	int i;
	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs in hibernate */
		{ GPIO_BAT_LED_BLUE, GPIO_INPUT | GPIO_PULL_UP },
		{ GPIO_BAT_LED_AMBER, GPIO_INPUT | GPIO_PULL_UP },
		{ GPIO_LID_OPEN, GPIO_INT_RISING | GPIO_PULL_DOWN },

		/*
		 * BD99956 handles charge input automatically. We'll disable
		 * charge output in hibernate. Charger will assert ACOK_OD
		 * when VBUS or VCC are plugged in.
		 */
		{ GPIO_USB_C0_5V_EN, GPIO_INPUT | GPIO_PULL_DOWN },
		{ GPIO_USB_C1_5V_EN, GPIO_INPUT | GPIO_PULL_DOWN },
	};

	/* Change GPIOs' state in hibernate for better power consumption */
	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);
}

void board_hibernate(void)
{
	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_do_shutdown();

	/* Added delay to allow AP to settle down */
	crec_msleep(100);

	/* Enable both the VBUS & VCC ports before entering PG3 */
	bd9995x_select_input_port(BD9995X_CHARGE_PORT_BOTH, 1);

	/* Turn BGATE OFF for saving the power */
	bd9995x_set_power_save_mode(BD9995X_PWR_SAVE_MAX);
}

struct {
	enum reef_it8320_board_version version;
	int thresh_mv;
} const reef_it8320_board_versions[] = {
	/* Vin = 3.3V, R1 = 46.4K, R2 values listed below */
	{ BOARD_VERSION_1, 328 * 1.03 }, /* 5.11 Kohm */
	{ BOARD_VERSION_2, 670 * 1.03 }, /* 11.8 Kohm */
	{ BOARD_VERSION_3, 1012 * 1.03 }, /* 20.5 Kohm */
	{ BOARD_VERSION_4, 1357 * 1.03 }, /* 32.4 Kohm */
	{ BOARD_VERSION_5, 1690 * 1.03 }, /* 48.7 Kohm */
	{ BOARD_VERSION_6, 2020 * 1.03 }, /* 73.2 Kohm */
	{ BOARD_VERSION_7, 2352 * 1.03 }, /* 115 Kohm */
	{ BOARD_VERSION_8, 2802 * 1.03 }, /* 261 Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(reef_it8320_board_versions) == BOARD_VERSION_COUNT);

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv, i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	/* FIXME(dhendrix): enable ADC */
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_ODR_HIGH);
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 0);
	/* Wait to allow cap charge */
	crec_msleep(1);
	mv = adc_read_channel(ADC_BOARD_ID);
	/* FIXME(dhendrix): disable ADC */
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 1);
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_INPUT);

	if (mv == ADC_READ_ERROR) {
		version = BOARD_VERSION_UNKNOWN;
		return version;
	}

	for (i = 0; i < BOARD_VERSION_COUNT; i++) {
		if (mv < reef_it8320_board_versions[i].thresh_mv) {
			version = reef_it8320_board_versions[i].version;
			break;
		}
	}

	CPRINTS("Board version: %d", version);
	return version;
}

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
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
