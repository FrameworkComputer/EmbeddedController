/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "atomic.h"
#include "battery.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_si114x.h"
#include "ec_version.h"
#include "gesture.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "lightbar.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_descriptor.h"
#include "usb_pd.h"
#include "usb_spi.h"
#include "usb-stm32f3.h"
#include "usb-stream.h"
#include "usart-stm32f3.h"
#include "usart_tx_dma.h"
#include "util.h"
#include "pi3usb9281.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* VBUS too low threshold */
#define VBUS_LOW_THRESHOLD_MV 4600

/* Input current error margin */
#define IADP_ERROR_MARGIN_MA 100

static int charge_current_limit;

/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to atomic_ functions which use assembly to access them.
 */
static struct ec_response_host_event_status host_event_status __aligned(4);

void vbus_evt(enum gpio_signal signal)
{
	usb_charger_vbus_change(0, gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void usb_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

#include "gpio_list.h"

const void *const usb_strings[] = {
	[USB_STR_DESC]           = usb_string_desc,
	[USB_STR_VENDOR]         = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]        = USB_STRING_DESC("Ryu debug"),
	[USB_STR_VERSION]        = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME]   = USB_STRING_DESC("EC_PD"),
	[USB_STR_AP_STREAM_NAME] = USB_STRING_DESC("AP"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/*
 * Define AP console forwarding queue and associated USART and USB
 * stream endpoints.
 */
static struct usart_config const ap_usart;

struct usb_stream_config const ap_usb;

static struct queue const ap_usart_to_usb = QUEUE_DIRECT(64, uint8_t,
							 ap_usart.producer,
							 ap_usb.consumer);
static struct queue const ap_usb_to_usart = QUEUE_DIRECT(64, uint8_t,
							 ap_usb.producer,
							 ap_usart.consumer);

static struct usart_tx_dma const ap_usart_tx_dma =
	USART_TX_DMA(STM32_DMAC_USART1_TX, 16);

static struct usart_config const ap_usart =
	USART_CONFIG(usart1_hw,
		     usart_rx_interrupt,
		     ap_usart_tx_dma.usart_tx,
		     115200,
		     ap_usart_to_usb,
		     ap_usb_to_usart);

#define AP_USB_STREAM_RX_SIZE	16
#define AP_USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(ap_usb,
		  USB_IFACE_AP_STREAM,
		  USB_STR_AP_STREAM_NAME,
		  USB_EP_AP_STREAM,
		  AP_USB_STREAM_RX_SIZE,
		  AP_USB_STREAM_TX_SIZE,
		  ap_usb_to_usart,
		  ap_usart_to_usb)

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_lock = NULL,
	}
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

