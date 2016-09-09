/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* lucid board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger_detect.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "led_common.h"
#include "registers.h"
#include "task.h"
#include "temp_sensor.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define USB_CHG_DETECT_DELAY_US 5000

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (C0 RX)
	 *  Chan 3 : SPI1_TX   (C0 TX)
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */

	/*
	 * Remap USART1 RX/TX DMA to match uart driver.
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10);
}

static void reset_charge(int port)
{
	struct charge_port_info charge_none;

	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
				     port,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP,
				     port,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP,
				     port,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP,
				     port,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER,
				     port,
				     &charge_none);
}

static void usb_charger_bc12_detect(void)
{
	int type;
	struct charge_port_info charge;

	type = charger_detect_get_device_type();
	if (gpio_get_level(GPIO_AC_PRESENT) && type) {
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		if (type == CHARGE_SUPPLIER_BC12_CDP)
			charge.current = 1500;
		else
			charge.current = 500;

		charge_manager_update_charge(type, 0, &charge);
	} else
		reset_charge(0);


	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(usb_charger_bc12_detect);

static void update_vbus_supplier(int vbus_level)
{
	struct charge_port_info charge;

	charge.voltage = USB_CHARGER_VOLTAGE_MV;
	charge.current = vbus_level ? USB_CHARGER_MIN_CURR_MA : 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0, &charge);
}

void vbus_evt(enum gpio_signal signal)
{
	/*
	 * We are using AC_PRESENT signal to detect VBUS presence since
	 * lucid only has one port and charging is always enabled.
	 */

	hook_call_deferred(&usb_charger_bc12_detect_data,
			   USB_CHG_DETECT_DELAY_US);
	update_vbus_supplier(gpio_get_level(signal));

	task_wake(TASK_ID_PD_C0);

	/* trigger AC present interrupt */
	extpower_interrupt(signal);
}

void charge_state_interrupt(enum gpio_signal signal)
{
	led_enable(gpio_get_level(signal));
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_C0_CC1_PD] = {"C0_CC1_PD", 3300, 4096, 0, STM32_AIN(1)},
	[ADC_C0_CC2_PD] = {"C0_CC2_PD", 3300, 4096, 0, STM32_AIN(3)},

	/* Vbus sensing. Converted to mV, full ADC is equivalent to 33.5V. */
	[ADC_VBUS] = {"VBUS",  33550, 4096, 0, STM32_AIN(7)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct temp_sensor_t temp_sensors[] = {
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val, 0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Initialize board. */
static void board_init(void)
{
	int i;

	/* Initialize all BC1.2 charge suppliers to 0 */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++)
		reset_charge(i);

	/* Enable charge status interrupt */
	gpio_enable_interrupt(GPIO_CHARGE_STATUS);

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	update_vbus_supplier(gpio_get_level(GPIO_AC_PRESENT));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int board_set_active_charge_port(int charge_port)
{
	/* Only one port and it's always enabled */
	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma)
{
	int rv;

	charge_ma = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);
	rv = charge_set_input_current_limit(charge_ma);
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
}

/**
 * Custom physical check of battery presence.
 */
enum battery_present battery_is_present(void)
{
	return gpio_get_level(GPIO_BAT_PRESENT) ? BP_YES : BP_NO;
}

void pd_send_host_event(int mask)
{
}
