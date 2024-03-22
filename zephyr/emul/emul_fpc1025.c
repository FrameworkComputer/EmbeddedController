/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <emul/emul_fpc1025.h>

#define DT_DRV_COMPAT fpc_fpc1025

LOG_MODULE_REGISTER(emul_fpc1025, LOG_LEVEL_INF);

struct fpc1025_emul_data {
	uint16_t hardware_id;
	uint8_t low_power_mode;
};

/* Sensor IC commands */
enum fpc1025_cmd {
	FPC1025_CMD_DEEPSLEEP = 0x2C,
	FPC1025_CMD_HW_ID = 0xFC,
};

void fpc1025_set_hwid(const struct emul *target, uint16_t hardware_id)
{
	struct fpc1025_emul_data *data = target->data;

	data->hardware_id = hardware_id;
}

uint8_t fpc1025_get_low_power_mode(const struct emul *target)
{
	struct fpc1025_emul_data *data = target->data;

	return data->low_power_mode;
}

static void fpc1025_write_response(const struct spi_buf_set *rx_bufs,
				   const uint8_t *resp, const size_t size)
{
	size_t idx = 0;

	for (size_t i = 0; i < rx_bufs->count; i++) {
		const struct spi_buf *rx = &rx_bufs->buffers[i];

		for (size_t j = 0; j < rx->len; j++) {
			((uint8_t *)rx->buf)[j] = idx < size ? resp[idx++] : 0;
		}
	}
}

static int fpc1025_emul_io(const struct emul *target,
			   const struct spi_config *config,
			   const struct spi_buf_set *tx_bufs,
			   const struct spi_buf_set *rx_bufs)
{
	struct fpc1025_emul_data *data = target->data;
	uint8_t cmd;

	ARG_UNUSED(config);

	__ASSERT_NO_MSG(tx_bufs != NULL);
	__ASSERT_NO_MSG(tx_bufs->buffers != NULL);
	__ASSERT_NO_MSG(tx_bufs->count > 0);

	__ASSERT_NO_MSG(tx_bufs->buffers[0].len > 0);
	/* The first byte contains the command. */
	cmd = *(uint8_t *)tx_bufs->buffers[0].buf;

	switch (cmd) {
	case FPC1025_CMD_HW_ID:
		__ASSERT_NO_MSG(rx_bufs != NULL);
		/*
		 * The first must be 0x00, because it's received when MCU is
		 * transmitting the command.
		 */
		uint32_t resp = ((uint32_t)data->hardware_id) << 8;

		resp = sys_cpu_to_be32(resp);
		fpc1025_write_response(rx_bufs, (uint8_t *)&resp, 3);
		break;

	case FPC1025_CMD_DEEPSLEEP:
		/* No bytes to return to MCU */
		data->low_power_mode = 1;
		break;

	default:
		LOG_WRN("Unimplemented command 0x%x", cmd);
	}

	return 0;
}

static struct spi_emul_api fpc1025_emul_api = {
	.io = fpc1025_emul_io,
};

static void fpc1025_emul_reset(const struct emul *target)
{
	struct fpc1025_emul_data *data = target->data;

	data->hardware_id = FPC1025_HWID;
	data->low_power_mode = 0;
}

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>

/* Add test reset handlers in when using emulators with tests */
#define FPC1025_EMUL_RESET_RULE_AFTER(inst) \
	fpc1025_emul_reset(EMUL_DT_GET(DT_DRV_INST(inst)))

static void fpc1025_emul_reset_rule_after(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(FPC1025_EMUL_RESET_RULE_AFTER);
}
ZTEST_RULE(fpc1025_emul_reset, NULL, fpc1025_emul_reset_rule_after);

#endif /* CONFIG_ZTEST */

static int fpc1025_emul_init(const struct emul *target,
			     const struct device *parent)
{
	ARG_UNUSED(parent);

	fpc1025_emul_reset(target);

	return 0;
}

#define FPC1025_EMUL(n)                                                        \
	static struct fpc1025_emul_data fpc1025_emul_data##n;                  \
	EMUL_DT_INST_DEFINE(n, fpc1025_emul_init, &fpc1025_emul_data##n, NULL, \
			    &fpc1025_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(FPC1025_EMUL);
