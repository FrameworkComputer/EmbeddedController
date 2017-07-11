/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug interface
 */
#ifndef __CROS_EC_CASE_CLOSED_DEBUG_H
#define __CROS_EC_CASE_CLOSED_DEBUG_H

enum ccd_mode {
	/*
	 * The disabled mode tri-states the DP and DN lines.
	 */
	CCD_MODE_DISABLED,

	/*
	 * The partial mode allows some CCD functionality and is to be set
	 * when the device is write protected and a CCD cable is detected.
	 * This mode gives access to the APs console.
	 */
	CCD_MODE_PARTIAL,

	/*
	 * The fully enabled mode is used in factory and test lab
	 * configurations where it is acceptable to be able to reflash the
	 * device over CCD.
	 */
	CCD_MODE_ENABLED,

	CCD_MODE_COUNT,
};

/*
 * Set current CCD mode, this function is idempotent.
 */
void ccd_set_mode(enum ccd_mode new_mode);

/* Initialize the PHY based on CCD state */
void ccd_phy_init(int enable_ccd);

/*
 * Get current CCD mode.
 */
enum ccd_mode ccd_get_mode(void);

/******************************************************************************/
/* New CCD "V1" configuration.  Eventually this will supersede the above code */

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
	/* AP and EC UART output and input */
	CCD_CAP_AP_UART_OUTPUT = 0,
	CCD_CAP_AP_UART_INPUT = 1,
	CCD_CAP_EC_UART_OUTPUT = 2,
	CCD_CAP_EC_UART_INPUT = 3,

	/* Access to AP SPI flash */
	CCD_CAP_AP_FLASH = 4,

	/* Access to EC flash (SPI or internal) */
	CCD_CAP_EC_FLASH = 5,

	/* Override WP temporarily or at boot */
	CCD_CAP_OVERRIDE_WP = 6,

	/* Reboot EC or AP */
	CCD_CAP_REBOOT_EC_AP = 7,

	/* Cr50 restricted console commands */
	CCD_CAP_CR50_RESTRICTED_CONSOLE = 8,

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

	/* Allow Cr50 firmware update without wiping TPM data */
	CCD_CAP_CR50_FW_UPDATE_WITHOUT_TPM_WIPE = 14,

	/* Number of currently defined capabilities */
	CCD_CAP_COUNT
};

/**
 * Initialize CCD configuration at boot.
 *
 * This must be called before any command which gets/sets the configuration.
 */
void ccd_config_init(void);

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
 * Check if a CCD capability is enabled in the current CCD mode
 *
 * @param cap		Capability to check
 * @return 1 if capability is enabled, 0 if disabled
 */
int ccd_is_cap_enabled(enum ccd_capability cap);

#endif /* __CROS_EC_CASE_CLOSED_DEBUG_H */
