/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UM_PPM_RTS5453_H_
#define UM_PPM_RTS5453_H_

#include "include/pd_driver.h"
#include "include/smbus.h"

#define RTS_DEFAULT_PORT 0

/* Forward declaration only. */
struct rts5453_device;

struct rts5453_ic_status {
	uint8_t code_location;
	uint16_t reserved_0;

	uint8_t major_version;
	uint8_t minor_version;
	uint8_t patch_version;
	uint16_t reserved_1;

	uint8_t pd_typec_status;
	uint8_t vid[2];
	uint8_t pid[2];
	uint8_t reserved_2;

	uint8_t flash_bank;
	uint8_t reserved_3[16];
} __attribute__((__packed__));

enum rts5453_flash_protect {
	RTS5453_FLASH_PROTECT_DISABLE = 0,
	RTS5453_FLASH_PROTECT_ENABLE = 1,
};

/* 32 - 3 [Count; ADDR_L; ADDR_H; WR_DATA_COUNT] */
#define FW_BLOCK_CHUNK_SIZE 29

int rts5453_vendor_cmd_disable(struct rts5453_device *dev, uint8_t port);
int rts5453_vendor_cmd_enable_smbus(struct rts5453_device *dev, uint8_t port);
int rts5453_vendor_cmd_enable_smbus_flash_access(struct rts5453_device *dev,
						 uint8_t port);

int rts5453_set_flash_protection(struct rts5453_device *dev, int flash_protect);
int rts5453_isp_validation(struct rts5453_device *dev);
int rts5453_reset_to_flash(struct rts5453_device *dev);

/**
 * Write data to a specific flash bank at a specific offset.
 *
 * @param dev - Data for this device.
 * @param flash_bank - The flash bank to write to.
 * @param inbuf - The buffer to write.
 * @param size - Size of |inbuf|.
 * @param offset - Offset in flash to write at.
 */
int rts5453_write_to_flash(struct rts5453_device *dev, int flash_bank,
			   const char *inbuf, uint8_t size, size_t offset);

int rts5453_get_ic_status(struct rts5453_device *dev,
			  struct rts5453_ic_status *status);

/* Establish connection and get basic info about the PD controller. */
int rts5453_get_info(struct ucsi_pd_driver *pd);

/* Firmware update for the PD controller. */
int rts5453_do_firmware_update(struct ucsi_pd_driver *pd, const char *filepath,
			       int dry_run);

/**
 * Open RTS5453 device using SMBUS driver.
 *
 * @param smbus_driver: Already open smbus connection.
 * @param config: Configuration for this driver.
 */
struct ucsi_pd_driver *rts5453_open(struct smbus_driver *smbus,
				    struct pd_driver_config *config);

/**
 * Get the driver configuration for the RTS5453 driver.
 */
struct pd_driver_config rts5453_get_driver_config();

#endif /* UM_PPM_RTS5453_H_ */
