/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TSU6721 USB port switch.
 */

#ifndef TSU6721_H
#define TSU6721_H

#define TSU6721_REG_DEV_ID    0x01
#define TSU6721_REG_CONTROL   0x02
#define TSU6721_REG_INT1      0x03
#define TSU6721_REG_INT2      0x04
#define TSU6721_REG_INT_MASK1 0x05
#define TSU6721_REG_INT_MASK2 0x06
#define TSU6721_REG_ADC       0x07
#define TSU6721_REG_TIMING1   0x08
#define TSU6721_REG_TIMING2   0x09
#define TSU6721_REG_DEV_TYPE1 0x0A
#define TSU6721_REG_DEV_TYPE2 0x0B
#define TSU6721_REG_BUTTON1   0x0C
#define TSU6721_REG_BUTTON2   0x0D
#define TSU6721_REG_MANUAL1   0x13
#define TSU6721_REG_MANUAL2   0x14
#define TSU6721_REG_DEV_TYPE3 0x15
#define TSU6721_REG_RESET     0x1B
#define TSU6721_REG_TIMER     0x20
#define TSU6721_REG_OCP1      0x21
#define TSU6721_REG_OCP2      0x22

#define TSU6721_CTRL_AUTO     (1 << 2)

enum tsu6721_mux {
	TSU6721_MUX_NONE  = 0x00,
	TSU6721_MUX_USB   = 0x24,
	TSU6721_MUX_AUDIO = 0x48,
	TSU6721_MUX_UART  = 0x6C,
};

uint8_t tsu6721_read(uint8_t reg);
void tsu6721_write(uint8_t reg, uint8_t val);

#endif /* TSU6721_H */
