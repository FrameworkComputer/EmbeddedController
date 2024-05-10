/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "backlight.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/battery/max17055.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/fusb302.h"
#include "driver/usb_mux/it5205.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "it8801.h"
#include "keyboard_backlight.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = { "BOARD_ID", 3300, 4096, 0, STM32_AIN(5) },
	[ADC_EC_SKU_ID] = { "EC_SKU_ID", 3300, 4096, 0, STM32_AIN(15) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "typec",
	  .port = 0,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "other",
	  .port = 2,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct i2c_port_t i2c_bitbang_ports[] = {
	{ .name = "battery",
	  .port = 3,
	  .kbps = 100,
	  .scl = GPIO_I2C4_SCL,
	  .sda = GPIO_I2C4_SDA,
	  .drv = &bitbang_drv },
};
const unsigned int i2c_bitbang_ports_used = ARRAY_SIZE(i2c_bitbang_ports);

#define BC12_I2C_ADDR PI3USB9201_I2C_ADDR_3

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L" },
	{ GPIO_PMIC_EC_RESETB, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * TODO(b/133200075): Tune this once we have the final performance
	 * out of the driver and the i2c bus.
	 */
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 10 * MSEC,
	.min_post_scan_delay_us = 10 * MSEC,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

struct ioexpander_config_t ioex_config[CONFIG_IO_EXPANDER_PORT_COUNT] = {
	[0] = {
		.i2c_host_port = I2C_PORT_KB_DISCRETE,
		.i2c_addr_flags = IT8801_I2C_ADDR1,
		.drv = &it8801_ioexpander_drv,
	},
};

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_ACCEL_PORT, 2, GPIO_EC_SENSOR_SPI_NSS },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/******************************************************************************/
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = FUSB302_I2C_ADDR_FLAGS,
		},
		.drv = &fusb302_tcpm_drv,
	},
};

static void board_hpd_status(const struct usb_mux *me, mux_state_t mux_state,
			     bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	/*
	 * svdm_dp_attention() did most of the work, we only need to notify
	 * host here.
	 */
	host_set_single_event(EC_HOST_EVENT_USB_MUX);
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
				.hpd_update = &board_hpd_status,
			},
	},
};

/* Charger config.  Start i2c address at 1, update during runtime */
struct charger_config_t chg_chips[] = {
	{
		.i2c_port = 2,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

/* Board version depends on ADCs, so init i2c port after ADC */
static void charger_config_complete(void)
{
	chg_chips[0].i2c_port = board_get_charger_i2c();
}
DECLARE_HOOK(HOOK_INIT, charger_config_complete, HOOK_PRIO_INIT_ADC + 1);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	return status;
}

static int force_discharge;

int board_set_active_charge_port(int charge_port)
{
	CPRINTS("New chg p%d", charge_port);

	/* ignore all request when discharge mode is on */
	if (force_discharge && charge_port != CHARGE_PORT_NONE)
		return EC_SUCCESS;

	switch (charge_port) {
	case CHARGE_PORT_USB_C:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;
		break;
	default:
		/*
		 * To ensure the fuel gauge (max17055) is always powered
		 * even when battery is disconnected, keep VBAT rail on but
		 * set the charging current to minimum.
		 */
		charger_set_current(CHARGER_SOLO, 0);
		break;
	}

	return EC_SUCCESS;
}

int board_discharge_on_ac(int enable)
{
	int ret, port;

	if (enable) {
		port = CHARGE_PORT_NONE;
	} else {
		/* restore the charge port state */
		port = charge_manager_get_override();
		if (port == OVERRIDE_OFF)
			port = charge_manager_get_active_charge_port();
	}

	ret = charger_discharge_on_ac(enable);
	if (ret)
		return ret;

	force_discharge = enable;
	return board_set_active_charge_port(port);
}

int pd_snk_is_vbus_provided(int port)
{
	/* TODO(b:138352732): read IT8801 GPIO EN_USBC_CHARGE_L */
	return EC_ERROR_UNIMPLEMENTED;
}

void bc12_interrupt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

#ifndef VARIANT_KUKUI_NO_SENSORS
static void board_spi_enable(void)
{
	/*
	 * Pin mux spi peripheral away from emmc, since RO might have
	 * left them there.
	 */
	gpio_config_module(MODULE_SPI_FLASH, 0);
#ifdef CHIP_FAMILY_STM32L4
	/* Set I/O speed before AF configured */
	/* EMMC SPI SLAVE: PB13/14/15 */
	/* SENSORS SPI controller: PB10, PB12, PC2, PC3 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xFF300000;
	STM32_GPIO_OSPEEDR(GPIO_C) |= 0x000000F0;

	/* Enable clocks to SPI2 module. */
	STM32_RCC_APB1ENR1 |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 to clear state left over from the emmc slave. */
	STM32_RCC_APB1RSTR1 |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR1 &= ~STM32_RCC_PB1_SPI2;
#else
	/* Enable clocks to SPI2 module. */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 to clear state left over from the emmc slave. */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;
#endif
	/* Reinitialize spi peripheral. */
	spi_enable(&spi_devices[0], 1);

	/* Pin mux spi peripheral toward the sensor. */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_spi_enable,
	     MOTION_SENSE_HOOK_PRIO - 1);

static void board_spi_disable(void)
{
	/* Set pins to a state calming the sensor down. */
	gpio_set_flags(GPIO_EC_SENSOR_SPI_CK, GPIO_OUT_LOW);
	gpio_set_level(GPIO_EC_SENSOR_SPI_CK, 0);
	gpio_config_module(MODULE_SPI_CONTROLLER, 0);

	/* Disable spi peripheral and clocks. */
	spi_enable(&spi_devices[0], 0);
#ifdef CHIP_FAMILY_STM32L4
	STM32_RCC_APB1ENR1 &= ~STM32_RCC_PB1_SPI2;
#else
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_spi_disable,
	     MOTION_SENSE_HOOK_PRIO + 1);
#endif /* !VARIANT_KUKUI_NO_SENSORS */

static void board_init(void)
{
	/* If the reset cause is external, pulse PMIC force reset. */
	if (system_get_reset_flags() == EC_RESET_FLAG_RESET_PIN) {
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 0);
		crec_msleep(100);
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 1);
	}

	/* Enable TCPC alert interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

#ifndef VARIANT_KUKUI_NO_SENSORS
	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL_INT_ODL);

	/* For some reason we have to do this again in case of sysjump */
	board_spi_enable();
