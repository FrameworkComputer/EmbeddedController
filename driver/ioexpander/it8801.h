/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * IT8801 is an I/O expander with the keyboard matrix controller.
 *
 */

#ifndef __CROS_EC_IO_EXPANDER_IT8801_H
#define __CROS_EC_IO_EXPANDER_IT8801_H

/* I2C address flags (7-bit without R/W) */
#define IT8801_I2C_ADDR1 0x38
#define IT8801_I2C_ADDR2 0x39

/* Keyboard Matrix Scan control (KBS) */
#define IT8801_REG_KSOMCR 0x40
#define IT8801_REG_MASK_KSOSDIC BIT(7)
#define IT8801_REG_MASK_KSE BIT(6)
#define IT8801_REG_MASK_AKSOSC BIT(5)
#define IT8801_REG_KSIDR 0x41
#define IT8801_REG_KSIEER 0x42
#define IT8801_REG_KSIIER 0x43
#define IT8801_REG_SMBCR 0xfa
#define IT8801_REG_MASK_ARE BIT(4)
#define IT8801_REG_GIECR 0xfb
#define IT8801_REG_MASK_GKSIIE BIT(3)
#define IT8801_REG_GPIO10 0x12
#define IT8801_REG_GPIO00_KSO19 0x0a
#define IT8801_REG_GPIO01_KSO18 0x0b
#define IT8801_REG_GPIO22_KSO21 0x1c
#define IT8801_REG_GPIO23_KSO20 0x1d
#define IT8801_REG_MASK_GPIOAFS_PULLUP BIT(7)
#define IT8801_REG_MASK_GPIOAFS_FUNC2 BIT(6)
#define IT8801_REG_MASK_GPIODIR BIT(5)
#define IT8801_REG_MASK_GPIOPUE BIT(0)
#define IT8801_REG_GPIO23SOV BIT(3)
#define IT8801_REG_MASK_SELKSO2 0x02
#define IT8801_REG_GISR 0xF9
#define IT8801_REG_MASK_GISR_GKSIIS BIT(6)
#define IT8801_REG_MASK_GISR_GGPIOG2IS BIT(2)
#define IT8801_REG_MASK_GISR_GGPIOG1IS BIT(1)
#define IT8801_REG_MASK_GISR_GGPIOG0IS BIT(0)
#define IT8801_REG_MASK_GISR_GGPIOGXIS                                     \
	(IT8801_REG_MASK_GISR_GGPIOG2IS | IT8801_REG_MASK_GISR_GGPIOG1IS | \
	 IT8801_REG_MASK_GISR_GGPIOG0IS)
#define IT8801_REG_LBVIDR 0xFE
#define IT8801_REG_HBVIDR 0xFF
#define IT8801_KSO_COUNT 18

/* General Purpose I/O Port (GPIO) */
#define IT8801_SUPPORT_GPIO_FLAGS                                            \
	(GPIO_OPEN_DRAIN | GPIO_INPUT | GPIO_OUTPUT | GPIO_LOW | GPIO_HIGH | \
	 GPIO_INT_ANY)

#define IT8801_REG_MASK_GPIOAFS_FUNC1 (0x00 << 7)

/* IT8801 only supports GPIO 0/1/2 */
#define IT8801_VALID_GPIO_G0_MASK 0xD9
#define IT8801_VALID_GPIO_G1_MASK 0x3F
#define IT8801_VALID_GPIO_G2_MASK 0x0F

extern __override_proto const uint8_t it8801_kso_mapping[];
extern const struct ioexpander_drv it8801_ioexpander_drv;

/* GPIO Register map */
/* Input pin status register */
#define IT8801_REG_GPIO_IPSR(port) (0x00 + (port))
/* Set output value register */
#define IT8801_REG_GPIO_SOVR(port) (0x05 + (port))
/* Control register */
#define IT8801_REG_GPIO_CR(port, mask) \
	(0x0A + (port) * 8 + GPIO_MASK_TO_NUM(mask))
/* Interrupt status register */
#define IT8801_REG_GPIO_ISR(port) (0x32 + (port))
/* Interrupt enable register */
#define IT8801_REG_GPIO_IER(port) (0x37 + (port))

/* Control register values */
#define IT8801_GPIOAFS_SHIFT 6 /* bit 6~7 */

#define IT8801_GPIODIR BIT(5) /* direction, output=1 */
/* input pin */
#define IT8801_GPIOIOT_INT_RISING BIT(3)
#define IT8801_GPIOIOT_INT_FALLING BIT(4)

#define IT8801_GPIODIR BIT(5)
#define IT8801_GPIOIOT BIT(4)
#define IT8801_GPIOPOL BIT(2) /* polarity */
#define IT8801_GPIOPDE BIT(1) /* pull-down enable */
#define IT8801_GPIOPUE BIT(0) /* pull-up enable */

/* ISR for IT8801's SMB_INT# */
void io_expander_it8801_interrupt(enum gpio_signal signal);

#ifdef CONFIG_IO_EXPANDER_IT8801_PWM

/* Mapping PWM_CH_LED_* to it8801 channel */
struct it8801_pwm_t {
	int index;
};

extern const struct it8801_pwm_t it8801_pwm_channels[];
extern const struct kblight_drv kblight_it8801;

/* standard pwm interface as defined in pwm.h */
void it8801_pwm_enable(enum pwm_channel ch, int enabled);
int it8801_pwm_get_enabled(enum pwm_channel ch);
void it8801_pwm_set_raw_duty(enum pwm_channel ch, uint16_t duty);
uint16_t it8801_pwm_get_raw_duty(enum pwm_channel ch);
void it8801_pwm_set_duty(enum pwm_channel ch, int percent);
int it8801_pwm_get_duty(enum pwm_channel ch);

#define IT8801_REG_PWMODDSR 0x5F
#define IT8801_REG_PWMMCR(n) (0x60 + ((n)-1) * 8)
#define IT8801_REG_PWMDCR(n) (0x64 + ((n)-1) * 8)
#define IT8801_REG_PWMPRSL(n) (0x66 + ((n)-1) * 8)
#define IT8801_REG_PWMPRSM(n) (0x67 + ((n)-1) * 8)

#define IT8801_PWMMCR_MCR_MASK 0x3
#define IT8801_PWMMCR_MCR_OFF 0
#define IT8801_PWMMCR_MCR_BLINKING 1
#define IT8801_PWMMCR_MCR_BREATHING 2
#define IT8801_PWMMCR_MCR_ON 3

#endif /* CONFIG_IO_EXPANDER_IT8801_PWM */

#endif /* __CROS_EC_KBEXPANDER_IT8801_H */
