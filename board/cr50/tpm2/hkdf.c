/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dcrypto.h"

#ifdef CRYPTO_TEST_SETUP

#include "extension.h"

enum {
	TEST_RFC = 0,
};

#define MAX_OKM_BYTES 1024

static void hkdf_command_handler(void *cmd_body,
				size_t cmd_size,
				size_t *response_size)
{
	uint8_t *cmd;
	uint8_t *out;
	uint8_t op;
	size_t salt_len;
	const uint8_t *salt;
	size_t IKM_len;
	const uint8_t *IKM;
	size_t info_len;
	const uint8_t *info;
	size_t OKM_len;
	uint8_t OKM[MAX_OKM_BYTES];

	/* Command format.
	 *
	 *   WIDTH	   FIELD
	 *   1		   OP
	 *   1             MSB SALT LEN
	 *   1             LSB SALT LEN
	 *   SALT_LEN      SALT
	 *   1             MSB IKM LEN
	 *   1             LSB IKM LEN
	 *   IKM_LEN       IKM
	 *   1             MSB INFO LEN
	 *   1             LSB INFO LEN
	 *   INFO_LEN      INFO
	 *   1             MSB OKM LEN
	 *   1             LSB OKM LEN
	 */
	cmd = (uint8_t *) cmd_body;
	out = (uint8_t *) cmd_body;
	op = *cmd++;

	salt_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	salt = cmd;
	cmd += salt_len;

	IKM_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	IKM = cmd;
	cmd += IKM_len;

	info_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];
	cmd += 2;
	info = cmd;
	cmd += info_len;

	OKM_len = ((uint16_t) (cmd[0] << 8)) | cmd[1];

	if (OKM_len > MAX_OKM_BYTES) {
		*response_size = 0;
		return;
	}

	switch (op) {
	case TEST_RFC:
		if (DCRYPTO_hkdf(OKM, OKM_len, salt, salt_len,
					IKM, IKM_len, info, info_len)) {
			memcpy(out, OKM, OKM_len);
			*response_size = OKM_len;
		} else {
			*response_size = 0;
		}
		break;
	default:
		*response_size = 0;
	}
}

DECLARE_EXTENSION_COMMAND(EXTENSION_HKDF, hkdf_command_handler);

#endif   /* CRYPTO_TEST_SETUP */
