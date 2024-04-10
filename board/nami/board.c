/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Poppy board-specific configuration */

#include "adc.h"
#include "anx7447.h"
#include "battery.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/baro_bmp280.h"
#include "driver/charger/isl923x.h"
#include "driver/led/lm3509.h"
#include "driver/pmic_tps650x30.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "espi.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "isl923x.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_backlight.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
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
#include "temp_sensor/f75303.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define USB_PD_PORT_PS8751 0
#define USB_PD_PORT_ANX7447 1

uint16_t board_version;
uint8_t oem = PROJECT_NAMI;
uint32_t sku;
uint8_t model;

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 },   { 1, 0 },	{ 0, 6 },   { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 },   { 1, 3 },	{ -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 },   { 1, 5 },	{ 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 },   { 1, 2 },	{ 2, 3 },   { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
	{ -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

/* Set PD discharge whenever VBUS detection is high (i.e. below threshold). */
static void vbus_discharge_handler(void)
{
	pd_set_vbus_discharge(0, gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));
	pd_set_vbus_discharge(1, gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L));
}
DECLARE_DEFERRED(vbus_discharge_handler);

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
	hook_call_deferred(&vbus_discharge_handler_data, 0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
	hook_call_deferred(&vbus_discharge_handler_data, 0);
}

void usb0_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing (10x voltage divider). PPVAR_BOOSTIN_SENSE */
	[ADC_VBUS] = { "VBUS", NPCX_ADC_CH2, ADC_MAX_VOLT * 10,
		       ADC_READ_MAX + 1, 0 },
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = { "AMON_BMON", NPCX_ADC_CH1, ADC_MAX_VOLT * 1000 / 18,
			    ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

/* Default, Nami, Vayne */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3100,
	.rpm_start = 3100,
	.rpm_max = 6900,
};

/* Sona */
const struct fan_rpm fan_rpm_1 = {
	.rpm_min = 2700,
	.rpm_start = 2700,
	.rpm_max = 6000,
};

/* Pantheon */
const struct fan_rpm fan_rpm_2 = {
	.rpm_min = 2100,
	.rpm_start = 2300,
	.rpm_max = 5100,
};

/* Akali */
const struct fan_rpm fan_rpm_3 = {
	.rpm_min = 2700,
	.rpm_start = 2700,
	.rpm_max = 5500,
};

const struct fan_rpm fan_rpm_4 = {
	.rpm_min = 2400,
	.rpm_start = 2400,
	.rpm_max = 4500,
};

struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_2, TCKC_LFCLK, PWM_CH_FAN },
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "tcpc0",
	  .port = NPCX_I2C_PORT0_0,
	  .kbps = 400,
	  .scl = GPIO_I2C0_0_SCL,
	  .sda = GPIO_I2C0_0_SDA },
	{ .name = "tcpc1",
	  .port = NPCX_I2C_PORT0_1,
	  .kbps = 400,
	  .scl = GPIO_I2C0_1_SCL,
	  .sda = GPIO_I2C0_1_SDA },
	{ .name = "battery",
	  .port = NPCX_I2C_PORT1,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "charger",
	  .port = NPCX_I2C_PORT2,
	  .kbps = 100,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "pmic",
	  .port = NPCX_I2C_PORT2,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "accelgyro",
	  .port = NPCX_I2C_PORT3,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_PS8751] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_0,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	[USB_PD_PORT_ANX7447] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_1,
			.addr_flags = AN7447_TCPC3_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
};

static int ps8751_tune_mux(const struct usb_mux *me)
{
	/* 0x98 sets lower EQ of DP port (3.6db) */
	mux_write(me, PS8XXX_REG_MUX_DP_EQ_CONFIGURATION, 0x98);
	return EC_SUCCESS;
}

struct usb_mux usb_mux_ps8751 = {
	.usb_port = USB_PD_PORT_PS8751,
	.driver = &tcpci_tcpm_usb_mux_driver,
	.hpd_update = &ps8xxx_tcpc_update_hpd_status,
};

struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_PS8751] = {
		.mux = &usb_mux_ps8751,
	},
	[USB_PD_PORT_ANX7447] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_ANX7447,
			.driver = &anx7447_usb_mux_driver,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
		},
	}
};

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_0,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

