/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FPC_PAL_SENSOR_H_
#define FPC_PAL_SENSOR_H_

#include <stdint.h>

typedef void *fpc_device_t;

/**
 * @brief Used to describe an interrupt
 */
typedef enum {
    IRQ_INT_TRIG = 0x01, /**< Internally triggered by sensor (fast interrupt)                   **/
    IRQ_EXT_TRIG = 0x02  /**< Externally triggered by event outside sensor (may take long time) **/
} fpc_pal_irq_t;

/**
 * @brief Write sensor access buffer to SPI interface
 *
 * @param[in] device             Client's device handle.
 * @param[in] access_buffer      Buffer holding data to write.
 * @param[in] access_buffer_size Size of the access buffer.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_spi_write(fpc_device_t device, uint8_t *access_buffer,
		      uint32_t access_buffer_size);

/**
 * @brief Write and read sensor access buffer to SPI interface
 *
 * SPI transfers always write the same number of bytes as they read,
 * hence the size of tx_buffer and rx_buffer must be the same.
 *
 * @param[in] device    Client's device handle.
 * @param[in] tx_buffer Buffer holding data to write.
 * @param[in] rx_buffer Buffer where read data will be stored.
 * @param[in] size      Size of tx and rx buffer.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_spi_writeread(fpc_device_t device, uint8_t *tx_buffer,
			  uint8_t *rx_buffer, uint32_t size);

/**
 * @brief Wait for IRQ
 *
 * @param[in] device   Client's device handle.
 * @param[in] irq_type The expected IRQ type.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_wait_irq(fpc_device_t device, fpc_pal_irq_t irq_type);

/**
 * @brief Get time
 *
 * @param[out] time_us Timestamp in microseconds.
 *
 * Not all platforms have microsecond resolution. These should
 * return time in terms of hole milliseconds.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_get_time(uint64_t *time_us);

/**
 * @brief Delay function
 *
 * @param[in] us Delay in microseconds.
 *
 * Not all platforms have microsecond resolution. These should
 * delay in terms of hole milliseconds.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_delay_us(uint64_t us);

/**
 * @brief Get platform SPI clock frequency
 *
 * @param[in]  device   Client's device handle.
 * @param[out] speed_hz SPI frequency in hertz.
 *
 * Required by platform for adaptive SPI calculations.
 *
 * @return 0 on success.
 *         negative value on error.
 */
int fpc_pal_spi_get_speed_hz(fpc_device_t device, uint32_t *speed_hz);

/**
 * @brief Print SDK log strings
 *
 * @param[in] tag sensor sdk log prefix
 * @param[in] log_level  FPC_SENSOR_SDK_LOG_LEVEL_DEBUG - debug print
 *                       FPC_SENSOR_SDK_LOG_LEVEL_INFO  - information print
 *                       FPC_SENSOR_SDK_LOG_LEVEL_ERROR - error print
 * @param[in] format     the format specifier.
 * @param[in] ...        additional arguments.
 *
 */
#define FPC_SENSOR_SDK_LOG_LEVEL_DEBUG    (1)
#define FPC_SENSOR_SDK_LOG_LEVEL_INFO     (2)
#define FPC_SENSOR_SDK_LOG_LEVEL_ERROR    (3)
#define FPC_SENSOR_SDK_LOG_LEVEL_DISABLED (4)
void fpc_pal_log_entry(const char *tag, int log_level, const char *format, ...);

#endif // FPC_PAL_SENSOR_H_
