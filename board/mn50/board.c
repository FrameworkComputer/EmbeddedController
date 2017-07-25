/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <endian.h>

#include "case_closed_debug.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "device_state.h"
#include "ec_version.h"
#include "extension.h"
#include "flash.h"
#include "flash_config.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina2xx.h"
#include "init_chip.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "registers.h"
#include "signed_header.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "uartn.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_spi.h"
#include "usb_i2c.h"
#include "util.h"

/* Define interrupt and gpio structs */
#include "gpio_list.h"

#include "cryptoc/sha.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* NvMem user buffer lengths table */
uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_CR50_SIZE
};

/* I2C Port definition. No GPIO access. */
const struct i2c_port_t i2c_ports[]  = {
	{"master", I2C_PORT_MASTER, 100, 0, 0},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Recall whether we have enable socket power. */
static int socket_set_enabled;

/*****************************************************************************/

#include "gpio.wrap"

static void init_interrupts(void)
{
	int i;
	uint32_t exiten = GREG32(PINMUX, EXITEN0);

	/* Clear wake pin interrupts */
	GREG32(PINMUX, EXITEN0) = 0;
	GREG32(PINMUX, EXITEN0) = exiten;

	/* Enable all GPIO interrupts */
	for (i = 0; i < gpio_ih_count; i++)
		if (gpio_list[i].flags & GPIO_INT_ANY)
			gpio_enable_interrupt(i);
}

void decrement_retry_counter(void)
{
	uint32_t counter = GREG32(PMU, LONG_LIFE_SCRATCH0);

	if (counter) {
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
		GREG32(PMU, LONG_LIFE_SCRATCH0) = counter - 1;
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
	}
}

void ccd_phy_init(int none)
{
	usb_select_phy(USB_SEL_PHY1);

	usb_init();
}

void usb_i2c_board_disable(void)
{
}

int usb_i2c_board_enable(void)
{
	return EC_SUCCESS;
}

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Deep sleep resets should be considered valid and should not impact
	 * the rolling reboot count.
	 */
	if (system_get_reset_flags() & RESET_FLAG_HIBERNATE)
		decrement_retry_counter();
	init_interrupts();
	init_trng();
	init_jittery_clock(1);
	init_runlevel(PERMISSION_MEDIUM);
	/* Initialize NvMem partitions */
	nvmem_init();
	/* Initialize the persistent storage. */
	initvars();

	/* Disable all power to socket, for hot swapping. */
	disable_socket();

	/* Indication that firmware is running, for debug purposes. */
	GREG32(PMU, PWRDN_SCRATCH16) = 0xCAFECAFE;

	/* Enable USB / CCD */
	ccd_set_mode(CCD_MODE_ENABLED);
	uartn_enable(UART_AP);

	/* Calibrate INA0 (VBUS) with 1mA/LSB scale */
	i2cm_init();
	ina2xx_init(0, 0x8000, INA2XX_CALIB_1MA(150 /*mOhm*/));
	ina2xx_init(1, 0x8000, INA2XX_CALIB_1MA(150 /*mOhm*/));
	ina2xx_init(4, 0x8000, INA2XX_CALIB_1MA(150 /*mOhm*/));
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Mn50"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
	[USB_STR_BLOB_NAME] = USB_STRING_DESC("Blob"),
	[USB_STR_AP_NAME] = USB_STRING_DESC("DUT UART"),
	[USB_STR_UPGRADE_NAME] = USB_STRING_DESC("Firmware upgrade"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_SERIALNO] = USB_STRING_DESC(DEFAULT_SERIALNO),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/* SPI devices */
/* port 0, 40MHz / (16 + 1) =  2.3MHz SPI, no soft CS */
const struct spi_device_t spi_devices[] = {
	[CONFIG_SPI_FLASH_PORT] = {0, 16, GPIO_COUNT}
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

int flash_regions_to_enable(struct g_flash_region *regions,
			    int max_regions)
{
	/*
	 * This needs to account for two regions: the "other" RW partition and
	 * the NVRAM in TOP_B.
	 *
	 * When running from RW_A the two regions are adjacent, but it is
	 * simpler to keep function logic the same and always configure two
	 * separate regions.
	 */

	if (max_regions < 3)
		return 0;

	/* Enable access to the other RW image... */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		/* Running RW_A, enable RW_B */
		regions[0].reg_base = CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_RW_B_MEM_OFF;
	else
		/* Running RW_B, enable RW_A */
		regions[0].reg_base = CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_RW_MEM_OFF;
	/* Size is the same */
	regions[0].reg_size = CONFIG_RW_SIZE;
	regions[0].reg_perms = FLASH_REGION_EN_ALL;

	/* Enable access to the NVRAM partition A region */
	regions[1].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CONFIG_FLASH_NVMEM_OFFSET_A;
	regions[1].reg_size = NVMEM_PARTITION_SIZE;
	regions[1].reg_perms = FLASH_REGION_EN_ALL;

	/* Enable access to the NVRAM partition B region */
	regions[2].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CONFIG_FLASH_NVMEM_OFFSET_B;
	regions[2].reg_size = NVMEM_PARTITION_SIZE;
	regions[2].reg_perms = FLASH_REGION_EN_ALL;

	return 3;
}

/* Check if socket has been anabled and power is OK. */
int is_socket_enabled(void)
{
	/* TODO: check voltage rails within approved bands. */
	return (gpio_get_level(GPIO_DUT_PWRGOOD) && socket_set_enabled);
}

/* Determine whether the socket has no voltage. TODO: check GPIOS? */
int is_socket_off(void)
{
	/* Check 3.3v = 0. */
	if (ina2xx_get_voltage(1) > 10)
		return 0;
	/* Check 2.6v = 0. */
	if (ina2xx_get_voltage(4) > 10)
		return 0;
	return 1;
}

void enable_socket(void)
{
	/* Power up. */
	gpio_set_level(GPIO_DUT_PWR_EN, 1);

	/* Indicate socket powered with red LED. */
	gpio_set_level(GPIO_LED_L, 0);

	/* GPIOs as ioutputs. */
	gpio_set_flags(GPIO_DUT_RST_L, GPIO_OUT_LOW);
	gpio_set_flags(GPIO_DUT_BOOT_CFG, GPIO_OUT_LOW);
	gpio_set_flags(GPIO_SPI_CS_ALT_L, GPIO_OUT_HIGH);

	/* Connect DIO A4, A8 to the SPI peripheral */
	GWRITE(PINMUX, DIOA4_SEL, 0); /* SPI_MOSI */
	GWRITE(PINMUX, DIOA8_SEL, 0); /* SPI_CLK */
	GWRITE(PINMUX, DIOA5_SEL, GC_PINMUX_GPIO0_GPIO10_SEL);

	/* UART */
	GWRITE(PINMUX, DIOA7_SEL, GC_PINMUX_UART1_TX_SEL);

	/* Chip select. */
	GWRITE_FIELD(PINMUX, DIOA5_CTL, PU, 1);

	socket_set_enabled = 1;
}

void disable_socket(void)
{
	/* Disable CS pin. */
	GWRITE_FIELD(PINMUX, DIOA5_CTL, PU, 0);

	/* TODO: Implement way to get the gpio */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO7_SEL) == GC_PINMUX_DIOA4_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO8_SEL) == GC_PINMUX_DIOA8_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO10_SEL) == GC_PINMUX_DIOA5_SEL);

	/* Set SPI MOSI, CLK, and CS_L as inputs */
	GWRITE(PINMUX, DIOA4_SEL, GC_PINMUX_GPIO0_GPIO7_SEL);
	GWRITE(PINMUX, DIOA8_SEL, GC_PINMUX_GPIO0_GPIO8_SEL);
	GWRITE(PINMUX, DIOA5_SEL, GC_PINMUX_GPIO0_GPIO10_SEL);

	/* UART */
	GWRITE(PINMUX, DIOA7_SEL, 0);

	/* GPIOs as inputs. */
	gpio_set_flags(GPIO_DUT_BOOT_CFG, GPIO_INPUT);
	gpio_set_flags(GPIO_DUT_RST_L, GPIO_INPUT);
	gpio_set_flags(GPIO_SPI_CS_ALT_L, GPIO_INPUT);

	/* Turn off socket power. */
	gpio_set_level(GPIO_DUT_PWR_EN, 0);

	/* Indicate socket unpowered with no red LED. */
	gpio_set_level(GPIO_LED_L, 1);
	socket_set_enabled = 0;
}

