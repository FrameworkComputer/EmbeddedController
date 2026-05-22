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

#define WDT_NODE DT_NODELABEL(wdog)
#define RTK_WDT_REG_BASE ((WDT_Type *)(DT_REG_ADDR(WDT_NODE)))

#define RTK_VIVO_BACKUP0_REG (*((volatile uint32_t *)0x40104ff8))
#define RTK_VIVO_BACKUP1_REG (*((volatile uint32_t *)0x40104ffc))

#define BBRAM_KEY_VALUE 0x52544b21 /* RTK! */
#define BBRAM_KEY_REV_VALUE ~BBRAM_KEY_VALUE

/* Driver data */
static int reset_cause = UNKNOWN_RST;

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
		LOG_ERR_DEVICE_NOT_READY(watchdog);
		return -ENODEV;
	}

	wdt_disable(watchdog);
#endif /* CONFIG_WATCHDOG */

	return 0;
}

const char *cros_system_chip_vendor(void)
{
	return "rtk";
}

#define RTK_CHIP_INFO_BASE 0x40010B80
#define CHIP_ID_OFFSET 0x70
#define RTK_CHIP_INFO_REG (RTK_CHIP_INFO_BASE + CHIP_ID_OFFSET)
#define RTK_PUF_INFO_BASE 0x40010800UL
#define OTP_OFFSET_BASE 0x680UL
#define OTP_CTRL_REGISTER 0x24
#define OTP_STS_REGISTER 0x20
#define OTP_STS_BUSY_MSK 0x01
#define OTP_STS_PDSTB_MSK 0x04
#define RTK_OTP_CTRL_REG \
	(RTK_PUF_INFO_BASE + OTP_OFFSET_BASE + OTP_CTRL_REGISTER)
#define RTK_OTP_STS_REG (RTK_PUF_INFO_BASE + OTP_OFFSET_BASE + OTP_STS_REGISTER)
#define PUF_OPERATION_STAGE_ACTIVE 1ul
#define PUF_OPERATION_STATE_SLEEP 0ul
#define OTP_TIMEROUT_WAIT 50
static uint32_t get_otp_chip_info(void)
{
	uint32_t timeout = k_ms_to_cyc_ceil32(OTP_TIMEROUT_WAIT);
	uint32_t start, temp_time;

	start = k_cycle_get_32();
	temp_time = start;
	*(volatile uint32_t *)RTK_OTP_CTRL_REG = PUF_OPERATION_STAGE_ACTIVE;
	while ((OTP_STS_PDSTB_MSK !=
		(*(volatile uint32_t *)RTK_OTP_STS_REG &
		 (OTP_STS_BUSY_MSK | OTP_STS_PDSTB_MSK))) &&
	       (temp_time - start < timeout)) {
		temp_time = k_cycle_get_32();
	}

	uint32_t chip_info = *((volatile uint32_t *)RTK_CHIP_INFO_REG);

	*(volatile uint32_t *)RTK_OTP_CTRL_REG = PUF_OPERATION_STATE_SLEEP;

	return chip_info;
}

static uint32_t system_get_chip_id(void)
{
	/* [31:16] main id */
	uint32_t raw_id = get_otp_chip_info();
	uint16_t main_id = (raw_id >> 16) & 0xFFFF;

	return main_id;
}

static uint8_t system_get_chip_version(void)
{
	/* [15:8] chip version */
	uint32_t raw_id = get_otp_chip_info();
	uint16_t sub_id = (raw_id >> 8) & 0xFF;

	return sub_id;
}

const char *cros_system_chip_name(void)
{
	static char buf[8] = { 'r', 't', 's' };
	uint32_t chip_id = system_get_chip_id();

	snprintf(buf + 3, sizeof(buf) - 3, "%04x", (uint16_t)chip_id);

	return buf;
}

const char *cros_system_chip_revision(void)
{
	static char buf[5];
	uint8_t rev = system_get_chip_version();

	snprintf(buf, sizeof(buf), "%c", rev);

	return buf;
}

int cros_system_get_reset_cause(void)
{
	return reset_cause;
}

static int cros_system_rtk_init(void)
{
	WDT_Type *wdt_reg = RTK_WDT_REG_BASE;
	uint32_t vivo_reg0 = RTK_VIVO_BACKUP0_REG;
	uint32_t vivo_reg1 = RTK_VIVO_BACKUP1_REG;
	uint32_t key_val = 0, key_rev_val = 0;
	uint32_t value = 0;
	uint32_t invalid_value = 0;
	/* In order to determine if reset from watchdog */
	uint32_t flag = 0;
	/* check reset cause */
	reset_cause = UNKNOWN_RST;

	/* is the WDT reset */
	if (wdt_reg->STS & WDT_STS_RSTFLAG) {
		reset_cause = WATCHDOG_RST;
		/* Clear watchdog reset status initially */
		wdt_reg->CTRL |= WDT_CTRL_CLRRSTFLAG;
		/* Setup flag if reset from watchdog */
		flag = 1;
	} else if ((vivo_reg0 ^ vivo_reg1) == UINT32_MAX) {
		/* VIN3 (GPIO115) connect to power button */
		if (vivo_reg1 & BIT(SYSTEM_VIVOCTRL_VIN3STS_Pos)) {
			reset_cause = POWERUP;
		}
	}

	/* check if bbram's key remained */
	bbram_read(bbram_dev, BBRAM_REGION_OFFSET(key), BBRAM_REGION_SIZE(key),
		   (uint8_t *)&key_val);
	bbram_read(bbram_dev, BBRAM_REGION_OFFSET(key_rev),
		   BBRAM_REGION_SIZE(key_rev), (uint8_t *)&key_rev_val);

	/* If No, Init BBRAM reset_flags to 0x0 */
	if ((key_val != BBRAM_KEY_VALUE) ||
	    (key_rev_val != BBRAM_KEY_REV_VALUE)) {
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
		key_rev_val = BBRAM_KEY_REV_VALUE;
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(key),
			    BBRAM_REGION_SIZE(key), (uint8_t *)&key_val);
		bbram_write(bbram_dev, BBRAM_REGION_OFFSET(key_rev),
			    BBRAM_REGION_SIZE(key_rev),
			    (uint8_t *)&key_rev_val);

	} else {
		/* If key remained and not reset from wdt, setup
		 * EC_RESET_FLAG_RESET_PIN flag for hard reset */
		if (flag == 0) {
			system_set_reset_flags(EC_RESET_FLAG_RESET_PIN);
		}
	}

	return 0;
}

int cros_system_soc_reset(void)
{
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

int cros_system_hibernate(uint32_t seconds, uint32_t microseconds)
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

SYS_INIT(cros_system_rtk_init, PRE_KERNEL_1, CONFIG_CROS_SYSTEM_INIT_PRIORITY);

#if CONFIG_CROS_SYSTEM_INIT_PRIORITY >= \
	CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY
#error "CROS_SYSTEM must initialize before the SYSTEM_PRE initialization"
#endif