void board_reset_pd_mcu(void)
{
	if (oem == PROJECT_AKALI && board_version < 0x0200) {
		if (anx7447_flash_erase(USB_PD_PORT_ANX7447))
			CPRINTS("Failed to erase OCM flash");
	}

	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST, 1);
	crec_msleep(1);
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST, 0);
	/* After TEST_R release, anx7447/3447 needs 2ms to finish eFuse
	 * loading. */
	crec_msleep(2);
}

void board_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	if (oem == PROJECT_SONA && model != MODEL_SYNDRA)
		usb_mux_ps8751.board_init = ps8751_tune_mux;

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (!gpio_get_level(GPIO_USB_C1_PD_RST))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

/*
 * F75303_Remote1 is near CPU, and F75303_Remote2 is near 5V power IC.
 */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
	{ "F75303_Local", TEMP_SENSOR_TYPE_BOARD, f75303_get_val,
	  F75303_IDX_LOCAL },
	{ "F75303_Remote1", TEMP_SENSOR_TYPE_CPU, f75303_get_val,
	  F75303_IDX_REMOTE1 },
	{ "F75303_Remote2", TEMP_SENSOR_TYPE_BOARD, f75303_get_val,
	  F75303_IDX_REMOTE2 },
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

/* Nami/Vayne Remote 1, 2 */
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(39),
	.temp_fan_max = C_TO_K(50),
};

/* Sona Remote 1 */
const static struct ec_thermal_config thermal_b1 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(82),
		[EC_TEMP_THRESH_HALT] = C_TO_K(89),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(72),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(38),
	.temp_fan_max = C_TO_K(58),
};

/* Sona Remote 2 */
const static struct ec_thermal_config thermal_b2 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(84),
		[EC_TEMP_THRESH_HALT] = C_TO_K(91),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(74),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(60),
};

/* Pantheon Remote 1 */
const static struct ec_thermal_config thermal_c1 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(66),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(56),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(38),
	.temp_fan_max = C_TO_K(61),
};

/* Pantheon Remote 2 */
const static struct ec_thermal_config thermal_c2 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(74),
		[EC_TEMP_THRESH_HALT] = C_TO_K(82),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(64),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(38),
	.temp_fan_max = C_TO_K(61),
};

/* Akali Local */
const static struct ec_thermal_config thermal_d0 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(79),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = C_TO_K(81),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(80),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = C_TO_K(82),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(70),
};

/* Akali Remote 1 */
const static struct ec_thermal_config thermal_d1 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(59),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

/* Akali Remote 2 */
const static struct ec_thermal_config thermal_d2 = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(59),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(60),
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

#define I2C_PMIC_READ(reg, data) \
	i2c_read8(I2C_PORT_PMIC, TPS650X30_I2C_ADDR1_FLAGS, (reg), (data))
#define I2C_PMIC_WRITE(reg, data) \
	i2c_write8(I2C_PORT_PMIC, TPS650X30_I2C_ADDR1_FLAGS, (reg), (data))