static int command_socket(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp("enable", argv[1]))
			enable_socket();
		else if (!strcasecmp("disable", argv[1]))
			disable_socket();
		else
			return EC_ERROR_PARAM1;

		/* Let power settle. */
		msleep(5);
	}

	ccprintf("Socket enabled: %s, powered: %s\n",
		 is_socket_enabled() ? "yes" : "no",
		 is_socket_off() ? "off" : "on");
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(socket, command_socket,
			     "[enable|disable]",
			     "Activate and deactivate socket");

void post_reboot_request(void)
{
	/* This will never return. */
	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD);
}

/* Determine key type based on the key ID. */
static const char *key_type(uint32_t key_id)
{

	/*
	 * It is a mere convention, but all prod keys are required to have key
	 * IDs such, that bit D2 is set, and all dev keys are required to have
	 * key IDs such, that bit D2 is not set.
	 *
	 * This convention is enforced at the key generation time.
	 */
	if (key_id & (1 << 2))
		return "prod";
	else
		return "dev";
}

static int command_sysinfo(int argc, char **argv)
{
	enum system_image_copy_t active;
	uintptr_t vaddr;
	const struct SignedHeader *h;

	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");

	ccprintf("Chip:	%s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());

	active = system_get_ro_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RO keyid:    0x%08x(%s)\n", h->keyid, key_type(h->keyid));

	active = system_get_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RW keyid:    0x%08x(%s)\n", h->keyid, key_type(h->keyid));

	ccprintf("DEV_ID:      0x%08x 0x%08x\n",
		 GREG32(FUSE, DEV_ID0), GREG32(FUSE, DEV_ID1));

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sysinfo, command_sysinfo,
			     NULL,
			     "Print system info");

/*
 * SysInfo command:
 * There are no input args.
 * Output is this struct, all fields in network order.
 */
struct sysinfo_s {
	uint32_t ro_keyid;
	uint32_t rw_keyid;
	uint32_t dev_id0;
	uint32_t dev_id1;
} __packed;



