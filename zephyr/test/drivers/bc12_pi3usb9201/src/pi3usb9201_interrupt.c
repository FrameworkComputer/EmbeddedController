/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "task.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_charge.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* Get reference to externally linked handlers (defined in DTS) */
#define USBC0_GPIO_PATH DT_PATH(named_gpios, usb_c0_bc12_int_l)
#define USBC0_GPIO_DEV DEVICE_DT_GET(DT_GPIO_CTLR(USBC0_GPIO_PATH, gpios))
#define USBC0_GPIO_PORT DT_GPIO_PIN(USBC0_GPIO_PATH, gpios)

#define USBC1_GPIO_PATH DT_PATH(named_gpios, usb_c1_bc12_int_l)
#define USBC1_GPIO_DEV DEVICE_DT_GET(DT_GPIO_CTLR(USBC1_GPIO_PATH, gpios))
#define USBC1_GPIO_PORT DT_GPIO_PIN(USBC1_GPIO_PATH, gpios)

static void toggle_gpio(const struct device *dev, gpio_pin_t pin)
{
	static const int values[] = { 1, 0, 1 };

	for (int i = 0; i < ARRAY_SIZE(values); ++i) {
		gpio_emul_input_set(dev, pin, values[i]);
	}
}

FAKE_VOID_FUNC(usb_charger_task_event, int, uint32_t);

struct pi3usb9201_fixture {
	const struct bc12_drv *drv[2];
	struct bc12_drv mock_drv;
};

static void *setup(void)
{
	static struct pi3usb9201_fixture fixture;

	fixture.mock_drv.usb_charger_task_event = usb_charger_task_event;

	return &fixture;
}

static void before(void *f)
{
	struct pi3usb9201_fixture *fixture = f;

	fixture->drv[0] = bc12_ports[0].drv;
	fixture->drv[1] = bc12_ports[1].drv;

	RESET_FAKE(usb_charger_task_event);
	test_set_chipset_to_s0();
}

static void after(void *f)
{
	struct pi3usb9201_fixture *fixture = f;

	bc12_ports[0].drv = fixture->drv[0];
	bc12_ports[1].drv = fixture->drv[1];
}

ZTEST_SUITE(pi3usb9201, drivers_predicate_post_main, setup, before, after,
	    NULL);

ZTEST_F(pi3usb9201, test_usb0_evt)
{
	/* Set up the driver to use the mock */
	bc12_ports[0].drv = &fixture->mock_drv;

	/* Trigger the event and verify that port0 was added to the task event
	 * bitmap
	 */
	toggle_gpio(USBC0_GPIO_DEV, USBC0_GPIO_PORT);
	zassert_true(*task_get_event_bitmap(TASK_ID_USB_CHG) & BIT(0));

	/* Give the task a bit of time to process the events */
	task_wake(TASK_ID_USB_CHG);
	k_msleep(500);

	/* Ensure that the callback was made (it should be the first, but others
	 * may exist).
	 */
	zassert_true(usb_charger_task_event_fake.call_count > 0);
	zassert_equal(0, usb_charger_task_event_fake.arg0_history[0]);
	zassert_equal(USB_CHG_EVENT_BC12,
		      usb_charger_task_event_fake.arg1_history[0]);
}

ZTEST_F(pi3usb9201, test_usb1_evt)
{
	/* Set up the driver to use the mock */
	bc12_ports[1].drv = &fixture->mock_drv;

	/* Trigger the event and verify that port1 was added to the task event
	 * bitmap
	 */
	toggle_gpio(USBC1_GPIO_DEV, USBC1_GPIO_PORT);
	zassert_true(*task_get_event_bitmap(TASK_ID_USB_CHG) & BIT(1));

	/* Give the task a bit of time to process the events */
	task_wake(TASK_ID_USB_CHG);
	k_msleep(500);

	/* Ensure that the callback was made (it should be the first, but others
	 * may exist).
	 */
	zassert_true(usb_charger_task_event_fake.call_count > 0);
	zassert_equal(1, usb_charger_task_event_fake.arg0_history[0]);
	zassert_equal(USB_CHG_EVENT_BC12,
		      usb_charger_task_event_fake.arg1_history[0]);
}
