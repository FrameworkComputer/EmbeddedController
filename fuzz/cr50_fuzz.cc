/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fuzzer for the TPM2 and vendor specific Cr50 commands.
 */

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#define HIDE_EC_STDLIB
#include "fuzz_config.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "persistence.h"
#include "pinweaver.h"
}

#define NVMEM_TPM_SIZE ((sizeof((struct nvmem_partition *)0)->buffer) \
  - NVMEM_CR50_SIZE)

extern "C" uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_TPM_SIZE,
	NVMEM_CR50_SIZE
};

extern "C" void rand_bytes(void *buffer, size_t len)
{
	size_t x = 0;

	for (; x < len; ++x)
		((uint8_t *)buffer)[x] = rand();
}

extern "C" void get_storage_seed(void *buf, size_t *len)
{
	memset(buf, 0x77, *len);
}

extern "C" uint8_t get_current_pcr_digest(const uint8_t bitmask[2],
					  uint8_t sha256_of_selected_pcr[32])
{
	memset(sha256_of_selected_pcr, 0, 32);
	return 0;
}

extern "C" void run_test(void)
{
}

static void assign_pw_field_from_bytes(const uint8_t *data, unsigned int size,
				       uint8_t *destination, size_t dest_size)
{
	if (size >= dest_size) {
		memcpy(destination, data, dest_size);
	} else {
		memcpy(destination, data, size);
		memset(destination + size, 0, dest_size - size);
	}
}

/* Prevent this from being stack allocated. */
static uint8_t tpm_io_buffer[PW_MAX_MESSAGE_SIZE];

extern "C" int test_fuzz_one_input(const uint8_t *data, unsigned int size)
{
	struct merkle_tree_t merkle_tree = {};
	struct pw_request_t *request = (struct pw_request_t *)tpm_io_buffer;
	struct pw_response_t *response = (struct pw_response_t *)tpm_io_buffer;

	memset(__host_flash, 0xff, sizeof(__host_flash));
	pinweaver_init();
	assign_pw_field_from_bytes(data, size, tpm_io_buffer, sizeof(tpm_io_buffer));
	pw_handle_request(&merkle_tree, request, response);
	return 0;
}
