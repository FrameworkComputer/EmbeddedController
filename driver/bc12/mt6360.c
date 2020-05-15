/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "charge_manager.h"
#include "console.h"
#include "driver/bc12/mt6360.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) \
	cprints(CC_USBCHARGE, "%s " format, "MT6360", ## args)

static enum ec_error_list mt6360_read8(int reg, int *val)
{
	return i2c_read8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static enum ec_error_list mt6360_write8(int reg, int val)
{
	return i2c_write8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static int mt6360_update_bits(int reg, int mask, int val)
{
	int rv;
	int reg_val;

	rv = mt6360_read8(reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = mt6360_write8(reg, reg_val);
	return rv;
}

static inline int mt6360_set_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, mask);
}

static inline int mt6360_clr_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, 0x00);
}

static int mt6360_get_bc12_device_type(void)
{
	int reg;

	if (mt6360_read8(MT6360_REG_USB_STATUS_1, &reg))
		return CHARGE_SUPPLIER_NONE;

	switch (reg & MT6360_MASK_USB_STATUS) {
	case MT6360_MASK_SDP:
		CPRINTS("BC12 SDP");
		return CHARGE_SUPPLIER_BC12_SDP;
	case MT6360_MASK_CDP:
		CPRINTS("BC12 CDP");
		return CHARGE_SUPPLIER_BC12_CDP;
	case MT6360_MASK_DCP:
		CPRINTS("BC12 DCP");
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		CPRINTS("BC12 NONE");
		return CHARGE_SUPPLIER_NONE;
	}
}

static int mt6360_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static int mt6360_enable_bc12_detection(int en)
{
	int rv;

	if (en) {
#ifdef CONFIG_MT6360_BC12_GPIO
		gpio_set_level(GPIO_BC12_DET_EN, 1);
#endif
		return mt6360_set_bit(MT6360_REG_DEVICE_TYPE,
				      MT6360_MASK_USBCHGEN);
	}

	rv = mt6360_clr_bit(MT6360_REG_DEVICE_TYPE, MT6360_MASK_USBCHGEN);
#ifdef CONFIG_MT6360_BC12_GPIO
	gpio_set_level(GPIO_BC12_DET_EN, 0);
#endif
	return rv;
}

static void mt6360_update_charge_manager(int port)
{
	static int current_bc12_type = CHARGE_SUPPLIER_NONE;
	int reg;
	int new_bc12_type = CHARGE_SUPPLIER_NONE;

	mt6360_read8(MT6360_REG_DPDMIRQ, &reg);

	if (pd_snk_is_vbus_provided(port) && (reg & MT6360_MASK_DPDMIRQ_ATTACH))
		new_bc12_type = mt6360_get_bc12_device_type();

	if (new_bc12_type != current_bc12_type) {
		charge_manager_update_charge(current_bc12_type, port, NULL);

		if (new_bc12_type != CHARGE_SUPPLIER_NONE) {
			struct charge_port_info chg = {
				.current = mt6360_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, port, &chg);
		}

		current_bc12_type = new_bc12_type;
	}

	/* write clear */
	mt6360_write8(MT6360_REG_DPDMIRQ, reg);
}

static void mt6360_usb_charger_task(const int port)
{
	mt6360_clr_bit(MT6360_REG_DPDM_MASK1,
		       MT6360_REG_DPDM_MASK1_CHGDET_DONEI_M);
	mt6360_enable_bc12_detection(0);

	while (1) {
		uint32_t evt = task_wait_event(-1);

		/* vbus change, start bc12 detection */
		if (evt & USB_CHG_EVENT_VBUS)
			mt6360_enable_bc12_detection(1);

		/* detection done, update charge_manager and stop detection */
		if (evt & USB_CHG_EVENT_BC12) {
			mt6360_update_charge_manager(port);
			mt6360_enable_bc12_detection(0);
		}
	}
}

/* RGB LED */
int mt6360_led_enable(enum mt6360_led_id led_id, int enable)
{
	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;

	if (enable)
		return mt6360_set_bit(MT6360_REG_RGB_EN,
				      MT6360_MASK_ISINK_EN(led_id));
	return mt6360_clr_bit(MT6360_REG_RGB_EN, MT6360_MASK_ISINK_EN(led_id));
}

int mt6360_led_set_brightness(enum mt6360_led_id led_id, int brightness)
{
	int val;

	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;
	if (!IN_RANGE(brightness, 0, 16))
		return EC_ERROR_INVAL;

	RETURN_ERROR(mt6360_read8(MT6360_REG_RGB_ISINK(led_id), &val));
	val &= ~MT6360_MASK_CUR_SEL;
	val |= brightness;

	return mt6360_write8(MT6360_REG_RGB_ISINK(led_id), val);
}

const struct bc12_drv mt6360_drv = {
	.usb_charger_task = mt6360_usb_charger_task,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &mt6360_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
