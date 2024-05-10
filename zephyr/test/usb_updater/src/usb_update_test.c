/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fakes.h"
#include "queue.h"
#include "update_fw.h"
#include "usb-stream.h"
#include "usb_descriptor.h"

#include <zephyr/device.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/fff.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

void send_error_reset(uint8_t resp_value);

static void send_pdu(size_t payload_size, uint32_t digest, uint32_t base)
{
	const struct queue *rx_queue = usb_update.producer.queue;
	struct update_frame_header pdu;

	pdu.block_size = sys_cpu_to_be32(sizeof(pdu) + payload_size);
	pdu.cmd.block_digest = sys_cpu_to_be32(digest);
	pdu.cmd.block_base = sys_cpu_to_be32(base);
	queue_add_units(rx_queue, &pdu, sizeof(pdu));
}

ZTEST(usb_update, test_rw_update)
{
	const struct queue *rx_queue = usb_update.producer.queue;
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t rx_buf[CONFIG_UPDATE_PDU_SIZE];
	const struct device *flash_dev =
		DEVICE_DT_GET(DT_NODELABEL(flashcontroller0));
	size_t flash_size;
	uint8_t *flash = flash_simulator_get_memory(flash_dev, &flash_size);
	uint8_t resp;
	struct first_response_pdu first_response_pdu;
	uint32_t update_done = sys_cpu_to_be32(UPDATE_DONE);

	/* send first pdu, expect receive EC_SUCCESS */
	send_pdu(0, 0, 0);
	zassert_equal(queue_count(tx_queue), sizeof(first_response_pdu));
	queue_remove_units(tx_queue, &first_response_pdu,
			   sizeof(first_response_pdu));
	zassert_equal(first_response_pdu.return_value, 0);

	/* send block start */
	send_pdu(sizeof(rx_buf), 0, CONFIG_RW_MEM_OFF);

	/* send random bytes to the flash */
	sys_rand_get(rx_buf, sizeof(rx_buf));
	for (int offset = 0; offset < sizeof(rx_buf);
	     offset += USB_MAX_PACKET_SIZE) {
		int chunk_size =
			MIN(USB_MAX_PACKET_SIZE, sizeof(rx_buf) - offset);

		queue_add_units(rx_queue, rx_buf + offset, chunk_size);
	}
	zassert_equal(queue_count(tx_queue), 1);
	zassert_equal(queue_remove_unit(tx_queue, &resp), 1);
	zassert_equal(resp, 0);
	zassert_mem_equal(flash + CONFIG_RW_MEM_OFF, rx_buf, sizeof(rx_buf));

	/* send UPDATE_DONE, expect EC_SUCCESS */
	queue_add_units(rx_queue, &update_done, sizeof(update_done));
	zassert_equal(queue_count(tx_queue), 1);
	zassert_equal(queue_remove_unit(tx_queue, &resp), 1);
	zassert_equal(resp, 0);
}

ZTEST(usb_update, test_bad_update_start)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	/* send bad first pdu, expect UPDATE_GEN_ERROR */
	send_pdu(0, 1234, 5678);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, UPDATE_GEN_ERROR);
}

ZTEST(usb_update, test_bad_block_start)
{
	const struct queue *rx_queue = usb_update.producer.queue;
	const struct queue *tx_queue = usb_update.consumer.queue;
	struct first_response_pdu first_response_pdu;
	uint8_t resp;

	/* send first pdu */
	send_pdu(0, 0, 0);
	zassert_equal(queue_count(tx_queue), sizeof(first_response_pdu));
	queue_remove_units(tx_queue, &first_response_pdu,
			   sizeof(first_response_pdu));
	zassert_equal(first_response_pdu.return_value, 0);

	/* expect UPDATE_GEN_ERROR if payload size = 0 */
	send_pdu(0, 0, CONFIG_RW_MEM_OFF);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, UPDATE_GEN_ERROR);

	/* send first pdu */
	send_pdu(0, 0, 0);
	zassert_equal(queue_count(tx_queue), sizeof(first_response_pdu));
	queue_remove_units(tx_queue, &first_response_pdu,
			   sizeof(first_response_pdu));
	zassert_equal(first_response_pdu.return_value, 0);

	/* expect UPDATE_GEN_ERROR if next message is not an update_frame_header
	 */
	queue_add_units(rx_queue, (uint8_t[]){ 1, 2, 3 }, 3);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, UPDATE_GEN_ERROR);
}

ZTEST(usb_update, test_bad_block)
{
	const struct queue *rx_queue = usb_update.producer.queue;
	const struct queue *tx_queue = usb_update.consumer.queue;
	struct first_response_pdu first_response_pdu;
	uint8_t resp;

	/* send first pdu */
	send_pdu(0, 0, 0);
	zassert_equal(queue_count(tx_queue), sizeof(first_response_pdu));
	queue_remove_units(tx_queue, &first_response_pdu,
			   sizeof(first_response_pdu));
	zassert_equal(first_response_pdu.return_value, 0);

	/* send block start */
	send_pdu(64, 0, CONFIG_RW_MEM_OFF);

	/* expect UPDATE_GEN_ERROR if we send a small block */
	queue_add_units(rx_queue, (uint8_t[]){ 1, 2, 3 }, 3);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, UPDATE_GEN_ERROR);
}

static void usb_update_before(void *f)
{
	/* reset the usb_updater's internal state */
	send_error_reset(0);

	/* clear RX/TX queue */
	queue_init(usb_update.consumer.queue);
	queue_init(usb_update.producer.queue);

	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
	system_get_image_copy_fake.return_val = EC_IMAGE_RO;
}

ZTEST_SUITE(usb_update, NULL, NULL, usb_update_before, NULL, NULL);
