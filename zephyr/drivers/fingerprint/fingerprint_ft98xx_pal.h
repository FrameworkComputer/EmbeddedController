/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FT98XX_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FT98XX_PAL_SENSOR_H_

#include <drivers/fingerprint.h>

/* Maximum number of attempts to check the sensor irq*/
#define FP_SENSOR_MAX_IRQ_ATTEMPTS 5

/* Maximum time of every attempt to check the sensor irq, in ms*/
#define FP_SENSOR_MAX_IRQ_CHECK_TIME_MS 3

/* Sensor hardware reset active time, in ms*/
#define FP_SENSOR_HW_RESET_TIME_MS 5
/**
 * @brief delay function, unit in ms.
 *
 * @param[in] the time need to delay, unit in ms
 */
void ft_delay_ms(uint32_t ms);

/**
 * @brief hardware reset the fp sensor by active and deactivate the fp's reset
 * pin
 *
 * @return 0 on success.
 *         negative value on error.
 */
int ft_sensor_hw_reset(void);

/**
 * @brief spi write function for fp sensor
 *
 * write data to fp sensor by spi
 *
 * @param[in]    buffer       the data need to write
 * @param[in]    len          data length in bytes
 *
 * @return 0 on success.
 *         negative value on error.
 */
int ft_spi_write(uint8_t *buffer, uint32_t len);

/**
 * @brief spi write read function for fp sensor
 *
 * write data to fp sensor by spi, and then read data.
 *
 * @param[in]    tx_buffer       the data need to write
 * @param[in]    tx_len          write data length in bytes
 * @param[out]    rx_buffer      the received data
 * @param[out]    rx_len         received data length in bytes
 *
 * @return 0 on success.
 *         negative value on error.
 */
int ft_spi_write_then_read(uint8_t *tx_buffer, uint32_t tx_len,
			   uint8_t *rx_buffer, uint32_t rx_len);

/**
 * @brief Allocate memory
 *
 * @param size Allocation size
 *
 * @return Address on successful allocation, panic otherwise
 */
void *__unused focal_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
void __unused focal_free(void *data);

#endif