static void board_pmic_init(void)
{
	int err;
	int error_count = 0;
	static uint8_t pmic_initialized = 0;

	if (pmic_initialized)
		return;

	/* Read vendor ID */
	while (1) {
		int data;
		err = I2C_PMIC_READ(TPS650X30_REG_VENDORID, &data);
		if (!err && data == TPS650X30_VENDOR_ID)
			break;
		else if (error_count > 5)
			goto pmic_error;
		error_count++;
	}

	/*
	 * VCCIOCNT register setting
	 * [6] : CSDECAYEN
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_VCCIOCNT, 0x4A);
	if (err)
		goto pmic_error;

	/*
	 * VRMODECTRL:
	 * [4] : VCCIOLPM clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_VRMODECTRL, 0x2F);
	if (err)
		goto pmic_error;

	/*
	 * PGMASK1 : Exclude VCCIO from Power Good Tree
	 * [7] : MVCCIOPG clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PGMASK1, 0x80);
	if (err)
		goto pmic_error;

	/*
	 * PWFAULT_MASK1 Register settings
	 * [7] : 1b V4 Power Fault Masked
	 * [4] : 1b V7 Power Fault Masked
	 * [2] : 1b V9 Power Fault Masked
	 * [0] : 1b V13 Power Fault Masked
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PWFAULT_MASK1, 0x95);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 4 register configuration
	 * [7:6] : 00b Reserved
	 * [5:4] : 01b V3.3S discharge resistance (V6S), 100 Ohm
	 * [3:2] : 01b V18S discharge resistance (V8S), 100 Ohm
	 * [1:0] : 01b V100S discharge resistance (V11S), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT4, 0x15);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 3 register configuration
	 * [7:6] : 01b V1.8U_2.5U discharge resistance (V9), 100 Ohm
	 * [5:4] : 01b V1.2U discharge resistance (V10), 100 Ohm
	 * [3:2] : 01b V100A discharge resistance (V11), 100 Ohm
	 * [1:0] : 01b V085A discharge resistance (V12), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT3, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 2 register configuration
	 * [7:6] : 01b V5ADS3 discharge resistance (V5), 100 Ohm
	 * [5:4] : 01b V33A_DSW discharge resistance (V6), 100 Ohm
	 * [3:2] : 01b V33PCH discharge resistance (V7), 100 Ohm
	 * [1:0] : 01b V18A discharge resistance (V8), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT2, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 1 register configuration
	 * [7:2] : 00b Reserved
	 * [1:0] : 01b VCCIO discharge resistance (V4), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_DISCHCNT1, 0x01);
	if (err)
		goto pmic_error;

	/*
	 * Increase Voltage
	 *  [7:0] : 0x2a default
	 *  [5:4] : 10b default
	 *  [5:4] : 01b 5.1V (0x1a)
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_V5ADS3CNT, 0x1a);
	if (err)
		goto pmic_error;

	/*
	 * PBCONFIG Register configuration
	 *   [7] :      1b Power button debounce, 0ms (no debounce)
	 *   [6] :      0b Power button reset timer logic, no action (default)
	 * [5:0] : 011111b Force an Emergency reset time, 31s (default)
	 */
	err = I2C_PMIC_WRITE(TPS650X30_REG_PBCONFIG, 0x9F);
	if (err)
		goto pmic_error;

	CPRINTS("PMIC init done");
	pmic_initialized = 1;
	return;

pmic_error:
	CPRINTS("PMIC init failed: %d", err);
}

void chipset_pre_init_callback(void)
{
	board_pmic_init();
}

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACPRESENT, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Set active charge port -- only one port can be active at a time. */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
	/* check if we are sourcing VBUS on the port */
	/* dnojiri: revisit */
	int is_source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
							  GPIO_USB_C1_5V_EN);

	if (is_real_port && is_source) {
		CPRINTF("No charging on source port p%d is ", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		/* dnojiri: revisit. there is always this assumption that
		 * battery is present. If not, this may cause brownout. */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_L :
					     GPIO_USB_C1_CHARGE_L,
			       1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L,
			       0);
	}

	return EC_SUCCESS;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * Limit the input current to 96% negotiated limit,
	 * to account for the charger chip margin.
	 */
	int factor = 96;

	if (oem == PROJECT_AKALI &&
	    (model == MODEL_EKKO || model == MODEL_BARD))
		factor = 95;
	charge_ma = charge_ma * factor / 100;
	charge_set_input_current_limit(charge_ma, charge_mv);
}

void board_hibernate(void)
{
	CPRINTS("Triggering PMIC shutdown.");
	uart_flush_output();
	gpio_set_level(GPIO_EC_HIBERNATE, 1);
	while (1)
		;
}

const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED1] = { 3, PWM_CONFIG_DSLEEP, 1200 },
	[PWM_CH_LED2] = { 5, PWM_CONFIG_DSLEEP, 1200 },
	[PWM_CH_FAN] = { 4, PWM_CONFIG_OPEN_DRAIN, 25000 },
	/*
	 * 1.2kHz is a multiple of both 50 and 60. So a video recorder
	 * (generally designed to ignore either 50 or 60 Hz flicker) will not
	 * alias with refresh rate.
	 */
	[PWM_CH_KBLIGHT] = { 2, 0, 1200 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Lid accel private data */
static struct bmi_drv_data_t g_bmi160_data;
static struct kionix_accel_data g_kx022_data;

/* BMA255 private data */
static struct accelgyro_saved_data_t g_bma255_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
				       { FLOAT_TO_FP(1), 0, 0 },
				       { 0, 0, FLOAT_TO_FP(1) } };

