/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "util.h"

#include <stdbool.h>

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#ifdef SECTION_IS_RW
#define CROS_EC_SECTION "RW"
#else
#define CROS_EC_SECTION "RO"
#endif

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Prism"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] =
		USB_STRING_DESC(CROS_EC_SECTION ":" CROS_EC_VERSION32),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
	[USB_STR_HOSTCMD_NAME] = USB_STRING_DESC("Host command"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support I2C bridging over USB.
 */

#ifdef SECTION_IS_RW
const struct spi_device_t spi_devices[] = {
	[SPI_RGB0_DEVICE_ID] = { CONFIG_SPI_RGB_PORT, 2, /* 2: Fpclk/8 = 48Mhz/8
							    = 6Mhz */
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

enum ec_rgbkbd_type rgbkbd_type = EC_RGBKBD_TYPE_PER_KEY;

#define LED(x, y) RGBKBD_COORD((x), (y))
#define DELM RGBKBD_DELM

const uint8_t rgbkbd_map[] = {
	DELM, /* 0: (null) */
	LED(0, 1),  LED(0, 2),	DELM, /* 1: ~ ` */
	LED(1, 1),  LED(1, 2),	DELM, /* 2: ! 1 */
	LED(2, 1),  LED(2, 2),	DELM, /* 3: @ 2 */
	LED(3, 1),  LED(3, 2),	DELM, /* 4: # 3 */
	LED(4, 1),  LED(4, 2),	DELM, /* 5: $ 4 */
	LED(5, 1),  LED(5, 2),	DELM, /* 6: % 5 */
	LED(6, 1),  LED(6, 2),	DELM, /* 7: ^ 6 */
	LED(7, 1),  LED(7, 2),	DELM, /* 8: & 7 */
	LED(8, 1),  LED(8, 2),	DELM, /* 9: * 8 */
	LED(9, 1),  LED(9, 2),	DELM, /* 10: ( 9 */
	LED(10, 1), LED(10, 2), DELM, /* 11: ) 0 */
	LED(11, 1), LED(11, 2), DELM, /* 12: _ - */
	LED(12, 1), LED(12, 2), DELM, /* 13: + = */
	DELM, /* 14: (null) */
	LED(13, 1), LED(13, 2), DELM, /* 15: backspace */
	LED(0, 3),  LED(15, 2), DELM, /* 16: tab */
	LED(1, 3),  DELM, /* 17: q */
	LED(2, 3),  DELM, /* 18: w */
	LED(3, 3),  DELM, /* 19: e */
	LED(4, 3),  DELM, /* 20: r */
	LED(5, 3),  DELM, /* 21: t */
	LED(6, 3),  DELM, /* 22: y */
	LED(7, 3),  DELM, /* 23: u */
	LED(8, 3),  DELM, /* 24: i */
	LED(9, 3),  DELM, /* 25: o */
	LED(10, 3), DELM, /* 26: p */
	LED(11, 3), LED(12, 3), DELM, /* 27: [ { */
	LED(13, 3), LED(14, 3), DELM, /* 28: ] } */
	LED(15, 3), LED(16, 3), DELM, /* 29: \ | */
	LED(0, 4),  LED(1, 4),	DELM, /* 30: caps lock */
	LED(2, 4),  DELM, /* 31: a */
	LED(3, 4),  DELM, /* 32: s */
	LED(4, 4),  DELM, /* 33: d */
	LED(5, 4),  DELM, /* 34: f */
	LED(6, 4),  DELM, /* 35: g */
	LED(7, 4),  DELM, /* 36: h */
	LED(8, 4),  DELM, /* 37: j */
	LED(9, 4),  DELM, /* 38: k */
	LED(10, 4), DELM, /* 39: l */
	LED(11, 4), LED(12, 4), DELM, /* 40: ; : */
	LED(13, 4), LED(14, 4), DELM, /* 41: " ' */
	DELM, /* 42: (null) */
	LED(15, 4), LED(16, 4), DELM, /* 43: enter */
	LED(0, 5),  LED(1, 5),	LED(2, 5),  DELM, /* 44: L-shift */
	DELM, /* 45: (null) */
	LED(3, 5),  DELM, /* 46: z */
	LED(4, 5),  DELM, /* 47: x */
	LED(5, 5),  DELM, /* 48: c */
	LED(6, 5),  DELM, /* 49: v */
	LED(7, 5),  DELM, /* 50: b */
	LED(8, 5),  DELM, /* 51: n */
	LED(9, 5),  DELM, /* 52: m */
	LED(10, 5), LED(11, 5), DELM, /* 53: , < */
	LED(12, 5), LED(13, 5), DELM, /* 54: . > */
	LED(14, 5), LED(15, 5), DELM, /* 55: / ? */
	DELM, /* 56: (null) */
	LED(16, 5), LED(17, 5), LED(18, 5), DELM, /* 57: R-shift */
	LED(17, 4), LED(18, 4), LED(19, 4), DELM, /* 58: L-ctrl */
	LED(15, 0), DELM, /* 59: power */
	LED(17, 2), LED(18, 2), LED(19, 2), DELM, /* 60: L-alt */
	LED(17, 3), LED(18, 3), LED(19, 3), LED(20, 3),
	LED(21, 3), LED(16, 2), DELM, /* 61: space */
	LED(20, 2), DELM, /* 62: R-alt */
	DELM, /* 63: (null) */
	LED(21, 2), DELM, /* 64: R-ctrl */
	DELM, /* 65: (null) */
	DELM, /* 66: (null) */
	DELM, /* 67: (null) */
	DELM, /* 68: (null) */
	DELM, /* 69: (null) */
	DELM, /* 70: (null) */
	DELM, /* 71: (null) */
	DELM, /* 72: (null) */
	DELM, /* 73: (null) */
	DELM, /* 74: (null) */
	DELM, /* 75: (null) */
	DELM, /* 76: (null) */
	DELM, /* 77: (null) */
	DELM, /* 78: (null) */
	LED(19, 5), DELM, /* 79: left */
	DELM, /* 80: (null) */
	DELM, /* 81: (null) */
	DELM, /* 82: (null) */
	LED(20, 4), DELM, /* 83: up */
	LED(20, 5), DELM, /* 84: down */
	DELM, /* 85: (null) */
	DELM, /* 86: (null) */
	DELM, /* 87: (null) */
	DELM, /* 88: (null) */
	LED(21, 5), DELM, /* 89: right */
	DELM, /* 90: (null) */
	DELM, /* 91: (null) */
	DELM, /* 92: (null) */
	DELM, /* 93: (null) */
	DELM, /* 94: (null) */
	DELM, /* 95: (null) */
	DELM, /* 96: (null) */
	DELM, /* 97: (null) */
	DELM, /* 98: (null) */
	DELM, /* 99: (null) */
	DELM, /* 100: (null) */
	DELM, /* 101: (null) */
	DELM, /* 102: (null) */
	DELM, /* 103: (null) */
	DELM, /* 104: (null) */
	DELM, /* 105: (null) */
	DELM, /* 106: (null) */
	DELM, /* 107: (null) */
	DELM, /* 108: (null) */
	DELM, /* 109: (null) */
	LED(0, 0),  DELM, /* 110: esc */
	LED(1, 0),  DELM, /* T1: previous page */
	LED(2, 0),  DELM, /* T2: refresh */
	LED(3, 0),  DELM, /* T3: full screen */
	LED(4, 0),  DELM, /* T4: windows */
	LED(5, 0),  DELM, /* T5: screenshot */
	LED(6, 0),  DELM, /* T6: brightness down */
	LED(7, 0),  DELM, /* T7: brightness up */
	LED(8, 0),  DELM, /* T8: KB backlight off */
	LED(9, 0),  DELM, /* T9: play/pause */
	LED(10, 0), DELM, /* T10: mute microphone */
	LED(11, 0), DELM, /* T11: mute speakers */
	LED(12, 0), DELM, /* T12: volume down */
	LED(13, 0), DELM, /* T13: volume up */
	DELM, /* T14: (null) */
	DELM, /* T15: (null) */
	DELM, /* 126: (null) */
	DELM, /* 127: (null) */
};
#undef LED
#undef DELM
const size_t rgbkbd_map_size = ARRAY_SIZE(rgbkbd_map);

__override void board_kblight_shutdown(void)
{
	gpio_set_level(GPIO_RGBKBD_POWER, 0);
}

__override void board_kblight_init(void)
{
	/*
	 * Keep hardware stand-by always on since it doesn't allow scale and PWM
	 * registers to be written. We use software stand-by for enable/disable.
	 */
	gpio_set_level(GPIO_RGBKBD_SDB_L, 1);
	gpio_set_level(GPIO_RGBKBD_POWER, 1);
	crec_msleep(10);
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
	const uint32_t timeout = 2 * MSEC;

	for (i = 0; i < len; i++) {
		STM32_CRS_ICR |= STM32_CRS_ICR_SYNCOKC;
		start = __hw_clock_source_read();
		while (!(STM32_CRS_ISR & STM32_CRS_ISR_SYNCOKF)) {
			if ((__hw_clock_source_read() - start) > timeout)
				return 0;
			crec_usleep(500);
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
			if (snprintf(&str[pos], sizeof(str) - pos, "%02x",
				     id[i]) < 0)
				return NULL;
		}
	}

	return str;
}

__override int board_write_serial(const char *serialno)
{
	return 0;
}
