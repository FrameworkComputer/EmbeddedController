/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ZEPHYR_ESPI_SHIM_H
#define __CROS_EC_ZEPHYR_ESPI_SHIM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if the message is an ACPI command.
 *
 * @param data The full ACPI event data.
 * @return True if the message is a command.
 */
bool is_acpi_command(uint32_t data);

/**
 * Get the value component of the ACPI message.
 *
 * @param data The full ACPI event data.
 * @return The value component of the ACPI message.
 */
uint32_t get_acpi_value(uint32_t data);

/**
 * Check if the 8042 event data contains an input-buffer-full (IBF) event.
 *
 * @param data The full 8042 event data.
 * @return True if the data contains an IBF event.
 */
bool is_8042_ibf(uint32_t data);

/**
 * Check if the 8042 event data contains an output-buffer-empty (OBE) event.
 *
 * @param data The full 8042 event data.
 * @return True if the data contains an OBE event.
 */
bool is_8042_obe(uint32_t data);

/**
 * Get the type of 8042 message.
 *
 * @param data The full 8042 event data.
 * @return The type component of the message.
 */
uint32_t get_8042_type(uint32_t data);

/**
 * Get the data from an 8042 message.
 *
 * @param data The full 8042 event data.
 * @return The data component of the message.
 */
uint32_t get_8042_data(uint32_t data);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ZEPHYR_ESPI_SHIM_H */
