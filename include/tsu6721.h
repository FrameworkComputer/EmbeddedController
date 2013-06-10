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
	TSU6721_MUX_AUTO  = 0x00,
	TSU6721_MUX_USB   = 0x24,
	TSU6721_MUX_AUDIO = 0x48,
	TSU6721_MUX_UART  = 0x6C,
};

#define TSU6721_INT_ATTACH		0x0001
#define TSU6721_INT_DETACH		0x0002
#define TSU6721_INT_KP			0x0004
#define TSU6721_INT_LKP			0x0008
#define TSU6721_INT_LKR			0x0010
#define TSU6721_INT_OVP_EN		0x0020
#define TSU6721_INT_OCP_EN		0x0040
#define TSU6721_INT_OVP_OCP_OTP_DIS	0x0080
#define TSU6721_INT_AV_CHANGE		0x0100
#define TSU6721_INT_RES_ATTACH		0x0200
#define TSU6721_INT_ADC_CHANGE		0x0400
#define TSU6721_INT_STUCK_KEY		0x0800
#define TSU6721_INT_STUCK_KEY_RCV	0x1000
#define TSU6721_INT_CONNECT		0x2000
#define TSU6721_INT_OTP_EN		0x4000
#define TSU6721_INT_VBUS		0x8000

#define TSU6721_TYPE_NONE		0x000000
#define TSU6721_TYPE_OTG		0x000080
#define TSU6721_TYPE_DCP		0x000040
#define TSU6721_TYPE_CDP		0x000020
#define TSU6721_TYPE_CHG12		0x000010
#define TSU6721_TYPE_UART		0x000008
#define TSU6721_TYPE_USB_HOST		0x000004
#define TSU6721_TYPE_AUDIO2		0x000002
#define TSU6721_TYPE_AUDIO1		0x000001
#define TSU6721_TYPE_AUDIO3		0x008000
#define TSU6721_TYPE_JIG_UART_ON	0x000400
#define TSU6721_TYPE_U200_CHG		0x400000
#define TSU6721_TYPE_APPLE_CHG		0x200000
#define TSU6721_TYPE_NON_STD_CHG	0x040000
/* VBUS_DEBOUNCED might show up together with other type */
#define TSU6721_TYPE_VBUS_DEBOUNCED	0x020000

/* Initialize TSU6721. */
int tsu6721_init(void);

/* Read TSU6721 register. */
uint8_t tsu6721_read(uint8_t reg);

/* Write TSU6721 register. */
int tsu6721_write(uint8_t reg, uint8_t val);

/* Enable interrupts. */
int tsu6721_enable_interrupts(void);

/* Disable all interrupts. */
int tsu6721_disable_interrupts(void);

/* Set interrupt mask. */
int tsu6721_set_interrupt_mask(uint16_t mask);

/* Get and clear current interrupt status. Return value is a combination of
 * TSU6721_INT_* */
int tsu6721_get_interrupts(void);

/* Get but keep interrupt status. Return value is a combination of
 * TSU6721_INT_* */
int tsu6721_peek_interrupts(void);

/* Get attached device type. Return value is one or a combination of
 * TSU6721_TYPE_* */
int tsu6721_get_device_type(void);

/* Control TSU6721 mux. */
int tsu6721_mux(enum tsu6721_mux sel);

/* Reset TSU6721. */
void tsu6721_reset(void);

#endif /* TSU6721_H */
