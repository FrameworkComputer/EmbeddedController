/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Palkia board-specific configuration */

#include "adc.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
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

static const uint8_t actual_key_mask[KEYBOARD_COLS_MAX] = {
	0x01, 0x68, 0xbd, 0x03, 0x7e, 0xff, 0xff, 0xff,
	0xff, 0x03, 0xfd, 0x48, 0x03, 0xff, 0xf7, 0x16 /* full set */
};

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

void board_config_pre_init(void)
{
	int i;

	/* override the keyscan key mask */
	for (i = 0; i < KEYBOARD_COLS_MAX; ++i)
		keyscan_config.actual_key_mask[i] = actual_key_mask[i];
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_INT_ODL:
		sn5s330_interrupt(0);
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

	default:
		break;
	}
}

static void board_lid_interrupt(enum gpio_signal signal)
{
	static int board_id = -1;

	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
	}

	lid_interrupt(signal);
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
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	}
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		},
	},
};

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USB_PD_PORT_TCPC_0] = {
		.i2c_port = I2C_PORT_PPC0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

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
	[ADC_TEMP_SENSOR_1] = { "TEMP_CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_2] = { "TEMP_5V_REG", NPCX_ADC_CH1, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_3] = { "TEMP_AMB", NPCX_ADC_CH3, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_4] = { "TEMP_CPU", NPCX_ADC_CH2, ADC_MAX_VOLT,
				ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Temp1",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Temp2",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Temp3",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
	[TEMP_SENSOR_4] = { .name = "Temp4",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_30k9_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_4 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Palkia temperature control thresholds */
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(60),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(65),
	.temp_fan_max = C_TO_K(80),
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

int board_tcpc_post_init(int port)
{
	int rv = EC_SUCCESS;

	if (port == USB_PD_PORT_TCPC_0)
		/* Set MUX_DP_EQ to 3.6dB (0x98) */
		rv = tcpc_write(port, PS8XXX_REG_MUX_DP_EQ_CONFIGURATION, 0x98);

	return rv;
}

bool board_is_convertible(void)
{
	return false;
}
