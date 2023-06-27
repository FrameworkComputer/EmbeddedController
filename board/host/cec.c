/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock CEC driver.
 */

#include "cec.h"
#include "util.h"

static int mock_cec_send(int port, const uint8_t *msg, uint8_t len)
{
	return EC_SUCCESS;
}

static int mock_cec_get_received_message(int port, uint8_t **msg, uint8_t *len)
{
	return EC_SUCCESS;
}

static int mock_cec_get_enable(int port, uint8_t *enable)
{
	return EC_SUCCESS;
}

static int mock_cec_set_enable(int port, uint8_t enable)
{
	return EC_SUCCESS;
}

static int mock_cec_get_logical_addr(int port, uint8_t *logical_addr)
{
	return EC_SUCCESS;
}

static int mock_cec_set_logical_addr(int port, uint8_t logical_addr)
{
	return EC_SUCCESS;
}

static int mock_cec_init(int port)
{
	return EC_SUCCESS;
}

static const struct cec_drv mock_cec_drv = {
	.init = &mock_cec_init,
	.get_enable = &mock_cec_get_enable,
	.set_enable = &mock_cec_set_enable,
	.get_logical_addr = &mock_cec_get_logical_addr,
	.set_logical_addr = &mock_cec_set_logical_addr,
	.send = &mock_cec_send,
	.get_received_message = &mock_cec_get_received_message,
};

const struct cec_config_t cec_config[] = {
	[CEC_PORT_0] = {
		.drv = &mock_cec_drv,
		.offline_policy = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(cec_config) == CEC_PORT_COUNT);
