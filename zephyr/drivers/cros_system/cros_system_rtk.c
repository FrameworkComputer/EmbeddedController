/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bbram.h"
#include "common.h"
#include "cros_version.h"
#include "drivers/cros_system.h"
#include "reg/reg_system.h"
#include "reg/reg_wdt.h"
#include "stdint.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/drivers/bbram.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(cros_system, LOG_LEVEL_ERR);

#define RTK_SCCON_REG_BASE ((SYSTEM_Type *)(DT_REG_ADDR(DT_NODELABEL(sccon))))

#define WDT_NODE DT_INST(0, realtek_rts5912_watchdog)
#define RTK_WDT_REG_BASE ((WDT_Type *)(DT_REG_ADDR(WDT_NODE)))

#define RTK_VIVO_BACKUP0_REG (*((volatile uint32_t *)0x40104ff8))
#define RTK_VIVO_BACKUP1_REG (*((volatile uint32_t *)0x40104ffc))

#define BBRAM_KEY_VALUE 0xA5

/* Driver data */
struct cros_system_rtk_data {
	int reset; /* reset cause */
};

static const struct device *const watchdog =
	DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog));

static const struct device *const bbram_dev =
	COND_CODE_1(DT_HAS_CHOSEN(cros_ec_bbram),
		    DEVICE_DT_GET(DT_CHOSEN(cros_ec_bbram)), NULL);

/* Soc specific system local functions */
static int system_rtk_watchdog_stop(void)
{
#ifdef CONFIG_WATCHDOG
	if (!device_is_ready(watchdog)) {
		LOG_ERR("device %s not ready", watchdog->name);
		return -ENODEV;
	}

	wdt_disable(watchdog);
#endif /* CONFIG_WATCHDOG */

	return 0;
}

static const char *cros_system_rtk_get_chip_vendor(const struct device *dev)
{
	ARG_UNUSED(dev);

	return "rtk";
}

#define RTK_CHIP_INFO_BASE 0x40010B80
#define CHIP_ID_OFFSET 0x70
static uint32_t system_get_chip_id(void)
{
	/* [31:16] main id */
	volatile uint32_t *chip_info_address =
		(volatile uint32_t *)(RTK_CHIP_INFO_BASE + CHIP_ID_OFFSET);
	uint32_t raw_id = *chip_info_address;
	uint16_t main_id = (raw_id >> 16) & 0xFFFF;

	return main_id;
}

static uint8_t system_get_chip_version(void)
{
	/* [15:8] chip version */
	volatile uint32_t *chip_info_address =
		(volatile uint32_t *)(RTK_CHIP_INFO_BASE + CHIP_ID_OFFSET);
	uint32_t raw_id = *chip_info_address;
	uint16_t sub_id = (raw_id >> 8) & 0xFF;

	return sub_id;
}

static const char *cros_system_rtk_get_chip_name(const struct device *dev)
{
	ARG_UNUSED(dev);

	static char buf[8] = { 'r', 't', 's' };
	uint32_t chip_id = system_get_chip_id();

	snprintf(buf + 3, sizeof(buf) - 3, "%04x", (uint16_t)chip_id);

	return buf;
}

static const char *cros_system_rtk_get_chip_revision(const struct device *dev)
{
	ARG_UNUSED(dev);

	static char buf[5];
	uint8_t rev = system_get_chip_version();

	snprintf(buf, sizeof(buf), "%c", rev);

	return buf;
}

static int cros_system_rtk_get_reset_cause(const struct device *dev)
{
	struct cros_system_rtk_data *data = dev->data;

	return data->reset;
}

