/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "driver/cec/it83xx.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

/* From chip/it83xx/intc.h, but that file has inline assembly. */
void cec_interrupt(void);

#define TEST_PORT 0

#define CEC_EVENT_BTE BIT(0)
#define CEC_EVENT_DBD BIT(4)
#define CEC_EVENT_HDRCV BIT(5)

struct mock_it83xx_cec_regs mock_it83xx_cec_regs;

static void cec_it83xx_after(void *fixture)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;

	/* Disable CEC after each test to reset driver state */
	drv->set_enable(TEST_PORT, 0);
}

ZTEST_USER(cec_it83xx, test_set_get_logical_addr)
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
	zassert_equal(logical_addr, CEC_UNREGISTERED_ADDR);
}

ZTEST_USER(cec_it83xx, test_set_get_enable)
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

ZTEST_USER(cec_it83xx, test_send_when_disabled)
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

ZTEST_USER(cec_it83xx, test_send_multiple)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	int ret;

	drv->set_enable(TEST_PORT, 1);

	/* Start sending a message */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/* Try to send another message, check the driver returns an error */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_ERROR_BUSY);
}

ZTEST_USER(cec_it83xx, test_send_success)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Start sending */
	drv->set_enable(TEST_PORT, 1);
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);

	/* Wait for the free time to elapse and first byte to be sent */
	k_sleep(K_SECONDS(1));

	/* Set ACK bit (not broadcast, so 0 means ACK bit set) */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;

	/* Set DBD interrupt */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();

	/* Wait for the second byte to be sent */
	k_sleep(K_SECONDS(1));

	/* Set ACK bit */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;

	/* Check there are no MKBP events yet */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Set DBD interrupt */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();

	/* Transfer is complete so driver will set CEC_TASK_EVENT_OKAY */
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));

	/* Check there are no more events */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_send_postponed)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t rx_msg[] = { 0x04, 0x8f };
	const uint8_t rx_msg_len = ARRAY_SIZE(rx_msg);
	const uint8_t tx_msg[] = { 0x40, 0x04 };
	const uint8_t tx_msg_len = ARRAY_SIZE(tx_msg);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Receive the first byte of a message */
	mock_it83xx_cec_regs.cecrh = rx_msg[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Send a message. The driver should queue it but keep receiving */
	ret = drv->send(TEST_PORT, tx_msg, tx_msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/* Receive the second byte of the rx message */
	mock_it83xx_cec_regs.cecdr = rx_msg[1];
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/*
	 * Receive complete. Check the HAVE_DATA event is set, send a read
	 * command and check the response contains the correct message.
	 */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, rx_msg_len);
	zassert_ok(memcmp(response.msg, rx_msg, rx_msg_len));

	/* When the receive finishes, the driver starts transmitting */

	/* DBD interrupt for first tx byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for second tx byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_send_retransmit_success)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Start sending */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/* Set ACK bit to 1 (not acked) and trigger DBD interrupt */
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Not acked, so driver starts retransmission */

	/* First byte transmitted successfully */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Second byte transmitted successfully */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_send_max_retransmissions)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Start sending */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/* First byte is not ACKed CEC_MAX_RESENDS + 1 times */
	for (int i = 0; i < CEC_MAX_RESENDS + 1; i++) {
		mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_AB;
		mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
		cec_interrupt();
		k_sleep(K_SECONDS(1));
	}

	/* Check SEND_FAILED MKBP event is sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_FAILED));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_send_broadcast)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x4f, 0x85 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Start sending */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/*
	 * Set ACK bit and trigger DBD interrupt. Since it's a broadcast
	 * message, ACK bit 1 means no follower NACKed it
	 */
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Repeat for second byte */
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_receive_success)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x04, 0x8f };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Set CECRH to the first byte and clear the EOM bit */
	mock_it83xx_cec_regs.cecrh = msg[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;

	/* Set HDRCV interrupt and wait for the byte to be received */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Set CECDR to the second byte and set the EOM bit */
	mock_it83xx_cec_regs.cecdr = msg[1];
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_EB;

	/* Set DBD interrupt and wait for the byte to be received */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/*
	 * Message complete, so driver will set CEC_TASK_EVENT_RECEIVED_DATA and
	 * CEC task will send MKBP event.
	 */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));

	/* Send read command and check response contains the correct message */
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, msg_len);
	zassert_ok(memcmp(response.msg, msg, msg_len));
}

