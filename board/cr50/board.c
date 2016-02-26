/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "flash_config.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "trng.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "util.h"

/* Define interrupt and gpio structs */
#include "gpio_list.h"

/*
 * There's no way to trigger on both rising and falling edges, so force a
 * compiler error if we try. The workaround is to use the pinmux to connect
 * two GPIOs to the same input and configure each one for a separate edge.
 */
#define GPIO_INT(name, pin, flags, signal)	\
	BUILD_ASSERT((flags & GPIO_INT_BOTH) != GPIO_INT_BOTH);
#include "gpio.wrap"

static void init_pmu(void)
{
	/* This boot sequence may be a result of previous soft reset,
	 * in which case the PMU low power sequence register needs to
	 * be reset. */
	GREG32(PMU, LOW_POWER_DIS) = 0;
}

static void init_timers(void)
{
	/* Cancel low speed timers that may have
	 * been initialized prior to soft reset. */
	GREG32(TIMELS, TIMER0_CONTROL) = 0;
	GREG32(TIMELS, TIMER0_LOAD) = 0;
	GREG32(TIMELS, TIMER1_CONTROL) = 0;
	GREG32(TIMELS, TIMER1_LOAD) = 0;
}

static void init_interrupts(void)
{
	int i;

	/* Enable all GPIO interrupts */
	for (i = 0; i < gpio_ih_count; i++)
		if (gpio_list[i].flags & GPIO_INT_ANY)
			gpio_enable_interrupt(i);
}

enum permission_level {
	PERMISSION_LOW = 0x00,
	PERMISSION_MEDIUM = 0x33,    /* APPS run at medium */
	PERMISSION_HIGH = 0x3C,
	PERMISSION_HIGHEST = 0x55
};

/* Drop run level to at least medium. */
static void init_runlevel(const enum permission_level desired_level)
{
	volatile uint32_t *const reg_addrs[] = {
		GREG32_ADDR(GLOBALSEC, CPU0_S_PERMISSION),
		GREG32_ADDR(GLOBALSEC, DDMA0_PERMISSION),
	};
	int i;

	/* Permission registers drop by 1 level (e.g. HIGHEST -> HIGH)
	 * each time a write is encountered (the value written does
	 * not matter).  So we repeat writes and reads, until the
	 * desired level is reached.
	 */
	for (i = 0; i < ARRAY_SIZE(reg_addrs); i++) {
		uint32_t current_level;

		while (1) {
			current_level = *reg_addrs[i];
			if (current_level <= desired_level)
				break;
			*reg_addrs[i] = desired_level;
		}
	}
}

/* Initialize board. */
static void board_init(void)
{
	init_pmu();
	init_timers();
	init_interrupts();
	init_trng();
	init_runlevel(PERMISSION_MEDIUM);

	/* TODO(crosbug.com/p/49959): For now, leave flash WP unlocked */
	GREG32(RBOX, EC_WP_L) = 1;

	/* Indication that firmware is running, for debug purposes. */
	GREG32(PMU, PWRDN_SCRATCH16) = 0xCAFECAFE;
}

DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#if defined(CONFIG_USB)
const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Cr50"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
	[USB_STR_BLOB_NAME] = USB_STRING_DESC("Blob"),
	[USB_STR_HID_NAME] = USB_STRING_DESC("PokeyPokey"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);
#endif

int flash_regions_to_enable(struct g_flash_region *regions,
			    int max_regions)
{
	uint32_t half = CONFIG_FLASH_SIZE / 2;

	if (max_regions < 1)
		return 0;

	if ((uint32_t)flash_regions_to_enable <
	    (CONFIG_MAPPED_STORAGE_BASE + half))
		/*
		 * Running from RW_A. Need to enable writes into the top half,
		 * which consists of NV_RAM and RW_B sections.
		 */
		regions->reg_base = CONFIG_MAPPED_STORAGE_BASE + half;
	else
		/*
		 * Running from RW_B, need to enable access to both program
		 * memory in the lower half and the NVRAM space in the top
		 * half.
		 *
		 * NVRAM space in the top half by design is at the same offset
		 * and of the same size as the RO section in the lower half.
		 */
		regions->reg_base = CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_RO_SIZE;

	/* The size of the write enable area is the same in both cases. */
	regions->reg_size = half;
	regions->reg_perms =  FLASH_REGION_EN_ALL;

	return 1; /* One region is enough. */
}
