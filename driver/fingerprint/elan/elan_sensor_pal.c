/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ELAN Platform Abstraction Layer callbacks */

#include "common.h"
#include "console.h"
#include "cryptoc/util.h"
#include "elan_sensor.h"
#include "elan_sensor_pal.h"
#include "elan_setting.h"
#include "fpsensor/fpsensor.h"
#include "gpio.h"
#include "link_defs.h"
#include "math_util.h"
#include "shared_mem.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#include <stddef.h>

static uint8_t tx_buf[CONFIG_SPI_TX_BUF_SIZE] __uncached;
static uint8_t rx_buf[CONFIG_SPI_RX_BUF_SIZE] __uncached;

int elan_write_cmd(uint8_t fp_cmd)
{
	int rc = 0;

	memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);

	tx_buf[0] = fp_cmd;
	rc = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			     SPI_READBACK_ALL);
	return rc;
}

int elan_read_cmd(uint8_t fp_cmd, uint8_t *regdata)
{
	int ret = 0;

	memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);

	tx_buf[0] = fp_cmd; /* one byte data read */
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);
	*regdata = rx_buf[1];

	return ret;
}

int elan_spi_transaction(uint8_t *tx, int tx_len, uint8_t *rx, int rx_len)
{
	int ret = 0;

	memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);

	memcpy(tx_buf, tx, tx_len);
	ret = spi_transaction(&spi_devices[0], tx_buf, tx_len, rx_buf, rx_len);
	memcpy(rx, rx_buf, rx_len);

	return ret;
}

int elan_write_register(uint8_t regaddr, uint8_t regdata)
{
	int ret = 0;

	memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);

	tx_buf[0] = WRITE_REG_HEAD + regaddr; /* one byte data write */
	tx_buf[1] = regdata;
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);
	return ret;
}

int elan_write_page(uint8_t page)
{
	int ret = 0;

	memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);

	tx_buf[0] = PAGE_SEL;
	tx_buf[1] = page;
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);

	return ret;
}

int elan_write_reg_vector(const uint8_t *reg_table, int length)
{
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

int raw_capture(uint16_t *short_raw)
{
	int ret = 0, i = 0, image_index = 0, index = 0;
	int cnt_timer = 0;
	int dma_loop = 0, dma_len = 0;
	uint8_t regdata[4] = { 0 };
	char *img_buf;

	memset(short_raw, 0, sizeof(uint16_t) * IMAGE_TOTAL_PIXEL);

	ret = shared_mem_acquire(sizeof(uint8_t) * IMG_BUF_SIZE, &img_buf);
	if (ret) {
		LOGE_SA("%s Can't get shared mem\n", __func__);
		return ret;
	}
	memset(img_buf, 0, sizeof(uint8_t) * IMG_BUF_SIZE);

	/* Write start scans command to fp sensor */
	if (elan_write_cmd(START_SCAN) < 0) {
		ret = ELAN_ERROR_SPI;
		LOGE_SA("%s SPISendCommand( SSP2, START_SCAN ) fail ret = %d",
			__func__, ret);
		goto exit;
	}

	/* Polling scan status */
	cnt_timer = 0;
	while (1) {
		usleep(1000);
		cnt_timer++;
		regdata[0] = SENSOR_STATUS;
		elan_spi_transaction(regdata, 2, regdata, 2);
		if (regdata[0] & 0x04)
			break;

		if (cnt_timer > POLLING_SCAN_TIMER) {
			ret = ELAN_ERROR_SCAN;
			LOGE_SA("%s regdata = 0x%x, fail ret = %d", __func__,
				regdata[0], ret);
			goto exit;
		}
	}

	/* Read the image from fp sensor */
	dma_loop = 4;
	dma_len = IMG_BUF_SIZE / dma_loop;

	for (i = 0; i < dma_loop; i++) {
		memset(tx_buf, 0, CONFIG_SPI_TX_BUF_SIZE);
		memset(rx_buf, 0, CONFIG_SPI_RX_BUF_SIZE);
		tx_buf[0] = START_READ_IMAGE;
		ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
				      dma_len);
		memcpy(&img_buf[dma_len * i], rx_buf, dma_len);
	}

	/* Remove dummy byte */
	for (image_index = 1; image_index < IMAGE_WIDTH; image_index++)
		memcpy(&img_buf[RAW_PIXEL_SIZE * image_index],
		       &img_buf[RAW_DATA_SIZE * image_index], RAW_PIXEL_SIZE);

	for (index = 0; index < IMAGE_TOTAL_PIXEL; index++)
		short_raw[index] =
			(img_buf[index * 2] << 8) + img_buf[index * 2 + 1];

exit:
	if (img_buf != NULL) {
		always_memset(img_buf, 0, sizeof(uint8_t) * IMG_BUF_SIZE);
		shared_mem_release(img_buf);
	}

	if (ret != 0)
		LOGE_SA("%s error = %d", __func__, ret);

	return ret;
}

int elan_execute_calibration(void)
{
	int retry_time = 0;
	int ret = 0;

	while (retry_time < REK_TIMES) {
		elan_write_cmd(SRST);
		elan_write_cmd(FUSE_LOAD);
		register_initialization();
		elan_sensing_mode();

		ret = calibration();
		if (ret == 0)
			break;

		retry_time++;
	}

	return ret;
}

int elan_fp_maintenance(uint16_t *error_state)
{
	int rv;
	fp_sensor_info_t sensor_info;
	timestamp_t start = get_time();

	if (error_state == NULL)
		return EC_ERROR_INVAL;

	/* Initial status */
	*error_state &= 0xFC00;
	sensor_info.num_defective_pixels = 0;
	sensor_info.sensor_error_code = 0;
	rv = fp_sensor_maintenance(&sensor_info);
	LOGE_SA("Maintenance took %d ms", time_since32(start) / MSEC);

	if (rv != 0) {
		/*
		 * Failure can occur if any of the fingerprint detection zones
		 * are covered (i.e., finger is on sensor).
		 */
		LOGE_SA("Failed to run maintenance: %d", rv);
		return EC_ERROR_HW_INTERNAL;
	}
	if (sensor_info.num_defective_pixels >= FP_ERROR_DEAD_PIXELS_UNKNOWN)
		*error_state = FP_ERROR_DEAD_PIXELS_UNKNOWN;
	else
		*error_state |=
			FP_ERROR_DEAD_PIXELS(sensor_info.num_defective_pixels);
	LOGE_SA("num_defective_pixels: %d", sensor_info.num_defective_pixels);
	LOGE_SA("sensor_error_code: %d", sensor_info.sensor_error_code);

	return EC_SUCCESS;
}

void __unused elan_sensor_set_rst(bool state)
{
	gpio_set_level(GPIO_FP_RST_ODL, state ? 0 : 1);
}
