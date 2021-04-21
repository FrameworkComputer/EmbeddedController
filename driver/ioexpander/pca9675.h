/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA9675PW I/O Port expander driver header
 */

#ifndef __CROS_EC_IOEXPANDER_PCA9675_H
#define __CROS_EC_IOEXPANDER_PCA9675_H

/* PCA9675 IO pins that can be referenced in gpio.inc */
enum pca9675_io_pins {
	PCA9675_IO_P00,
	PCA9675_IO_P01,
	PCA9675_IO_P02,
	PCA9675_IO_P03,
	PCA9675_IO_P04,
	PCA9675_IO_P05,
	PCA9675_IO_P06,
	PCA9675_IO_P07,
	PCA9675_IO_P10,
	PCA9675_IO_P11,
	PCA9675_IO_P12,
	PCA9675_IO_P13,
	PCA9675_IO_P14,
	PCA9675_IO_P15,
	PCA9675_IO_P16,
	PCA9675_IO_P17,
};

/* Write 06 to address 00 to reset the PCA9675 to back to power up state */
#define PCA9675_RESET_SEQ_DATA 0x06

/* Default I/O directons of PCA9675 is input */
#define PCA9675_DEFAULT_IO_DIRECTION 0xffff

extern const struct ioexpander_drv pca9675_ioexpander_drv;

#endif /* __CROS_EC_IOEXPANDER_PCA9675_H */
