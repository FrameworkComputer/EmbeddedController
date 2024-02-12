/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/platform.h"
#include "smbus_usermode.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
#include <gpiod.h>
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH_SIZE 32

/* 10ms timeout for gpiod wakeup */
#define GPIOD_WAIT_TIMEOUT_NS (10 * 1000 * 1000)

#define GPIOD_CONSUMER "um_ppm"

/**
 * Internal structure for usermode smbus implementation.
 */
struct smbus_usermode_device {
	int fd;

	/* Currently active chip address. */
	uint8_t chip_address;

	pthread_mutex_t cmd_lock;
	pthread_mutex_t gpio_lock;
	struct gpiod_chip *chip;
	struct gpiod_line *line;

	volatile bool cleaning_up;
};

#define CAST_FROM(v) (struct smbus_usermode_device *)(v)

int __smbus_switch_address_nolock(struct smbus_usermode_device *dev,
				  uint8_t chip_address)
{
	int ret;

	/* No-op since we're already on the active chip. */
	if (dev->chip_address == chip_address) {
		return 0;
	}

	/* Make sure this returns -1 or 0. */
	ret = ioctl(dev->fd, I2C_SLAVE, chip_address);
	if (ret < 0) {
		ELOG("IOCTL switch to chip_address 0x%02x failed: %d",
		     chip_address, ret);
		return -1;
	}

	dev->chip_address = chip_address;

	return 0;
}

int __smbus_um_read_byte_nolock(struct smbus_usermode_device *dev)
{
	return i2c_smbus_read_byte(dev->fd);
}

int smbus_um_read_byte(struct smbus_device *device, uint8_t chip_address)
{
	struct smbus_usermode_device *dev = CAST_FROM(device);
	int ret = 0;

	if (dev->fd < 0) {
		ELOG("Saw fd of %d", dev->fd);
		return -1;
	}

	pthread_mutex_lock(&dev->cmd_lock);
	ret = __smbus_switch_address_nolock(dev, chip_address);
	if (ret == 0) {
		ret = __smbus_um_read_byte_nolock(dev);
	}
	pthread_mutex_unlock(&dev->cmd_lock);

	return ret;
}

int smbus_um_read_block(struct smbus_device *device, uint8_t chip_address,
			uint8_t address, void *buf, size_t length)
{
	struct smbus_usermode_device *dev = CAST_FROM(device);
	uint8_t local_data[32];
	int ret = 0;

	if (dev->fd < 0) {
		ELOG("Saw fd of %d", dev->fd);
		return -1;
	}

	/* Block read will read at most 32 bytes. */
	if (length > 32) {
		ELOG("Got length > 32 for block read");
		return -1;
	}

	pthread_mutex_lock(&dev->cmd_lock);

	DLOG("[0x%02x]: Reading block at 0x%02x", chip_address, address);

	ret = __smbus_switch_address_nolock(dev, chip_address);
	if (ret != 0) {
		goto unlock;
	}

	ret = i2c_smbus_read_block_data(dev->fd, address, local_data);
	if (ret <= 0) {
		goto unlock;
	}

	if (ret != length) {
		length = ret;
	}

	platform_memcpy(buf, local_data, length);
	DLOG_START("[0x%02x]: Reading data from %02x [", chip_address, address);
	for (int i = 0; i < length; ++i) {
		DLOG_LOOP("%02x, ", ((uint8_t *)buf)[i]);
	}
	DLOG_END("]");

unlock:
	pthread_mutex_unlock(&dev->cmd_lock);
	return ret;
}

int smbus_um_write_block(struct smbus_device *device, uint8_t chip_address,
			 uint8_t address, void *buf, size_t length)
{
	struct smbus_usermode_device *dev = CAST_FROM(device);
	int ret = 0;

	if (dev->fd < 0) {
		ELOG("Saw fd of %d", dev->fd);
		return -1;
	}

	DLOG_START("[0x%02x]: Sending data to %02x [", chip_address, address);
	for (int i = 0; i < length; ++i) {
		DLOG_LOOP("%02x, ", ((uint8_t *)buf)[i]);
	}
	DLOG_END("]");

	pthread_mutex_lock(&dev->cmd_lock);
	ret = __smbus_switch_address_nolock(dev, chip_address);
	if (ret != 0) {
		goto unlock;
	}
	ret = i2c_smbus_write_block_data(dev->fd, address, length, buf);

unlock:
	pthread_mutex_unlock(&dev->cmd_lock);
	return ret;
}

int smbus_um_read_ara(struct smbus_device *device, uint8_t ara_address)
{
	struct smbus_usermode_device *dev = CAST_FROM(device);
	uint8_t chip_address;
	int ret;

	pthread_mutex_lock(&dev->cmd_lock);

	/* Restore to this address. */
	chip_address = dev->chip_address;

	do {
		/* First set the I2C address to the alert receiving address
		 * (0xC).
		 */
		if (ioctl(dev->fd, I2C_SLAVE, ara_address) < 0) {
			ELOG("Couldn't switch to alert receiving address: 0x%x!",
			     ara_address);
			ret = -1;
			break;
		}

		/* ARA address will have 8 bits with top 7 bits of address.
		 * Right shift to get actual chip address. Even if ARA is wrong,
		 * we still need to restore slave address so don't exit yet.
		 */
		ret = __smbus_um_read_byte_nolock(dev);
		if (ret < 0) {
			ELOG("Failed to read ARA byte: %d", ret);
		} else {
			ret = (ret & 0xff) >> 1;
		}

		if (ioctl(dev->fd, I2C_SLAVE, chip_address) < 0) {
			ELOG("Couldn't restore chip address: 0x%x. ARA was 0x%x",
			     chip_address, ret);
			ret = -1;
		}

	} while (false);

	pthread_mutex_unlock(&dev->cmd_lock);

	return ret;
}

