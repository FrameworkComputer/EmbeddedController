/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* lucid board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "usb_charge.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

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

	update_vbus_supplier(gpio_get_level(signal));

	if (task_start_called())
		task_wake(TASK_ID_PD);

	/* trigger AC present interrupt */
	extpower_interrupt(signal);
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_C0_CC1_PD] = {"C0_CC1_PD", 3300, 4096, 0, STM32_AIN(1)},
	[ADC_C0_CC2_PD] = {"C0_CC2_PD", 3300, 4096, 0, STM32_AIN(3)},

	/* Vbus sensing. Converted to mV, full ADC is equivalent to 30.25V. */
	[ADC_VBUS] = {"VBUS",  30250, 4096, 0, STM32_AIN(7)},
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

/* Initialize board. */
static void board_init(void)
{
	int i;
	struct charge_port_info charge_none;

	/* Initialize all BC1.2 charge suppliers to 0 */
	/*
	 * TODO: use built-in USB peripheral to detect BC1.2 suppliers an
	 * update charge manager.
	 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_OTHER,
					     i,
					     &charge_none);
	}

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	update_vbus_supplier(gpio_get_level(GPIO_AC_PRESENT));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

int board_set_active_charge_port(int charge_port)
{
	/* Only one port and it's always enabled */
	return EC_SUCCESS;
}

void board_set_charge_limit(int charge_ma)
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