const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t rotation_x180_z90 = { { 0, FLOAT_TO_FP(-1), 0 },
				       { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };

const struct motion_sensor_t lid_accel_1 = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_kx022_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	.rot_standard_ref = &rotation_x180_z90,
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.default_range = 2, /* g, to support lid angle calculation. */
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma255_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support lid angle calculation. */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 0,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Enable or disable input devices, based on chipset state and tablet mode */
__override void lid_angle_peripheral_enable(int enable)
{
	/* If the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard.
	 */
	if (tablet_get_mode() || chipset_in_state(CHIPSET_STATE_ANY_OFF))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 0);
	gpio_set_level(GPIO_USB3_POWER_DOWN_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 1);
	gpio_set_level(GPIO_USB3_POWER_DOWN_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

static void setup_motion_sensors(void)
{
	switch (oem) {
	case PROJECT_AKALI:
		if (sku & SKU_ID_MASK_CONVERTIBLE) {
			/* Rotate axis for Akali 360 */
			motion_sensors[LID_ACCEL] = lid_accel_1;
			motion_sensors[BASE_ACCEL].rot_standard_ref = NULL;
			motion_sensors[BASE_GYRO].rot_standard_ref = NULL;
		} else {
			/* Clamshell Akali has no accel/gyro */
			motion_sensor_count = ARRAY_SIZE(motion_sensors) - 2;
		}
		break;
	default:
		break;
	}
}

static void setup_fans(void)
{
	switch (oem) {
	case PROJECT_SONA:
		if (model == MODEL_SYNDRA)
			fans[FAN_CH_0].rpm = &fan_rpm_4;
		else
			fans[FAN_CH_0].rpm = &fan_rpm_1;
		thermal_params[TEMP_SENSOR_REMOTE1] = thermal_b1;
		thermal_params[TEMP_SENSOR_REMOTE2] = thermal_b2;
		break;
	case PROJECT_PANTHEON:
		fans[FAN_CH_0].rpm = &fan_rpm_2;
		thermal_params[TEMP_SENSOR_REMOTE1] = thermal_c1;
		thermal_params[TEMP_SENSOR_REMOTE2] = thermal_c2;
		break;
	case PROJECT_AKALI:
		fans[FAN_CH_0].rpm = &fan_rpm_3;
		thermal_params[TEMP_SENSOR_LOCAL] = thermal_d0;
		thermal_params[TEMP_SENSOR_REMOTE1] = thermal_d1;
		thermal_params[TEMP_SENSOR_REMOTE2] = thermal_d2;
		break;
	case PROJECT_NAMI:
	case PROJECT_VAYNE:
	default:
		thermal_params[TEMP_SENSOR_REMOTE1] = thermal_a;
		thermal_params[TEMP_SENSOR_REMOTE2] = thermal_a;
	}
}

/*
 * Read CBI from i2c eeprom and initialize variables for board variants
 */
static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS && val <= UINT16_MAX)
		board_version = val;
	CPRINTS("Board Version: 0x%04x", board_version);

	if (cbi_get_oem_id(&val) == EC_SUCCESS && val < PROJECT_COUNT)
		oem = val;
	CPRINTS("OEM: %d", oem);

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku = val;
	CPRINTS("SKU: 0x%08x", sku);

	if (cbi_get_model_id(&val) == EC_SUCCESS)
		model = val;
	CPRINTS("MODEL: 0x%08x", model);

	if (board_version < 0x300)
		/* Previous boards have GPIO42 connected to TP_INT_CONN */
		gpio_set_flags(GPIO_USB2_ID, GPIO_INPUT);

	setup_motion_sensors();

	setup_fans();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

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
		0xa4, 0xff, 0xfe, 0x55, 0xfe, 0xff, 0xff, 0xff,  /* full set */
	},
};

static void anx7447_set_aux_switch(void)
{
	const int port = USB_PD_PORT_ANX7447;

	/* Debounce */
	if (gpio_get_level(GPIO_CCD_MODE_ODL))
		return;

	CPRINTS("C%d: AUX_SW_SEL=0x%x", port, 0xc);
	if (tcpc_write(port, ANX7447_REG_TCPC_AUX_SWITCH, 0xc))
		CPRINTS("C%d: Setting AUX_SW_SEL failed", port);
}
DECLARE_DEFERRED(anx7447_set_aux_switch);

