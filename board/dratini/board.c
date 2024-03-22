/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hatch board-specific configuration */

#include "adc.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_tcs3400.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
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
#include "tablet_mode.h"
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

static void check_reboot_deferred(void);
DECLARE_DEFERRED(check_reboot_deferred);

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

static void control_mst_power(void)
{
	baseboard_mst_enable_control(MST_HDMI,
				     gpio_get_level(GPIO_HDMI_CONN_HPD));
}
DECLARE_DEFERRED(control_mst_power);

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/*
	 * When the HPD goes high, enable the MST hub right away,
	 * but debounce the low signal for 2 seconds to avoid transient low
	 * pulses on the HPD signal.
	 */
	if (gpio_get_level(signal))
		hook_call_deferred(&control_mst_power_data, 0);
	else
		hook_call_deferred(&control_mst_power_data, 2 * SECOND);
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
	[PWM_CH_KBLIGHT] = { .channel = 3, .flags = 0, .freq = 100 },
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

/* Set aux switch to 0xc when CCD enabled on port C0 */
static int board_anx7447_mux_set_c0(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int port = me->usb_port;
	int rv = EC_SUCCESS;

	if (port != USB_PD_PORT_TCPC_0)
		return rv;

	if (gpio_get_level(GPIO_CCD_MODE_ODL))
		return rv;

	CPRINTS("C%d: set AUX_SW_SEL=0x%x", port, 0xc);
	rv = tcpc_write(port, ANX7447_REG_TCPC_AUX_SWITCH, 0xc);
	if (rv)
		CPRINTS("C%d: Setting AUX_SW_SEL failed", port);

	return rv;
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
			.driver = &anx7447_usb_mux_driver,
			.board_set = &board_anx7447_mux_set_c0,
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

/* Base accel private data */
static struct bmi_drv_data_t g_bmi160_data;

/* BMA255 private data */
static struct accelgyro_saved_data_t g_bma255_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(-1), 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

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
		.default_range = 2, /* g, enough for lid angle calculation. */
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
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
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
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
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
	.rpm_min = 2500,
	.rpm_start = 2500,
	.rpm_max = 6500,
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
	[ADC_TEMP_SENSOR_1] = { "TEMP_CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_2] = { "TEMP_5V_REG", NPCX_ADC_CH1, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_3] = { "TEMP_CPU", NPCX_ADC_CH2, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "5V Reg",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "CPU",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Dratini Temperature sensors */
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(73),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(70),
};

const static struct ec_thermal_config thermal_b = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(68),
		[EC_TEMP_THRESH_HALT] = C_TO_K(70),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(40),
	.temp_fan_max = C_TO_K(55),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_1] = thermal_a;
	thermal_params[TEMP_SENSOR_2] = thermal_b;
}

/*
 * Returns true for boards that are convertible into tablet mode, and
 * false for clamshells.
 */
bool board_is_convertible(void)
{
	uint8_t sku_id = get_board_sku();

	/*
	 * Dragonair (SKU 21 ,22, 23 and 24) is a convertible.
	 * Dratini is not.
	 * Unprovisioned SKU 255.
	 */
	return sku_id == 21 || sku_id == 22 || sku_id == 23 || sku_id == 24 ||
	       sku_id == 255;
}

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_convertible()) {
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable gpio interrupt for base accelgyro sensor */
		gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static void board_init(void)
{
	/* Initialize Fans */
	setup_fans();

	/*
	 * If HDMI is plugged in at boot, the interrupt may have been missed,
	 * so check if the MST hub needs to be powered now.
	 */
	control_mst_power();

	/* Enable HDMI HPD interrupt. */
	gpio_enable_interrupt(GPIO_HDMI_CONN_HPD);

	board_update_sensor_config_from_sku();
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

bool board_has_kb_backlight(void)
{
	uint8_t sku_id = get_board_sku();
	/*
	 * SKUs have keyboard backlight.
	 * Dratini: 2, 3, 5, 8
	 * Dragonair: 22, 24
	 * Unprovisioned: 255
	 */
	return sku_id == 2 || sku_id == 3 || sku_id == 5 || sku_id == 8 ||
	       sku_id == 22 || sku_id == 24 || sku_id == 255;
}

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	if (board_has_kb_backlight())
		return flags0;
	else
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
}

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 }, { 1, 1 },	  { 1, 0 },   { 0, 6 },
	{ 0, 7 },   { 1, 4 }, { 1, 3 },	  { 1, 6 },   { 1, 7 },
	{ 3, 1 },   { 2, 0 }, { 1, 5 },	  { 2, 6 },   { 2, 7 },
	{ 2, 1 },   { 2, 4 }, { 2, 5 },	  { 1, 2 },   { 2, 3 },
	{ 2, 2 },   { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

/* Disable HDMI power while AP is suspended / off */
static void disable_hdmi(void)
{
	gpio_set_level(GPIO_EN_HDMI, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, disable_hdmi, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, disable_hdmi, HOOK_PRIO_DEFAULT);

/* Enable HDMI power while AP is active */
static void enable_hdmi(void)
{
	gpio_set_level(GPIO_EN_HDMI, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_hdmi, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, enable_hdmi, HOOK_PRIO_DEFAULT);

void all_sys_pgood_check_reboot(void)
{
	hook_call_deferred(&check_reboot_deferred_data, 3000 * MSEC);
}

__override void board_chipset_forced_shutdown(void)
{
	hook_call_deferred(&check_reboot_deferred_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_forced_shutdown,
	     HOOK_PRIO_DEFAULT);

static void check_reboot_deferred(void)
{
	if (!gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD))
		system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED);
}
