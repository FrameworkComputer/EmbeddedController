/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "gpu.h"
#include "hooks.h"
#include "i2c.h"
#include "lotus/amd_r23m.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"

#define EJ899I_ADDR	0x60

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static int host_dp_ready;

int ej889i_read_reg8(int reg, int *data)
{
	int rv;

	rv = i2c_read_offset16(I2C_PORT_GPU0, EJ899I_ADDR, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: reg=0x%02x", __func__, reg);
	return rv;
}

int ej889i_write_reg8(int reg, int data)
{
	int rv;

	rv = i2c_write_offset16(I2C_PORT_GPU0, EJ899I_ADDR, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: reg=0x%02x", __func__, reg);
	return rv;
}

void read_gpu_id(void);
DECLARE_DEFERRED(read_gpu_id);

void read_gpu_id(void)
{
	int rv, data;

	/* Read the register */
	rv = ej889i_read_reg8(0x800E, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x0E, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x8010, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x10, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x8011, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x11, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x80DE, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0xDE, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x810C, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x10C, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x8110, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x110, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x8111, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x111, result=%d", data);

	k_msleep(5);

	rv = ej889i_read_reg8(0x8112, &data);
	if (rv == EC_SUCCESS)
		CPRINTS("reg = 0x112, result=%d", data);
}

void set_host_dp_ready(int ready)
{
	host_dp_ready = ready;

	if (ready) {
		CPRINTS("ready to send the Qevent 58!");
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_dp_hot_plug));

		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec)))
			host_set_single_event(EC_HOST_EVENT_DGPU_TYPEC_NOTIFY);
	} else
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_dp_hot_plug));
}

void dp_hot_plug_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec)))
		host_set_single_event(EC_HOST_EVENT_DGPU_TYPEC_NOTIFY);
}

/* Currently, the GPU pd interrupt does not function */
void gpu_pd_interrupt(enum gpio_signal signal)
{
	CPRINTS("gpu pd interrupt!");
	hook_call_deferred(&read_gpu_id_data, 50 * MSEC);
}
