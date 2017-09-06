/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debugging configuration
 */
#ifndef __CROS_EC_CCD_CONFIG_H
#define __CROS_EC_CCD_CONFIG_H

/* Case-closed debugging state */
enum ccd_state {
	CCD_STATE_LOCKED = 0,
	CCD_STATE_UNLOCKED,
	CCD_STATE_OPENED,

	/* Number of CCD states */
	CCD_STATE_COUNT
};

/* Flags */
enum ccd_flag {
	/* Flags that can only be set internally; fill from bottom up */

	/*
	 * Test lab mode is enabled.  This MUST be in the first byte so that
	 * it's in a constant position across all versions of CCD config.
	 *
	 * Note: This is used internally by CCD config.  Do NOT test this
	 * to control other things; use capabilities for those.
	 */
	CCD_FLAG_TEST_LAB = (1 << 0),

	/*
	 * What state were we in when the password was set?
	 * (0=opened, 1=unlocked)
	 */
	CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED = (1 << 1),

	/* (flags in the middle are unused) */

	/* Flags that can be set via ccd_set_flags(); fill from top down */

	/* Override write protect at boot */
	CCD_FLAG_OVERRIDE_WP_AT_BOOT = (1 << 22),

	/*
	 * If overriding WP at boot, set it to what value
	 * (0=disabled, 1=enabled)
	 */
	CCD_FLAG_OVERRIDE_WP_STATE_ENABLED = (1 << 23),
};

/* Capabilities */
enum ccd_capability {
	/* UARTs to/from AP and EC */
	CCD_CAP_GSC_RX_AP_TX = 0,
	CCD_CAP_GSC_TX_AP_RX = 1,
	CCD_CAP_GSC_RX_EC_TX = 2,
	CCD_CAP_GSC_TX_EC_RX = 3,

	/* Access to AP SPI flash */
	CCD_CAP_AP_FLASH = 4,

	/* Access to EC flash (SPI or internal) */
	CCD_CAP_EC_FLASH = 5,

	/* Override WP temporarily or at boot */
	CCD_CAP_OVERRIDE_WP = 6,

	/* Reboot EC or AP */
	CCD_CAP_REBOOT_EC_AP = 7,

	/* GSC restricted console commands */
	CCD_CAP_GSC_RESTRICTED_CONSOLE = 8,

	/* Allow ccd-unlock or ccd-open without AP reboot */
	CCD_CAP_UNLOCK_WITHOUT_AP_REBOOT = 9,

	/* Allow ccd-unlock or ccd-open without short physical presence */
	CCD_CAP_UNLOCK_WITHOUT_SHORT_PP = 10,

	/* Allow ccd-open without wiping TPM data */
	CCD_CAP_OPEN_WITHOUT_TPM_WIPE = 11,

	/* Allow ccd-open without long physical presence */
	CCD_CAP_OPEN_WITHOUT_LONG_PP = 12,

	/* Allow removing the battery to bypass physical presence requirement */
	CCD_CAP_REMOVE_BATTERY_BYPASSES_PP = 13,

	/* Allow GSC firmware update without wiping TPM data */
	CCD_CAP_GSC_FW_UPDATE_WITHOUT_TPM_WIPE = 14,

	/* Access to I2C via USB */
	CCD_CAP_I2C = 15,

	/* Number of currently defined capabilities */
	CCD_CAP_COUNT
};

/**
 * Initialize CCD configuration at boot.
 *
 * This must be called before any command which gets/sets the configuration.
 *
 * @param state		Initial case-closed debugging state.  This should be
 *			CCD_STATE_LOCKED unless this is a debug build, or if
 *			a previous value is being restored after a low-power
 *			resume.
 */
void ccd_config_init(enum ccd_state state);

/**
 * Get a single CCD flag.
 *
 * @param flag		Flag to get
 * @return 1 if flag is set, 0 if flag is clear
 */
int ccd_get_flag(enum ccd_flag flag);

/**
 * Set a single CCD flag.
 *
 * @param flag		Flag to set
 * @param value		New value for flag (0=clear, non-zero=set)
 * @return EC_SUCCESS or non-zero error code.
 */
int ccd_set_flag(enum ccd_flag flag, int value);

/**
 * Check if a CCD capability is enabled in the current CCD mode.
 *
 * @param cap		Capability to check
 * @return 1 if capability is enabled, 0 if disabled
 */
int ccd_is_cap_enabled(enum ccd_capability cap);

/**
 * Get the current CCD state.
 *
 * This is intended for use by the board if it needs to back up the CCD state
 * across low-power states and then restore it when calling ccd_config_init().
 * Do NOT use this to gate debug capabilities; use ccd_is_cap_enabled() or
 * ccd_get_flag() instead.
 *
 * @return The current CCD state.
 */
enum ccd_state ccd_get_state(void);

/**
 * Force CCD disabled.
 *
 * This should be called if security checks fail and for some reason the board
 * can't immediately reboot.  It locks CCD and disables all CCD capabilities
 * until reboot.
 */
void ccd_disable(void);

#endif /* __CROS_EC_CCD_CONFIG_H */
