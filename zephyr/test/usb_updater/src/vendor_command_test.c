/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "common/rollback_private.h"
#include "fakes.h"
#include "queue.h"
#include "rollback.h"
#include "system.h"
#include "update_fw.h"
#include "usb-stream.h"
#include "usb_descriptor.h"

#include <zephyr/device.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

void send_error_reset(uint8_t resp_value);

static void send_vendor_command(enum update_extra_command command,
				const void *data, size_t data_size)
{
	const struct queue *rx_queue = usb_update.producer.queue;
	struct update_frame_header pdu;
	uint8_t buffer[USB_MAX_PACKET_SIZE];
	size_t total_size = sizeof(pdu) + sizeof(uint16_t) + data_size;

	pdu.block_size = sys_cpu_to_be32(total_size);
	pdu.cmd.block_digest = sys_cpu_to_be32(0);
	pdu.cmd.block_base = sys_cpu_to_be32(UPDATE_EXTRA_CMD);

	memcpy(buffer, &pdu, sizeof(pdu));
	sys_put_be16(command, buffer + sizeof(pdu));
	memcpy(buffer + sizeof(pdu) + sizeof(uint16_t), data, data_size);

	queue_add_units(rx_queue, buffer, total_size);
}

ZTEST(vendor_command, test_immediate_reset)
{
	send_vendor_command(UPDATE_EXTRA_CMD_IMMEDIATE_RESET, NULL, 0);
	zassert_equal(system_reset_fake.call_count, 1);
	zassert_equal(system_reset_fake.arg0_history[0],
		      SYSTEM_RESET_MANUALLY_TRIGGERED);
}

ZTEST(vendor_command, test_rollback_update)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	/* fail with no payload */
	send_vendor_command(UPDATE_EXTRA_CMD_INJECT_ENTROPY, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_INVALID_PARAM);

	/* test valid update */
	const uint8_t entropy[CONFIG_ROLLBACK_SECRET_SIZE] = {
		'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!',
	};
	send_vendor_command(UPDATE_EXTRA_CMD_INJECT_ENTROPY, entropy,
			    sizeof(entropy));
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_SUCCESS);

	/* SHA256(b'Init' + b'\x00' * 28 + b'Hello world!' + b'\x00' * 20) */
	const uint8_t expected[] = { 70,  196, 18,  174, 32,  154, 96,	129,
				     193, 214, 92,  142, 241, 15,  140, 214,
				     183, 32,  127, 43,	 28,  192, 149, 18,
				     104, 128, 128, 100, 247, 217, 199, 102 };
	uint8_t secret[CONFIG_ROLLBACK_SECRET_SIZE];

	rollback_get_secret(secret);
	zassert_mem_equal(secret, expected, CONFIG_ROLLBACK_SECRET_SIZE);
}

ZTEST(vendor_command, test_usb_pairing)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;
	struct pair_challenge challenge;

	/* fail with no payload */
	send_vendor_command(UPDATE_EXTRA_CMD_PAIR_CHALLENGE, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_INVALID_PARAM);

	/* test valid request */
	send_vendor_command(UPDATE_EXTRA_CMD_PAIR_CHALLENGE, &challenge,
			    sizeof(challenge));
	zassert_equal(queue_count(tx_queue), 1 + 32 + 16);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_SUCCESS);
}

int custom_touchpad_debug(const uint8_t *param, unsigned int param_size,
			  uint8_t **data, unsigned int *data_size)
{
	static uint8_t buffer[] = { 'H', 'e', 'l', 'l', 'o' };

	*data = buffer;
	*data_size = sizeof(buffer);

	return 0;
}

ZTEST(vendor_command, test_touchpad_info)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	struct touchpad_info tp_info;
	uint8_t resp;

	touchpad_get_info_fake.return_val = sizeof(struct touchpad_info);
	send_vendor_command(UPDATE_EXTRA_CMD_TOUCHPAD_INFO, NULL, 0);
	zassert_equal(queue_count(tx_queue), sizeof(tp_info));
	queue_remove_units(tx_queue, &tp_info, sizeof(tp_info));
	zassert_equal(tp_info.fw_address, CONFIG_TOUCHPAD_VIRTUAL_OFF);
	zassert_equal(tp_info.fw_size, CONFIG_TOUCHPAD_VIRTUAL_SIZE);

	touchpad_get_info_fake.return_val = 0;
	send_vendor_command(UPDATE_EXTRA_CMD_TOUCHPAD_INFO, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_ERROR);

	touchpad_get_info_fake.return_val = sizeof(struct touchpad_info);
	send_vendor_command(UPDATE_EXTRA_CMD_TOUCHPAD_INFO, " ", 1);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_INVALID_PARAM);
}

