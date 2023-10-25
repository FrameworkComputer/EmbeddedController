/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "driver/cec/bitbang.h"
#include "driver/cec/it83xx.h"
#include "gpio.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <zephyr/ztest.h>

#define CEC_GPIO_PORT(name) \
	DEVICE_DT_GET(DT_GPIO_CTLR(NAMED_GPIOS_GPIO_NODE(name), gpios))
#define CEC_GPIO_PIN(name) DT_GPIO_PIN(NAMED_GPIOS_GPIO_NODE(name), gpios)
#define CEC_GPIO_SIGNAL(name) GPIO_SIGNAL(DT_NODELABEL(name))

#define CEC_OUT_PORT CEC_GPIO_PORT(gpio_hdmi_cec_out)
#define CEC_OUT_PIN CEC_GPIO_PIN(gpio_hdmi_cec_out)
#define CEC_OUT_SIGNAL CEC_GPIO_SIGNAL(gpio_hdmi_cec_out)
#define CEC_IN_PORT CEC_GPIO_PORT(gpio_hdmi_cec_in)
#define CEC_IN_PIN CEC_GPIO_PIN(gpio_hdmi_cec_in)
#define CEC_IN_SIGNAL CEC_GPIO_SIGNAL(gpio_hdmi_cec_in)
#define CEC_PULL_UP_PORT CEC_GPIO_PORT(gpio_hdmi_cec_pull_up)
#define CEC_PULL_UP_PIN CEC_GPIO_PIN(gpio_hdmi_cec_pull_up)
#define CEC_PULL_UP_SIGNAL CEC_GPIO_SIGNAL(gpio_hdmi_cec_pull_up)

#define TEST_PORT 1

#define CEC_STATE_INITIATOR_ACK_LOW 13

struct mock_it83xx_cec_regs mock_it83xx_cec_regs;

/* Timestamp when the timer was last started */
static timestamp_t start_time;

/* The capture edge we're waiting for */
static enum cec_cap_edge expected_cap_edge;

/* Whether we should mock the ACK bit from the follower when sending */
static bool mock_ack;

/* Mock a rising/falling edge on the CEC bus */
static void edge_received_f(enum cec_cap_edge edge, int line)
{
	if (edge == CEC_CAP_EDGE_NONE || edge != expected_cap_edge)
		zassert_unreachable("Unexpected edge %d, line %d", edge, line);

	cec_event_cap(TEST_PORT);
}
#define edge_received(edge) edge_received_f(edge, __LINE__)

/*
 * Main timer for used sending/receiving CEC messages. Used in a similar way to
 * the HW timer when running on hardware.
 */
static void timer_expired(struct k_timer *unused)
{
	cec_event_timeout(TEST_PORT);
}
K_TIMER_DEFINE(timer, timer_expired, NULL);

/*
 * Timer used to mock ACK bits from the follower. Started at the start of the
 * ACK bit and expires when the ACK bit low time is complete.
 */
static void ack_low_time_complete(struct k_timer *unused)
{
	gpio_set_level(CEC_OUT_SIGNAL, 1);
}
K_TIMER_DEFINE(ack_timer, ack_low_time_complete, NULL);

/*
 * Mocks of CEC timer functions which are usually provided by the chip. We mock
 * their behaviour using a software timer.
 */
void cec_tmr_cap_start(int port, enum cec_cap_edge edge, int timeout)
{
	expected_cap_edge = edge;

	if (timeout > 0) {
		start_time = get_time();
		k_timer_start(&timer, K_USEC(timeout), K_NO_WAIT);
	}

	if (cec_get_state(TEST_PORT) == CEC_STATE_INITIATOR_ACK_LOW &&
	    mock_ack) {
		/*
		 * If we're sending, mock the ACK bit from the follower if
		 * requested. Pull the gpio low at the start of the ACK bit, and
		 * release it after 0-bit low time.
		 */
		gpio_set_level(CEC_OUT_SIGNAL, 0);
		k_timer_start(&ack_timer, K_USEC(CEC_DATA_ZERO_LOW_US),
			      K_NO_WAIT);
	}
}

int cec_tmr_cap_get(int port)
{
	return get_time().val - start_time.val;
}

void cec_trigger_send(int port)
{
	/* Trigger tx event directly */
	cec_event_tx(port);
}

static void cec_bitbang_setup(void *fixture)
{
	/*
	 * Workaround for a limitation in gpio_emul. Currently if a pin is
	 * configured as input + output, the output-wiring callbacks will not be
	 * fired. However if it also has an interrupt configured, callbacks will
	 * be fired.
	 * TODO(b/309361422): Remove this once gpio_emul is fixed.
	 */
	zassert_ok(gpio_pin_interrupt_configure(CEC_OUT_PORT, CEC_OUT_PIN,
						GPIO_INT_EDGE_BOTH));
}

static void cec_bitbang_before(void *fixture)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;

	/* Disable CEC between each test to reset driver state */
	drv->set_enable(TEST_PORT, 0);

	/* Reset globals */
	start_time.val = 0;
	expected_cap_edge = CEC_CAP_EDGE_NONE;
	mock_ack = false;
}

