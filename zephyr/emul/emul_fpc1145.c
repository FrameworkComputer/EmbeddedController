/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <emul/emul_fpc1145.h>

#define DT_DRV_COMPAT fpc_fpc1145

LOG_MODULE_REGISTER(emul_fpc1145, LOG_LEVEL_INF);

struct fpc1145_emul_data {
	uint16_t hardware_id;
	uint8_t low_power_mode;
	struct gpio_callback irq_cb;
	const struct emul *target;
	bool stop_irq;
	bool stop_spi;
};

struct fpc1145_emul_cfg {
	struct gpio_dt_spec interrupt_pin;
	struct gpio_dt_spec reset_pin;
};

/* Sensor IC commands */
enum fpc1145_cmd {
	FPC1145_CMD_STATUS = 0x14,
	FPC1145_CMD_INT_STS = 0x18,
	FPC1145_CMD_INT_CLR = 0x1C,
	FPC1145_CMD_FINGER_QUERY = 0x20,
	FPC1145_CMD_SLEEP = 0x28,
	FPC1145_CMD_DEEPSLEEP = 0x2C,
	FPC1145_CMD_SOFT_RESET = 0xF8,
	FPC1145_CMD_HW_ID = 0xFC,
};

void fpc1145_set_hwid(const struct emul *target, uint16_t hardware_id)
{
	struct fpc1145_emul_data *data = target->data;

	data->hardware_id = hardware_id;
}

uint8_t fpc1145_get_low_power_mode(const struct emul *target)
{
	struct fpc1145_emul_data *data = target->data;

	return data->low_power_mode;
}

void fpc1145_stop_irq(const struct emul *target)
{
	struct fpc1145_emul_data *data = target->data;

	data->stop_irq = true;
}

void fpc1145_stop_spi(const struct emul *target)
{
	struct fpc1145_emul_data *data = target->data;

	data->stop_spi = true;
}

void fpc1145_start_spi(const struct emul *target)
{
	struct fpc1145_emul_data *data = target->data;

	data->stop_spi = false;
}

static void fpc1145_write_response(const struct spi_buf_set *rx_bufs,
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

static int fpc1145_emul_io(const struct emul *target,
			   const struct spi_config *config,
			   const struct spi_buf_set *tx_bufs,
			   const struct spi_buf_set *rx_bufs)
{
	const struct fpc1145_emul_cfg *cfg = target->cfg;
	struct fpc1145_emul_data *data = target->data;
	uint8_t cmd;

	if (data->stop_spi) {
		return -EINVAL;
	}

	ARG_UNUSED(config);

	__ASSERT_NO_MSG(tx_bufs != NULL);
	__ASSERT_NO_MSG(tx_bufs->buffers != NULL);
	__ASSERT_NO_MSG(tx_bufs->count > 0);

	__ASSERT_NO_MSG(tx_bufs->buffers[0].len > 0);
	/* The first byte contains the command. */
	cmd = *(uint8_t *)tx_bufs->buffers[0].buf;

	switch (cmd) {
	case FPC1145_CMD_HW_ID:
		__ASSERT_NO_MSG(rx_bufs != NULL);
		/*
		 * The first must be 0x00, because it's received when MCU is
		 * transmitting the command.
		 */
		uint32_t resp = ((uint32_t)data->hardware_id) << 8;

		resp = sys_cpu_to_be32(resp);
		fpc1145_write_response(rx_bufs, (uint8_t *)&resp, 3);
		break;

	case FPC1145_CMD_SLEEP:
		/* No bytes to return to MCU */
		data->low_power_mode = 1;
		break;

	case FPC1145_CMD_INT_CLR:
		gpio_emul_input_set(cfg->interrupt_pin.port,
				    cfg->interrupt_pin.pin, 0);
		break;

	default:
		LOG_WRN("Unimplemented command 0x%x", cmd);
	}

	return 0;
}

static struct spi_emul_api fpc1145_emul_api = {
	.io = fpc1145_emul_io,
};

static void fpc1145_emul_reset(const struct emul *target)
{
	struct fpc1145_emul_data *data = target->data;

	data->hardware_id = FPC1145_HWID;
	data->low_power_mode = 0;
	data->stop_irq = false;
	data->stop_spi = false;
}

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>

/* Add test reset handlers in when using emulators with tests */
#define FPC1145_EMUL_RESET_RULE_AFTER(inst) \
	fpc1145_emul_reset(EMUL_DT_GET(DT_DRV_INST(inst)))

static void fpc1145_emul_reset_rule_after(const struct ztest_unit_test *test,
					  void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(FPC1145_EMUL_RESET_RULE_AFTER);
}
ZTEST_RULE(fpc1145_emul_reset, NULL, fpc1145_emul_reset_rule_after);

#endif /* CONFIG_ZTEST */

static void fpc1145_emul_gpio_irq(const struct device *dev,
				  struct gpio_callback *cb, uint32_t pins)
{
	struct fpc1145_emul_data *data =
		CONTAINER_OF(cb, struct fpc1145_emul_data, irq_cb);
	const struct fpc1145_emul_cfg *cfg = data->target->cfg;

	if (!data->stop_irq) {
		gpio_emul_input_set(cfg->interrupt_pin.port,
				    cfg->interrupt_pin.pin, 1);
	}
}

static int fpc1145_emul_init(const struct emul *target,
			     const struct device *parent)
{
	const struct fpc1145_emul_cfg *cfg = target->cfg;
	struct fpc1145_emul_data *data = target->data;
	ARG_UNUSED(parent);

	data->target = target;

	fpc1145_emul_reset(target);
	gpio_init_callback(&data->irq_cb, fpc1145_emul_gpio_irq,
			   BIT(cfg->reset_pin.pin));
	gpio_add_callback_dt(&cfg->reset_pin, &data->irq_cb);
	gpio_pin_interrupt_configure_dt(&cfg->reset_pin,
					GPIO_INT_EDGE_TO_INACTIVE);

	return 0;
}

#define FPC1145_EMUL(n)                                                  \
	static const struct fpc1145_emul_cfg fpc1145_emul_cfg_##n = {    \
		.interrupt_pin = GPIO_DT_SPEC_INST_GET(n, irq_gpios),    \
		.reset_pin = GPIO_DT_SPEC_INST_GET(n, reset_gpios),      \
	};                                                               \
	static struct fpc1145_emul_data fpc1145_emul_data##n;            \
	EMUL_DT_INST_DEFINE(n, fpc1145_emul_init, &fpc1145_emul_data##n, \
			    &fpc1145_emul_cfg_##n, &fpc1145_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(FPC1145_EMUL);