int smbus_um_block_for_interrupt(struct smbus_device *device)
{
	struct smbus_usermode_device *dev = CAST_FROM(device);
	int ret = 0;
	bool cleaning_up = false;
	struct timespec ts;

	if (!(dev->chip && dev->line)) {
		ELOG("Gpio not initialized for polling.");
		return -1;
	}

	if (dev->cleaning_up) {
		return -1;
	}

	ts.tv_sec = 0;
	ts.tv_nsec = GPIOD_WAIT_TIMEOUT_NS;

	DLOG("Polling for smbus interrupt.");

	do {
		pthread_mutex_lock(&dev->gpio_lock);

		ret = gpiod_line_event_wait(dev->line, &ts);
		cleaning_up = dev->cleaning_up;

		pthread_mutex_unlock(&dev->gpio_lock);

		/* If we're cleaning up, exit out with an error. */
		if (cleaning_up) {
			ret = -1;
			break;
		}

		/* Either error or result will break here. Otherwise, continue.
		 */
		if (ret == 1 || ret == -1) {
			break;
		}
	} while (true);

	/* Got an event. Clear the event before forwarding interrupt. */
	if (ret == 1) {
		struct gpiod_line_event event;
		DLOG("Got SMBUS interrupt!");

		/* First clear the line event. */
		if (gpiod_line_event_read(dev->line, &event) == -1) {
			ELOG("Failed to read line event.");
			ret = -1;
		}
	} else {
		DLOG("Smbus polling resulted in ret %d", ret);
	}

	return ret == 1 ? 0 : -1;
}

void smbus_um_cleanup(struct smbus_driver *driver)
{
	if (driver->dev) {
		struct smbus_usermode_device *dev = CAST_FROM(driver->dev);
		if (dev->fd) {
			close(dev->fd);
		}

		dev->cleaning_up = true;
		pthread_mutex_lock(&dev->gpio_lock);
		pthread_mutex_unlock(&dev->gpio_lock);

		free(driver->dev);
		driver->dev = NULL;
	}
}

static int init_interrupt(struct smbus_usermode_device *dev, int gpio_chip,
			  int gpio_line)
{
	struct gpiod_chip *chip = NULL;
	struct gpiod_line *line = NULL;
	char filename[MAX_PATH_SIZE];

	if (pthread_mutex_init(&dev->gpio_lock, NULL) != 0) {
		ELOG("Failed to init gpio mutex");
		return -1;
	}

	if (pthread_mutex_init(&dev->cmd_lock, NULL) != 0) {
		ELOG("Failed to init command lock");
		return -1;
	}

	/* Request gpiochip and lines */
	snprintf(filename, MAX_PATH_SIZE - 1, "/dev/gpiochip%d", gpio_chip);
	chip = gpiod_chip_open(filename);
	if (!chip) {
		ELOG("Failed to open %s", filename);
		goto cleanup;
	}

	line = gpiod_chip_get_line(chip, gpio_line);
	if (!line) {
		ELOG("Failed to get line %d", gpio_line);
		goto cleanup;
	}

	if (gpiod_line_request_falling_edge_events(line, GPIOD_CONSUMER) != 0) {
		ELOG("Failed to set line config.");
		goto cleanup;
	}

	dev->chip = chip;
	dev->line = line;

	return 0;

cleanup:
	if (line) {
		gpiod_line_release(line);
	}

	if (chip) {
		gpiod_chip_close(chip);
	}

	return -1;
}

struct smbus_driver *smbus_um_open(int bus_num, uint8_t chip_address,
				   int gpio_chip, int gpio_line)
{
	struct smbus_usermode_device *dev = NULL;
	struct smbus_driver *drv = NULL;
	int fd = -1;
	char filename[MAX_PATH_SIZE];

	/* Make sure we can open the i2c device. */
	snprintf(filename, MAX_PATH_SIZE - 1, "/dev/i2c-%d", bus_num);
	fd = open(filename, O_RDWR);
	if (fd < 0) {
		ELOG("Could not open i2c device.");
		return NULL;
	}

	/* Switch to a specific chip address. */
	if (ioctl(fd, I2C_SLAVE, chip_address) < 0) {
		ELOG("Could not switch to given chip address.");
		goto handle_error;
	}

	dev = calloc(1, sizeof(struct smbus_usermode_device));
	if (!dev) {
		goto handle_error;
	}

	dev->fd = fd;
	dev->chip_address = chip_address;

	/* Initialize the gpio lines */
	if (init_interrupt(dev, gpio_chip, gpio_line) == -1) {
		ELOG("Failed to initialize gpio for interrupt.");
		goto handle_error;
	}

	drv = calloc(1, sizeof(struct smbus_driver));
	if (!drv) {
		goto handle_error;
	}

	drv->dev = (struct smbus_device *)dev;
	drv->read_byte = smbus_um_read_byte;
	drv->read_block = smbus_um_read_block;
	drv->write_block = smbus_um_write_block;
	drv->read_ara = smbus_um_read_ara;
	drv->block_for_interrupt = smbus_um_block_for_interrupt;
	drv->cleanup = smbus_um_cleanup;

	/* Make sure chip address is valid before returning. */
	if (smbus_um_read_byte((struct smbus_device *)dev, chip_address) < 0) {
		ELOG("Could not read byte at given chip address.");
		goto handle_error;
	}

	return drv;

handle_error:
	close(fd);
	free(dev);
	free(drv);

	return NULL;
}
