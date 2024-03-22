/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PAL_SENSOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

typedef bool (*fpc_wfi_check_t)(void);

/**
 * @brief Writes and reads SPI data.
 *
 * Writes data to SPI interface and reads data from SPI interface, with chip
 * select control. The caller is blocked until the operation is complete. By use
 * of the chip select control parameter a single SPI transaction can be split in
 * several calls.
 *
 * @param[in]     write Data to write. Must not be NULL if size > 0.
 * @param[in,out] read  Receive data buffer. The caller is responsible for
 *                      allocating buffer. NULL => response is thrown away.
 * @param[in]     size  Number of bytes to write (same as bytes received).
 *                      0 => Only chip select control.
 * @param[in]     leave_cs_asserted True  => chip select is left in asserted
 *                                           state.
 *                                  False => chip select is de-asserted before
 *                                           return.
 * @return ::fpc_bep_result_t
 */
int __unused fpc_sensor_spi_write_read(uint8_t *write, uint8_t *read,
				       size_t size, bool leave_cs_asserted);

/**
 * @brief Read sensor IRQ status.
 *
 * Returns status of the sensor IRQ.
 *
 * @return true if the sensor IRQ is currently active, otherwise false.
 */
bool __unused fpc_sensor_spi_check_irq(void);

/**
 * @brief Read sensor IRQ status and then set status to false.
 *
 * Returns status of the sensor IRQ and sets the status to false.
 *
 * @return true if the sensor IRQ has been active, otherwise false.
 */
bool __unused fpc_sensor_spi_read_irq(void);

/**
 * @brief Set sensor reset state.
 *
 * Set sensor reset state.
 *
 * @param[in] state Reset state.
 *                  true  => reset sensor, i.e. low GPIO state
 *                  false => normal operation, i.e. high GPIO state
 */
void __unused fpc_sensor_spi_reset(bool state);

/**
 * @brief Initializes SPI controller.
 *
 * @param[in] speed_hz  Maximum SPI clock speed according to sensor HW spec
 *                      (unit Hz).
 *
 */
void __unused fpc_sensor_spi_init(uint32_t speed_hz);

/**
 * @brief Set system in WFI mode while waiting sensor IRQ.
 *
 * @note This mode only requires the system to be able to wake up from Sensor
 * IRQ pin, all other peripheral can be turned off.
 *
 * @note The system time must be adjusted upon WFI return.
 *
 * @param[in] timeout_ms Time in ms before waking up, 0 if no timeout.
 * @param[in] enter_wfi Function pointer to check WFI entry.
 * @param[in] enter_wfi_mode Bool that is used when comparing the value returned
 *                           by enter_wfi.
 * @return FPC_RESULT_OK, FPC_RESULT_TIMEOUT
 */
int __unused fpc_sensor_wfi(uint16_t timeout_ms, fpc_wfi_check_t enter_wfi,
			    bool enter_wfi_mode);

/**
 * @brief Reads the system tick counter.
 *
 * @details To handle tick counter wrap around when checking for timeout, make
 *          sure to do the calculation in the following manner:
 *          "if ((current_tick - old_tick) > timeout) {"
 *          Example: current time (uint32_t) = 10 ticks
 *                   old time (uint32_t) = 30 ticks before overflow of uint32_t
 *          current_time - old_time = 10 - (2**32 - 30) -> wraps around to 40
 *
 * @return Tick count since fpc_timebase_init() call. [ms]
 */
uint32_t __unused fpc_timebase_get_tick(void);

/**
 * @brief Busy wait.
 *
 * @param[in] ms  Time to wait [ms].
 * 0 => return immediately
 * 1 => wait at least 1ms etc.
 */
void __unused fpc_timebase_busy_wait(uint32_t ms);

/**
 * @brief Allocate memory
 *
 * @param size Allocation size
 * @return Address on successful allocation, panic otherwise
 */
void *__unused fpc_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
void __unused fpc_free(void *data);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FPC1025_PAL_SENSOR_H_ */
