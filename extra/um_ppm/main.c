/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/platform.h"
#include "rts5453.h"
#include "smbus_usermode.h"
#include "um_ppm_chardev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

/* Some extra functionality that's used by this binary that's not necessarily
 * for UCSI (such as getting info about the PD controller, firmware update,
 * etc).
 */
struct extra_driver_ops {
	int (*get_info)(struct ucsi_pd_driver *driver);
	int (*do_firmware_update)(struct ucsi_pd_driver *driver,
				  const char *filepath, int dry_run);

	// Establish communication with SMBUS transport LPMs.
	struct ucsi_pd_driver *(*smbus_lpm_open)(
		struct smbus_driver *smbus, struct pd_driver_config *config);
};

struct extra_driver_ops rts5453_ops = {
	.get_info = rts5453_get_info,
	.do_firmware_update = rts5453_do_firmware_update,
	.smbus_lpm_open = rts5453_open,
};

// Set up the um_ppm device to start communicating with kernel.
int cdev_prepare_um_ppm(char *um_test_devpath, struct ucsi_pd_driver *pd,
			struct smbus_driver *smbus,
			struct pd_driver_config *config)
{
	// Open the kernel um_ppm chardev to establish the PPM communication.
	struct um_ppm_cdev *cdev =
		um_ppm_cdev_open(um_test_devpath, pd, smbus, config);

	if (!cdev) {
		ELOG("Failed to initialize PPM chardev. Exit early!");
		return -1;
	}

	// TODO - Register sigterm handler so we know when to exit.

	// Mainloop with chardev handling.
	um_ppm_cdev_mainloop(cdev);

	return 0;
}

static const char *usage_str =
	("um_ppm [options]\n"
	 "\n"
	 "Userspace PPM implementation and helper functions. Use this to\n"
	 "develop against and test new devkits.\n"
	 "\n"
	 "General options:\n"
	 "\t-p        PD driver config to use. Valid values: ['rts5453']\n"
	 "\t-b        I2C Bus number (/dev/i2c-N) (required)\n"
	 "\t-g        /dev/gpiochip[N] (required)\n"
	 "\t-l        Gpio line for LPM alert (required)\n"
	 "\t-v        Enable verbose logs\n"
	 "Actions (exclusive):\n"
	 "\t-f <path>     Do firmware update with file at this path\n"
	 "\t-k <dev path> Attach ucsi_um_kernel driver via this chardev path\n"
	 "\t-d            Demo commands\n");

void usage()
{
	printf(usage_str);
}

int main(int argc, char *argv[])
{
	int opt;

	int fwupdate = 0;
	char *fwupdate_file = NULL;
	int demo = 0;
	int i2c_bus = -1;
	int i2c_chip_address = -1;
	int gpio_chip = -1;
	int gpio_line = -1;
	int attach_to_kernel = 0;
	char *ucsi_um_kernel_dev = NULL;
	char *driver_config_in = NULL;
	struct pd_driver_config driver_config;
	struct extra_driver_ops *ops;

	while ((opt = getopt(argc, argv, ":f:k:dvb:p:g:l:")) != -1) {
		switch (opt) {
		case 'b':
			i2c_bus = strtol(optarg, NULL, 10);
			break;
		case 'p':
			driver_config_in = strdup(optarg);
			break;
		case 'g':
			gpio_chip = strtol(optarg, NULL, 10);
			break;
		case 'l':
			gpio_line = strtol(optarg, NULL, 10);
			break;
		case 'f':
			fwupdate = 1;
			fwupdate_file = strdup(optarg);
			break;
		case 'k':
			attach_to_kernel = 1;
			ucsi_um_kernel_dev = strdup(optarg);
			break;
		case 'd':
			demo = 1;
			break;
		case 'v':
			platform_set_debug(true);
			break;
		case ':':
			printf("Missing arg for %c\n", optopt);
			usage();
			return -1;
		case '?':
		default:
			printf("Unknown option: %c\n", optopt);
			usage();
			return -1;
		}
	}

	struct smbus_driver *smbus = NULL;
	struct ucsi_pd_driver *pd_driver = NULL;

	if (i2c_bus == -1) {
		ELOG("Invalid bus (%d) parameter", i2c_bus);
		return -1;
	}

	if (!driver_config_in) {
		ELOG("No driver config given. Defaulting to rts5453.");
		driver_config_in = "rts5453";
	}

	// Get driver config
	if (strcmp(driver_config_in, "rts5453") == 0) {
		driver_config = rts5453_get_driver_config();
		ops = &rts5453_ops;

		// Use port-0 for smbus addressing
		i2c_chip_address = driver_config.port_address_map[0];
	} else {
		ELOG("Unsupported PD driver config: %s", driver_config_in);
		return -1;
	}

	// Open usermode smbus.
	smbus = smbus_um_open(i2c_bus, i2c_chip_address, gpio_chip, gpio_line);
	if (!smbus) {
		ELOG("Failed to open smbus");
		goto handle_error;
	}

	// Open PD driver
	pd_driver = ops->smbus_lpm_open(smbus, &driver_config);
	if (!pd_driver) {
		ELOG("Failed to open PD driver.");
		goto handle_error;
	}

	DLOG("RTS5453 is initialized. Now taking desired action...");

	if (demo) {
		return ops->get_info(pd_driver);
	} else if (fwupdate && fwupdate_file) {
		return ops->do_firmware_update(pd_driver, fwupdate_file,
					       /*dry_run=*/0);
	} else if (attach_to_kernel) {
		return cdev_prepare_um_ppm(ucsi_um_kernel_dev, pd_driver, smbus,
					   &driver_config);
	}

	return 0;

handle_error:
	if (smbus) {
		smbus->cleanup(smbus);
		free(smbus);
	}

	if (pd_driver) {
		pd_driver->cleanup(pd_driver);
		free(pd_driver);
	}

	return -1;
}
