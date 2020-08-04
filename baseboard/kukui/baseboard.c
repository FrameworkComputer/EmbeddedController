/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "charger.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#ifndef CONFIG_CHARGER_RUNTIME_CONFIG
#if defined(VARIANT_KUKUI_CHARGER_MT6370)
#include "driver/charger/rt946x.h"
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = RT946X_ADDR_FLAGS,
		.drv = &rt946x_drv,
	},
};
#elif defined(VARIANT_KUKUI_CHARGER_ISL9238)
#include "driver/charger/isl923x.h"
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
#endif /* VARIANT_KUKUI_CHARGER_* */

#endif /* CONFIG_CHARGER_RUNTIME_CONFIG */

void board_reset_pd_mcu(void)
{
}

void board_config_pre_init(void)
{
	STM32_RCC_AHBENR |= STM32_RCC_HB_DMA1;
	/*
	 * Remap USART1 and SPI2 DMA:
	 *
	 * Ch4: USART1_TX / Ch5: USART1_RX (1000)
	 * Ch6: SPI2_RX / Ch7: SPI2_TX (0011)
	 */
	STM32_DMA_CSELR(STM32_DMAC_CH4) = (8 << 12) | (8 << 16) |
					  (3 << 20) | (3 << 24);
}

enum kukui_board_version {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_REV0 = 0,
	BOARD_VERSION_REV1 = 1,
	BOARD_VERSION_REV2 = 2,
	BOARD_VERSION_REV3 = 3,
	BOARD_VERSION_REV4 = 4,
	BOARD_VERSION_REV5 = 5,
	BOARD_VERSION_REV6 = 6,
	BOARD_VERSION_REV7 = 7,
	BOARD_VERSION_REV8 = 8,
	BOARD_VERSION_REV9 = 9,
	BOARD_VERSION_REV10 = 10,
	BOARD_VERSION_REV11 = 11,
	BOARD_VERSION_REV12 = 12,
	BOARD_VERSION_REV13 = 13,
	BOARD_VERSION_REV14 = 14,
	BOARD_VERSION_REV15 = 15,
	BOARD_VERSION_COUNT,
};

/* map from kukui_board_version to board id voltage in mv */
const int16_t kukui_board_id_map[] = {
	109,   /* 51.1K , 2.2K(gru 3.3K) ohm */
	211,   /* 51.1k , 6.8K ohm */
	319,   /* 51.1K , 11K ohm */
	427,   /* 56K   , 17.4K ohm */
	542,   /* 51.1K , 22K ohm */
	666,   /* 51.1K , 30K ohm */
	781,   /* 51.1K , 39.2K ohm */
	900,   /* 56K   , 56K ohm */
	1023,  /* 47K   , 61.9K ohm */
	1137,  /* 47K   , 80.6K ohm */
	1240,  /* 56K   , 124K ohm */
	1343,  /* 51.1K , 150K ohm */
	1457,  /* 47K   , 200K ohm */
	1576,  /* 47K   , 330K ohm */
	1684,  /* 47K   , 680K ohm */
	1800,  /* 56K   , NC */
};
BUILD_ASSERT(ARRAY_SIZE(kukui_board_id_map) == BOARD_VERSION_COUNT);

#define THRESHOLD_MV 56 /* Simply assume 1800/16/2 */

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv;
	int i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	gpio_set_level(GPIO_EC_BOARD_ID_EN_L, 0);
	/* Wait to allow cap charge */
	msleep(10);
	mv = adc_read_channel(ADC_BOARD_ID);

	if (mv == ADC_READ_ERROR)
		mv = adc_read_channel(ADC_BOARD_ID);

	gpio_set_level(GPIO_EC_BOARD_ID_EN_L, 1);

	for (i = 0; i < BOARD_VERSION_COUNT; ++i) {
		if (mv < kukui_board_id_map[i] + THRESHOLD_MV) {
			version = i;
			break;
		}
	}

	/*
	 * For devices without pogo, Disable ADC module after we detect the
	 * board version, since this is the only thing ADC module needs to do
	 * for this board.
	 */
	if (CONFIG_DEDICATED_CHARGE_PORT_COUNT == 0 &&
			version != BOARD_VERSION_UNKNOWN)
		adc_disable();

	return version;
}

static void baseboard_spi_init(void)
{
	/* Set SPI PA15,PB3/4/5/13/14/15 pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xc0000000;
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xfc000fc0;
}
DECLARE_HOOK(HOOK_INIT, baseboard_spi_init, HOOK_PRIO_INIT_SPI + 1);

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY);
}
