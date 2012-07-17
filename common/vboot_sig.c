/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "board.h"
#include "config.h"
#include "console.h"
#include "cryptolib.h"
#include "gpio.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "vboot.h"
#include "vboot_api.h"
#include "vboot_common.h"
#include "vboot_struct.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_VBOOT, outstr)
#define CPRINTF(format, args...) cprintf(CC_VBOOT, format, ## args)

enum howgood {
	IMAGE_IS_BAD,
	IMAGE_IS_GOOD,
	IMAGE_IS_GOOD_BUT_USE_RO_ANYWAY,
};

static enum howgood good_image(uint8_t *key_data,
			       uint8_t *vblock_data, uint32_t vblock_size,
			       uint8_t *fv_data, uint32_t fv_size) {
	VbPublicKey *sign_key;
	VbKeyBlockHeader *key_block;
	VbECPreambleHeader *preamble;
	uint32_t now = 0;
	RSAPublicKey *rsa;

	key_block = (VbKeyBlockHeader *)vblock_data;
	sign_key = (VbPublicKey *)key_data;

	watchdog_reload();
	if (0 != KeyBlockVerify(key_block, vblock_size, sign_key, 0)) {
		CPRINTF("[Error verifying key block]\n");
		return IMAGE_IS_BAD;
	}

	now += key_block->key_block_size;
	rsa = PublicKeyToRSA(&key_block->data_key);
	if (!rsa) {
		CPRINTF("[Error parsing data key]\n");
		return IMAGE_IS_BAD;
	}

	watchdog_reload();
	preamble = (VbECPreambleHeader *)(vblock_data + now);
	if (0 != VerifyECPreamble(preamble, vblock_size - now, rsa)) {
		CPRINTF("[Error verifying preamble]\n");
		RSAPublicKeyFree(rsa);
		return IMAGE_IS_BAD;
	}

	if (preamble->flags & VB_FIRMWARE_PREAMBLE_USE_RO_NORMAL) {
		CPRINTF("[Flags says USE_RO_NORMAL]\n");
		RSAPublicKeyFree(rsa);
		return IMAGE_IS_GOOD_BUT_USE_RO_ANYWAY;
	}

	watchdog_reload();
	if (0 != EqualData(fv_data, fv_size, &preamble->body_digest, rsa)) {
		CPRINTF("Error verifying firmware body]\n");
		RSAPublicKeyFree(rsa);
		return IMAGE_IS_BAD;
	}

	RSAPublicKeyFree(rsa);

	watchdog_reload();
	CPRINTF("[Verified!]\n");
	return IMAGE_IS_GOOD;
}

/* Might I want to jump to one of the RW images? */
static int maybe_jump_to_other_image(void)
{
	/* We'll only jump to another image if we're currently in RO */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return 0;

#ifdef CONFIG_TASK_KEYSCAN
	/* Don't jump if recovery requested */
	if (keyboard_scan_recovery_pressed()) {
		CPUTS("[Vboot staying in RO because recovery key pressed]\n");
		return 0;
	}
#endif

	/*
	 * Don't jump if we're in RO becuase we jumped there (this keeps us
	 * from jumping to RO only to jump right back).
	 */
	if (system_jumped_to_this_image())
		return 0;

#if !defined(CHIP_stm32)
	/*
	 * TODO: (crosbug.com/p/8572) Daisy and Snow don't define a GPIO for
	 * the recovery signal from servo, so we can't check it.  BDS uses the
	 * DOWN button.
	 */
	if (gpio_get_level(GPIO_RECOVERYn) == 0) {
		CPUTS("[Vboot staying in RO due to recovery signal]\n");
		return 0;
	}
#endif

	/* Okay, we might want to jump to a RW image. */
	return 1;
}

int vboot_check_signature(void)
{
	enum howgood r;
	timestamp_t ts1, ts2;

	CPRINTF("[%T Vboot init]\n");

	if (!maybe_jump_to_other_image())
		return EC_SUCCESS;

	CPRINTF("[%T Vboot check image A...]\n");

	ts1 = get_time();
	r = good_image((uint8_t *)CONFIG_VBOOT_ROOTKEY_OFF,
		       (uint8_t *)CONFIG_VBLOCK_RW_OFF, CONFIG_VBLOCK_SIZE,
		       (uint8_t *)CONFIG_FW_RW_OFF, CONFIG_FW_RW_SIZE);
	ts2 = get_time();

	CPRINTF("[%T Vboot result=%d, elapsed time=%ld us]\n",
		r, ts2.val - ts1.val);

	switch (r) {
	case IMAGE_IS_GOOD:
		CPRINTF("[Image A verified]\n");
		system_run_image_copy(SYSTEM_IMAGE_RW_A);
		CPRINTF("[ERROR: Unable to jump to image A]\n");
		goto bad;
	case IMAGE_IS_GOOD_BUT_USE_RO_ANYWAY:
		CPRINTF("[Image A verified]\n");
		CPRINTF("[Staying in RO mode]\n");
		return EC_SUCCESS;
	default:
		CPRINTF("[Image A is invalid]\n");
	}

#ifdef CONFIG_RW_B
	CPRINTF("[%T Vboot check image B...]\n");

	ts1 = get_time();
	r = good_image((uint8_t *)CONFIG_VBOOT_ROOTKEY_OFF,
		       (uint8_t *)CONFIG_VBLOCK_RW_B_OFF, CONFIG_VBLOCK_SIZE,
		       (uint8_t *)CONFIG_FW_RW_B_OFF, CONFIG_FW_RW_B_SIZE);
	ts2 = get_time();

	CPRINTF("[%T Vboot result=%d, elapsed time=%ld us]\n",
		r, ts2.val - ts1.val);

	switch (r) {
	case IMAGE_IS_GOOD:
		CPRINTF("[Image B verified]\n");
		system_run_image_copy(SYSTEM_IMAGE_RW_B);
		CPRINTF("[ERROR: Unable to jump to image B]\n");
		goto bad;
	case IMAGE_IS_GOOD_BUT_USE_RO_ANYWAY:
		CPRINTF("[Image B verified]\n");
		CPRINTF("[Staying in RO mode]\n");
		return EC_SUCCESS;
	default:
		CPRINTF("[Image B is invalid]\n");
	}
#else  /* CONFIG_RW_B */
	CPRINTF("[Vboot no image B to check]\n");
#endif  /* CONFIG_RW_B */

bad:
	CPRINTF("[Staying in RO mode]\n");
	CPRINTF("[FIXME: How to trigger recovery mode?]\n");
	return EC_ERROR_UNKNOWN;
}

