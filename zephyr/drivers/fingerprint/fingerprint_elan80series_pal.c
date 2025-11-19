/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fingerprint_elan80series.h"
#include "fingerprint_elan80series_pal.h"
#include "fingerprint_elan80series_private.h"

#include <assert.h>
#include <stdio.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/cbprintf.h>

#include <drivers/fingerprint.h>

/* Platform Abstraction Layer for ELAN binary */

LOG_MODULE_REGISTER(elan80elan80series_pal, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(cros_fp_fingerprint_sensor)
#error "cros-fp,fingerprint-sensor device must be chosen"
#else
#define fp_sensor_dev DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#endif

K_HEAP_DEFINE(fp_driver_heap, CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES_HEAP_SIZE);
K_SEM_DEFINE(trx_buffer_lock, 1, 1);

static uint8_t tx_buf[ELAN_SPI_TX_BUF_SIZE];
static uint8_t rx_buf[ELAN_SPI_RX_BUF_SIZE];
BUILD_ASSERT(ELAN_SPI_TX_BUF_SIZE == 2);

#define LOG_SPI_WRITE_FAIL(func_name, err_val)                                 \
	LOG_ERR("spi_write FAILED: in func: %s with retval = %d\n", func_name, \
		err_val)

static int elan_spi_transaction_fullplex(uint8_t *tx_buf, uint8_t *rx_buf,
					 size_t trx_len)
{
	assert(tx_buf != NULL);
	assert(rx_buf != NULL);

	const struct elan80series_cfg *cfg = fp_sensor_dev->config;

	const struct spi_buf write_buf[1] = { { .buf = tx_buf,
						.len = trx_len } };
	const struct spi_buf read_buf[1] = { { .buf = rx_buf,
					       .len = trx_len } };
	const struct spi_buf_set tx = { .buffers = write_buf, .count = 1 };
	const struct spi_buf_set rx = { .buffers = read_buf, .count = 1 };

	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

	return err;
}

static int elan_spi_transaction_duplex(uint8_t *tx_buf, size_t tx_len,
				       uint8_t *rx_buf, size_t rx_len)
{
	assert(tx_buf != NULL);
	assert(rx_buf != NULL);

	const struct elan80series_cfg *cfg = fp_sensor_dev->config;

	const struct spi_buf write_buf[] = { { .buf = tx_buf, .len = tx_len },
					     { .buf = NULL, .len = rx_len } };
	const struct spi_buf read_buf[] = { { .buf = NULL, .len = tx_len },
					    { .buf = rx_buf, .len = rx_len } };
	const struct spi_buf_set tx = { .buffers = write_buf,
					.count = ARRAY_SIZE(write_buf) };
	const struct spi_buf_set rx = { .buffers = read_buf,
					.count = ARRAY_SIZE(read_buf) };

	int err = spi_transceive_dt(&cfg->spi, &tx, &rx);

	return err;
}

int __unused elan_write_cmd(uint8_t fp_cmd)
{
	k_sem_take(&trx_buffer_lock, K_FOREVER);

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	tx_buf[0] = fp_cmd;

	int err = elan_spi_transaction_fullplex(tx_buf, rx_buf,
						ELAN_SPI_TX_BUF_SIZE);

	if (err != 0) {
		LOG_SPI_WRITE_FAIL(__func__, err);

		k_sem_give(&trx_buffer_lock);
		return -EIO;
	}

	k_sem_give(&trx_buffer_lock);
	return err;
}

int __unused elan_read_cmd(uint8_t fp_cmd, uint8_t *regdata)
{
	assert(regdata != NULL);

	k_sem_take(&trx_buffer_lock, K_FOREVER);

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	tx_buf[0] = fp_cmd; /* one byte data read */

	int err = elan_spi_transaction_fullplex(tx_buf, rx_buf,
						ELAN_SPI_TX_BUF_SIZE);

	if (err != 0) {
		LOG_SPI_WRITE_FAIL(__func__, err);

		k_sem_give(&trx_buffer_lock);
		return -EIO;
	}

	*regdata = rx_buf[1];

	k_sem_give(&trx_buffer_lock);
	return err;
}

int __unused elan_spi_transaction(uint8_t *tx_data, int tx_len,
				  uint8_t *rx_data, int rx_len)
{
	assert(tx_data != NULL);
	assert(rx_data != NULL);
	assert(tx_len <= ELAN_SPI_TX_BUF_SIZE);
	assert(rx_len <= ELAN_SPI_RX_BUF_SIZE);

	k_sem_take(&trx_buffer_lock, K_FOREVER);

	memcpy(tx_buf, tx_data, tx_len);

	int err = elan_spi_transaction_duplex(tx_buf, tx_len, rx_buf, rx_len);

	if (err != 0) {
		LOG_SPI_WRITE_FAIL(__func__, err);

		k_sem_give(&trx_buffer_lock);
		return -EIO;
	}

	memcpy(rx_data, rx_buf, rx_len);

	k_sem_give(&trx_buffer_lock);
	return err;
}

int __unused elan_write_register(uint8_t regaddr, uint8_t regdata)
{
	k_sem_take(&trx_buffer_lock, K_FOREVER);

	tx_buf[0] = WRITE_REG_HEAD + regaddr; /* one byte data write */
	tx_buf[1] = regdata;

	int err = elan_spi_transaction_fullplex(tx_buf, rx_buf,
						ELAN_SPI_TX_BUF_SIZE);

	if (err != 0) {
		LOG_SPI_WRITE_FAIL(__func__, err);

		k_sem_give(&trx_buffer_lock);
		return -EIO;
	}

	k_sem_give(&trx_buffer_lock);
	return err;
}

int __unused elan_read_register(uint8_t regaddr, uint8_t *regdata)
{
	assert(regdata != NULL);

	return elan_read_cmd(READ_REG_HEAD + regaddr, regdata);
}

int __unused elan_write_page(uint8_t page)
{
	k_sem_take(&trx_buffer_lock, K_FOREVER);

	tx_buf[0] = PAGE_SEL;
	tx_buf[1] = page;

	int err = elan_spi_transaction_fullplex(tx_buf, rx_buf,
						ELAN_SPI_TX_BUF_SIZE);

	if (err != 0) {
		LOG_SPI_WRITE_FAIL(__func__, err);

		k_sem_give(&trx_buffer_lock);
		return -EIO;
	}

	k_sem_give(&trx_buffer_lock);
	return err;
}

int __unused elan_write_reg_vector(const uint8_t *reg_table, int length)
{
	assert(reg_table != NULL);
	assert(length % 2 == 0);

	int ret = 0;
	int i = 0;
	uint8_t write_regaddr;
	uint8_t write_regdata;

	for (i = 0; i < length; i = i + 2) {
		write_regaddr = reg_table[i];
		write_regdata = reg_table[i + 1];
		ret = elan_write_register(write_regaddr, write_regdata);
		if (ret < 0)
			break;
	}
	return ret;
}

/**
 * @brief Reads the fingerprint image data from the sensor.
 *
 * This function polls the scan status register until the image is ready
 * and then reads the image data into the provided buffer.
 *
 * @param short_raw Pointer to the buffer to store the image data.
 * @return 0 on success, negative error code on failure.
 */
static __unused int elan_read_image_full(uint16_t *short_raw)
{
	assert(short_raw != NULL);

	int ret = 0, cnt_timer = 0, rx_index = 0;
	uint8_t regdata[2] = { 0 };

	/* Polling scan status */
	cnt_timer = 0;
	while (1) {
		k_msleep(1);
		cnt_timer++;
		regdata[0] = SENSOR_STATUS;
		elan_spi_transaction(regdata, 2, regdata, 2);
		if (regdata[0] & IMG_READY)
			break;

		if (cnt_timer > POLLING_SCAN_TIMER) {
			ret = ELAN_ERROR_SCAN;
			LOG_ERR("%s regdata = 0x%x, fail ret = %d", __func__,
				regdata[0], ret);
			return ret;
		}
	}

	/* Read the image from fp sensor */
	k_sem_take(&trx_buffer_lock, K_FOREVER);
	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	tx_buf[0] = START_READ_IMAGE;
	for (int line_idx = 0; line_idx < ELAN_DMA_LOOP; line_idx++) {
		ret = elan_spi_transaction_duplex(tx_buf, ELAN_SPI_TX_BUF_SIZE,
						  rx_buf, ELAN_SPI_RX_BUF_SIZE);

		if (ret != 0) {
			LOG_SPI_WRITE_FAIL(__func__, ret);
			k_sem_give(&trx_buffer_lock);
			return -EIO;
		}

		for (int y = 0; y < IMAGE_HEIGHT / ELAN_DMA_LOOP; y++) {
			for (int x = 0; x < IMAGE_WIDTH; x++) {
				rx_index = (x * 2) + (RAW_DATA_SIZE * y);
				short_raw[(x + y * IMAGE_WIDTH) +
					  line_idx * ELAN_DMA_SIZE] =
					(rx_buf[rx_index] << 8) +
					(rx_buf[rx_index + 1]);
			}
		}
	}
	k_sem_give(&trx_buffer_lock);

	return 0;
}

/**
 * @brief Reads the fingerprint image data from the sensor line by line.
 *
 * This function polls the scan status register until each line of the
 * image is ready, and then reads the image data into the provided buffer
 * sequentially, line by line.
 *
 * @param short_raw Pointer to the buffer to store the linewise image data.
 * @return 0 on success, negative error code on failure.
 */
static __unused int elan_image_read_linewise(uint16_t *short_raw)
{
	assert(short_raw != NULL);

	int ret = 0, cnt_timer = 0, rx_index = 0;
	uint8_t regdata[2] = { 0 };

	for (int line_idx = 0; line_idx < ELAN_DMA_LOOP; line_idx++) {
		/* Polling scan status */
		cnt_timer = 0;
		do {
			cnt_timer++;
			regdata[0] = SENSOR_STATUS;
			/* The first byte of regdata is the register to read
			 * (SENSOR_STATUS), and the received status overwrites
			 * the buffer content. */
			elan_spi_transaction(regdata, 2, regdata, 2);
			if (cnt_timer > POLLING_SCAN_TIMER)
				return ELAN_ERROR_SCAN;
		} while ((regdata[0] & IMG_READY) == 0);

		/* Read block */
		k_sem_take(&trx_buffer_lock, K_FOREVER);
		memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
		tx_buf[0] = START_READ_IMAGE;
		ret = elan_spi_transaction_duplex(tx_buf, ELAN_SPI_TX_BUF_SIZE,
						  rx_buf, ELAN_SPI_RX_BUF_SIZE);
		if (ret != 0) {
			LOG_SPI_WRITE_FAIL(__func__, ret);
			k_sem_give(&trx_buffer_lock);
			return -EIO;
		}

		for (int x = 0; x < IMAGE_WIDTH; x++) {
			rx_index = x * 2;
			/* Sensor outputs Big-Endian: rx_buf[0] is MSB,
			 * rx_buf[1] is LSB */
			short_raw[x + line_idx * ELAN_DMA_SIZE] =
				(rx_buf[rx_index] << 8) | rx_buf[rx_index + 1];
		}
		k_sem_give(&trx_buffer_lock);
	}

	return ret;
}

int elan_read_image(uint16_t *short_raw)
{
#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES_READ_MODE_FULL)
	return elan_read_image_full(short_raw);
#elif defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES_READ_MODE_LINEWISE)
	return elan_image_read_linewise(short_raw);
#else
#error "No read mode selected."
#endif
}

int __unused elan_raw_capture(uint16_t *short_raw)
{
	assert(short_raw != NULL);

	int ret = 0;

	/* Write start scans command to fp sensor */
	if (elan_write_cmd(START_SCAN) < 0) {
		ret = ELAN_ERROR_SPI;
		LOG_ERR("%s SPISendCommand( SSP2, START_SCAN ) fail ret = %d",
			__func__, ret);
		return ret;
	}

	ret = elan_read_image(short_raw);
	if (ret < 0)
		LOG_ERR("%s: elan_image_read_func failed (%d)", __func__, ret);

	return ret;
}

int __unused elan_execute_calibration(void)
{
	if (!IS_ENABLED(CONFIG_HAVE_ELAN80SERIES_PRIVATE_DRIVER)) {
		return 0;
	}

	int retry_time = 0;
	int ret = 0;

	while (retry_time < REK_TIMES) {
		elan_write_cmd(SRST);
		elan_write_cmd(FUSE_LOAD);
		elan_register_initialization();
		elan_set_hv_chip(false);
		elan_sensing_mode();

		ret = elan_calibration();
		if (ret == 0)
			break;

		retry_time++;
	}

	return ret;
}

int __unused elan_set_hv_chip(bool state)
{
	int ret = 0;

	if (state) {
		elan_write_cmd(FUSE_LOAD);
		k_msleep(1);

		k_sem_take(&trx_buffer_lock, K_FOREVER);
		/*
		 * This command sequence "0x0B 0x02" switches the FP IC to
		 * Bypass mode. This command is sent after the FUSE_LOAD
		 * operation.
		 */
		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x02;

		ret = elan_spi_transaction_duplex(tx_buf, ELAN_SPI_TX_BUF_SIZE,
						  rx_buf, ELAN_SPI_TX_BUF_SIZE);

		if (ret != 0) {
			LOG_SPI_WRITE_FAIL(__func__, ret);
			k_sem_give(&trx_buffer_lock);
			return -EIO;
		}

		k_sem_give(&trx_buffer_lock);
	} else {
		k_sem_take(&trx_buffer_lock, K_FOREVER);
		/*
		 * This command sequence "0x0B 0x00" sets the FP IC to Local
		 * mode.
		 */
		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x00;

		ret |= elan_spi_transaction_duplex(tx_buf, ELAN_SPI_TX_BUF_SIZE,
						   rx_buf,
						   ELAN_SPI_TX_BUF_SIZE);

		if (ret != 0) {
			LOG_SPI_WRITE_FAIL(__func__, ret);
			k_sem_give(&trx_buffer_lock);
			return -EIO;
		}

		k_sem_give(&trx_buffer_lock);
		k_msleep(1);

		elan_write_register(HV_CONTROL, CHARGE_PUMP_HVIC);
		elan_write_register(HV_ENABLE, VOLTAGE_HVIC);

		k_sem_take(&trx_buffer_lock, K_FOREVER);
		/*
		 * This command sequence "0x0B 0x02" switches the FP IC to
		 * Bypass mode. This command is sent after the FUSE_LOAD
		 * operation.
		 */
		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x02;

		ret |= elan_spi_transaction_duplex(tx_buf, ELAN_SPI_TX_BUF_SIZE,
						   rx_buf,
						   ELAN_SPI_TX_BUF_SIZE);

		if (ret != 0) {
			LOG_SPI_WRITE_FAIL(__func__, ret);
			k_sem_give(&trx_buffer_lock);
			return -EIO;
		}

		k_sem_give(&trx_buffer_lock);
	}

	k_msleep(1);
	return ret;
}

int __unused elan_usleep(unsigned int us)
{
	return k_usleep(us);
}

void *__unused elan_malloc(uint32_t size)
{
	void *p = k_heap_aligned_alloc(&fp_driver_heap, sizeof(void *), size,
				       K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Error - %s of size %u failed.", __func__, size);
		k_oops();
		CODE_UNREACHABLE;
	}

	return p;
}

void __unused elan_free(void *data)
{
	k_heap_free(&fp_driver_heap, data);
}

void __unused elan_log_var(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprintk(format, args);
	va_end(args);
}

uint32_t __unused elan_get_tick(void)
{
	return k_uptime_get_32();
}

void __unused elan_sensor_set_rst(bool state)
{
	const struct elan80series_cfg *cfg = fp_sensor_dev->config;
	int ret = gpio_pin_set_dt(&cfg->reset_pin, state ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("Failed to set FP reset pin, status: %d", ret);
	}
}
