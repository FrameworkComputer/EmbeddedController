/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "gpio_signal.h"
#include "keyboard_config.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_hid_touchpad.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

FAKE_VALUE_FUNC(int, mkbp_keyboard_add, const uint8_t *);

ZTEST(one_wire_uart_tablet, test_keyboard_event)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = KEYBOARD_COLS_MAX + 1;
	msg.payload[0] = ROACH_CMD_KEYBOARD_MATRIX;
	msg.header.sender = 1;
	msg.header.msg_id = 1;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + msg.header.payload_len);

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(mkbp_keyboard_add_fake.call_count, 1);
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 1);
}

ZTEST(one_wire_uart_tablet, test_touchpad_event)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);

	memset(&msg, 0, sizeof(msg));
	msg.header.magic = 0xEC;
	msg.header.payload_len = sizeof(struct usb_hid_touchpad_report) + 1;
	msg.payload[0] = ROACH_CMD_TOUCHPAD_REPORT;
	msg.header.sender = 1;
	msg.header.msg_id = 1;
	msg.header.checksum = checksum(&msg);
	ring_buf_put(data->rx_ring_buf, (uint8_t *)&msg,
		     sizeof(msg.header) + msg.header.payload_len);

	process_rx_fifo(dev);
	process_packet();

	zassert_equal(mkbp_keyboard_add_fake.call_count, 0);
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 0);
}

ZTEST(one_wire_uart_tablet, test_ap_power_state)
{
	struct one_wire_uart_data *data = dev->data;
	struct one_wire_uart_message msg;

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_equal(k_msgq_num_used_get(data->tx_queue), 1);
	zassert_ok(k_msgq_get(data->tx_queue, &msg, K_NO_WAIT));
	zassert_equal(msg.payload[0], ROACH_CMD_SUSPEND);

	ap_power_ev_send_callbacks(AP_POWER_STARTUP);
	zassert_equal(k_msgq_num_used_get(data->tx_queue), 1);
	zassert_ok(k_msgq_get(data->tx_queue, &msg, K_NO_WAIT));
	zassert_equal(msg.payload[0], ROACH_CMD_RESUME);
}

static void tablet_before(void *fixture)
{
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);

	one_wire_uart_reset(dev);

	RESET_FAKE(mkbp_keyboard_add);
	gpio_pin_set_dt(hid_irq, 0);
}

ZTEST_SUITE(one_wire_uart_tablet, drivers_predicate_post_main, NULL,
	    tablet_before, NULL, NULL);