ZTEST_USER(cec_it83xx, test_receive_not_destined_to_us)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg1[] = { 0x05, 0x8f };
	const uint8_t msg2[] = { 0x04, 0x8f };
	const uint8_t msg2_len = ARRAY_SIZE(msg2);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/*
	 * Receive first byte of message not destined to us. Driver will ignore
	 * it and stay in the idle state.
	 */
	mock_it83xx_cec_regs.cecrh = msg1[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/*
	 * For messages not destined to us, hardware does not send DBD
	 * interrupts for data bytes.
	 */

	/* Check driver did not send HAVE_DATA event */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Receive first byte of message destined to us. */
	mock_it83xx_cec_regs.cecrh = msg2[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Receive second byte of message destined to us. */
	mock_it83xx_cec_regs.cecdr = msg2[1];
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check HAVE_DATA event is sent. */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Send read command and check response contains the correct message */
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, msg2_len);
	zassert_ok(memcmp(response.msg, msg2, msg2_len));
}

ZTEST_USER(cec_it83xx, test_receive_during_free_time)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t rx_msg[] = { 0x04, 0x8f };
	const uint8_t rx_msg_len = ARRAY_SIZE(rx_msg);
	const uint8_t tx_msg[] = { 0x40, 0x04 };
	const uint8_t tx_msg_len = ARRAY_SIZE(tx_msg);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/*
	 * Start sending a message and wait for free time to start but not
	 * complete. Free time is 9.6 ms, so wait for 1 ms.
	 */
	ret = drv->send(TEST_PORT, tx_msg, tx_msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_MSEC(1));

	/*
	 * Receive the first byte of a message. Driver will abort the free time
	 * and start receiving instead.
	 */
	mock_it83xx_cec_regs.cecrh = rx_msg[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Receive the second byte of the message */
	mock_it83xx_cec_regs.cecdr = rx_msg[1];
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/*
	 * Receive complete. Check the HAVE_DATA event is set, send a read
	 * command and check the response contains the correct message.
	 */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, rx_msg_len);
	zassert_ok(memcmp(response.msg, rx_msg, rx_msg_len));

	/* When the receive finished, the driver restarted sending */

	/* DBD interrupt for first tx byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for second tx byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_receive_unavailable)
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

ZTEST_USER(cec_it83xx, test_error_during_free_time)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/*
	 * Start sending a message and wait for free time to start but not
	 * complete. Free time is 9.6 ms, so wait for 1 ms.
	 */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_MSEC(1));

	/* Error on the CEC bus. Driver will restart free time. */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_BTE;
	cec_interrupt();

	/* Wait for free time to complete and first byte to be sent */
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for first byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for second byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_error_while_sending)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg[] = { 0x40, 0x04 };
	const uint8_t msg_len = ARRAY_SIZE(msg);
	struct ec_response_get_next_event_v1 event;
	int ret;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Start sending */
	ret = drv->send(TEST_PORT, msg, msg_len);
	zassert_equal(ret, EC_SUCCESS);
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for first byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Error on the CEC bus. Driver will restart transmission. */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_BTE;
	cec_interrupt();

	/* Wait for free time to complete and first byte to be send again. */
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for first byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* DBD interrupt for second byte */
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_AB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check SEND_OK MKBP event was sent */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_SEND_OK));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);
}

ZTEST_USER(cec_it83xx, test_error_while_receiving)
{
	const struct cec_drv *drv = cec_config[TEST_PORT].drv;
	const uint8_t msg1[] = { 0x04, 0x8f };
	const uint8_t msg2[] = { 0x04, 0x46 };
	const uint8_t msg2_len = ARRAY_SIZE(msg2);
	struct ec_response_get_next_event_v1 event;
	struct ec_response_cec_read response;

	/* Enable CEC and set logical address */
	drv->set_enable(TEST_PORT, 1);
	drv->set_logical_addr(TEST_PORT, 0x4);

	/* Receive first byte of msg1 */
	mock_it83xx_cec_regs.cecrh = msg1[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Error on the CEC bus. Driver will abort this receive. */
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_BTE;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check driver did not send HAVE_DATA event */
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Receive first byte of msg2 */
	mock_it83xx_cec_regs.cecrh = msg2[0];
	mock_it83xx_cec_regs.cecopsts &= ~IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_HDRCV;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Receive second byte of msg2 */
	mock_it83xx_cec_regs.cecdr = msg2[1];
	mock_it83xx_cec_regs.cecopsts |= IT83XX_CEC_CECOPSTS_EB;
	mock_it83xx_cec_regs.cecsts = CEC_EVENT_DBD;
	cec_interrupt();
	k_sleep(K_SECONDS(1));

	/* Check HAVE_DATA event is sent. */
	zassert_ok(get_next_cec_mkbp_event(&event));
	zassert_true(
		cec_event_matches(&event, TEST_PORT, EC_MKBP_CEC_HAVE_DATA));
	zassert_not_equal(get_next_cec_mkbp_event(&event), 0);

	/* Send read command and check response contains msg2 */
	zassert_ok(host_cmd_cec_read(TEST_PORT, &response));
	zassert_equal(response.msg_len, msg2_len);
	zassert_ok(memcmp(response.msg, msg2, msg2_len));
}

ZTEST_SUITE(cec_it83xx, drivers_predicate_post_main, NULL, NULL,
	    cec_it83xx_after, NULL);