/* Initialize board. */
static void board_init(void)
{
	int i;

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USBC_BC12_INT_L);

	/*
	 * Initialize AP console forwarding USART and queues.
	 */
	queue_init(&ap_usart_to_usb);
	queue_init(&ap_usb_to_usart);
	usart_init(&ap_usart);
	/* Disable UART input when the Write Protect is enabled */
	if (system_is_locked())
		ap_usb.state->rx_disabled = 1;

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_DEVICE_ODL lines are
	 * set low to specify device mode.
	 */
	gpio_set_level(GPIO_USBC_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_CHGR_ACOK);

	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACC_IRQ1);

	/* Enable interrupts from SI1141 sensor. */
	gpio_enable_interrupt(GPIO_ALS_PROXY_INT_L);

	if (board_has_spi_sensors()) {
		for (i = MOTIONSENSE_TYPE_ACCEL;
		     i <= MOTIONSENSE_TYPE_MAG; i++) {
			motion_sensors[i].addr =
				BMI160_SET_SPI_ADDRESS(CONFIG_SPI_ACCEL_PORT);
		}
		/* SPI sensors: put back the GPIO in its expected state */
		gpio_set_level(GPIO_SPI3_NSS, 1);

		/* Enable SPI for BMI160 */
		gpio_config_module(MODULE_SPI_MASTER, 1);

		/* Set all four SPI3 pins to high speed */
		/* pins C10/C11/C12 */
		STM32_GPIO_OSPEEDR(GPIO_C) |= 0x03f00000;

		/* pin A4 */
		STM32_GPIO_OSPEEDR(GPIO_A) |= 0x00000300;

		/* Enable clocks to SPI3 module */
		STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI3;

		/* Reset SPI3 */
		STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI3;
		STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI3;

		spi_enable(CONFIG_SPI_ACCEL_PORT, 1);
		CPRINTS("Board using SPI sensors");
	} else { /* I2C sensors on rev v6/7/8 */
		CPRINTS("Board using I2C sensors");
		/*
		 * On EVT2, when the sensors are on the same bus as other
		 * sensors, motion task would not leave enough time for
		 * processing as soon as its frequency is around ~200Hz.
		 */
		motion_min_interval = 8 * MSEC;
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_startup_key_combo(void)
{
	int vold = !gpio_get_level(GPIO_BTN_VOLD_L);
	int volu = !gpio_get_level(GPIO_BTN_VOLU_L);
	int pwr = power_button_signal_asserted();

	/*
	 * Determine recovery mode is requested by the power and
	 * voldown buttons being pressed (while device was off).
	 */
	if (pwr && vold && !volu) {
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
		CPRINTS("> RECOVERY mode");
	}

	/*
	 * Determine fastboot mode is requested by the power and
	 * voldown buttons being pressed (while device was off).
	 */
	if (pwr && volu && !vold) {
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_FASTBOOT);
		CPRINTS("> FASTBOOT mode");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_startup_key_combo, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 1000,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_SPI_FLASH_NSS},
	{ CONFIG_SPI_ACCEL_PORT, 1, GPIO_SPI3_NSS }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/* Sensor mutex */
static struct mutex g_mutex;

/* Matrix to rotate sensor vector into standard reference frame */
const matrix_3x3_t accelgyro_standard_ref = {
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0, FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

const matrix_3x3_t mag_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{FLOAT_TO_FP(1),  0,  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {

	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[RYU_LID_ACCEL] = {
		.name = "Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &accelgyro_standard_ref,
		.default_range = 8,  /* g, use hifi requirements */
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* Used for double tap */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = TAP_ODR,
				/* Interrupt driven, no polling */
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = TAP_ODR,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = TAP_ODR,
				.ec_rate = 0,
			},
		},
	},
	[RYU_LID_GYRO] = {
		.name = "Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.addr = BMI160_ADDR0,
		.default_range = 1000, /* dps, use hifi requirement */
		.rot_standard_ref = &accelgyro_standard_ref,
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC does not need gyro in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* Unused */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 0,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
			},
		},
	},
	[RYU_LID_MAG] = {
		.name = "Mag",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_MAG,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bmi160_drv,
		.mutex = &g_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.addr = BMI160_ADDR0,
		.rot_standard_ref = &mag_standard_ref,
		.default_range = 1 << 11, /* 16LSB / uT, fixed */
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC does not need compass in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* Unused */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 0,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
			},
		},
	},
	[RYU_LID_LIGHT] = {
		.name = "Light",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_SI1141,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &si114x_drv,
		.mutex = &g_mutex,
		.drv_data = &g_si114x_data,
		.addr = SI114X_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 9000, /* 90%: int = 0 - frac = 9000/10000 */
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC needs sensor for light adaptive brightness */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 1000,
				/* Interrupt driven, for double tap */
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 1000,
				.ec_rate = 0,
			},
		},
	},
	[RYU_LID_PROX] = {
		.name = "Prox",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_SI1141,
		.type = MOTIONSENSE_TYPE_PROX,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &si114x_drv,
		.mutex = &g_mutex,
		.drv_data = &g_si114x_data,
		.port = I2C_PORT_ALS,
		.addr = SI114X_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 7630, /* Upon testing at desk */
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC does not need proximity in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* Unused */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 0,
				.ec_rate = 0,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

const struct lb_brightness_def lb_brightness_levels[] = {
	{
		/* regular brightness */
		.lux_up = 60,
		.lux_down = 40,
		.color = {
			{0x74, 0x58, 0xb4},	/* Segment0: Google blue */
			{0xd6, 0x40, 0x20},	/* Segment1: Google red */
			{0xfa, 0xe6, 0x20},	/* Segment2: Google yellow */
			{0x66, 0xb0, 0x50},	/* Segment3: Google green */
		},
	},
	{
		/* 25 - 50% brightness */
		.lux_up = 40,
		.lux_down = 20,
		.color = {
			{0x51, 0x38, 0x7d},
			{0x99, 0x28, 0x15},
			{0xb8, 0x9e, 0x1a},
			{0x44, 0x80, 0x35},
		},
	},
	{
		/* 0 .. 25% brightness */
		.lux_up = 0,
		.lux_down = 0,
		.color = {
			{0x3d, 0x28, 0x5c},
			{0x71, 0x28, 0x10},
			{0x8a, 0x6f, 0x10},
			{0x2f, 0x60, 0x25},
		},
	},
};
const unsigned int lb_brightness_levels_count =
		ARRAY_SIZE(lb_brightness_levels);

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

void usb_board_connect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 0);
}