#endif /* !VARIANT_KUKUI_NO_SENSORS */

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* Enable BC12 interrupt */
	gpio_enable_interrupt(GPIO_BC12_EC_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifndef VARIANT_KUKUI_NO_SENSORS
/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Rotation matrixes */
static const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					      { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

/* sensor private data */
/* Lid accel private data */
static struct stprivate_data g_lis2dwl_data;
/* Base accel private data */
static struct bmi_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LIS2DWL,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lis2dw12_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_lis2dwl_data,
	 .port = I2C_PORT_SENSORS,
	 .i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2, /* g */
	 .min_frequency = LIS2DW12_ODR_MIN_VAL,
	 .max_frequency = LIS2DW12_ODR_MAX_VAL,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 12500 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
	 .name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = CONFIG_SPI_ACCEL_PORT,
	 .i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(CONFIG_SPI_ACCEL_PORT),
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},
	[BASE_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = CONFIG_SPI_ACCEL_PORT,
	 .i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(CONFIG_SPI_ACCEL_PORT),
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

const struct it8801_pwm_t it8801_pwm_channels[] = {
	[IT8801_PWM_CH_KBLIGHT] = { .index = 4 },
};

void board_kblight_init(void)
{
	kblight_register(&kblight_it8801);
}

bool board_has_kb_backlight(void)
{
	/* Default enable keyboard backlight */
	return true;
}
#endif /* !VARIANT_KUKUI_NO_SENSORS */

/* Battery functions */
#define SB_SMARTCHARGE 0x26
/* Quick charge enable bit */
#define SMART_QUICK_CHARGE 0x02
/* Quick charge support bit */
#define MODE_QUICK_CHARGE_SUPPORT 0x01

static void sb_quick_charge_mode(int enable)
{
	int val, rv;

	rv = sb_read(SB_SMARTCHARGE, &val);
	if (rv || !(val & MODE_QUICK_CHARGE_SUPPORT))
		return;

	if (enable)
		val |= SMART_QUICK_CHARGE;
	else
		val &= ~SMART_QUICK_CHARGE;

	sb_write(SB_SMARTCHARGE, val);
}

/* Called on AP S0iX -> S0 transition */
static void board_chipset_resume(void)
{
#ifndef VARIANT_KUKUI_NO_SENSORS
	if (board_has_kb_backlight())
		ioex_set_level(IOEX_KB_BL_EN, 1);
#endif

	/* Normal charge mode */
	sb_quick_charge_mode(0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S0iX transition */
static void board_chipset_suspend(void)
{
#ifndef VARIANT_KUKUI_NO_SENSORS
	if (board_has_kb_backlight())
		ioex_set_level(IOEX_KB_BL_EN, 0);
#endif

	/* Quick charge mode */
	sb_quick_charge_mode(1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	gpio_set_level(GPIO_EN_USBA_5V, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_EN_USBA_5V, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

int board_get_charger_i2c(void)
{
	/* TODO(b:138415463): confirm the bus allocation for future builds */
	return I2C_PORT_CHARGER;
}

int board_get_battery_i2c(void)
{
	return I2C_PORT_BATTERY;
}

#ifdef SECTION_IS_RW
static int it8801_get_target_channel(enum pwm_channel *channel, int type,
				     int index)
{
	switch (type) {
	case EC_PWM_TYPE_GENERIC:
		*channel = index;
		break;
	default:
		return -1;
	}

	return *channel >= 1;
}

static enum ec_status
host_command_pwm_set_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_set_duty *p = args->params;
	enum pwm_channel channel;
	uint16_t duty;

	if (it8801_get_target_channel(&channel, p->pwm_type, p->index))
		return EC_RES_INVALID_PARAM;

	duty = (uint32_t)p->duty * 255 / 65535;
	it8801_pwm_set_raw_duty(channel, duty);
	it8801_pwm_enable(channel, p->duty > 0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_SET_DUTY, host_command_pwm_set_duty,
		     EC_VER_MASK(0));

static enum ec_status
host_command_pwm_get_duty(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwm_get_duty *p = args->params;
	struct ec_response_pwm_get_duty *r = args->response;

	enum pwm_channel channel;

	if (it8801_get_target_channel(&channel, p->pwm_type, p->index))
		return EC_RES_INVALID_PARAM;

	r->duty = (uint32_t)it8801_pwm_get_raw_duty(channel) * 65535 / 255;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_DUTY, host_command_pwm_get_duty,
		     EC_VER_MASK(0));
#endif
