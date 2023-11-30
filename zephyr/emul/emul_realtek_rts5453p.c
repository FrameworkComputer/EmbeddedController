/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_realtek_rts5453p.h"
#include "emul/emul_stub_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT realtek_rts5453p_emul

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(realtek_rts5453_emul);

struct rts5453p_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;

	/** Data required to simulate PD Controller */
	struct rts5453p_emul_pdc_data pdc_data;
};

struct rts5453p_emul_pdc_data *
rts5453p_emul_get_pdc_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->pdc_data;
}

/**
 * @brief Function which handles read messages. It expects that data->cur_cmd
 *        is set to command number which should be handled. It guarantee that
 *        data->num_to_read is set to number of bytes in data->msg_buf on
 *        successful handling read request. On error, data->num_to_read is
 *        always set to 0.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg Command selected by last write message. If data->cur_cmd is
 *            different than NO_CMD, then reg should equal to
 *            data->cur_cmd
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_handle_read_msg(const struct emul *emul, int reg)
{
	return 0;
}

/**
 * @brief Function which finalize write messages.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param bytes Number of bytes received in data->msg_buf
 *
 * @return 0 on success
 * @return -EIO on error
 */
static int rts5453p_emul_finalize_write_msg(const struct emul *emul, int reg,
					    int bytes)
{
	return 0;
}

/**
 * @brief Function called for each byte of write message which is saved in
 *        data->msg_buf
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of write message, usually selected command
 * @param val Received byte of write message
 * @param bytes Number of bytes already received
 *
 * @return 0 on success
 */
static int rts5453p_emul_write_byte(const struct emul *emul, int reg,
				    uint8_t val, int bytes)
{
	return 0;
}

/**
 * @brief Function called for each byte of read message. Byte from data->msg_buf
 *        is copied to read message response.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param val Pointer where byte to read should be stored
 * @param bytes Number of bytes already read
 *
 * @return 0 on success
 */
static int rts5453p_emul_read_byte(const struct emul *emul, int reg,
				   uint8_t *val, int bytes)
{
	return 0;
}

/**
 * @brief Get currently accessed register, which always equals to selected
 *        command.
 *
 * @param emul Pointer to RTS5453P emulator
 * @param reg First byte of last write message, usually selected command
 * @param bytes Number of bytes already handled from current message
 * @param read If currently handled is read message
 *
 * @return Currently accessed register
 */
static int rts5453p_emul_access_reg(const struct emul *emul, int reg, int bytes,
				    bool read)
{
	return reg;
}

/* Device instantiation */

/**
 * @brief Set up a new RTS5453P emulator
 *
 * This should be called for each RTS5453P device that needs to be
 * emulated. It registers it with the I2C emulation controller.
 *
 * @param emul Emulation information
 * @param parent Device to emulate
 *
 * @return 0 indicating success (always)
 */
static int rts5453p_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct rts5453p_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;

	i2c_common_emul_init(&data->common);

	return 0;
}

#define RTS5453P_EMUL_DEFINE(n)                                             \
	static struct rts5453p_emul_data rts5453p_emul_data_##n = {	\
		.common = {						\
			.start_write = NULL,				\
			.write_byte = rts5453p_emul_write_byte,		\
			.finish_write = rts5453p_emul_finalize_write_msg,\
			.start_read = rts5453p_emul_handle_read_msg,	\
			.read_byte = rts5453p_emul_read_byte,		\
			.finish_read = NULL,				\
			.access_reg = rts5453p_emul_access_reg,		\
		},							\
	};       \
	static const struct i2c_common_emul_cfg rts5453p_emul_cfg_##n = {   \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),             \
		.data = &rts5453p_emul_data_##n.common,                     \
		.addr = DT_INST_REG_ADDR(n),                                \
	};                                                                  \
	EMUL_DT_INST_DEFINE(n, rts5453p_emul_init, &rts5453p_emul_data_##n, \
			    &rts5453p_emul_cfg_##n, &i2c_common_emul_api,   \
			    NULL)

DT_INST_FOREACH_STATUS_OKAY(RTS5453P_EMUL_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
rts5453p_emul_get_i2c_common_data(const struct emul *emul)
{
	struct rts5453p_emul_data *data = emul->data;

	return &data->common;
}
