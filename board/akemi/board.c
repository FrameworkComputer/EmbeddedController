/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch board-specific configuration */

#include "adc.h"
#include "battery_smart.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/g753.h"
#include "ec_commands.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		sn5s330_interrupt(0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		sn5s330_interrupt(1);
		break;

	default:
		break;
	}
}

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

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

static void bc12_interrupt(enum gpio_signal signal)
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

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 10000 },
	[PWM_CH_FAN] = { .channel = 5,
			 .flags = PWM_CONFIG_OPEN_DRAIN,
			 .freq = 25000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		.flags = TCPC_FLAGS_RESET_ACTIVE_HIGH,
	},
	[USB_PD_PORT_TCPC_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
			.driver = &anx7447_usb_mux_driver,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
		},
	},
	[USB_PD_PORT_TCPC_1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_1,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	}
};

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},

	[USB_PD_PORT_TCPC_1] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/******************************************************************************/
/* Sensors */
/* Base Sensor mutex */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

/* Lid accel private data */
static struct stprivate_data g_lis2dwl_data;
/* Base accel private data */
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(1), 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
					     { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

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
		.port = I2C_PORT_ACCEL,
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

	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
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
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

/* Default */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3100,
	.rpm_start = 3100,
	.rpm_max = 6900,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN },
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = { "TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_2] = { "TEMP_CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Temp1",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Temp2",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Temp3",
			    .type = TEMP_SENSOR_TYPE_CPU,
			    .read = g753_get_val,
			    .idx = 0 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Hatch Temperature sensors */
/*
 * TODO(b/124316213): These setting need to be reviewed and set appropriately
 * for Hatch. They matter when the EC is controlling the fan as opposed to DPTF
 * control.
 */
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
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(50),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_1] = thermal_a;
	thermal_params[TEMP_SENSOR_2] = thermal_a;
}

static void board_init(void)
{
	/* Initialize Fans */
	setup_fans();
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC_ODL, !is_overcurrented);
}

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	uint8_t sku = get_board_sku();
	/*
	 * Check if the current sku id does not support keyboard backlight
	 * and return the feature flag without EC_FEATURE_PWM_KEYB
	 * sku_id = 1/2 - without keyboard backlight
	 * sku_id = 3/4 - with keyboard backlight
	 */
	if (sku == 1 || sku == 2)
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
	else
		return flags0;
}

/* Battery functions */
#define SB_OPTIONALMFG_FUNCTION2 0x26
#define QUICK_CHARGE_SUPPORT 0x01
#define QUICK_CHARGE_ENABLE 0x02

#define SB_QUICK_CHARGE_ENABLE 1
#define SB_QUICK_CHARGE_DISABLE 0

static void sb_quick_charge_mode(int enable)
{
	int val, rv;

	rv = sb_read(SB_OPTIONALMFG_FUNCTION2, &val);
	if (rv)
		return;

	if (val & QUICK_CHARGE_SUPPORT) {
		if (enable)
			val |= QUICK_CHARGE_ENABLE;
		else
			val &= ~QUICK_CHARGE_ENABLE;

		sb_write(SB_OPTIONALMFG_FUNCTION2, val);
	}
}

/* Called on AP S5 -> S0 transition */
static void board_chipset_startup(void)
{
	/* Normal charge current */
	sb_quick_charge_mode(SB_QUICK_CHARGE_DISABLE);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup,
	     HOOK_PRIO_INIT_I2C + 1);

/* Called on AP S0 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Quick charge current */
	sb_quick_charge_mode(SB_QUICK_CHARGE_ENABLE);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

bool board_is_convertible(void)
{
	const uint8_t sku = get_board_sku();

	return (sku == 255) || (sku == 1) || (sku == 2) || (sku == 3) ||
	       (sku == 4);
}