ZTEST(vendor_command, test_touchpad_debug)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	char output[5];

	touchpad_debug_fake.custom_fake = custom_touchpad_debug;
	send_vendor_command(UPDATE_EXTRA_CMD_TOUCHPAD_DEBUG, NULL, 0);
	zassert_equal(queue_count(tx_queue), 5);
	queue_remove_units(tx_queue, output, 5);
	zassert_mem_equal(output, "Hello", 5);
}

ZTEST(vendor_command, test_get_version)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	send_vendor_command(UPDATE_EXTRA_CMD_GET_VERSION_STRING, NULL, 0);
	zassert_true(queue_count(tx_queue) >= 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_SUCCESS);
}

ZTEST(vendor_command, test_invalid_command)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	send_vendor_command(99, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_INVALID_COMMAND);
}

ZTEST(vendor_command, test_jump_to_rw)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	rwsig_get_status_fake.return_val = RWSIG_UNKNOWN;
	send_vendor_command(UPDATE_EXTRA_CMD_JUMP_TO_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_ERROR);

	rwsig_get_status_fake.return_val = RWSIG_IN_PROGRESS;
	send_vendor_command(UPDATE_EXTRA_CMD_JUMP_TO_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_IN_PROGRESS);

	rwsig_get_status_fake.return_val = RWSIG_VALID;
	send_vendor_command(UPDATE_EXTRA_CMD_JUMP_TO_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_SUCCESS);

	rwsig_get_status_fake.return_val = RWSIG_INVALID;
	send_vendor_command(UPDATE_EXTRA_CMD_JUMP_TO_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_INVALID_CHECKSUM);

	rwsig_get_status_fake.return_val = RWSIG_ABORTED;
	send_vendor_command(UPDATE_EXTRA_CMD_JUMP_TO_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	zassert_equal(resp, EC_RES_ERROR);
}

ZTEST(vendor_command, test_stay_in_ro)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	send_vendor_command(UPDATE_EXTRA_CMD_STAY_IN_RO, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	/* Always success */
	zassert_equal(resp, EC_SUCCESS);
}

ZTEST(vendor_command, test_unlock_rw)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	send_vendor_command(UPDATE_EXTRA_CMD_UNLOCK_RW, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	/* Always success */
	zassert_equal(resp, EC_SUCCESS);
}

ZTEST(vendor_command, test_unlock_rollback)
{
	const struct queue *tx_queue = usb_update.consumer.queue;
	uint8_t resp;

	send_vendor_command(UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK, NULL, 0);
	zassert_equal(queue_count(tx_queue), 1);
	queue_remove_units(tx_queue, &resp, 1);
	/* Always success */
	zassert_equal(resp, EC_SUCCESS);
}

static void vendor_command_before(void *f)
{
	const struct device *flash_dev =
		DEVICE_DT_GET(DT_NODELABEL(flashcontroller0));
	size_t flash_size;
	uint8_t *flash = flash_simulator_get_memory(flash_dev, &flash_size);
	struct rollback_data initial_rollback = {
		.id = 0,
		.rollback_min_version = 0,
		.secret = { 'I', 'n', 'i', 't' },
		.cookie = CROS_EC_ROLLBACK_COOKIE,
	};

	/* reset rollback region */
	memset(flash + CONFIG_ROLLBACK_OFF, 0, CONFIG_FLASH_ERASE_SIZE * 2);
	memcpy(flash + CONFIG_ROLLBACK_OFF, &initial_rollback,
	       sizeof(initial_rollback));
	memcpy(flash + CONFIG_ROLLBACK_OFF + CONFIG_FLASH_ERASE_SIZE,
	       &initial_rollback, sizeof(initial_rollback));

	/* reset the usb_updater's internal state */
	send_error_reset(0);

	/* clear RX/TX queue */
	queue_init(usb_update.consumer.queue);
	queue_init(usb_update.producer.queue);

	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
}

ZTEST_SUITE(vendor_command, NULL, NULL, vendor_command_before, NULL, NULL);
