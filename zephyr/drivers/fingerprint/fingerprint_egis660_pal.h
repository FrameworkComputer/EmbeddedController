/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PAL_SENSOR_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PAL_SENSOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

typedef bool (*egis_wfi_check_t)(void);

/**
 * @brief Storage information.
 *
 * The alignment requirements are specified in number of bytes.
 */
typedef struct {
	/** Required address and size alignment for read operations. */
	uint32_t read_align;
	/** Required address and size alignment for write operations. */
	uint32_t write_align;
	/** Required address and size alignment for erase operations. */
	uint32_t erase_align;
	/** True if flash can be read using memcpy. */
	bool is_memory_mapped;
} egis_storage_info_t;

/**
 * @brief Writes and reads SPI data.
 *
 * Writes data to SPI interface and reads data from SPI interface, with chip
 * select control. The caller is blocked until the operation is complete. By use
 * of the chip select control parameter a single SPI transaction can be split in
 * several calls.
 *
 * @param[in,out] data  Data to write and read. Must not be NULL if write_size
			or read_size > 0.
 * @param[in]     write_size Number of bytes to write.
 *                           0 => Only chip select control.
 * @param[in]     read_size  Number of bytes to read.
 *                           0 => Only chip select control.
 * @param[in]     leave_cs_asserted True  => chip select is left in asserted
 *                                           state.
 *                                  False => chip select is de-asserted before
 *                                           return.
 * @return ::egis_bep_result_t
 */
int __unused egis_sensor_spi_write_read(uint8_t *data, size_t write_size,
					size_t read_size,
					bool leave_cs_asserted);
/**
 * @brief Read sensor spi duplex mode.
 *
 * Returns status of the sensor spi mode.
 *
 * @return 0 if the sensor spi mode is support deplex mode, 1 if half deplex
 * mode.
 */
int __unused egis_sensor_spi_get_duplex_mode(void);
/**
 * @brief Read sensor IRQ status.
 *
 * Returns status of the sensor IRQ.
 *
 * @return true if the sensor IRQ is currently active, otherwise false.
 */
bool __unused egis_sensor_check_irq(void);

/**
 * @brief Read sensor IRQ status and then set status to false.
 *
 * Returns status of the sensor IRQ and sets the status to false.
 *
 * @return true if the sensor IRQ has been active, otherwise false.
 */
bool __unused egis_sensor_read_irq(void);

/**
 * @brief Set sensor reset state.
 *
 * Set sensor reset state.
 *
 * @param[in] state Reset state.
 *                  true  => reset sensor, i.e. low GPIO state
 *                  false => normal operation, i.e. high GPIO state
 */
void __unused egis_sensor_reset(bool state);

/**
 * @brief Initializes SPI controller.
 *
 * @param[in] speed_hz  Maximum SPI clock speed according to sensor HW spec
 *                      (unit Hz).
 *
 */
void __unused egis_sensor_spi_init(uint32_t speed_hz);

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
 * @return EGIS_RESULT_OK, EGIS_RESULT_TIMEOUT
 */
int __unused egis_sensor_wfi(uint16_t timeout_ms, egis_wfi_check_t enter_wfi,
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
 * @return Tick count since egis_timebase_init() call. [ms]
 */
uint32_t __unused egis_timebase_get_tick(void);

/**
 * @brief Delay us.
 *
 * @param[in] us  Time to delay [us].
 * 0 => return immediately
 * 1 => delay at least 1us etc.
 */
void __unused egis_timebase_delay_us(uint32_t us);
/**
 * @brief Delay ms.
 *
 * @param[in] ms  Time to delay [ms].
 * 0 => return immediately
 * 1 => delay at least 1ms etc.
 */
void __unused egis_timebase_delay_ms(uint32_t delay);
/**
 * @brief Allocate memory
 *
 * @param size Allocation size
 * @return Address on successful allocation, panic otherwise
 */
void *__unused egis_malloc(uint32_t size);

/**
 * @brief Free previously allocated memory
 *
 * @param data Pointer to buffer that should be freed
 */
void __unused egis_free(void *data);

/**
 * @brief Get APNS flash storage information.
 *
 * @return Pointer to egis_storage_info_t with alignment requirements.
 */
const egis_storage_info_t __unused *egis_apns_storage_get_info(void);

/**
 * @brief Erase a region of APNS data storage.
 *
 * @param[in] offset  Offset within the APNS partition.
 * @param[in] size    Number of bytes to erase.
 * @return 0 on success, negative errno on failure.
 */
int __unused egis_apns_data_erase(uint32_t offset, size_t size);

/**
 * @brief Read data from APNS storage.
 *
 * @param[in]  offset  Offset within the APNS partition.
 * @param[in]  size    Number of bytes to read.
 * @param[out] data    Buffer to store read data.
 * @return 0 on success, negative errno on failure.
 */
int __unused egis_apns_data_read(uint32_t offset, size_t size, void *data);

/**
 * @brief Write data to APNS storage.
 *
 * @param[in] offset  Offset within the APNS partition.
 * @param[in] data    Data to write.
 * @param[in] size    Number of bytes to write.
 * @return 0 on success, negative errno on failure.
 */
int __unused egis_apns_data_write(uint32_t offset, const void *data,
				  size_t size);

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_PAL_SENSOR_H_ */