ZTEST_USER(cec_bitbang, test_set_get_logical_addr)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	uint8_t logical_addr;

	drv->set_logical_addr(TEST_PORT, 0x4);
	drv->get_logical_addr(TEST_PORT, &logical_addr);
	zassert_equal(logical_addr, 0x4);

	drv->set_logical_addr(TEST_PORT, CEC_UNREGISTERED_ADDR);
	drv->get_logical_addr(TEST_PORT, &logical_addr);
	zassert_equal(logical_addr, CEC_UNREGISTERED_ADDR);

	drv->set_logical_addr(TEST_PORT, CEC_INVALID_ADDR);
	drv->get_logical_addr(TEST_PORT, &logical_addr);
	zassert_equal(logical_addr, CEC_INVALID_ADDR);
}

ZTEST_USER(cec_bitbang, test_set_get_enable)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	uint8_t enable;

	drv->set_enable(TEST_PORT, 1);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 1);

	drv->set_enable(TEST_PORT, 0);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 0);

	/* Enabling when enabled */
	drv->set_enable(TEST_PORT, 1);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 1);
	drv->set_enable(TEST_PORT, 1);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 1);

	/* Disabling when disabled */
	drv->set_enable(TEST_PORT, 0);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 0);
	drv->set_enable(TEST_PORT, 0);
	drv->get_enable(TEST_PORT, &enable);
	zassert_equal(enable, 0);
}

ZTEST_USER(cec_bitbang, test_send_when_disabled)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	int ret;

	/* Sending when disabled returns an error */
	drv->set_enable(TEST_PORT, 0);
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_ERROR_BUSY);
}

ZTEST_USER(cec_bitbang, test_send_multiple)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	int ret;

	drv->set_enable(TEST_PORT, 1);

	/* Start sending a message */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_MSEC(10));

	/* Try to send another message, check the driver returns an error */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_ERROR_BUSY);
}

/*
 * When sending, we record the GPIO transitions generated by the driver using a
 * GPIO callback. When sending is complete, we check that the recording has
 * the correct timing for the message we were sending.
 */
#define MAX_GPIO_RECORDINGS 1024

struct gpio_state {
	/* GPIO state - 0 means low, 1 means high */
	uint32_t val;
	/* How long the GPIO was in that state */
	uint32_t duration_us;
};

static struct gpio_state gpio_recordings[MAX_GPIO_RECORDINGS];
static size_t gpio_index;

static void gpio_out_callback(const struct device *gpio,
			      struct gpio_callback *const cb,
			      gpio_port_pins_t pins)
{
	static timestamp_t previous_time;
	static int previous_val = -1;
	int val;

	val = gpio_emul_output_get(CEC_OUT_PORT, CEC_OUT_PIN);

	/*
	 * If we're currently pulling the line low to mock an ACK from the
	 * follower, don't let the driver set it high. This makes it behave
	 * like an open drain.
	 */
	if (k_timer_remaining_ticks(&ack_timer) && val) {
		gpio_set_level(CEC_OUT_SIGNAL, 0);
		return;
	}

	/* Record the gpio value if it has changed */
	if (val == previous_val)
		return;
	gpio_recordings[gpio_index].val = val;

	/* Record the duration of the previous state */
	if (gpio_index > 0)
		gpio_recordings[gpio_index - 1].duration_us =
			get_time().val - previous_time.val;

	previous_time = get_time();
	previous_val = val;
	gpio_index++;
	zassert_true(gpio_index < MAX_GPIO_RECORDINGS);
}

static void check_gpio_state(int i, int val, int duration_us)
{
	/* Allow a 100 us delta since our measurements are not perfect */
	const int delta_us = 100;

	/* Print every state to help with debugging if there are errors */
	printk("%3d %6d %3d %6d\n", gpio_recordings[i].val,
	       gpio_recordings[i].duration_us, val, duration_us);

	zassert_equal(gpio_recordings[i].val, val);
	zassert_within(gpio_recordings[i].duration_us, duration_us, delta_us);
}

static void check_gpio_recording(const uint8_t *msg, uint8_t msg_len)
{
	int i = 0;

	printk("GPIO recording:\n");

	/* Start bit */
	check_gpio_state(i++, 0, CEC_START_BIT_LOW_US);
	check_gpio_state(i++, 1, CEC_START_BIT_HIGH_US);

	for (int byte = 0; byte < msg_len; byte++) {
		/* Data bits */
		for (int bit = 7; bit >= 0; bit--) {
			if (msg[byte] & BIT(bit)) {
				/* 1 bit */
				check_gpio_state(i++, 0, CEC_DATA_ONE_LOW_US);
				check_gpio_state(i++, 1, CEC_DATA_ONE_HIGH_US);
			} else {
				/* 0 bit */
				check_gpio_state(i++, 0, CEC_DATA_ZERO_LOW_US);
				check_gpio_state(i++, 1, CEC_DATA_ZERO_HIGH_US);
			}
		}

		if (byte == msg_len - 1) {
			/* EOM is set */
			check_gpio_state(i++, 0, CEC_DATA_ONE_LOW_US);
			check_gpio_state(i++, 1, CEC_DATA_ONE_HIGH_US);
		} else {
			/* EOM is cleared */
			check_gpio_state(i++, 0, CEC_DATA_ZERO_LOW_US);
			check_gpio_state(i++, 1, CEC_DATA_ZERO_HIGH_US);
		}

		/* ACK bit is set */
		check_gpio_state(i++, 0, CEC_DATA_ZERO_LOW_US);
		if (byte < msg_len - 1)
			check_gpio_state(i++, 1, CEC_DATA_ZERO_HIGH_US);
	}
}

