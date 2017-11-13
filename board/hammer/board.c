/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Hammer board configuration */

#include "common.h"
#include "ec_version.h"
#include "touchpad.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "printf.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "rollback.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_descriptor.h"
#include "usb_i2c.h"
#include "util.h"

#include "gpio_list.h"

#ifdef SECTION_IS_RW
#define CROS_EC_SECTION "RW"
#else
#define CROS_EC_SECTION "RO"
#endif

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Hammer"),
	[USB_STR_SERIALNO]     = 0,
	[USB_STR_VERSION]      =
			USB_STRING_DESC(CROS_EC_SECTION ":" CROS_EC_VERSION32),
	[USB_STR_I2C_NAME]     = USB_STRING_DESC("I2C"),
	[USB_STR_UPDATE_NAME]  = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support I2C bridging over USB, this requires usb_i2c_board_enable and
 * usb_i2c_board_disable to be defined to enable and disable the I2C bridge.
 */

#ifdef SECTION_IS_RW
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 400,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef BOARD_STAFF
#define KBLIGHT_PWM_FREQ 100 /* Hz */
#else
#define KBLIGHT_PWM_FREQ 10000 /* Hz */
#endif

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{STM32_TIM(TIM_KBLIGHT), STM32_TIM_CH(1), 0, KBLIGHT_PWM_FREQ},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

int usb_i2c_board_enable(void) { return EC_SUCCESS; }
void usb_i2c_board_disable(void) {}

int usb_i2c_board_is_enabled(void)
{
	/* Disable I2C passthrough when the system is locked */
	return !system_is_locked();
}

#ifdef CONFIG_KEYBOARD_BOARD_CONFIG
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 50,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
#endif
#endif

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;

	/* Remap USART DMA to match the USART driver */
	/*
	 * the DMA mapping is :
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= (1 << 9) | (1 << 10); /* Remap USART1 RX/TX DMA */
}

/*
 * Side-band USB wake, to be able to wake lid even in deep S3, when USB
 * controller is off.
 */
void board_usb_wake(void)
{
	/*
	 * Poke detection pin for about 500us, we disable interrupts
	 * to make sure that we do not get preempted (setting GPIO high
	 * for too long would prevent pulse detection on lid EC side from
	 * working properly, or even kill hammer power if it is held for
	 * longer than debounce time).
	 */
	interrupt_disable();
	gpio_set_flags(GPIO_BASE_DET, GPIO_OUT_HIGH);
	udelay(500);
	gpio_set_flags(GPIO_BASE_DET, GPIO_INPUT);
	interrupt_enable();
}

/*
 * Get entropy based on Clock Recovery System, which is enabled on hammer to
 * synchronize USB SOF with internal oscillator.
 */
int board_get_entropy(void *buffer, int len)
{
	int i = 0;
	uint8_t *data = buffer;
	uint32_t start;
	/* We expect one SOF per ms, so wait at most 2ms. */
	const uint32_t timeout = 2*MSEC;

	for (i = 0; i < len; i++) {
		STM32_CRS_ICR |= STM32_CRS_ICR_SYNCOKC;
		start = __hw_clock_source_read();
		while (!(STM32_CRS_ISR & STM32_CRS_ISR_SYNCOKF)) {
			if ((__hw_clock_source_read() - start) > timeout)
				return 0;
			usleep(500);
		}
		/* Pick 8 bits, including FEDIR and 7 LSB of FECAP. */
		data[i] = STM32_CRS_ISR >> 15;
	}

	return 1;
}

/*
 * Generate a USB serial number from unique chip ID.
 */
const char *board_read_serial(void)
{
	static char str[CONFIG_SERIALNO_LEN];

	if (str[0] == '\0') {
		uint8_t *id;
		int pos = 0;
		int idlen = system_get_chip_unique_id(&id);
		int i;

		for (i = 0; i < idlen && pos < sizeof(str); i++, pos += 2) {
			snprintf(&str[pos], sizeof(str)-pos,
				"%02x", id[i]);
		}
	}

	return str;
}

int board_write_serial(const char *serialno)
{
	return 0;
}

