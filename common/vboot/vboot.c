/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Verify and jump to a RW image if power supply is not sufficient.
 */

#include "battery.h"
#include "charge_manager.h"
#include "chipset.h"
#include "clock.h"
#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_command.h"
#include "rsa.h"
#include "rwsig.h"
#include "stdbool.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "usb_pd.h"
#include "vboot.h"
#include "vb21_struct.h"

#define CPRINTS(format, args...) cprints(CC_VBOOT,"VB " format, ## args)
#define CPRINTF(format, args...) cprintf(CC_VBOOT,"VB " format, ## args)

static int has_matrix_keyboard(void)
{
	return 0;
}

static int verify_slot(enum ec_image slot)
{
	const struct vb21_packed_key *vb21_key;
	const struct vb21_signature *vb21_sig;
	const struct rsa_public_key *key;
	const uint8_t *sig;
	const uint8_t *data;
	int len;
	int rv;

	CPRINTS("Verifying %s", ec_image_to_string(slot));

	vb21_key = (const struct vb21_packed_key *)(
			CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_EC_PROTECTED_STORAGE_OFF +
			CONFIG_RO_PUBKEY_STORAGE_OFF);
	rv = vb21_is_packed_key_valid(vb21_key);
	if (rv) {
		CPRINTS("Invalid key (%d)", rv);
		return EC_ERROR_VBOOT_KEY;
	}
	key = (const struct rsa_public_key *)
		((const uint8_t *)vb21_key + vb21_key->key_offset);

	if (slot == EC_IMAGE_RW_A) {
		data = (const uint8_t *)(CONFIG_MAPPED_STORAGE_BASE +
				CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_A_STORAGE_OFF);
		vb21_sig = (const struct vb21_signature *)(
				CONFIG_MAPPED_STORAGE_BASE +
				CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_A_SIGN_STORAGE_OFF);
	} else {
		data = (const uint8_t *)(CONFIG_MAPPED_STORAGE_BASE +
				CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_B_STORAGE_OFF);
		vb21_sig = (const struct vb21_signature *)(
				CONFIG_MAPPED_STORAGE_BASE +
				CONFIG_EC_WRITABLE_STORAGE_OFF +
				CONFIG_RW_B_SIGN_STORAGE_OFF);
	}

	rv = vb21_is_signature_valid(vb21_sig, vb21_key);
	if (rv) {
		CPRINTS("Invalid signature (%d)", rv);
		return EC_ERROR_INVAL;
	}
	sig = (const uint8_t *)vb21_sig + vb21_sig->sig_offset;
	len = vb21_sig->data_size;

	if (vboot_is_padding_valid(data, len,
				   CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)) {
		CPRINTS("Invalid padding");
		return EC_ERROR_INVAL;
	}

	rv = vboot_verify(data, len, key, sig);
	if (rv) {
		CPRINTS("Invalid data (%d)", rv);
		return EC_ERROR_INVAL;
	}

	CPRINTS("Verified %s", ec_image_to_string(slot));

	return EC_SUCCESS;
}

static enum ec_status hc_verify_slot(struct host_cmd_handler_args *args)
{
	const struct ec_params_efs_verify *p = args->params;
	enum ec_image slot;

	switch (p->region) {
	case EC_FLASH_REGION_ACTIVE:
		slot = system_get_active_copy();
		break;
	case EC_FLASH_REGION_UPDATE:
		slot = system_get_update_copy();
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return verify_slot(slot) ? EC_RES_ERROR : EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EFS_VERIFY, hc_verify_slot, EC_VER_MASK(0));

static int verify_and_jump(void)
{
	enum ec_image slot;
	int rv;

	/* 1. Decide which slot to try */
	slot = system_get_active_copy();

	/* 2. Verify the slot */
	rv = verify_slot(slot);
	if (rv) {
		if (rv == EC_ERROR_VBOOT_KEY)
			/* Key error. The other slot isn't worth trying. */
			return rv;
		slot = system_get_update_copy();
		/* TODO(chromium:767050): Skip reading key again. */
		rv = verify_slot(slot);
		if (rv)
			/* Both slots failed */
			return rv;

		/* Proceed with the other slot. If this slot isn't expected, AP
		 * will catch it and request recovery after a few attempts. */
		if (system_set_active_copy(slot))
			CPRINTS("Failed to activate %s",
				ec_image_to_string(slot));
	}

	/* 3. Jump (and reboot) */
	rv = system_run_image_copy(slot);
	CPRINTS("Failed to jump (%d)", rv);

	return rv;
}

/* Request more power: charging battery or more powerful AC adapter */
__overridable void show_power_shortage(void)
{
	CPRINTS("%s", __func__);
}

__overridable void show_critical_error(void)
{
	CPRINTS("%s", __func__);
}

static bool pd_comm_enabled;

bool vboot_allow_usb_pd(void)
{
	return pd_comm_enabled;
}

void vboot_main(void)
{
	CPRINTS("Main");

	if (system_is_in_rw()) {
		/*
		 * We come here and immediately return. LED shows power shortage
		 * but it will be immediately corrected if the adapter can
		 * provide enough power.
		 */
		CPRINTS("Already in RW. Wait for power...");
		show_power_shortage();
		return;
	}

	if (!(crec_flash_get_protect() & EC_FLASH_PROTECT_GPIO_ASSERTED)) {
		/*
		 * If hardware WP is disabled, PD communication is enabled.
		 * We can return and wait for more power.
		 * Note: If software WP is disabled, we still perform EFS even
		 * though PD communication is enabled.
		 */
		CPRINTS("HW-WP not asserted.");
		show_power_shortage();
		return;
	}

	if (system_is_manual_recovery() ||
	    (system_get_reset_flags() & EC_RESET_FLAG_STAY_IN_RO)) {
		if (system_is_manual_recovery())
			CPRINTS("Manual recovery");

		if (battery_is_present() || has_matrix_keyboard()) {
			show_power_shortage();
			return;
		}
		/* We don't request_power because we don't want to assume all
		 * devices support a non type-c charger. We open up a security
		 * hole by allowing EC-RO to do PD negotiation but attackers
		 * don't gain meaningful advantage on devices without a matrix
		 * keyboard */
		CPRINTS("Enable PD comm");
		pd_comm_enabled = true;
		return;
	}

	clock_enable_module(MODULE_FAST_CPU, 1);
	/* If successful, this won't return. */
	verify_and_jump();
	clock_enable_module(MODULE_FAST_CPU, 0);

	/* Failed to jump. Need recovery. */
	show_critical_error();
}