ZTEST_USER(cec_bitbang, test_send_success)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	static struct gpio_callback callback;
	int ret;

	/* Set up callback to record gpio state */
	gpio_init_callback(&callback, gpio_out_callback, BIT(CEC_OUT_PIN));
	gpio_add_callback(CEC_OUT_PORT, &callback);

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Start recording gpio state */
	gpio_index = 0;

	/* Mock the ACK bit from the follower */
	mock_ack = true;

	/* Start sending */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);

	/*
	 * Driver will automatically set timeouts and transition through the
	 * necessary states to send the message.
	 */
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Validate the recorded gpio state */
	check_gpio_recording(msg, msg_len);

	/* Remove the callback */
	gpio_remove_callback(CEC_OUT_PORT, &callback);
}

ZTEST_USER(cec_bitbang, test_receive_success)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x04, 0x8f };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Receive start bit */
	edge_received(CEC_CAP_EDGE_FALLING);
	k_sleep(K_USEC(CEC_START_BIT_LOW_US));
	edge_received(CEC_CAP_EDGE_RISING);
	k_sleep(K_USEC(CEC_START_BIT_HIGH_US));

	for (int byte = 0; byte < msg_len; byte++) {
		/* Receive data bits */
		for (int bit = 7; bit >= 0; bit--) {
			if (msg[byte] & BIT(bit)) {
				/* 1 bit */
				edge_received(CEC_CAP_EDGE_FALLING);
				k_sleep(K_USEC(CEC_DATA_ONE_LOW_US));
				edge_received(CEC_CAP_EDGE_RISING);
				k_sleep(K_USEC(CEC_DATA_ONE_HIGH_US));
			} else {
				/* 0 bit */
				edge_received(CEC_CAP_EDGE_FALLING);
				k_sleep(K_USEC(CEC_DATA_ZERO_LOW_US));
				edge_received(CEC_CAP_EDGE_RISING);
				k_sleep(K_USEC(CEC_DATA_ZERO_HIGH_US));
			}
		}

		if (byte == msg_len - 1) {
			/* EOM is set */
			edge_received(CEC_CAP_EDGE_FALLING);
			k_sleep(K_USEC(CEC_DATA_ONE_LOW_US));
			edge_received(CEC_CAP_EDGE_RISING);
			k_sleep(K_USEC(CEC_DATA_ONE_HIGH_US));
		} else {
			/* EOM is cleared */
			edge_received(CEC_CAP_EDGE_FALLING);
			k_sleep(K_USEC(CEC_DATA_ZERO_LOW_US));
			edge_received(CEC_CAP_EDGE_RISING);
			k_sleep(K_USEC(CEC_DATA_ZERO_HIGH_US));
		}

		/* ACK bit falling edge */
		edge_received(CEC_CAP_EDGE_FALLING);

		/*
		 * Message is destined to us, so the driver should assert the
		 * ACK bit. Wait until the safe sample time and check the GPIO
		 * is low.
		 */
		k_sleep(K_USEC(CEC_NOMINAL_SAMPLE_TIME_US));
		zassert_equal(gpio_emul_output_get(CEC_OUT_PORT, CEC_OUT_PIN),
			      0);
		k_sleep(K_USEC(CEC_NOMINAL_BIT_PERIOD_US -
			       CEC_NOMINAL_SAMPLE_TIME_US));
	}

	/*
	 * Message complete, so driver will set CEC_TASK_EVENT_RECEIVED_DATA and
	 * CEC task will send MKBP event.
	 */
	k_sleep(K_SECONDS(1));
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Send read command and check response contains the correct message */
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, msg_len);
	zassert_ok(memcmp(response.msg, msg, msg_len));
}

ZTEST_USER(cec_bitbang, test_receive_unavailable)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	uint8_t *msg;
	uint8_t msg_len;
	int ret;

	/*
	 * Try to get a received message when there isn't one, check the driver
	 * returns an error.
	 */
	ret = drv->get_received_message(TEST_PORT, &msg, &msg_len);
	zassert_equal(ret, EC_ERROR_UNAVAILABLE);
}

ZTEST_SUITE(cec_bitbang, drivers_predicate_post_main, cec_bitbang_setup,
	    cec_bitbang_before, NULL, NULL);