void ccd_mode_isr(enum gpio_signal signal)
{
	/* Wait 2 seconds until all mux setting is done by PD task */
	hook_call_deferred(&anx7447_set_aux_switch_data, 2 * SECOND);
}

static void board_init(void)
{
	int reg;

	/*
	 * This enables pull-down on F_DIO1 (SPI MISO), and F_DIO0 (SPI MOSI),
	 * whenever the EC is not doing SPI flash transactions. This avoids
	 * floating SPI buffer input (MISO), which causes power leakage (see
	 * b/64797021).
	 */
	NPCX_PUPD_EN1 |= BIT(NPCX_DEVPU1_F_SPI_PUD_EN);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACPRESENT, extpower_is_present());

	/* Reduce Buck-boost mode switching frequency to reduce heat */
	if (i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
		       ISL9238_REG_CONTROL3, &reg) == EC_SUCCESS) {
		reg |= ISL9238_C3_BB_SWITCHING_PERIOD;
		if (i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				ISL9238_REG_CONTROL3, reg))
			CPRINTF("Failed to set isl9238\n");
	}

	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Trigger once to set mux in case CCD cable is already connected. */
	ccd_mode_isr(GPIO_CCD_MODE_ODL);
	gpio_enable_interrupt(GPIO_CCD_MODE_ODL);

	/* Enable Accel/Gyro interrupt for convertibles. */
	if (sku & SKU_ID_MASK_CONVERTIBLE)
		gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);

#ifndef TEST_BUILD
	/* Disable scanning KSO13 & 14 if keypad isn't present. */
	if (!(sku & SKU_ID_MASK_KEYPAD)) {
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
		keyscan_config.actual_key_mask[11] = 0xfa;
		keyscan_config.actual_key_mask[12] = 0xca;
	}
	if (oem == PROJECT_AKALI && model == MODEL_BARD) {
		/* Search key is moved to col=0,row=3 */
		keyscan_config.actual_key_mask[0] = 0x1c;
		keyscan_config.actual_key_mask[1] = 0xfe;
		/* No need to swap scancode_set2[0][3] and [1][0] because both
		 * are mapped to search key. */
	}
	if (sku & SKU_ID_MASK_UK2) {
		/*
		 * Observed on Shyvana with UK keyboard,
		 *   \|:     0x0061->0x61->0x56
		 *   r-ctrl: 0xe014->0x14->0x1d
		 */
		uint16_t tmp = get_scancode_set2(4, 0);
		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(2, 7, tmp);
	}
#endif

	isl923x_set_ac_prochot(CHARGER_SOLO, 3328 /* mA */);

	switch (oem) {
	case PROJECT_VAYNE:
		isl923x_set_dc_prochot(CHARGER_SOLO, 11008 /* mA */);
		break;
	case PROJECT_PANTHEON:
		isl923x_set_dc_prochot(CHARGER_SOLO, 9984 /* mA */);
		break;
	case PROJECT_SONA:
		isl923x_set_dc_prochot(CHARGER_SOLO, 5888 /* mA */);
		break;
	case PROJECT_NAMI:
	case PROJECT_AKALI:
	/* default 4096mA 0x1000 */
	default:
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int board_is_lid_angle_tablet_mode(void)
{
	/* Boards with no GMR sensor use lid angles to detect tablet mode. */
	return oem != PROJECT_AKALI;
}

void board_kblight_init(void)
{
	if (!(sku & SKU_ID_MASK_KBLIGHT))
		return;

	switch (oem) {
	default:
	case PROJECT_NAMI:
	case PROJECT_AKALI:
	case PROJECT_VAYNE:
	case PROJECT_PANTHEON:
		kblight_register(&kblight_lm3509);
		break;
	case PROJECT_SONA:
		kblight_register(&kblight_pwm);
		break;
	}
}

enum critical_shutdown
board_critical_shutdown_check(struct charge_state_data *curr)
{
	if (oem == PROJECT_VAYNE)
		return CRITICAL_SHUTDOWN_CUTOFF;
	else
		return CRITICAL_SHUTDOWN_HIBERNATE;
}

uint8_t board_set_battery_level_shutdown(void)
{
	if (oem == PROJECT_VAYNE)
		/* We match the shutdown threshold with Powerd's.
		 * 4 + 1 = 5% because Powerd uses '<=' while EC uses '<'. */
		return CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE + 1;
	else
		return BATTERY_LEVEL_SHUTDOWN;
}
