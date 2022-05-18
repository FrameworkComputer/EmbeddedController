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
		.init = &rgbkbd_default,
		.buf = grid0,
	},
	[1] = {
		.cfg = &(const struct rgbkbd_cfg) {
			.drv = &is31fl3743b_drv,
			.spi = SPI_RGB1_DEVICE_ID,
			.col_len = RGB_GRID1_COL,
			.row_len = RGB_GRID1_ROW,
		},
		.init = &rgbkbd_default,
		.buf = grid1,
	},
};
const uint8_t rgbkbd_count = ARRAY_SIZE(rgbkbds);
const uint8_t rgbkbd_hsize = RGB_GRID0_COL + RGB_GRID1_COL;
const uint8_t rgbkbd_vsize = RGB_GRID0_ROW;

const uint8_t rgbkbd_map[] = {
	RGBKBD_DELM,				/* 0: (null) */
	RGBKBD_COORD( 0, 1), RGBKBD_DELM,	/* 1: ~ ` */
	RGBKBD_COORD( 1, 1), RGBKBD_COORD( 1, 2), RGBKBD_DELM,	/* 2: ! 1 */
	RGBKBD_COORD( 2, 1), RGBKBD_COORD( 2, 2), RGBKBD_DELM,	/* 3: @ 2 */
	RGBKBD_COORD( 3, 1), RGBKBD_COORD( 3, 2), RGBKBD_DELM,	/* 4: # 3 */
	RGBKBD_COORD( 4, 1), RGBKBD_COORD( 4, 2), RGBKBD_DELM,	/* 5: $ 4 */
	RGBKBD_COORD( 5, 1), RGBKBD_COORD( 5, 2), RGBKBD_DELM,	/* 6: % 5 */
	RGBKBD_COORD( 6, 1), RGBKBD_COORD( 6, 2), RGBKBD_DELM,	/* 7: ^ 6 */
	RGBKBD_COORD( 7, 1), RGBKBD_COORD( 7, 2), RGBKBD_DELM,	/* 8: & 7 */
	RGBKBD_COORD( 8, 1), RGBKBD_COORD( 8, 2), RGBKBD_DELM,	/* 9: * 8 */
	RGBKBD_COORD( 9, 1), RGBKBD_COORD( 9, 2), RGBKBD_DELM,	/* 10: ( 9 */
	RGBKBD_COORD(10, 1), RGBKBD_COORD(10, 2), RGBKBD_DELM,	/* 11: ) 0 */
	RGBKBD_COORD(11, 1), RGBKBD_COORD(11, 2), RGBKBD_DELM,	/* 12: _ - */
	RGBKBD_COORD(12, 1), RGBKBD_COORD(12, 2), RGBKBD_DELM,	/* 13: + = */
	RGBKBD_DELM,				/* 14: (null) */
	RGBKBD_COORD(13, 1), RGBKBD_COORD(13, 2), RGBKBD_DELM,	/* 15: backspace */
	RGBKBD_COORD( 0, 3), RGBKBD_DELM,	/* 16: tab */
	RGBKBD_COORD( 1, 3), RGBKBD_DELM,	/* 17: q */
	RGBKBD_COORD( 2, 3), RGBKBD_DELM,	/* 18: w */
	RGBKBD_COORD( 3, 3), RGBKBD_DELM,	/* 19: e */
	RGBKBD_COORD( 4, 3), RGBKBD_DELM,	/* 20: r */
	RGBKBD_COORD( 5, 3), RGBKBD_DELM,	/* 21: t */
	RGBKBD_COORD( 6, 3), RGBKBD_DELM,	/* 22: y */
	RGBKBD_COORD( 7, 3), RGBKBD_DELM,	/* 23: u */
	RGBKBD_COORD( 8, 3), RGBKBD_DELM,	/* 24: i */
	RGBKBD_COORD( 9, 3), RGBKBD_DELM,	/* 25: o */
	RGBKBD_COORD(10, 3), RGBKBD_DELM,	/* 26: p */
	RGBKBD_COORD(11, 3), RGBKBD_COORD(12, 3), RGBKBD_DELM,	/* 27: [ { */
	RGBKBD_COORD(13, 3), RGBKBD_COORD(14, 3), RGBKBD_DELM,	/* 28: ] } */
	RGBKBD_COORD(15, 3), RGBKBD_COORD(16, 3), RGBKBD_DELM,	/* 29: \ | */
	RGBKBD_COORD( 0, 4), RGBKBD_COORD( 1, 4), RGBKBD_DELM,	/* 30: caps lock */
	RGBKBD_COORD( 2, 4), RGBKBD_DELM,	/* 31: a */
	RGBKBD_COORD( 3, 4), RGBKBD_DELM,	/* 32: s */
	RGBKBD_COORD( 4, 4), RGBKBD_DELM,	/* 33: d */
	RGBKBD_COORD( 5, 4), RGBKBD_DELM,	/* 34: f */
	RGBKBD_COORD( 6, 4), RGBKBD_DELM,	/* 35: g */
	RGBKBD_COORD( 7, 4), RGBKBD_DELM,	/* 36: h */
	RGBKBD_COORD( 8, 4), RGBKBD_DELM,	/* 37: j */
	RGBKBD_COORD( 9, 4), RGBKBD_DELM,	/* 38: k */
	RGBKBD_COORD(10, 4), RGBKBD_DELM,	/* 39: l */
	RGBKBD_COORD(11, 4), RGBKBD_COORD(12, 4), RGBKBD_DELM,	/* 40: ; : */
	RGBKBD_COORD(13, 4), RGBKBD_COORD(14, 4), RGBKBD_DELM,	/* 41: " ' */
	RGBKBD_DELM,				/* 42: (null) */
	RGBKBD_COORD(15, 4), RGBKBD_COORD(16, 4), RGBKBD_DELM,	/* 43: enter */
	RGBKBD_COORD( 0, 5), RGBKBD_COORD( 1, 5),
	RGBKBD_COORD( 2, 5), RGBKBD_DELM,	/* 44: L-shift */
	RGBKBD_DELM,				/* 45: (null) */
	RGBKBD_COORD( 3, 5), RGBKBD_DELM,	/* 46: z */
	RGBKBD_COORD( 4, 5), RGBKBD_DELM,	/* 47: x */
	RGBKBD_COORD( 5, 5), RGBKBD_DELM,	/* 48: c */
	RGBKBD_COORD( 6, 5), RGBKBD_DELM,	/* 49: v */
	RGBKBD_COORD( 7, 5), RGBKBD_DELM,	/* 50: b */
	RGBKBD_COORD( 8, 5), RGBKBD_DELM,	/* 51: n */
	RGBKBD_COORD( 9, 5), RGBKBD_DELM,	/* 52: m */
	RGBKBD_COORD(10, 5), RGBKBD_COORD(11, 5), RGBKBD_DELM,	/* 53: , < */
	RGBKBD_COORD(12, 5), RGBKBD_COORD(13, 5), RGBKBD_DELM,	/* 54: . > */
	RGBKBD_COORD(14, 5), RGBKBD_COORD(15, 5), RGBKBD_DELM,	/* 55: / ? */
	RGBKBD_DELM,				/* 56: (null) */
	RGBKBD_COORD(16, 5), RGBKBD_COORD(17, 5),
	RGBKBD_COORD(18, 5), RGBKBD_DELM,	/* 57: R-shift */
	RGBKBD_COORD(17, 4), RGBKBD_COORD(18, 4),
	RGBKBD_COORD(19, 4), RGBKBD_DELM,	/* 58: L-ctrl */
	RGBKBD_COORD(15, 0), RGBKBD_DELM,	/* 59: power */
	RGBKBD_COORD(17, 2), RGBKBD_COORD(18, 2),
	RGBKBD_COORD(19, 2), RGBKBD_DELM,	/* 60: L-alt */
	RGBKBD_COORD(17, 3), RGBKBD_COORD(18, 3),
	RGBKBD_COORD(19, 3), RGBKBD_COORD(20, 3),
	RGBKBD_COORD(21, 3), RGBKBD_DELM,	/* 61: space */
	RGBKBD_COORD(20, 2), RGBKBD_DELM,	/* 62: R-alt */
	RGBKBD_DELM,				/* 63: (null) */
	RGBKBD_COORD(21, 2), RGBKBD_DELM,	/* 64: R-ctrl */
	RGBKBD_DELM,				/* 65: (null) */
	RGBKBD_DELM,				/* 66: (null) */
	RGBKBD_DELM,				/* 67: (null) */
	RGBKBD_DELM,				/* 68: (null) */
	RGBKBD_DELM,				/* 69: (null) */
	RGBKBD_DELM,				/* 70: (null) */
	RGBKBD_DELM,				/* 71: (null) */
	RGBKBD_DELM,				/* 72: (null) */
	RGBKBD_DELM,				/* 73: (null) */
	RGBKBD_DELM,				/* 74: (null) */
	RGBKBD_DELM,				/* 75: (null) */
	RGBKBD_DELM,				/* 76: (null) */
	RGBKBD_DELM,				/* 77: (null) */
	RGBKBD_DELM,				/* 78: (null) */
	RGBKBD_COORD(19, 5), RGBKBD_DELM,	/* 79: left */
	RGBKBD_DELM,				/* 80: (null) */
	RGBKBD_DELM,				/* 81: (null) */
	RGBKBD_DELM,				/* 82: (null) */
	RGBKBD_COORD(20, 4), RGBKBD_DELM,	/* 83: up */
	RGBKBD_COORD(20, 5), RGBKBD_DELM,	/* 84: down */
	RGBKBD_DELM,				/* 85: (null) */
	RGBKBD_DELM,				/* 86: (null) */
	RGBKBD_DELM,				/* 87: (null) */
	RGBKBD_DELM,				/* 88: (null) */
	RGBKBD_COORD(21, 5), RGBKBD_DELM,	/* 89: right */
	RGBKBD_DELM,				/* 90: (null) */
	RGBKBD_DELM,				/* 91: (null) */
	RGBKBD_DELM,				/* 92: (null) */
	RGBKBD_DELM,				/* 93: (null) */
	RGBKBD_DELM,				/* 94: (null) */
	RGBKBD_DELM,				/* 95: (null) */
	RGBKBD_DELM,				/* 96: (null) */
	RGBKBD_DELM,				/* 97: (null) */
	RGBKBD_DELM,				/* 98: (null) */
	RGBKBD_DELM,				/* 99: (null) */
	RGBKBD_DELM,				/* 100: (null) */
	RGBKBD_DELM,				/* 101: (null) */
	RGBKBD_DELM,				/* 102: (null) */
	RGBKBD_DELM,				/* 103: (null) */
	RGBKBD_DELM,				/* 104: (null) */
	RGBKBD_DELM,				/* 105: (null) */
	RGBKBD_DELM,				/* 106: (null) */
	RGBKBD_DELM,				/* 107: (null) */
	RGBKBD_DELM,				/* 108: (null) */
	RGBKBD_DELM,				/* 109: (null) */
	RGBKBD_COORD( 0, 0), RGBKBD_DELM,	/* 110: esc */
	RGBKBD_COORD( 1, 0), RGBKBD_DELM,	/* T1: previous page */
	RGBKBD_COORD( 2, 0), RGBKBD_DELM,	/* T2: refresh */
	RGBKBD_COORD( 3, 0), RGBKBD_DELM,	/* T3: full screen */
	RGBKBD_COORD( 4, 0), RGBKBD_DELM,	/* T4: windows */
	RGBKBD_COORD( 5, 0), RGBKBD_DELM,	/* T5: screenshot */
	RGBKBD_COORD( 6, 0), RGBKBD_DELM,	/* T6: brightness down */
	RGBKBD_COORD( 7, 0), RGBKBD_DELM,	/* T7: brightness up */
	RGBKBD_COORD( 8, 0), RGBKBD_DELM,	/* T8: KB backlight off */
	RGBKBD_COORD( 9, 0), RGBKBD_DELM,	/* T9: play/pause */
	RGBKBD_COORD(10, 0), RGBKBD_DELM,	/* T10: mute microphone */
	RGBKBD_COORD(11, 0), RGBKBD_DELM,	/* T11: mute speakers */
	RGBKBD_COORD(12, 0), RGBKBD_DELM,	/* T12: volume down */
	RGBKBD_COORD(13, 0), RGBKBD_DELM,	/* T13: volume up */
	RGBKBD_DELM,				/* T14: (null) */
	RGBKBD_DELM,				/* T15: (null) */
	RGBKBD_DELM,				/* 126: (null) */
	RGBKBD_DELM,				/* 127: (null) */
};
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

__override void board_kblight_init(void)
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
