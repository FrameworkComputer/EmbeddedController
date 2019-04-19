/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kohaku board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_bh1730.h"
#include "driver/ppc/sn5s330.h"
#include "driver/bc12/max14637.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "extpower.h"
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
#include "thermal.h"
#include "thermistor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

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
		task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
		break;

	case GPIO_USB_C1_BC12_INT_ODL:
		task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
		break;

	default:
		break;
	}
}

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT]   = { .channel = 3, .flags = 0, .freq = 10000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.flags = 0,
	},
	[USB_PD_PORT_TCPC_1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.flags = 0,
	},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
	[USB_PD_PORT_TCPC_1] = {
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

/* BC 1.2 chip Configuration */
const struct max14637_config_t max14637_config[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	{
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};

/******************************************************************************/
/* Sensors */
/* Base Sensor mutex */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

/* Base accel private data */
static struct bmi160_drv_data_t g_bmi160_data;

/* BMA255 private data */
static struct accelgyro_saved_data_t g_bma255_data;

/* BH1730 private data */
struct bh1730_drv_data_t g_bh1730_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

/*
 * TODO(b/124337208): P0 boards don't have this sensor mounted so the rotation
 * matrix can't be tested properly. This needs to be revisited after EVT to make
 * sure the rotaiton matrix for the lid sensor is correct.
 */
static const mat33_fp_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
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
		.addr = BMA2x2_I2C_ADDR1,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support tablet mode */
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
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI160_ACCEL_MIN_FREQ,
		.max_frequency = BMI160_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support tablet mode  */
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
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
		.addr = BMI160_ADDR0,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI160_GYRO_MIN_FREQ,
		.max_frequency = BMI160_GYRO_MAX_FREQ,
	},

	[LID_ALS] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BH1730,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bh1730_drv,
		.drv_data = &g_bh1730_data,
		.port = I2C_PORT_ACCEL,
		.addr = BH1730_I2C_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 65535,
		.min_frequency = 10,
		.max_frequency = 10,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 100000,
				.ec_rate = 0,
			},
		},
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

/**********************************************************************/
/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		"TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	[ADC_TEMP_SENSOR_2] = {
		"TEMP_CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = {.name = "Temp1",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_51k1_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_1,
				 .action_delay_sec = 1},
	[TEMP_SENSOR_2] = {.name = "Temp2",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_51k1_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_2,
				 .action_delay_sec = 1},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

enum gpio_signal gpio_en_pp5000_a = GPIO_EN_PP5000_A;

static void board_init(void)
{
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Sanity check the port. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC_ODL, !is_overcurrented);
}
