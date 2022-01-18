/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>

#include "emul/emul_pi3usb9201.h"

#include "timer.h"
#include "usb_charge.h"
#include "battery.h"
#include "extpower.h"
#include "stubs.h"
#include "test_state.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(test_drivers_bc12, LOG_LEVEL_DBG);

#define EMUL_LABEL DT_NODELABEL(pi3usb9201_emul)

#define PI3USB9201_ORD DT_DEP_ORD(EMUL_LABEL)

/* Control_1 register bit definitions */
#define PI3USB9201_REG_CTRL_1_INT_MASK BIT(0)
#define PI3USB9201_REG_CTRL_1_MODE_SHIFT 1
#define PI3USB9201_REG_CTRL_1_MODE_MASK (0x7 << \
					 PI3USB9201_REG_CTRL_1_MODE_SHIFT)

/* Control_2 register bit definitions */
#define PI3USB9201_REG_CTRL_2_AUTO_SW BIT(1)
#define PI3USB9201_REG_CTRL_2_START_DET BIT(3)

/* Host status register bit definitions */
#define PI3USB9201_REG_HOST_STS_BC12_DET BIT(0)
#define PI3USB9201_REG_HOST_STS_DEV_PLUG BIT(1)
#define PI3USB9201_REG_HOST_STS_DEV_UNPLUG BIT(2)

enum pi3usb9201_mode {
	PI3USB9201_POWER_DOWN,
	PI3USB9201_SDP_HOST_MODE,
	PI3USB9201_DCP_HOST_MODE,
	PI3USB9201_CDP_HOST_MODE,
	PI3USB9201_CLIENT_MODE,
	PI3USB9201_RESERVED_1,
	PI3USB9201_RESERVED_2,
	PI3USB9201_USB_PATH_ON,
};

enum pi3usb9201_client_sts {
	CHG_OTHER = 0,
	CHG_2_4A,
	CHG_2_0A,
	CHG_1_0A,
	CHG_RESERVED,
	CHG_CDP,
	CHG_SDP,
	CHG_DCP,
};

struct bc12_status {
	enum charge_supplier supplier;
	int current_limit;
};

static const struct bc12_status bc12_chg_limits[] = {
	[CHG_OTHER]    = { .supplier = CHARGE_SUPPLIER_OTHER,
			   .current_limit = 500 },
	[CHG_2_4A]     = { .supplier = CHARGE_SUPPLIER_PROPRIETARY,
			   .current_limit = USB_CHARGER_MAX_CURR_MA },
	[CHG_2_0A]     = { .supplier = CHARGE_SUPPLIER_PROPRIETARY,
			   .current_limit = USB_CHARGER_MAX_CURR_MA },
	[CHG_1_0A]     = { .supplier = CHARGE_SUPPLIER_PROPRIETARY,
			   .current_limit = 1000 },
	[CHG_RESERVED] = { .supplier = CHARGE_SUPPLIER_NONE,
			   .current_limit = 0 },
	[CHG_CDP]      = { .supplier = CHARGE_SUPPLIER_BC12_CDP,
			   .current_limit = USB_CHARGER_MAX_CURR_MA },
	[CHG_SDP]      = { .supplier = CHARGE_SUPPLIER_BC12_SDP,
			   .current_limit = 500 },
#if defined(CONFIG_CHARGE_RAMP_SW) || defined(CONFIG_CHARGE_RAMP_HW)
	[CHG_DCP]      = { .supplier = CHARGE_SUPPLIER_BC12_DCP,
			   .current_limit = USB_CHARGER_MAX_CURR_MA },
#else
	[CHG_DCP]      = { .supplier = CHARGE_SUPPLIER_BC12_DCP,
			   .current_limit = 500 },
#endif
};

#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

#define GPIO_ACOK_OD_PATH DT_PATH(named_gpios, acok_od)
#define GPIO_ACOK_OD_PORT DT_GPIO_PIN(GPIO_ACOK_OD_PATH, gpios)

static void test_bc12_pi3usb9201_host_mode(void)
{
	struct i2c_emul *emul = pi3usb9201_emul_get(PI3USB9201_ORD);
	uint8_t a, b;

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to DFP.
	 */
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_DR_DFP);
	msleep(1);
	/*
	 * Expect the pi3usb9201 driver to configure CDP host mode and unmask
	 * interrupts.
	 */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_CDP_HOST_MODE << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	zassert_equal(a, b, NULL);

	/* Pretend that a device has been plugged in. */
	msleep(500);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_HOST_STS,
				PI3USB9201_REG_HOST_STS_DEV_PLUG);
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
	msleep(1);
	/* Expect the pi3usb9201 driver to configure SDP host mode. */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_SDP_HOST_MODE << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	zassert_equal(a, b, NULL);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_HOST_STS, 0);

	/* Pretend that a device has been unplugged. */
	msleep(500);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_HOST_STS,
				PI3USB9201_REG_HOST_STS_DEV_UNPLUG);
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
	msleep(1);
	/* Expect the pi3usb9201 driver to configure CDP host mode. */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_CDP_HOST_MODE << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	zassert_equal(a, b, NULL);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_HOST_STS, 0);
}

