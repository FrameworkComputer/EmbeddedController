/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LP5562 LED driver.
 */

#ifndef LP5562_H
#define LP5562_H

#define LP5562_REG_ENABLE	0x00
#define LP5562_REG_OP_MODE	0x01
#define LP5562_REG_B_PWM	0x02
#define LP5562_REG_G_PWM	0x03
#define LP5562_REG_R_PWM	0x04
#define LP5562_REG_B_CURRENT	0x05
#define LP5562_REG_G_CURRENT	0x06
#define LP5562_REG_R_CURRENT	0x07
#define LP5562_REG_CONFIG	0x08
#define LP5562_REG_ENG1_PC	0x09
#define LP5562_REG_ENG2_PC	0x0a
#define LP5562_REG_ENG3_PC	0x0b
#define LP5562_REG_STATUS	0x0c
#define LP5562_REG_RESET	0x0d
#define LP5562_REG_W_PWM	0x0e
#define LP5562_REG_W_CURRENT	0x0f
#define LP5562_REG_LED_MAP	0x70

/* Power on and initialize LP5562. */
int lp5562_poweron(void);

/* Power off LP5562. */
int lp5562_poweroff(void);

/* Set LED color. */
int lp5562_set_color(uint8_t red, uint8_t green, uint8_t blue);

#endif /* LP5562_H */
