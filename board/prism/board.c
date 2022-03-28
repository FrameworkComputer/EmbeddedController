/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "clock.h"
#include "common.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "printf.h"
#include "registers.h"
#include "rgb_keyboard.h"
#include "rollback.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "util.h"

#include "gpio_list.h"

#ifdef SECTION_IS_RW
#define CROS_EC_SECTION "RW"
#else
#define CROS_EC_SECTION "RO"
#endif

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Prism"),
	[USB_STR_SERIALNO]     = 0,
	[USB_STR_VERSION]      =
			USB_STRING_DESC(CROS_EC_SECTION ":" CROS_EC_VERSION32),
	[USB_STR_UPDATE_NAME]  = USB_STRING_DESC("Firmware update"),
	[USB_STR_HOSTCMD_NAME]  = USB_STRING_DESC("Host command"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support I2C bridging over USB.
 */

#ifdef SECTION_IS_RW
const struct spi_device_t spi_devices[] = {
	[SPI_RGB0_DEVICE_ID] = {
			CONFIG_SPI_RGB_PORT,
			2, /* 2: Fpclk/8 = 48Mhz/8 = 6Mhz */
			GPIO_SPI1_CS1_L },
	[SPI_RGB1_DEVICE_ID] = { CONFIG_SPI_RGB_PORT, 2, GPIO_SPI1_CS2_L },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/*
 * RGB Keyboard config & data
 */
BUILD_ASSERT(RGB_GRID0_ROW == RGB_GRID1_ROW);

extern struct rgbkbd_drv is31fl3743b_drv;

static struct rgb_s grid0[RGB_GRID0_COL * RGB_GRID0_ROW];
static struct rgb_s grid1[RGB_GRID1_COL * RGB_GRID1_ROW];

struct rgbkbd rgbkbds[] = {
	[0] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &is31fl3743b_drv,
			.spi = SPI_RGB0_DEVICE_ID,
			.col_len = RGB_GRID0_COL,
			.row_len = RGB_GRID0_ROW,
		},
		.buf = grid0,
	},
	[1] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &is31fl3743b_drv,
			.spi = SPI_RGB1_DEVICE_ID,
			.col_len = RGB_GRID1_COL,
			.row_len = RGB_GRID1_ROW,
		},
		.buf = grid1,
	},
};
const uint8_t rgbkbd_count = ARRAY_SIZE(rgbkbds);
const uint8_t rgbkbd_hsize = RGB_GRID0_COL + RGB_GRID1_COL;
const uint8_t rgbkbd_vsize = RGB_GRID0_ROW;

const uint8_t rgbkbd_map[] = {
	RGBKBD_DELM,
	RGBKBD_COORD( 0, 0), RGBKBD_DELM,	/* `~ */
	RGBKBD_COORD( 1, 0), RGBKBD_DELM,	/* 1! */
	RGBKBD_COORD( 2, 0), RGBKBD_DELM,	/* 2@ */
	RGBKBD_COORD( 3, 0), RGBKBD_DELM,	/* 3# */
	RGBKBD_COORD( 4, 0), RGBKBD_DELM,	/* 4$ */
	RGBKBD_COORD( 5, 0), RGBKBD_DELM,
	RGBKBD_COORD( 6, 0), RGBKBD_DELM,
	RGBKBD_COORD( 7, 0), RGBKBD_DELM,
	RGBKBD_COORD( 8, 0), RGBKBD_DELM,
	RGBKBD_COORD( 9, 0), RGBKBD_DELM,
	RGBKBD_COORD(10, 0), RGBKBD_DELM,
	RGBKBD_COORD(11, 0), RGBKBD_DELM,
	RGBKBD_COORD(12, 0), RGBKBD_DELM,
	RGBKBD_COORD(13, 0), RGBKBD_DELM,
	RGBKBD_COORD(14, 0), RGBKBD_DELM,
	RGBKBD_COORD(15, 0), RGBKBD_DELM,
	RGBKBD_DELM,
	RGBKBD_DELM,
	RGBKBD_DELM,
	RGBKBD_DELM,
	RGBKBD_DELM,
};
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

__override void board_enable_rgb_keyboard(bool enable)
{
	gpio_set_level(GPIO_RGBKBD_POWER, 1);
	msleep(10);
}

#endif

/******************************************************************************
 * Initialize board.
 */
static int has_keyboard_backlight;

#ifdef SECTION_IS_RW
static void board_init(void)
{
	spi_enable(&spi_devices[SPI_RGB0_DEVICE_ID], 0);
	spi_enable(&spi_devices[SPI_RGB1_DEVICE_ID], 0);

	/* Set all SPI pins to high speed */
	/* pins A1, 2, 5, 6, 7 */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0x0000fc3c;

	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= STM32_RCC_PB2_SPI1;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_PB2_SPI1;
	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;

	clock_wait_bus_cycles(BUS_APB, 1);
	/* Enable SPI for RGB matrix. */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);
	spi_enable(&spi_devices[SPI_RGB0_DEVICE_ID], 1);
	spi_enable(&spi_devices[SPI_RGB1_DEVICE_ID], 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_INIT_SPI - 1);
#endif /* SECTION_IS_RW */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/* Remap USART DMA to match the USART driver */
	/*
	 * the DMA mapping is :
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= BIT(9) | BIT(10); /* Remap USART1 RX/TX DMA */
}

int board_has_keyboard_backlight(void)
{
	return has_keyboard_backlight;
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
__override const char *board_read_serial(void)
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

__override int board_write_serial(const char *serialno)
{
	return 0;
}
