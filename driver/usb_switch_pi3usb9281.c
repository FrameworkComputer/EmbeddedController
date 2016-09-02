/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Pericom PI3USB3281 USB port switch driver.
 */

#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "pi3usb9281.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

 /* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* 8-bit I2C address */
#define PI3USB9281_I2C_ADDR (0x25 << 1)

/* Delay values */
#define PI3USB9281_SW_RESET_DELAY 20

/* Wait after a charger is detected to debounce pin contact order */
#define PI3USB9281_DETECT_DEBOUNCE_DELAY_MS 1000
/*
 * Wait after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define PI3USB9281_RESET_DEBOUNCE_DELAY_MS 100

/* Store the state of our USB data switches so that they can be restored. */
static int usb_switch_state[CONFIG_USB_PD_PORT_COUNT];
static struct mutex usb_switch_lock[CONFIG_USB_PD_PORT_COUNT];

static void select_chip(int port)
{
	struct pi3usb9281_config *chip = &pi3usb9281_chips[port];
	ASSERT(port < CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

	if (chip->mux_lock) {
		mutex_lock(chip->mux_lock);
		gpio_set_level(chip->mux_gpio, chip->mux_gpio_level);
	}
}

static void unselect_chip(int port)
{
	struct pi3usb9281_config *chip = &pi3usb9281_chips[port];

	if (chip->mux_lock)
		/* Just release the mutex, no need to change the mux gpio */
		mutex_unlock(chip->mux_lock);
}

static uint8_t pi3usb9281_do_read(int port, uint8_t reg, int with_lock)
{
	struct pi3usb9281_config *chip = &pi3usb9281_chips[port];
	int res, val;

	if (with_lock)
		select_chip(port);

	res = i2c_read8(chip->i2c_port, PI3USB9281_I2C_ADDR, reg, &val);

	if (with_lock)
		unselect_chip(port);

	if (res)
		return 0xee;

	return val;
}

static uint8_t pi3usb9281_read_u(int port, uint8_t reg)
{
	return pi3usb9281_do_read(port, reg, 0);
}

static uint8_t pi3usb9281_read(int port, uint8_t reg)
{
	return pi3usb9281_do_read(port, reg, 1);
}

static int pi3usb9281_do_write(
	int port, uint8_t reg, uint8_t val, int with_lock)
{
	struct pi3usb9281_config *chip = &pi3usb9281_chips[port];
	int res;

	if (with_lock)
		select_chip(port);

	res = i2c_write8(chip->i2c_port, PI3USB9281_I2C_ADDR, reg, val);

	if (with_lock)
		unselect_chip(port);

	if (res)
		CPRINTS("PI3USB9281 I2C write failed");
	return res;
}

static int pi3usb9281_write(int port, uint8_t reg, uint8_t val)
{
	return pi3usb9281_do_write(port, reg, val, 1);
}

/* Write control register, taking care to correctly set reserved bits. */
static int pi3usb9281_do_write_ctrl(int port, uint8_t ctrl, int with_lock)
{
	return pi3usb9281_do_write(port, PI3USB9281_REG_CONTROL,
				(ctrl & PI3USB9281_CTRL_MASK) |
				PI3USB9281_CTRL_RSVD_1, with_lock);
}

static int pi3usb9281_write_ctrl(int port, uint8_t ctrl)
{
	return pi3usb9281_do_write_ctrl(port, ctrl, 1);
}

static int pi3usb9281_write_ctrl_u(int port, uint8_t ctrl)
{
	return pi3usb9281_do_write_ctrl(port, ctrl, 0);
}

static int pi3usb9281_set_interrupt_mask(int port, uint8_t mask)
{
	return pi3usb9281_write(port, PI3USB9281_REG_INT_MASK, ~mask);
}

void pi3usb9281_init(int port)
{
	uint8_t dev_id;

	dev_id = pi3usb9281_read(port, PI3USB9281_REG_DEV_ID);

	if (dev_id != PI3USB9281_DEV_ID && dev_id != PI3USB9281_DEV_ID_A)
		CPRINTS("PI3USB9281 invalid ID 0x%02x", dev_id);

	pi3usb9281_set_interrupt_mask(port, 0xff);
	pi3usb9281_enable_interrupts(port);
}


int pi3usb9281_enable_interrupts(int port)
{
	uint8_t ctrl = pi3usb9281_read(port, PI3USB9281_REG_CONTROL);

	if (ctrl == 0xee)
		return EC_ERROR_UNKNOWN;

	return pi3usb9281_write_ctrl(port, ctrl & ~PI3USB9281_CTRL_INT_DIS);
}

int pi3usb9281_disable_interrupts(int port)
{
	uint8_t ctrl = pi3usb9281_read(port, PI3USB9281_REG_CONTROL);
	int rv;

	if (ctrl == 0xee)
		return EC_ERROR_UNKNOWN;

	rv = pi3usb9281_write_ctrl(port, ctrl | PI3USB9281_CTRL_INT_DIS);
	pi3usb9281_get_interrupts(port);
	return rv;
}

int pi3usb9281_get_interrupts(int port)
{
	return pi3usb9281_read(port, PI3USB9281_REG_INT);
}

int pi3usb9281_get_device_type(int port)
{
	return pi3usb9281_read(port, PI3USB9281_REG_DEV_TYPE) & 0x77;
}

int pi3usb9281_get_charger_status(int port)
{
	return pi3usb9281_read(port, PI3USB9281_REG_CHG_STATUS) & 0x1f;
}

static int pi3usb9281_get_ilim(int device_type, int charger_status)
{
	/* Limit USB port current. 500mA for not listed types. */
	int current_limit_ma = 500;

	if (charger_status & PI3USB9281_CHG_CAR_TYPE1 ||
	    charger_status & PI3USB9281_CHG_CAR_TYPE2)
		current_limit_ma = 3000;
	else if (charger_status & PI3USB9281_CHG_APPLE_1A)
		current_limit_ma = 1000;
	else if (charger_status & PI3USB9281_CHG_APPLE_2A)
		current_limit_ma = 2000;
	else if (charger_status & PI3USB9281_CHG_APPLE_2_4A)
		current_limit_ma = 2400;
	else if (device_type & PI3USB9281_TYPE_CDP)
		current_limit_ma = 1500;
	else if (device_type & PI3USB9281_TYPE_DCP)
		current_limit_ma = 500;

	return current_limit_ma;
}

static int pi3usb9281_reset(int port)
{
	int rv = pi3usb9281_write(port, PI3USB9281_REG_RESET, 0x1);

	if (!rv)
		/* Reset takes ~15ms. Wait for 20ms to be safe. */
		msleep(PI3USB9281_SW_RESET_DELAY);

	return rv;
}

static int pi3usb9281_set_switch_manual(int port, int val)
{
	int res = EC_ERROR_UNKNOWN;
	uint8_t ctrl;

	select_chip(port);
	ctrl = pi3usb9281_read_u(port, PI3USB9281_REG_CONTROL);

	if (ctrl != 0xee) {
		if (val)
			ctrl &= ~PI3USB9281_CTRL_AUTO;
		else
			ctrl |= PI3USB9281_CTRL_AUTO;
		res = pi3usb9281_write_ctrl_u(port, ctrl);
	}

	unselect_chip(port);
	return res;
}

static int pi3usb9281_set_pins(int port, uint8_t val)
{
	return pi3usb9281_write(port, PI3USB9281_REG_MANUAL, val);
}

static int pi3usb9281_set_switches(int port, int open)
{
	int res = EC_ERROR_UNKNOWN;
	uint8_t ctrl;

	select_chip(port);
	ctrl = pi3usb9281_read_u(port, PI3USB9281_REG_CONTROL);

	if (ctrl != 0xee) {
		if (open)
			ctrl &= ~PI3USB9281_CTRL_SWITCH_AUTO;
		else
			ctrl |= PI3USB9281_CTRL_SWITCH_AUTO;
		res = pi3usb9281_write_ctrl_u(port, ctrl);
	}

	unselect_chip(port);
	return res;
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/* If switch is not changing then return */
	if (setting == usb_switch_state[port])
		return;

	mutex_lock(&usb_switch_lock[port]);
	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;

	pi3usb9281_set_switches(port, usb_switch_state[port]);

	mutex_unlock(&usb_switch_lock[port]);
}

static void bc12_detect(int port)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;

	charge.voltage = USB_CHARGER_VOLTAGE_MV;

	if (usb_charger_port_is_sourcing_vbus(port)) {
		/* If we're sourcing VBUS then we're not charging */
		device_type = charger_status = 0;
	} else {
		/* Set device type */
		device_type = pi3usb9281_get_device_type(port);
		charger_status = pi3usb9281_get_charger_status(port);
	}

	/* Debounce pin plug order if we detect a charger */
	if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
		/* next operation might trigger a detach interrupt */
		pi3usb9281_disable_interrupts(port);
		/*
		 * Ensure D+/D- are open before resetting
		 * Note: we can't simply call pi3usb9281_set_switches() because
		 * another task might override it and set the switches closed.
		 */
		pi3usb9281_set_switch_manual(port, 1);
		pi3usb9281_set_pins(port, 0);

		/* Delay to debounce pin attach order */
		msleep(PI3USB9281_DETECT_DEBOUNCE_DELAY_MS);

		/*
		 * Trigger chip reset to refresh detection registers.
		 * WARNING: This reset is acceptable for samus_pd,
		 * but may not be acceptable for devices that have
		 * an OTG / device mode, as we may be interrupting
		 * the connection.
		 */
		pi3usb9281_reset(port);
		/*
		 * Restore data switch settings - switches return to
		 * closed on reset until restored.
		 */
		usb_charger_set_switches(port, USB_SWITCH_RESTORE);
		/* Clear possible disconnect interrupt */
		pi3usb9281_get_interrupts(port);
		/* Mask attach interrupt */
		pi3usb9281_set_interrupt_mask(port,
					      0xff &
					      ~PI3USB9281_INT_ATTACH);
		/* Re-enable interrupts */
		pi3usb9281_enable_interrupts(port);
		msleep(PI3USB9281_RESET_DEBOUNCE_DELAY_MS);

		/* Clear possible attach interrupt */
		pi3usb9281_get_interrupts(port);
		/* Re-enable attach interrupt */
		pi3usb9281_set_interrupt_mask(port, 0xff);

		/* Re-read ID registers */
		device_type = pi3usb9281_get_device_type(port);
		charger_status = pi3usb9281_get_charger_status(port);
	}

	/* Attachment: decode + update available charge */
	if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		charge.current = pi3usb9281_get_ilim(device_type,
						     charger_status);
		charge_manager_update_charge(type, port, &charge);
	} else { /* Detachment: update available charge to 0 */
		charge.current = 0;
		charge_manager_update_charge(
					CHARGE_SUPPLIER_PROPRIETARY,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_CDP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_DCP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_BC12_SDP,
					port,
					&charge);
		charge_manager_update_charge(
					CHARGE_SUPPLIER_OTHER,
					port,
					&charge);
	}
}