static void test_bc12_pi3usb9201_client_mode(
			enum pi3usb9201_client_sts detect_result,
			enum charge_supplier supplier, int current_limit)
{
	struct i2c_emul *emul = pi3usb9201_emul_get(PI3USB9201_ORD);
	uint8_t a, b;
	int port, voltage;

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to UFP and decided charging from the port is allowed.
	 */
	msleep(500);
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_DR_UFP);
	charge_manager_update_dualrole(USBC_PORT_C0, CAP_DEDICATED);
	msleep(1);
	/*
	 * Expect the pi3usb9201 driver to configure client mode and start
	 * detection.
	 */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_CLIENT_MODE << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	zassert_equal(a, b, NULL);
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_2, &a);
	b = PI3USB9201_REG_CTRL_2_START_DET;
	zassert_equal(a, b, NULL);

	/* Pretend that detection completed. */
	msleep(500);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_CLIENT_STS,
				1 << detect_result);
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
	msleep(1);
	/* Expect the pi3usb9201 driver to clear the start bit. */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_2, &a);
	zassert_equal(a, 0, NULL);
	pi3usb9201_emul_set_reg(emul, PI3USB9201_REG_CLIENT_STS, 0);
	/*
	 * Expect the charge manager to select the detected BC1.2 supplier.
	 */
	port = CHARGE_PORT_NONE;
	voltage = 0;
	if (supplier != CHARGE_SUPPLIER_NONE) {
		port = USBC_PORT_C0;
		voltage = USB_CHARGER_VOLTAGE_MV;
	}
	/* Wait for the charge port to update. */
	msleep(500);
	zassert_equal(charge_manager_get_active_charge_port(),
		      port, NULL);
	zassert_equal(charge_manager_get_supplier(),
		      supplier, NULL);
	zassert_equal(charge_manager_get_charger_current(),
		      current_limit, NULL);
	zassert_equal(charge_manager_get_charger_voltage(),
		      voltage, NULL);

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to disconnected.
	 */
	msleep(500);
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_CC_OPEN);
	msleep(1);
	/*
	 * Expect the pi3usb9201 driver to configure power down mode and mask
	 * interrupts.
	 */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_POWER_DOWN << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	b |= PI3USB9201_REG_CTRL_1_INT_MASK;
	zassert_equal(a, b, NULL);
	/* Expect the charge manager to have no active supplier. */
	zassert_equal(charge_manager_get_active_charge_port(),
		      CHARGE_PORT_NONE, NULL);
	zassert_equal(charge_manager_get_supplier(),
		      CHARGE_SUPPLIER_NONE, NULL);
	zassert_equal(charge_manager_get_charger_current(), 0, NULL);
	zassert_equal(charge_manager_get_charger_voltage(), 0, NULL);
}

/*
 * PI3USB9201 is a dual-role BC1.2 charger detector/advertiser used on USB
 * ports. It can be programmed to operate in host mode or client mode through
 * I2C. When operating as a host, PI3USB9201 enables BC1.2 SDP/CDP/DCP
 * advertisement to the attached USB devices via the D+/- connection. When
 * operating as a client, PI3USB9201 starts BC1.2 detection to detect the
 * attached host type. In both host mode and client mode, the detection results
 * are reported through I2C to the controller.
 */
ZTEST_USER(bc12, test_bc12_pi3usb9201)
{
	const struct device *batt_pres_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));
	const struct device *acok_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_ACOK_OD_PATH, gpios));
	struct i2c_emul *emul = pi3usb9201_emul_get(PI3USB9201_ORD);
	uint8_t a, b;

	/* Pretend we have battery and AC so charging works normally. */
	zassert_ok(gpio_emul_input_set(batt_pres_dev,
				       GPIO_BATT_PRES_ODL_PORT, 0), NULL);
	zassert_equal(BP_YES, battery_is_present(), NULL);
	zassert_ok(gpio_emul_input_set(acok_dev, GPIO_ACOK_OD_PORT, 1), NULL);
	msleep(CONFIG_EXTPOWER_DEBOUNCE_MS + 1);
	zassert_equal(1, extpower_is_present(), NULL);

	/* Wait long enough for TCPMv2 to be idle. */
	msleep(2000);

	/*
	 * Pretend that the USB-C Port Manager (TCPMv2) has set the port data
	 * role to disconnected.
	 */
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_CC_OPEN);
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_CC_OPEN);
	msleep(1);
	/*
	 * Expect the pi3usb9201 driver to configure power down mode and mask
	 * interrupts.
	 */
	pi3usb9201_emul_get_reg(emul, PI3USB9201_REG_CTRL_1, &a);
	b = PI3USB9201_POWER_DOWN << PI3USB9201_REG_CTRL_1_MODE_SHIFT;
	b |= PI3USB9201_REG_CTRL_1_INT_MASK;
	zassert_equal(a, b, NULL);

	test_bc12_pi3usb9201_host_mode();

	for (int c = CHG_OTHER; c <= CHG_DCP; c++) {
		test_bc12_pi3usb9201_client_mode(c,
					bc12_chg_limits[c].supplier,
					bc12_chg_limits[c].current_limit);
	}
}

ZTEST_SUITE(bc12, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
