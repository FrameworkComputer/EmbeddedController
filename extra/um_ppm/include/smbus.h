/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_INCLUDE_SMBUS_H_
#define UM_PPM_INCLUDE_SMBUS_H_

#include <stddef.h>
#include <stdint.h>

/* Internal data structure for smbus driver implementations. */
struct smbus_device;

/* Forward declaration only. */
struct smbus_driver;

/**
 * Read byte over smbus.
 *
 * @param device: smbus device.
 * @param chip_address: Chip address to target.
 *
 * @return Byte read or -1 for errors.
 */
typedef int(smbus_read_byte)(struct smbus_device *device, uint8_t chip_address);

/**
 * Read data over smbus at given address.
 *
 * @param device: smbus device.
 * @param chip_address: Chip address to target.
 * @param address: Address on device to read.
 * @param buf: Buffer to read into. Must be at least as big as given length.
 * @param length: Number of bytes to read.
 *
 * @return Bytes read or -1 for errors.
 */
typedef int(smbus_read_block)(struct smbus_device *device, uint8_t chip_address,
			      uint8_t address, void *buf, size_t length);

/**
 * Write data over smbus at given address.
 *
 * @param device: smbus device.
 * @param chip_address: Chip address to target.
 * @param address: Address on device to write.
 * @param buf: Buffer to write from. Must be at least as big as given length.
 * @param length: Number of bytes to write.
 *
 * @return Bytes written or -1 for errors.
 */
typedef int(smbus_write_block)(struct smbus_device *device,
			       uint8_t chip_address, uint8_t address, void *buf,
			       size_t length);

/**
 * Read the Alert Receiving Address.
 *
 * Switches to the alert receiving address and reads the byte before switching
 * back to the active chip address.
 *
 * @param device: smbus device.
 * @param ara_address: Where to read alerting address.
 *
 * @return -1 on error. ARA (uint8_t) on success (mask with 0xff).
 */
typedef int(smbus_read_ara)(struct smbus_device *device, uint8_t ara_address);

/**
 * Blocks until a GPIO interrupt is seen.
 *
 * This api blocks until an interrupt is received or the smbus_device is cleaned
 * up. The caller is responsible for making sure sharing the |device| is not
 * destroyed while being polled.
 *
 * @param device: smbus device.
 *
 * @return 0 on success. -1 on error.
 */
typedef int(smbus_block_for_interrupt)(struct smbus_device *device);

/**
 * Clean up the given smbus driver. Call before freeing.
 *
 * @param driver: Driver object to clean up.
 */
typedef void(smbus_cleanup)(struct smbus_driver *driver);

/**
 * General driver for smbus access.
 */
struct smbus_driver {
	struct smbus_device *dev;

	smbus_read_byte *read_byte;
	smbus_read_block *read_block;
	smbus_write_block *write_block;

	smbus_read_ara *read_ara;
	smbus_block_for_interrupt *block_for_interrupt;

	smbus_cleanup *cleanup;
};

#endif // UM_PPM_INCLUDE_SMBUS_H_
