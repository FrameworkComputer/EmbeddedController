/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Puff board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "common.h"
#include "cros_board_info.h"
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

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
};

/******************************************************************************/
/* USB-C TPCP Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
};
struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
};

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"ina",     I2C_PORT_INA,     100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"ppc0",    I2C_PORT_PPC0,    100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"power",   I2C_PORT_POWER,   100, GPIO_I2C5_SCL, GPIO_I2C5_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Physical fan. This are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2200,
	.rpm_start = 2200,
	.rpm_max = 5600,
};

struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VBUS] = {
		"VBUS", NPCX_ADC_CH4, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);


struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
}

static void board_init(void)
{
	/* Initialize Fans */
	setup_fans();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* Power Delivery and charging functions */
void baseboard_tcpc_init(void)
{
	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

int64_t get_time_dsw_pwrok(void)
{
	/* DSW_PWROK is turned on before EC was powered. */
	return -20 * MSEC;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	return status;
}

void board_reset_pd_mcu(void)
{
}

int board_set_active_charge_port(int port)
{
	return EC_SUCCESS;
}

int ppc_get_alert_status(int port)
{
	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
}
