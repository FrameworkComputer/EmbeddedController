/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/platform.h"
#include "rts5453.h"

#include <stdio.h>

#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

/* #define DO_FLASH_PROTECT */

int rts5453_do_firmware_update(struct ucsi_pd_driver *pd, const char *filepath,
			       int dry_run)
{
	struct rts5453_device *dev = (struct rts5453_device *)pd->dev;
	int fd;
	int flash_bank = 0;
	char fbuf[FW_BLOCK_CHUNK_SIZE];
	ssize_t bytes_read = 0;
	int offset = 0;

	struct rts5453_ic_status status;

	if (!filepath) {
		ELOG("Filepath was empty.");
		return -1;
	}

	DLOG("Fwupdate: File path is %s", filepath);
	/* Open the file descriptor */
	fd = open(filepath, O_RDONLY);
	if (fd < 0) {
		ELOG("Could not open file at %s", filepath);
		return -1;
	}

	/* Do fwupdate commands:
	 * - VENDOR_CMD_ENABLE (smbus)
	 * - GET_IC_STATUS to figure out which bank to write to
	 * - VENDOR_CMD_ENABLE (smbus|flash)
	 * - SET_FLASH_PROTECTION (unlock)
	 * - Loop
	 * - Write to bank 0/1 (32-3 = 27 bytes per loop)
	 * - VENDOR_CMD_ENABLE (smbus) (disable flash access)
	 * - ISP_VALIDATION
	 * - SET_FLASH_PROTECTION (lock)
	 * - (!dry_run) RESET_TO_FLASH
	 */

	if (rts5453_vendor_cmd_enable_smbus(dev, RTS_DEFAULT_PORT) == -1) {
		ELOG("Failed to enable vendor commands");
		return -1;
	}

	if (rts5453_get_ic_status(dev, &status) == -1) {
		ELOG("Failed to GET_IC_STATUS");
		goto cleanup;
	}

	/* Set the flash bank as the opposite of the one currently in-use */
	flash_bank = status.flash_bank == 1 ? 0 : 1;
	printf("Writing to flash_bank %d\n", flash_bank);

	if (rts5453_vendor_cmd_enable_smbus_flash_access(
		    dev, RTS_DEFAULT_PORT) == -1) {
		ELOG("Failed to enable flash access");
		goto cleanup;
	}

#ifdef DO_FLASH_PROTECT
	if (rts5453_set_flash_protection(dev, RTS5453_FLASH_PROTECT_DISABLE) ==
	    -1) {
		ELOG("Failed to disable flash protection");
		goto cleanup;
	}
#endif

	/* Keep writing while there's data in the firmware image. */
	while ((bytes_read = read(fd, fbuf, FW_BLOCK_CHUNK_SIZE)) > 0) {
		if (rts5453_write_to_flash(dev, flash_bank, fbuf, bytes_read,
					   offset) == -1) {
			ELOG("Failed to write to flash at bank %d (bytes = %d, offset = %d)",
			     flash_bank, (int)bytes_read, offset);
			goto cleanup;
		}

		offset += bytes_read;
	}

	if (rts5453_vendor_cmd_enable_smbus(dev, RTS_DEFAULT_PORT) == -1) {
		ELOG("Failed to disable smbus flash access.");
		goto cleanup;
	}

	if (rts5453_isp_validation(dev) == -1) {
		ELOG("Failed ISP validation.");
		goto cleanup;
	}

#ifdef DO_FLASH_PROTECT
	if (rts5453_set_flash_protection(dev, RTS5453_FLASH_PROTECT_ENABLE) ==
	    -1) {
		ELOG("Failed to enable flash protection");
		goto cleanup;
	}
#endif

	/* Only commit changes if not dry run */
	if (!dry_run) {
		if (rts5453_reset_to_flash(dev) == -1) {
			ELOG("Reset to flash failed.");
			goto cleanup;
		}
	}

	return 0;

cleanup:
	/* Protect flash and disable smbus */
	if (rts5453_vendor_cmd_disable(dev, RTS_DEFAULT_PORT) == -1) {
		ELOG("Failed to disable vendor commands and flash access");
		return -1;
	}

	return -1;
}

int rts5453_get_info(struct ucsi_pd_driver *pd)
{
	struct rts5453_device *dev = (struct rts5453_device *)pd->dev;
	struct rts5453_ic_status status;

	if (rts5453_vendor_cmd_enable_smbus(dev, RTS_DEFAULT_PORT) == -1) {
		ELOG("Failed to enable vendor commands");
		return -1;
	}

	if (rts5453_get_ic_status(dev, &status) == -1) {
		ELOG("Failed to get ic status");
		return -1;
	}

	printf("Code location (%s), Bank (%d)\n",
	       status.code_location ? "Flash" : "ROM", status.flash_bank);
	printf("Fw version: %d.%d.%d\n", status.major_version,
	       status.minor_version, status.patch_version);
	printf("VID:PID: %02x%02x:%02x%02x\n", status.vid_pid[1],
	       status.vid_pid[0], status.vid_pid[3], status.vid_pid[2]);

	return 0;
}
