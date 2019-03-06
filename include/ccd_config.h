/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debugging configuration
 */
#ifndef __CROS_EC_CCD_CONFIG_H
#define __CROS_EC_CCD_CONFIG_H

#include <stdint.h>
#include "common.h"
#include "compile_time_macros.h"

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
	CCD_FLAG_TEST_LAB = BIT(0),

	/*
	 * What state were we in when the password was set?
	 * (0=opened, 1=unlocked)
	 */
	CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED = BIT(1),

	/*
	 * Factory mode state
	 */
	CCD_FLAG_FACTORY_MODE_ENABLED = BIT(2),

	/* (flags in the middle are unused) */

	/* Flags that can be set via ccd_set_flags(); fill from top down */

	/* Override BATT_PRES_L at boot */
	CCD_FLAG_OVERRIDE_BATT_AT_BOOT = BIT(20),

	/*
	 * If overriding BATT_PRES_L at boot, set it to what value
	 * (0=disconnect, 1=connected)
	 */
	CCD_FLAG_OVERRIDE_BATT_STATE_CONNECT = BIT(21),

	/* Override write protect at boot */
	CCD_FLAG_OVERRIDE_WP_AT_BOOT = BIT(22),

	/*
	 * If overriding WP at boot, set it to what value
	 * (0=disabled, 1=enabled)
	 */
	CCD_FLAG_OVERRIDE_WP_STATE_ENABLED = BIT(23),
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

	/* Read-only access to hash or dump EC or AP flash */
	CCD_CAP_FLASH_READ = 16,

	/* Allow ccd open without dev mode enabled */
	CCD_CAP_OPEN_WITHOUT_DEV_MODE = 17,

	/* Allow ccd open from usb */
	CCD_CAP_OPEN_FROM_USB = 18,

	/* Override battery presence temporarily or at boot */
	CCD_CAP_OVERRIDE_BATT_STATE = 19,

	/* Number of currently defined capabilities */
	CCD_CAP_COUNT
};

/* Capability states */
enum ccd_capability_state {
	/* Default value */
	CCD_CAP_STATE_DEFAULT = 0,

	/* Always available (state >= CCD_STATE_LOCKED) */
	CCD_CAP_STATE_ALWAYS = 1,

	/* Unless locked (state >= CCD_STATE_UNLOCKED) */
	CCD_CAP_STATE_UNLESS_LOCKED = 2,

	/* Only if opened (state >= CCD_STATE_OPENED) */
	CCD_CAP_STATE_IF_OPENED = 3,

	/* Number of capability states */
	CCD_CAP_STATE_COUNT
};

struct ccd_capability_info {
	/* Capability name */
	const char *name;

	/* Default state, if config set to CCD_CAP_STATE_DEFAULT */
	enum ccd_capability_state default_state;
};

#ifdef CONFIG_CCD_OPEN_PREPVT
/* In prepvt images always allow ccd open from the console without dev mode */
#define CCD_CAP_STATE_OPEN_REQ CCD_CAP_STATE_ALWAYS
#else
/* In prod images restrict how ccd can be opened */
#define CCD_CAP_STATE_OPEN_REQ CCD_CAP_STATE_IF_OPENED
#endif

#define CAP_INFO_DATA {					  \
	{"UartGscRxAPTx",	CCD_CAP_STATE_ALWAYS},	  \
	{"UartGscTxAPRx",	CCD_CAP_STATE_ALWAYS},	  \
	{"UartGscRxECTx",	CCD_CAP_STATE_ALWAYS},	  \
	{"UartGscTxECRx",	CCD_CAP_STATE_IF_OPENED}, \
							  \
	{"FlashAP",		CCD_CAP_STATE_IF_OPENED}, \
	{"FlashEC",		CCD_CAP_STATE_IF_OPENED}, \
	{"OverrideWP",		CCD_CAP_STATE_IF_OPENED}, \
	{"RebootECAP",		CCD_CAP_STATE_IF_OPENED}, \
							  \
	{"GscFullConsole",	CCD_CAP_STATE_IF_OPENED}, \
	{"UnlockNoReboot",	CCD_CAP_STATE_ALWAYS},	  \
	{"UnlockNoShortPP",	CCD_CAP_STATE_ALWAYS},	  \
	{"OpenNoTPMWipe",	CCD_CAP_STATE_IF_OPENED}, \
							  \
	{"OpenNoLongPP",	CCD_CAP_STATE_IF_OPENED}, \
	{"BatteryBypassPP",	CCD_CAP_STATE_ALWAYS},	  \
	{"UpdateNoTPMWipe",	CCD_CAP_STATE_ALWAYS},	  \
	{"I2C",			CCD_CAP_STATE_IF_OPENED}, \
	{"FlashRead",		CCD_CAP_STATE_ALWAYS},	  \
	{"OpenNoDevMode",	CCD_CAP_STATE_OPEN_REQ}, \
	{"OpenFromUSB",		CCD_CAP_STATE_OPEN_REQ}, \
	{"OverrideBatt",	CCD_CAP_STATE_IF_OPENED}, \
	}

