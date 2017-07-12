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
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "rsa.h"
#include "rwsig.h"
#include "sha256.h"
#include "shared_mem.h"
#include "system.h"
#include "usb_pd.h"
#include "vboot.h"
#include "vb21_struct.h"

#define CPRINTS(format, args...) cprints(CC_VBOOT,"VB " format, ## args)
#define CPRINTF(format, args...) cprintf(CC_VBOOT,"VB " format, ## args)

enum vboot_ec_slot {
	VBOOT_EC_SLOT_A,
	VBOOT_EC_SLOT_B,
};

static int has_matrix_keyboard(void)
{
	return 0;
}

static int is_vboot_ec_supported(void)
{
#ifdef CONFIG_VBOOT_EC
	return 1;
#else
	return 0;
#endif
}

static int is_low_power_ap_boot_supported(void)
{
	return 0;
}

static int verify_slot(int slot)
{
	const struct vb21_packed_key *vb21_key;
	const struct vb21_signature *vb21_sig;
	const struct rsa_public_key *key;
	const uint8_t *sig;
	const uint8_t *data;
	int len;

	CPRINTS("Verifying RW_%c", slot == VBOOT_EC_SLOT_A ? 'A' : 'B');

	vb21_key = (const struct vb21_packed_key *)(
			CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_EC_PROTECTED_STORAGE_OFF +
			CONFIG_RO_PUBKEY_STORAGE_OFF);
	if (vb21_is_packed_key_valid(vb21_key)) {
		CPRINTS("Invalid key");
		return EC_ERROR_INVAL;
	}
	key = (const struct rsa_public_key *)
		((const uint8_t *)vb21_key + vb21_key->key_offset);

	if (slot == VBOOT_EC_SLOT_A) {
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

	if (vb21_is_signature_valid(vb21_sig, vb21_key)) {
		CPRINTS("Invalid signature");
		return EC_ERROR_INVAL;
	}
	sig = (const uint8_t *)vb21_sig + vb21_sig->sig_offset;
	len = vb21_sig->data_size;

	if (vboot_is_padding_valid(data, len,
				   CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)) {
		CPRINTS("Invalid padding");
		return EC_ERROR_INVAL;
	}

	if (vboot_verify(data, len, key, sig)) {
		CPRINTS("Invalid data");
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static int verify_and_jump(void)
{
	uint8_t slot;
	int rv;

	/* 1. Decide which slot to try */
	if (system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, &slot)) {
		CPRINTS("Failed to read try slot");
		slot = VBOOT_EC_SLOT_A;
	}

	/* 2. Verify the slot */
	rv = verify_slot(slot);
	if (rv) {
		if (rv != EC_ERROR_INVAL)
			/* Unknown error. The other slot isn't worth trying. */
			return rv;
		/* Verification error. The other slot is worth trying. */
		slot = 1 - slot;
		if (verify_slot(slot))
			/* Both slots failed */
			return rv;
		/* Proceed with the other slot. AP will help us fix it. */
	}

	/* 3. Jump (and reboot) */
	system_run_image_copy(slot == VBOOT_EC_SLOT_A ?
			SYSTEM_IMAGE_RW : SYSTEM_IMAGE_RW_B);

	return EC_ERROR_UNKNOWN;
}

/* Request more power: charging battery or more powerful AC adapter */
static void request_power(void)
{
	/* TODO: Blink LED */
	CPRINTS("%s", __func__);
}

static void request_recovery(void)
{
	/* TODO: Blink LED */
	CPRINTS("%s", __func__);
}

static int is_manual_recovery(void)
{
	return host_is_event_set(EC_HOST_EVENT_KEYBOARD_RECOVERY);
}

static void vboot_main(void);
DECLARE_DEFERRED(vboot_main);
static void vboot_main(void)
{
	const int check_charge_manager_frequency_usec = 10 * MSEC;
	int port = charge_manager_get_active_charge_port();

	if (port == CHARGE_PORT_NONE) {
		/* We loop here until charge manager is ready */
		hook_call_deferred(&vboot_main_data,
				   check_charge_manager_frequency_usec);
		return;
	}

	CPRINTS("Checking power");

	if (system_can_boot_ap()) {
		/*
		 * We are here for the two cases:
		 * 1. Booting on RO with a barrel jack adapter. We can continue
		 *    to boot AP with EC-RO. We'll jump later in softsync.
		 * 2. Booting on RW with a type-c charger. PD negotiation is
		 *    done and we can boot AP.
		 */
		CPRINTS("Got enough power");
		return;
	}

	if (system_get_image_copy() != SYSTEM_IMAGE_RO || !system_is_locked()) {
		/*
		 * If we're here, it means PD negotiation was attempted but
		 * we didn't get enough power to boot AP. This happens on RW
		 * or unlocked RO.
		 *
		 * This could be caused by a weak type-c charger. If that's
		 * the case, users need to plug a better charger. We could
		 * also be here because PD negotiation is still taking place.
		 * If so, we'll briefly show request power sign but it will
		 * be immediately corrected.
		 */
		request_power();
		return;
	}

	CPRINTS("Booting RO on weak battery/charger");

	if (is_manual_recovery()) {
		CPRINTS("Manual recovery requested");
		if (battery_is_present() || has_matrix_keyboard()) {
			request_power();
			return;
		}
		/* We don't request_power because we don't want to assume all
		 * devices support a non type-c charger. We open up a security
		 * hole by allowing EC-RO to do PD negotiation but attackers
		 * don't gain meaningful advantage on devices without a matrix
		 * keyboard */
		CPRINTS("Enable C%d PD comm", port);
		pd_comm_enable(port, 1);
		/* TODO: Inform PD task and make it negotiate */
		return;
	}

	if (!is_vboot_ec_supported()) {
		if (is_low_power_ap_boot_supported())
			/* If a device supports this feature, AP's boot power
			 * threshold should be set low. That will let EC-RO
			 * boot AP and softsync take care of RW verification. */
			return;
		request_power();
		return;
	}

	/* If successful, this won't return. */
	verify_and_jump();

	/* Failed to jump. Need recovery. */
	request_recovery();
}

DECLARE_HOOK(HOOK_INIT, vboot_main, HOOK_PRIO_DEFAULT);
