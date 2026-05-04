/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "fakes.h"
#include "sha256.h"
#include "update_fw.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

extern uint8_t touchpad_fw_hashes[][SHA256_DIGEST_SIZE];

static uint8_t send_update_command(uint32_t block_base, void *body,
				   size_t body_size, size_t *response_size)
{
	struct {
		struct update_command cmd;
		uint8_t body[CONFIG_UPDATE_PDU_SIZE];
	} __packed request = { .body = {} };
	/* Cache the address for code clarity. */
	uint8_t *error_code = (uint8_t *)&request;

	request.cmd.block_digest = 0;
	request.cmd.block_base = sys_cpu_to_be32(block_base);
	if (body) {
		memcpy(request.body, body, body_size);
	}
	fw_update_command_handler(&request,
				  sizeof(struct update_command) + body_size,
				  response_size);

	return *error_code;
}

ZTEST(update_fw, test_ro_write_ro)
{
	size_t response_size;
	uint8_t error_code;

	system_get_image_copy_fake.return_val = EC_IMAGE_RO;
	send_update_command(0, NULL, 0, &response_size);
	zassert_equal(response_size, sizeof(struct first_response_pdu));

	error_code = send_update_command(CONFIG_RO_MEM_OFF, NULL,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_BAD_ADDR);
}

ZTEST(update_fw, test_rw_write_rw)
{
	size_t response_size;
	uint8_t error_code;

	system_get_image_copy_fake.return_val = EC_IMAGE_RW;
	send_update_command(0, NULL, 0, &response_size);
	zassert_equal(response_size, sizeof(struct first_response_pdu));

	error_code = send_update_command(CONFIG_RW_MEM_OFF, NULL,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_BAD_ADDR);
}

ZTEST(update_fw, test_touchpad_update)
{
	size_t response_size;
	uint8_t error_code;
	uint8_t tp_chunk[CONFIG_UPDATE_PDU_SIZE] = { 't', 'o', 'u', 'c',
						     'h', 'p', 'a', 'd' };

	error_code = send_update_command(CONFIG_TOUCHPAD_VIRTUAL_OFF, tp_chunk,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_SUCCESS);
	zassert_equal(touchpad_update_write_fake.call_count, 1);
	/* offset == 0 */
	zassert_equal(touchpad_update_write_fake.arg0_history[0], 0);
	/* size == 1024 */
	zassert_equal(touchpad_update_write_fake.arg1_history[0],
		      CONFIG_UPDATE_PDU_SIZE);
}

ZTEST(update_fw, test_touchpad_write_fail)
{
	size_t response_size;
	uint8_t error_code;
	uint8_t tp_chunk[CONFIG_UPDATE_PDU_SIZE] = { 't', 'o', 'u', 'c',
						     'h', 'p', 'a', 'd' };

	touchpad_update_write_fake.return_val = 1;
	error_code = send_update_command(CONFIG_TOUCHPAD_VIRTUAL_OFF, tp_chunk,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_WRITE_FAILURE);
	zassert_equal(touchpad_update_write_fake.call_count, 1);
	/* offset == 0 */
	zassert_equal(touchpad_update_write_fake.arg0_history[0], 0);
	/* size == 1024 */
	zassert_equal(touchpad_update_write_fake.arg1_history[0],
		      CONFIG_UPDATE_PDU_SIZE);
}

ZTEST(update_fw, test_bad_touchpad_chunk)
{
	size_t response_size;
	uint8_t error_code;

	/* fail if block_offset is not multiple of CONFIG_UPDATE_PDU_SIZE */
	error_code = send_update_command(CONFIG_TOUCHPAD_VIRTUAL_OFF + 5, NULL,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_ROLLBACK_ERROR);
}

ZTEST(update_fw, test_bad_touchpad_hash)
{
	size_t response_size;
	uint8_t error_code;
	uint8_t tp_chunk[CONFIG_UPDATE_PDU_SIZE] = {};

	error_code = send_update_command(CONFIG_TOUCHPAD_VIRTUAL_OFF, tp_chunk,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(error_code, UPDATE_ROLLBACK_ERROR);
}

ZTEST(update_fw, test_bad_command_size)
{
	uint8_t request[1];
	size_t response_size;
	/* Cache the address for code clarity. */
	uint8_t *error_code = &request[0];

	/* fall if request size < sizeof(struct update_command) */
	fw_update_command_handler(request, 1, &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(*error_code, UPDATE_GEN_ERROR);
}

ZTEST(update_fw, test_rwsig_busy_start)
{
	size_t response_size;

	rwsig_get_status_fake.return_val = RWSIG_IN_PROGRESS;
	system_get_image_copy_fake.return_val = EC_IMAGE_RO;

	/* Send connection establishment PDU.
	 * fw_update_command_handler writes the response into the same buffer.
	 */
	uint8_t buffer[sizeof(struct first_response_pdu) +
		       sizeof(struct update_command)];
	struct update_command *cmd = (struct update_command *)buffer;

	cmd->block_base = 0;
	fw_update_command_handler(buffer, sizeof(struct update_command),
				  &response_size);

	struct first_response_pdu *resp = (struct first_response_pdu *)buffer;

	zassert_equal(sys_be32_to_cpu(resp->return_value), UPDATE_RWSIG_BUSY);
}

ZTEST(update_fw, test_rwsig_busy_erase)
{
	size_t response_size;
	uint8_t error_code;

	system_get_image_copy_fake.return_val = EC_IMAGE_RO;
	/* Initialize update_section */
	send_update_command(0, NULL, 0, &response_size);

	rwsig_get_status_fake.return_val = RWSIG_IN_PROGRESS;

	/* First chunk of RW section should trigger erase check */
	error_code = send_update_command(CONFIG_RW_MEM_OFF, NULL,
					 CONFIG_UPDATE_PDU_SIZE,
					 &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_RWSIG_BUSY);
}

ZTEST(update_fw, test_rwsig_busy_write)
{
	size_t response_size;
	uint8_t error_code;

	system_get_image_copy_fake.return_val = EC_IMAGE_RO;
	/* Initialize update_section */
	send_update_command(0, NULL, 0, &response_size);

	rwsig_get_status_fake.return_val = RWSIG_IN_PROGRESS;

	/* Non-first chunk to pass erase check */
	error_code = send_update_command(
		CONFIG_RW_MEM_OFF + CONFIG_UPDATE_PDU_SIZE, NULL,
		CONFIG_UPDATE_PDU_SIZE, &response_size);
	zassert_equal(response_size, 1);
	zassert_equal(error_code, UPDATE_RWSIG_BUSY);
}

static void *update_fw_setup(void)
{
	/* sha256("touchpad" + "\x00" * 1016) */
	uint8_t checksum[SHA256_DIGEST_SIZE] = {
		204, 146, 218, 243, 125, 152, 204, 56,	6,   218, 250,
		95,  15,  191, 36,  231, 220, 116, 253, 136, 76,  37,
		201, 229, 236, 101, 143, 168, 45,  105, 48,  234
	};

	memcpy(touchpad_fw_hashes[0], checksum, SHA256_DIGEST_SIZE);

	system_get_version_fake.return_val = "fake-version-str";

	return NULL;
}

static void update_fw_before(void *f)
{
	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
	rwsig_get_status_fake.return_val = RWSIG_ABORTED;
}

ZTEST_SUITE(update_fw, NULL, update_fw_setup, update_fw_before, NULL, NULL);