#define CCD_STATE_NAMES { "Locked", "Unlocked", "Opened" }
#define CCD_CAP_STATE_NAMES { "Default", "Always", "UnlessLocked", "IfOpened" }

/* Macros regarding ccd_capabilities */
#define CCD_CAP_BITS		2
#define CCD_CAP_BITMASK	(BIT(CCD_CAP_BITS) - 1)
#define CCD_CAPS_PER_BYTE	(8 / CCD_CAP_BITS)

/*
 * Subcommand code, used to pass different CCD commands using the same TPM
 * vendor command.
 */
enum ccd_vendor_subcommands {
	CCDV_PASSWORD = 0,
	CCDV_OPEN = 1,
	CCDV_UNLOCK = 2,
	CCDV_LOCK = 3,
	CCDV_PP_POLL_UNLOCK = 4,
	CCDV_PP_POLL_OPEN = 5,
	CCDV_GET_INFO = 6
};

enum ccd_pp_state {
	CCD_PP_CLOSED = 0,
	CCD_PP_AWAITING_PRESS = 1,
	CCD_PP_BETWEEN_PRESSES = 2,
	CCD_PP_DONE = 3
};

/* Structure to communicate information about CCD state. */
#define CCD_CAPS_WORDS ((CCD_CAP_COUNT * 2 + 31)/32)
struct ccd_info_response {
	uint32_t ccd_caps_current[CCD_CAPS_WORDS];
	uint32_t ccd_caps_defaults[CCD_CAPS_WORDS];
	uint32_t ccd_flags;
	uint8_t ccd_state;
	uint8_t ccd_force_disabled;
	/*
	 * A bitmap indicating ccd internal state.
	 * See "enum ccd_indicator_bits" below.
	 */
	uint8_t ccd_indicator_bitmap;
} __packed;

enum ccd_indicator_bits {
	/* has_password? */
	CCD_INDICATOR_BIT_HAS_PASSWORD = BIT(0),

	/* Are CCD capabilities in CCD_CAP_STATE_DEFAULT */
	CCD_INDICATOR_BIT_ALL_CAPS_DEFAULT = BIT(1),
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

/**
 * Get the factory mode state.
 *
 * @return 0 if factory mode is disabled, !=0 if factory mode is enabled.
 */
int ccd_get_factory_mode(void);

/* Flags for ccd_reset_config() */
enum ccd_reset_config_flags {
	/* Also reset test lab flag */
	CCD_RESET_TEST_LAB = BIT(0),

	/* Only reset Always/UnlessLocked settings */
	CCD_RESET_UNLOCKED_ONLY = BIT(1),

	/*
	 * Do a factory reset to enable factory mode. Factory mode sets all ccd
	 * capabilities to always and disables write protect
	 */
	CCD_RESET_FACTORY = BIT(2)
};

/**
 * Reset CCD config to the desired state.
 *
 * @param flags		Reset flags (see enum ccd_reset_config_flags)
 * @return EC_SUCCESS, or non-zero if error.
 */
int ccd_reset_config(unsigned int flags);

/**
 * Inform CCD about TPM reset so that the password management state machine
 * can be restarted.
 */
void ccd_tpm_reset_callback(void);

/**
 * Return True if the ccd password is set. It is possible that a pending ccd
 * change would set or clear the password, but we don't think this is a big
 * issue or risk for now.
 *
 * @return 1 if password is set, 0 if it's not
 */
int ccd_has_password(void);

/**
 * Enter CCD factory mode. This will clear the TPM, update the ccd config, and
 * then do a hard reboot if 'reset_required' is True.
 */
void enable_ccd_factory_mode(int reset_required);

/*
 * Enable factory mode but not necessarily rebooting the device. This will
 * clear the TPM and disable flash write protection. Will trigger system reset
 * only if 'reset_required' is True.
 */
void factory_enable(int reset_required);

#endif /* __CROS_EC_CCD_CONFIG_H */
