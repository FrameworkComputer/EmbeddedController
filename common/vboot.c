/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Verified boot module for Chrome EC */

#include "console.h"
#include "cryptolib.h"
#include "eoption.h"
#include "gpio.h"
#include "host_command.h"
#include "power_button.h"
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

/****************************************************************************/

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

/****************************************************************************/

/* Might I want to jump to one of the RW images? */
static int maybe_jump_to_other_image(void)
{
	/* We'll only jump to another image if we're currently in RO */
	if (system_get_image_copy() != SYSTEM_IMAGE_RO)
		return 0;

#ifdef CONFIG_TASK_POWERBTN
	/* Don't jump if recovery requested */
	if (power_recovery_pressed()) {
		CPUTS("[Vboot staying in RO because recovery key pressed]\n");
		return 0;
	}
#endif

	/* Don't jump if we're in RO becuase we jumped there (this keeps us
	 * from jumping to RO only to jump right back). */
	if (system_jumped_to_this_image())
		return 0;

#if !defined(CHIP_stm32)
	/* TODO: (crosbug.com/p/8572) Daisy and Snow don't define a GPIO
	 * for the recovery signal from servo, so we can't check it.
	 * BDS uses the DOWN button. */
	if (gpio_get_level(GPIO_RECOVERYn) == 0) {
		CPUTS("[Vboot staying in RO due to recovery signal]\n");
		return 0;
	}
#endif

	/* Okay, we might want to jump to a RW image. */
	return 1;
}

/*****************************************************************************/
/* Initialization */

int vboot_pre_init(void)
{
	/* FIXME(wfrichar): crosbug.com/p/7453: should protect flash */
	return EC_SUCCESS;
}

int vboot_init(void)
{
	enum howgood r;
	timestamp_t ts1, ts2;

	CPRINTF("[%T Vboot init]\n");

	if (!maybe_jump_to_other_image())
		return EC_SUCCESS;

	CPRINTF("[%T Vboot check image A...]\n");

	ts1 = get_time();
	r = good_image((uint8_t *)CONFIG_VBOOT_ROOTKEY_OFF,
		       (uint8_t *)CONFIG_VBLOCK_A_OFF, CONFIG_VBLOCK_SIZE,
		       (uint8_t *)CONFIG_FW_A_OFF, CONFIG_FW_A_SIZE);
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

#ifdef CONFIG_NO_RW_B
	CPRINTF("[Vboot no image B to check]\n");
#else
	CPRINTF("[%T Vboot check image B...]\n");

	ts1 = get_time();
	r = good_image((uint8_t *)CONFIG_VBOOT_ROOTKEY_OFF,
		       (uint8_t *)CONFIG_VBLOCK_B_OFF, CONFIG_VBLOCK_SIZE,
		       (uint8_t *)CONFIG_FW_B_OFF, CONFIG_FW_B_SIZE);
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
#endif

bad:
	CPRINTF("[Staying in RO mode]\n");
	CPRINTF("[FIXME: How to trigger recovery mode?]\n");
	return EC_ERROR_UNKNOWN;
}

/****************************************************************************/
/* Host commands via LPC bus */
/****************************************************************************/

static int host_cmd_vboot(uint8_t *data, int *resp_size)
{
	struct ec_params_vboot_cmd *ptr =
		(struct ec_params_vboot_cmd *)data;
	uint8_t v;

	switch (ptr->in.cmd) {
	case VBOOT_CMD_GET_FLAGS:
		v = VBOOT_FLAGS_IMAGE_MASK & system_get_image_copy();
#ifdef CONFIG_FAKE_DEV_SWITCH
		if (eoption_get_bool(EOPTION_BOOL_FAKE_DEV))
			v |= VBOOT_FLAGS_FAKE_DEVMODE;
#endif
		ptr->out.get_flags.val = v;
		*resp_size = sizeof(struct ec_params_vboot_cmd);
		break;
	case VBOOT_CMD_SET_FLAGS:
		v = ptr->in.set_flags.val;
#ifdef CONFIG_FAKE_DEV_SWITCH
		if (v & VBOOT_FLAGS_FAKE_DEVMODE) {
			eoption_set_bool(EOPTION_BOOL_FAKE_DEV, 1);
			CPUTS("[Enabling fake dev-mode]\n");
		} else {
			eoption_set_bool(EOPTION_BOOL_FAKE_DEV, 0);
			CPUTS("[Disabling fake dev-mode]\n");
		}
#endif
		break;
	default:
		CPRINTF("[%T LB bad cmd 0x%x]\n", ptr->in.cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_VBOOT_CMD, host_cmd_vboot);
