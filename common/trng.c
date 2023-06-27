/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common Random Number Generation (RNG) routines */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "host_command.h"
#include "panic.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"

test_mockable void trng_rand_bytes(void *buffer, size_t len)
{
	while (len) {
		uint32_t number = trng_rand();
		size_t cnt = 4;
		/* deal with the lack of alignment guarantee in the API */
		uintptr_t align = (uintptr_t)buffer & 3;

		if (len < 4 || align) {
			cnt = MIN(4 - align, len);
			memcpy(buffer, &number, cnt);
		} else {
			*(uint32_t *)buffer = number;
		}
		len -= cnt;
		buffer += cnt;
	}
}

#if defined(CONFIG_CMD_RAND)
/*
 * We want to avoid accidentally exposing debug commands in RO since we can't
 * update RO once in production.
 */
#if defined(SECTION_IS_RW)
static int command_rand(int argc, const char **argv)
{
	uint8_t data[32];
	char str_buf[hex_str_buf_size(sizeof(data))];

	trng_init();
	trng_rand_bytes(data, sizeof(data));
	trng_exit();
	snprintf_hex_buffer(str_buf, sizeof(str_buf),
			    HEX_BUF(data, sizeof(data)));
	ccprintf("rand %s\n", str_buf);

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rand, command_rand, NULL,
			"Output random bytes to console.");

static enum ec_status host_command_rand(struct host_cmd_handler_args *args)
{
	const struct ec_params_rand_num *p = args->params;
	struct ec_response_rand_num *r = args->response;
	uint16_t num_rand_bytes = p->num_rand_bytes;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;
	if (num_rand_bytes > args->response_max)
		return EC_RES_OVERFLOW;
	trng_init();
	trng_rand_bytes(r->rand, num_rand_bytes);
	trng_exit();
	args->response_size = num_rand_bytes;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_RAND_NUM, host_command_rand,
		     EC_VER_MASK(EC_VER_RAND_NUM));
#endif /* SECTION_IS_RW */
#endif /* CONFIG_CMD_RAND */
