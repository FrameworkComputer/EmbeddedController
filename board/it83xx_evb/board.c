/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* IT83xx development board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "it83xx_pd.h"
#include "ec2i_chip.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#if IT83XX_PD_EVB
int board_get_battery_soc(void)
{
	return 100;
}

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{-1, -1, &it83xx_tcpm_drv},
	{-1, -1, &it83xx_tcpm_drv},
};

void board_pd_vconn_ctrl(int port, int cc_pin, int enabled)
{
	int cc1_enabled = 0, cc2_enabled = 0;

	if (cc_pin)
		cc2_enabled = enabled;
	else
		cc1_enabled = enabled;

	if (port) {
		gpio_set_level(GPIO_USBPD_PORTB_CC2_VCONN, cc2_enabled);
		gpio_set_level(GPIO_USBPD_PORTB_CC1_VCONN, cc1_enabled);
	} else {
		gpio_set_level(GPIO_USBPD_PORTA_CC2_VCONN, cc2_enabled);
		gpio_set_level(GPIO_USBPD_PORTA_CC1_VCONN, cc1_enabled);
	}
}

void board_pd_vbus_ctrl(int port, int enabled)
{
	if (port) {
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_INPUT, !enabled);
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_OUTPUT, enabled);
		if (!enabled) {
			gpio_set_level(GPIO_USBPD_PORTB_VBUS_DROP, 1);
			udelay(MSEC);
		}
		gpio_set_level(GPIO_USBPD_PORTB_VBUS_DROP, 0);
	} else {
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_INPUT, !enabled);
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_OUTPUT, enabled);
		if (!enabled) {
			gpio_set_level(GPIO_USBPD_PORTA_VBUS_DROP, 1);
			udelay(MSEC);
		}
		gpio_set_level(GPIO_USBPD_PORTA_VBUS_DROP, 0);
	}
}
#else
/* EC EVB */
void pd_task(void)
{
	while (1)
		task_wait_event(-1);
}
#endif

#include "gpio_list.h"

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	{7, 0,                     30000, PWM_PRESCALER_C4},
};

BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1500,
	 .rpm_start = 1500,
	 .rpm_max = 6500,
	/*
	 * index of pwm_channels, not pwm output channel.
	 * pwm output channel is member "channel" of pwm_t.
	 */
	 .ch = 0,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/*
 * PWM HW channelx binding tachometer channelx for fan control.
 * Four tachometer input pins but two tachometer modules only,
 * so always binding [TACH_CH_TACH0A | TACH_CH_TACH0B] and/or
 * [TACH_CH_TACH1A | TACH_CH_TACH1B]
 */
const struct fan_tach_t fan_tach[] = {
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_NULL,  -1, -1, -1},
	{TACH_CH_TACH0A, 2, 50, 30},
};
BUILD_ASSERT(ARRAY_SIZE(fan_tach) == PWM_HW_CH_TOTAL);

/* PNPCFG settings */
const struct ec2i_t pnpcfg_settings[] = {
	/* Select logical device 06h(keyboard) */
	{HOST_INDEX_LDN, LDN_KBC_KEYBOARD},
	/* Set IRQ=01h for logical device */
	{HOST_INDEX_IRQNUMX, 0x01},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 05h(mouse) */
	{HOST_INDEX_LDN, LDN_KBC_MOUSE},
	/* Set IRQ=0Ch for logical device */
	{HOST_INDEX_IRQNUMX, 0x0C},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 11h(PM1 ACPI) */
	{HOST_INDEX_LDN, LDN_PMC1},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 12h(PM2) */
	{HOST_INDEX_LDN, LDN_PMC2},
	/* I/O Port Base Address 200h/204h */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0x00},
	{HOST_INDEX_IOBAD1_MSB, 0x02},
	{HOST_INDEX_IOBAD1_LSB, 0x04},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 0Fh(SMFI) */
	{HOST_INDEX_LDN, LDN_SMFI},
	/* H2RAM LPC I/O cycle Dxxx */
	{HOST_INDEX_DSLDC6, 0x00},
	/* Enable H2RAM LPC I/O cycle */
	{HOST_INDEX_DSLDC7, 0x01},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},

	/* Select logical device 17h(PM3) */
	{HOST_INDEX_LDN, LDN_PMC3},
	/* I/O Port Base Address 80h */
	{HOST_INDEX_IOBAD0_MSB, 0x00},
	{HOST_INDEX_IOBAD0_LSB, 0x80},
	{HOST_INDEX_IOBAD1_MSB, 0x00},
	{HOST_INDEX_IOBAD1_LSB, 0x00},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
	/* Select logical device 10h(RTCT) */
	{HOST_INDEX_LDN, LDN_RTCT},
	/* P80L Begin Index */
	{HOST_INDEX_DSLDC4, P80L_P80LB},
	/* P80L End Index */
	{HOST_INDEX_DSLDC5, P80L_P80LE},
	/* P80L Current Index */
	{HOST_INDEX_DSLDC6, P80L_P80LC},
#ifdef CONFIG_UART_HOST
	/* Select logical device 2h(UART2) */
	{HOST_INDEX_LDN, LDN_UART2},
	/*
	 * I/O port base address is 2F8h.
	 * Host can use LPC I/O port 0x2F8 ~ 0x2FF to access UART2.
	 * See specification 7.24.4 for more detial.
	 */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0xF8},
	/* IRQ number is 3 */
	{HOST_INDEX_IRQNUMX, 0x03},
	/*
	 * Interrupt Request Type Select
	 * bit1, 0: IRQ request is buffered and applied to SERIRQ.
	 *       1: IRQ request is inverted before being applied to SERIRQ.
	 * bit0, 0: Edge triggered mode.
	 *       1: Level triggered mode.
	 */
	{HOST_INDEX_IRQTP, 0x02},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(pnpcfg_settings) == EC2I_SETTING_COUNT);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L, GPIO_LID_OPEN
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* Initialize board. */
static void board_init(void)
{
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"ADC_VBUSSA", 3000, 1024, 0, 0}, /*GPI0*/
	{"ADC_VBUSSB", 3000, 1024, 0, 1}, /*GPI1*/
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/*
 * I2C channels (A, B, and C) are using the same timing registers (00h~07h)
 * at default.
 * In order to set frequency independently for each channels,
 * We use timing registers 09h~0Bh, and the supported frequency will be:
 * 50KHz, 100KHz, 400KHz, or 1MHz.
 * I2C channels (D, E and F) can be set different frequency on different ports.
 * The I2C(D/E/F) frequency depend on the frequency of SMBus Module and
 * the individual prescale register.
 * The frequency of SMBus module is 24MHz on default.
 * The allowed range of I2C(D/E/F) frequency is as following setting.
 * SMBus Module Freq = PLL_CLOCK / ((IT83XX_ECPM_SCDCR2 & 0x0F) + 1)
 * (SMBus Module Freq / 510) <=  I2C Freq <= (SMBus Module Freq / 8)
 * Channel D has multi-function and can be used as UART interface.
 * Channel F is reserved for EC debug.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"battery", IT83XX_I2C_CH_C, 100, GPIO_I2C_C_SCL, GPIO_I2C_C_SDA},
	{"evb-1",   IT83XX_I2C_CH_A, 100, GPIO_I2C_A_SCL, GPIO_I2C_A_SDA},
	{"evb-2",   IT83XX_I2C_CH_B, 100, GPIO_I2C_B_SCL, GPIO_I2C_B_SDA},
	{"opt-4",   IT83XX_I2C_CH_E, 100, GPIO_I2C_E_SCL, GPIO_I2C_E_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, -1},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