static int cros_system_rtk_init(const struct device *dev)
{
	struct cros_system_rtk_data *data = dev->data;
	WDT_Type *wdt_reg = RTK_WDT_REG_BASE;
	uint32_t vivo_reg0 = RTK_VIVO_BACKUP0_REG;
	uint32_t vivo_reg1 = RTK_VIVO_BACKUP1_REG;
	uint32_t key_val = 0;
	uint32_t value = 0;
	uint32_t invalid_value = 0;
	/* In order to determine if reset from watchdog */
	uint32_t flag = 0;
	/* check reset cause */
	data->reset = UNKNOWN_RST;

	/* is the WDT reset */
	if (wdt_reg->STS & WDT_STS_RSTFLAG) {
		data->reset = WATCHDOG_RST;
		/* Clear watchdog reset status initially */
		wdt_reg->CTRL |= WDT_CTRL_CLRRSTFLAG;
		/* Setup flag if reset from watchdog */
		flag = 1;
	} else if ((vivo_reg0 ^ vivo_reg1) == UINT32_MAX) {
		/* VIN3 (GPIO115) connect to power button */
		if (vivo_reg1 & BIT(SYSTEM_VIVOCTRL_VIN3STS_Pos)) {
			data->reset = POWERUP;
		}
	}

	/* check if bbram's key remained */
	bbram_read(bbram_dev, BBRAM_REGION_OFFSET(key), BBRAM_REGION_SIZE(key),
		   (uint8_t *)&key_val);

	/* If No, Init BBRAM reset_flags to 0x0 */
	if (key_val != BBRAM_KEY_VALUE) {
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(saved_reset_flags),
			    BBRAM_REGION_SIZE(saved_reset_flags),
			    (uint8_t *)&value);

		/* If not reset from wdt, default set as POWER_ON */
		if (flag == 0) {
			value |= EC_RESET_FLAG_POWER_ON;
			bbram_write(bbram_dev,
				    BBRAM_REGION_OFFSET(saved_reset_flags),
				    BBRAM_REGION_SIZE(saved_reset_flags),
				    (uint8_t *)&value);
			system_set_reset_flags(EC_RESET_FLAG_RESET_PIN);
		}

		/* set wp_at_boot as invalid */
		invalid_value = BBRAM_WP_FLAG_INVALID;
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(wp_at_boot),
			    BBRAM_REGION_SIZE(wp_at_boot),
			    (uint8_t *)&invalid_value);

		/* Set key as BBRAM_KEY_VALUE  */
		key_val = BBRAM_KEY_VALUE;
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(key),
			    BBRAM_REGION_SIZE(key), (uint8_t *)&key_val);

	} else {
		/* If key remained and not reset from wdt, setup
		 * EC_RESET_FLAG_RESET_PIN flag for hard reset */
		if (flag == 0) {
			system_set_reset_flags(EC_RESET_FLAG_RESET_PIN);
		}
	}

	return 0;
}

static int cros_system_rtk_soc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable_all();

	/*
	 * Set minimal watchdog timeout - 10 millisecond.
	 * RTK WDT can be set for lower value, but we are limited by
	 * Zephyr API.
	 */
	struct wdt_timeout_cfg minimal_timeout = {
		.window.max = 50, .flags = WDT_FLAG_RESET_SOC
	};
	/* stop watchdog */
	wdt_disable(watchdog);
	/* Setup watchdog */
	wdt_install_timeout(watchdog, &minimal_timeout);
	/* Apply the changes (the driver will reload watchdog) */
	wdt_setup(watchdog, 0);
	/* Spin and wait for reboot */
	while (1)
		;

	/* Should never return */
	return 0;
}

/*
 * Fake wake ISR handler, needed for pins that do not have a handler.
 */
void wake_isr(enum gpio_signal signal)
{
}

static int cros_system_rtk_hibernate(const struct device *dev, uint32_t seconds,
				     uint32_t microseconds)
{
	/* Disable interrupt first */
	interrupt_disable_all();
	/* Stop the watchdog */
	system_rtk_watchdog_stop();

	/*
	 * Give the board a chance to do any late stage hibernation work.  This
	 * is likely going to configure GPIOs for hibernation.  On some boards,
	 * it's possible that this may not return at all.  On those boards,
	 * power to the EC is likely being turn off entirely.
	 */
	if (board_hibernate_late)
		board_hibernate_late();

#ifdef CONFIG_POWEROFF
	/* For the board support ULPM, go power_off to turn off the power. */
	sys_poweroff();
#endif

	return 0;
}

static const struct cros_system_driver_api cros_system_driver_rtk_api = {
	.get_reset_cause = cros_system_rtk_get_reset_cause,
	.soc_reset = cros_system_rtk_soc_reset,
	.hibernate = cros_system_rtk_hibernate,
	.chip_vendor = cros_system_rtk_get_chip_vendor,
	.chip_name = cros_system_rtk_get_chip_name,
	.chip_revision = cros_system_rtk_get_chip_revision,
};
#if CONFIG_CROS_SYSTEM_REALTEK_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif

static struct cros_system_rtk_data cros_system_rtk_data_0;

DEVICE_DEFINE(cros_system_rtk_0, "CROS_SYSTEM", cros_system_rtk_init, NULL,
	      &cros_system_rtk_data_0, NULL, PRE_KERNEL_1,
	      CONFIG_CROS_SYSTEM_REALTEK_INIT_PRIORITY,
	      &cros_system_driver_rtk_api);
