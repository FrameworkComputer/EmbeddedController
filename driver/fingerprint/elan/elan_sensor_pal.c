/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ELAN Platform Abstraction Layer callbacks */

#include "common.h"
#include "console.h"
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
#include <stdint.h>

static uint8_t tx_buf[ELAN_SPI_TX_BUF_SIZE] __uncached;
static uint8_t rx_buf[ELAN_SPI_RX_BUF_SIZE] __uncached;

/* Unused by staticlib. */
int elan_write_cmd(uint8_t fp_cmd)
{
	int rc = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	tx_buf[0] = fp_cmd;
	rc = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			     SPI_READBACK_ALL);
	return rc;
}

__staticlib_hook int elan_read_cmd(uint8_t fp_cmd, uint8_t *regdata)
{
	int ret = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	tx_buf[0] = fp_cmd; /* one byte data read */
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);
	*regdata = rx_buf[1];

	return ret;
}

/* Unused by staticlib. */
int elan_spi_transaction(uint8_t *tx, int tx_len, uint8_t *rx, int rx_len)
{
	int ret = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	memcpy(tx_buf, tx, tx_len);
	ret = spi_transaction(&spi_devices[0], tx_buf, tx_len, rx_buf, rx_len);
	memcpy(rx, rx_buf, rx_len);

	return ret;
}

__staticlib_hook int elan_write_register(uint8_t regaddr, uint8_t regdata)
{
	int ret = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	tx_buf[0] = WRITE_REG_HEAD + regaddr; /* one byte data write */
	tx_buf[1] = regdata;
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);
	return ret;
}

__staticlib_hook int elan_write_page(uint8_t page)
{
	int ret = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	tx_buf[0] = PAGE_SEL;
	tx_buf[1] = page;
	ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
			      SPI_READBACK_ALL);

	return ret;
}

__staticlib_hook int elan_write_reg_vector(const uint8_t *reg_table, int length)
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

__staticlib_hook int raw_capture(uint16_t *short_raw)
{
	int ret = 0, i = 0, cnt_timer = 0, rx_index = 0;
	uint8_t regdata[4] = { 0 };

	memset(short_raw, 0, sizeof(uint16_t) * IMAGE_TOTAL_PIXEL);

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
	for (i = 0; i < ELAN_DMA_LOOP; i++) {
		memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
		memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);
		tx_buf[0] = START_READ_IMAGE;
		ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf,
				      ELAN_SPI_RX_BUF_SIZE);

		for (int y = 0; y < IMAGE_HEIGHT / ELAN_DMA_LOOP; y++) {
			for (int x = 0; x < IMAGE_WIDTH; x++) {
				rx_index = (x * 2) + (RAW_DATA_SIZE * y);
				short_raw[(x + y * IMAGE_WIDTH) +
					  i * ELAN_DMA_SIZE] =
					(rx_buf[rx_index] << 8) +
					(rx_buf[rx_index + 1]);
			}
		}
	}

exit:

	if (ret != 0)
		LOGE_SA("%s error = %d", __func__, ret);
	return ret;
}

__staticlib_hook int elan_execute_calibration(void)
{
	int retry_time = 0;
	int ret = 0;

	while (retry_time < REK_TIMES) {
		elan_write_cmd(SRST);
		elan_write_cmd(FUSE_LOAD);
		register_initialization();

		if (IC_SELECTION == EFSA80SG)
			elan_set_hv_chip(0);

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

__staticlib_hook void elan_sensor_set_rst(bool state)
{
	gpio_set_level(GPIO_FP_RST_ODL, state ? 0 : 1);
}

int elan_set_hv_chip(bool state)
{
	int ret = 0;

	memset(tx_buf, 0, ELAN_SPI_TX_BUF_SIZE);
	memset(rx_buf, 0, ELAN_SPI_RX_BUF_SIZE);

	if (state) {
		elan_write_cmd(FUSE_LOAD);
		usleep(1000);

		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x02;

		ret = spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf, 2);
		usleep(1000);
	} else {
		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x00;

		ret |= spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf, 2);
		usleep(1000);

		const uint8_t charge_pump[] = { 0x00,
						(uint8_t)CHARGE_PUMP_HVIC };

		elan_write_reg_vector(charge_pump, ((int)sizeof(charge_pump)));

		const uint8_t disable_hv[] = { 0x01, VOLTAGE_HVIC };

		elan_write_reg_vector(disable_hv, ((int)sizeof(disable_hv)));

		tx_buf[0] = 0x0B;
		tx_buf[1] = 0x02;

		ret |= spi_transaction(&spi_devices[0], tx_buf, 2, rx_buf, 2);
		usleep(1000);
	}
	return ret;
}
