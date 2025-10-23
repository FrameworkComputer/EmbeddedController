/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <emul/emul_ft98xx.h>

#define DT_DRV_COMPAT focaltech_ft98xx

LOG_MODULE_REGISTER(emul_ft98xx, LOG_LEVEL_INF);

struct ft98xx_emul_data {
	const struct emul *target;
	bool stop_spi;
};

struct ft98xx_emul_cfg {
	struct gpio_dt_spec interrupt_pin;
	struct gpio_dt_spec reset_pin;
};

enum ft98xx_cmd {
	FT98XX_CMD_HW_ID = 0xFD,
	FT98XX_CMD_DEEPSLEEP = 0xFE,
};

void ft98xx_stop_spi(const struct emul *target)
{
	struct ft98xx_emul_data *data = target->data;

	data->stop_spi = true;
}

static void ft98xx_write_response(const struct spi_buf_set *rx_bufs,
				  const uint8_t *resp, size_t size)
{
	size_t idx = 0;

	for (size_t i = 0; i < rx_bufs->count; i++) {
		const struct spi_buf *rx = &rx_bufs->buffers[i];

		if (rx->buf != NULL) {
			for (size_t j = 0; j < rx->len; j++) {
				((uint8_t *)rx->buf)[j] =
					idx < size ? resp[idx++] : 0;
			}
		} else {
			idx += rx->len;
		}
	}
}

static int ft98xx_emul_io(const struct emul *target,
			  const struct spi_config *config,
			  const struct spi_buf_set *tx_bufs,
			  const struct spi_buf_set *rx_bufs)
{
	const struct ft98xx_emul_cfg *cfg = target->cfg;
	struct ft98xx_emul_data *data = target->data;
	uint8_t cmd;

	if (data->stop_spi) {
		return -EINVAL;
	}

	/*
	 * NOTE: gpio_emul_output_get returns the **physical** value of the
	 * output pin (0 or 1), NOT the logical value.
	 */
	if (gpio_emul_output_get(cfg->reset_pin.port, cfg->reset_pin.pin) ==
	    0) {
		return -EAGAIN;
	}

	ARG_UNUSED(config);

	__ASSERT_NO_MSG(tx_bufs != NULL);
	__ASSERT_NO_MSG(tx_bufs->buffers != NULL);
	__ASSERT_NO_MSG(tx_bufs->count > 0);
	__ASSERT_NO_MSG(tx_bufs->buffers[0].len > 0);

	/* The first byte contains the command. */
	cmd = *(uint8_t *)tx_bufs->buffers[0].buf;

	switch (cmd) {
	case FT98XX_CMD_HW_ID:
		__ASSERT_NO_MSG(rx_bufs != NULL);
		/*
		 * The first must be 0x00, because it's received when MCU is
		 * transmitting the command.
		 */
		const uint8_t resp[3] = { 0x00, 0x49, 0x98 };

		ft98xx_write_response(rx_bufs, resp, ARRAY_SIZE(resp));
		break;

	case FT98XX_CMD_DEEPSLEEP:
		/* No bytes to return to MCU */
		break;

	default:
		LOG_WRN("Unimplemented command 0x%x", cmd);
	}

	return 0;
}

static struct spi_emul_api ft98xx_emul_api = {
	.io = ft98xx_emul_io,
};

static void ft98xx_emul_reset(const struct emul *target)
{
	struct ft98xx_emul_data *data = target->data;

	data->stop_spi = false;
}

/* Add test reset handlers in when using emulators with tests */
#define FT98XX_EMUL_RESET_RULE_AFTER(inst) \
	ft98xx_emul_reset(EMUL_DT_GET(DT_DRV_INST(inst)))

static void ft98xx_emul_reset_rule_after(const struct ztest_unit_test *test,
					 void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(FT98XX_EMUL_RESET_RULE_AFTER);
}
ZTEST_RULE(ft98xx_emul_reset, NULL, ft98xx_emul_reset_rule_after);

static int ft98xx_emul_init(const struct emul *target,
			    const struct device *parent)
{
	struct ft98xx_emul_data *data = target->data;
	ARG_UNUSED(parent);

	data->target = target;

	ft98xx_emul_reset(target);

	return 0;
}

#define FT98XX_EMUL(n)                                                 \
	static const struct ft98xx_emul_cfg ft98xx_emul_cfg_##n = {    \
		.interrupt_pin = GPIO_DT_SPEC_INST_GET(n, irq_gpios),  \
		.reset_pin = GPIO_DT_SPEC_INST_GET(n, reset_gpios),    \
	};                                                             \
	static struct ft98xx_emul_data ft98xx_emul_data##n;            \
	EMUL_DT_INST_DEFINE(n, ft98xx_emul_init, &ft98xx_emul_data##n, \
			    &ft98xx_emul_cfg_##n, &ft98xx_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(FT98XX_EMUL);
