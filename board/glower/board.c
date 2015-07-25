/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Glower board-specific configuration */

#include "charger.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "util.h"

#define GPIO_KB_INPUT GPIO_INPUT
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH | GPIO_PULL_UP)

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP1050_PGOOD,      1, "PGOOD_PP1050"},
	{GPIO_PP3300_PCH_PGOOD,  1, "PGOOD_PP3300_PCH"},
	{GPIO_PP5000_PGOOD,      1, "PGOOD_PP5000"},
	{GPIO_S5_PGOOD,          1, "PGOOD_S5"},
	{GPIO_VCORE_PGOOD,       1, "PGOOD_VCORE"},
	{GPIO_PP1000_S0IX_PGOOD, 1, "PGOOD_PP1000_S0IX"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4#_DEASSERTED"},
#ifdef CONFIG_CHIPSET_DEBUG
	{GPIO_PCH_SLP_SX_L,      1, "SLP_SX#_DEASSERTED"},
	{GPIO_PCH_SUS_STAT_L,    0, "SUS_STAT#_ASSERTED"},
	{GPIO_PCH_SUSPWRDNACK,   1, "SUSPWRDNACK_ASSERTED"},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", MEC1322_I2C1, 100},
	{"thermal",  MEC1322_I2C2, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