void usb_charger_task(void)
{
	const int attach_mask = PI3USB9281_INT_ATTACH | PI3USB9281_INT_DETACH;
	int port = (task_get_current() == TASK_ID_USB_CHG_P0 ? 0 : 1);
	int interrupt;
	uint32_t evt;

	/* Initialize chip and enable interrupts */
	pi3usb9281_init(port);

	bc12_detect(port);

	while (1) {
		/* Wait for interrupt */
		evt = task_wait_event(-1);

		/* Interrupt from the Pericom chip, determine charger type */
		if (evt & USB_CHG_EVENT_BC12) {
			/* Read interrupt register to clear on chip */
			pi3usb9281_get_interrupts(port);
			bc12_detect(port);
		} else if (evt & USB_CHG_EVENT_INTR) {
			/* Check the interrupt register, and clear on chip */
			interrupt = pi3usb9281_get_interrupts(port);
			if (interrupt & attach_mask)
				bc12_detect(port);
		}

		/*
		 * Re-enable interrupts on pericom charger detector since the
		 * chip may periodically reset itself, and come back up with
		 * registers in default state. TODO(crosbug.com/p/33823): Fix
		 * these unwanted resets.
		 */
		if (evt & USB_CHG_EVENT_VBUS) {
			pi3usb9281_enable_interrupts(port);
#ifndef CONFIG_USB_PD_VBUS_DETECT_TCPC
			CPRINTS("VBUS p%d %d", port,
				pd_snk_is_vbus_provided(port));
#endif
		}
	}
}