void usb_board_disconnect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 1);
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
	/* check if we are source vbus on that port */
	int src = gpio_get_level(GPIO_CHGR_OTG);

	if (charge_port >= 0 && charge_port < CONFIG_USB_PD_PORT_COUNT && src) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		return EC_ERROR_INVAL;
	}

	/* Enable/disable charging */
	gpio_set_level(GPIO_USBC_CHARGE_EN_L, charge_port == CHARGE_PORT_NONE);

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma)
{
	int rv;

	charge_current_limit = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);
	rv = charge_set_input_current_limit(charge_current_limit);
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP ||
	       supplier == CHARGE_SUPPLIER_BC12_SDP ||
	       supplier == CHARGE_SUPPLIER_BC12_CDP ||
	       supplier == CHARGE_SUPPLIER_PROPRIETARY;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2400;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
		return 2400;
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/* Send host event up to AP */
void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&(host_event_status.status), mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}

/**
 * Enable and disable SPI for case closed debugging.  This forces the AP into
 * reset while SPI is enabled, thus preventing contention on the SPI interface.
 */
void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Place AP into reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 0);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 1);
	gpio_set_flags(SPI_FLASH_DEVICE->gpio_cs, GPIO_OUT_HIGH);

	/* Set all four SPI pins to high speed */
	/* pins B10/B14/B15 and B9 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xf03c0000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	/* Enable SPI LDO to power the flash chip */
	gpio_set_level(GPIO_VDDSPI_EN, 1);

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);

	/* Disable SPI LDO */
	gpio_set_level(GPIO_VDDSPI_EN, 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 0);
	gpio_set_flags(SPI_FLASH_DEVICE->gpio_cs, GPIO_INPUT);

	/* Release AP from reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 1);
}

int board_get_version(void)
{
	static int ver;

	if (!ver) {
		/*
		 * read the board EC ID on the tristate strappings
		 * using ternary encoding: 0 = 0, 1 = 1, Hi-Z = 2
		 */
		uint8_t id0 = 0, id1 = 0;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_DOWN | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_DOWN | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0);
		id1 = gpio_get_level(GPIO_BOARD_ID1);
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_UP | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_UP | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0) && !id0 ? 2 : id0;
		id1 = gpio_get_level(GPIO_BOARD_ID1) && !id1 ? 2 : id1;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_INPUT);
		ver = id1 * 3 + id0;
		CPRINTS("Board ID = %d", ver);
	}

	return ver;
}

int board_has_spi_sensors(void)
{
	/*
	 * boards version 6 / 7 / 8 have an I2C bus to sensors.
	 * board version 0+ has a SPI bus to sensors
	 */
	int ver = board_get_version();
	return (ver < 6);
}

/****************************************************************************/
/* Host commands */

static int host_event_status_host_cmd(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = atomic_read_clear(&(host_event_status.status));

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, host_event_status_host_cmd,
			EC_VER_MASK(0));

/****************************************************************************/
/* Console commands */

static int cmd_btn_press(int argc, char **argv)
{
	enum gpio_signal gpio;
	char *e;
	int v;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "volup"))
		gpio = GPIO_BTN_VOLU_L;
	else if (!strcasecmp(argv[1], "voldown"))
		gpio = GPIO_BTN_VOLD_L;
	else
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		/* Just reading */
		ccprintf("Button %s pressed = %d\n", argv[1],
						     !gpio_get_level(gpio));
		return EC_SUCCESS;
	}

	v = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (v)
		gpio_set_flags(gpio, GPIO_OUT_LOW);
	else
		gpio_set_flags(gpio, GPIO_INPUT | GPIO_PULL_UP);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(btnpress, cmd_btn_press,
			"<volup|voldown> [0|1]",
			"Simulate button press");
