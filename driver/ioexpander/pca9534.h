/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9534 I/O expander
 */

#ifndef __CROS_EC_IOEXPANDER_PCA9534_H
#define __CROS_EC_IOEXPANDER_PCA9534_H

#define PCA9534_REG_INPUT  0x0
#define PCA9534_REG_OUTPUT 0x1
#define PCA9534_REG_CONFIG 0x3

#define PCA9534_OUTPUT 0
#define PCA9534_INPUT  1

/*
 * Get input level. Note that this reflects the actual level on the
 * pin, even if the pin is configured as output.
 *
 * @param port  The I2C port of PCA9534.
 * @param addr  The address of PCA9534.
 * @param pin   The index of the pin to read.
 * @param level The pointer to where the read level is stored.
 *
 * @return EC_SUCCESS, or EC_ERROR_* on error.
 */
int pca9534_get_level(const int port, const uint16_t addr_flags,
		      int pin, int *level);

/*
 * Set output level. This function has no effect if the pin is
 * configured as input.
 *
 * @param port  The I2C port of PCA9534.
 * @param addr  The address of PCA9534.
 * @param pin   The index of the pin to set.
 * @param level The level to set.
 *
 * @return EC_SUCCESS, or EC_ERROR_* on error.
 */
int pca9534_set_level(const int port, const uint16_t addr_flags,
		      int pin, int level);

/*
 * Config a pin as input or output.
 *
 * @param port     The I2C port of PCA9534.
 * @param addr     The address of PCA9534.
 * @param pin      The index of the pin to set.
 * @param is_input PCA9534_INPUT or PCA9534_OUTPUT.
 *
 * @return EC_SUCCESS, or EC_ERROR_* on error.
 */
int pca9534_config_pin(const int port, const uint16_t addr_flags,
		       int pin, int is_input);

#endif  /* __CROS_EC_IOEXPANDER_PCA9534_H */
