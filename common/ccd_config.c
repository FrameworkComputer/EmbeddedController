/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug configuration
 */

#include "case_closed_debug.h"
#include "common.h"
#include "console.h"
#include "cryptoc/sha256.h"
#include "dcrypto.h"
#include "hooks.h"
#include "nvmem_vars.h"
#include "physical_presence.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"
#include "trng.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CCD, format, ## args)

/* Restriction state for ccdunlock when no password is set */
enum ccd_unlock_restrict {
	/* Unrestricted */
	CCD_UNLOCK_UNRESTRICTED = 0,

	/* Physical presence required for unlock unless disabled by config */
	CCD_UNLOCK_NEED_PP,

	/* Unlock not allowed */
	CCD_UNLOCK_DISABLED
};

/* Minimum time between password attempts */
#define PASSWORD_RATE_LIMIT_US (3 * SECOND)

/* Current version of case-closed debugging configuration struct */
#define CCD_CONFIG_VERSION 0x10

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

/* Size of password salt and digest in bytes */
#define CCD_PASSWORD_SALT_SIZE 4
#define CCD_PASSWORD_DIGEST_SIZE 16

struct ccd_config {
	/* Version (CCD_CONFIG_VERSION) */
	uint8_t version;

	/*
	 * Flags.  These MUST immediately follow version, so that the test
	 * lab flag is always the LSBit of the first flags byte.
	 */
	uint8_t flags[3];

	/* Capabilities */
	uint8_t capabilities[8];

	/* Password salt (random) */
	uint8_t password_salt[CCD_PASSWORD_SALT_SIZE];

	/*
	 * Password digest = truncated
	 * SHA256_digest(password_salt+device_id+password)
	 */
	uint8_t password_digest[CCD_PASSWORD_DIGEST_SIZE];
};

struct ccd_capability_info {
	/* Capability name */
	const char *name;

	/* Default state, if config set to CCD_CAP_STATE_DEFAULT */
	enum ccd_capability_state default_state;
};

/* Flags for ccd_reset_config() */
enum ccd_reset_config_flags {
	/* Also reset test lab flag */
	CCD_RESET_TEST_LAB = (1 << 0),

	/* Only reset Always/UnlessLocked settings */
	CCD_RESET_UNLOCKED_ONLY = (1 << 1),

	/* Use RMA/factory defaults */
	CCD_RESET_RMA = (1 << 2)
};

/* Forward declarations of static functions */
static int ccd_reset_config(unsigned flags);

/* Nvmem variable name for CCD config */
static const uint8_t k_ccd_config = NVMEM_VAR_CCD_CONFIG;

/* Flags which can be set via ccd_set_flag() */
static const uint32_t k_public_flags =
		CCD_FLAG_OVERRIDE_WP_AT_BOOT |
		CCD_FLAG_OVERRIDE_WP_STATE_ENABLED;

/* List of CCD capability info; must be in same order as enum ccd_capability */
static const struct ccd_capability_info cap_info[CCD_CAP_COUNT] = {
	{"UartAPTX",		CCD_CAP_STATE_ALWAYS},
	{"UartAPRX",		CCD_CAP_STATE_ALWAYS},
	{"UartECTX",		CCD_CAP_STATE_ALWAYS},
	{"UartECRX",		CCD_CAP_STATE_IF_OPENED},

	{"FlashAP",		CCD_CAP_STATE_IF_OPENED},
	{"FlashEC",		CCD_CAP_STATE_IF_OPENED},
	{"WPOverride",		CCD_CAP_STATE_IF_OPENED},
	{"RebootECAP",		CCD_CAP_STATE_IF_OPENED},

	{"Cr50FullConsole",	CCD_CAP_STATE_IF_OPENED},
	{"UnlockNoReboot",	CCD_CAP_STATE_ALWAYS},
	{"UnlockNoShortPP",	CCD_CAP_STATE_ALWAYS},
	{"OpenNoTPMWipe",	CCD_CAP_STATE_IF_OPENED},

	{"OpenNoLongPP",	CCD_CAP_STATE_IF_OPENED},
	{"BatteryBypassPP",	CCD_CAP_STATE_ALWAYS},
	{"UpdateNoTPMWipe",	CCD_CAP_STATE_ALWAYS},
};

static const char *ccd_state_names[CCD_STATE_COUNT] = {
	"Locked", "Unlocked", "Opened"};
static const char *ccd_cap_state_names[CCD_CAP_STATE_COUNT] = {
	"Default", "Always", "UnlessLocked", "IfOpened"};

static enum ccd_state ccd_state = CCD_STATE_LOCKED;
static struct ccd_config config;
static uint8_t ccd_config_loaded;
static uint8_t force_disabled;
static struct mutex ccd_config_mutex;

/******************************************************************************/
/* Raw config accessors */

/**
 * Get CCD flags.
 *
 * @return the current flags mask.
 */
static uint32_t raw_get_flags(void)
{
	return (uint32_t)(config.flags[0] << 0)
			| ((uint32_t)config.flags[1] << 8)
			| ((uint32_t)config.flags[2] << 16);
}

/**
 * Set a single CCD flag.
 *
 * This does NOT call ccd_save_config() or lock the mutex.  Caller must do
 * those.
 *
 * @param flag		Flag to set
 * @param value		New value for flag (0=clear, non-zero=set)
 */
static void raw_set_flag(enum ccd_flag flag, int value)
{
	uint32_t f;

	f = raw_get_flags();
	if (value)
		f |= flag;
	else
		f &= ~flag;

	config.flags[0] = (uint8_t)(f >> 0);
	config.flags[1] = (uint8_t)(f >> 8);
	config.flags[2] = (uint8_t)(f >> 16);
}

/**
 * Get a raw capability state from the config
 *
 * @param cap			Capability to check
 * @param translate_default	If non-zero, translate CCD_CAP_STATE_DEFAULT
 *				to the actual default for that config
 * @return The capability state.
 */
static enum ccd_capability_state raw_get_cap(enum ccd_capability cap,
					     int translate_default)
{
	int c =	(config.capabilities[cap / 4] >> (2 * (cap % 4))) & 3;

	if (c == CCD_CAP_STATE_DEFAULT && translate_default)
		c = cap_info[cap].default_state;

	return c;
}

/**
 * Set a raw capability to the config.
 *
 * This does NOT call ccd_save_config() or lock the mutex.  Caller must do
 * those.
 *
 * @param cap		Capability to set
 * @param state		New state for capability
 */
static void raw_set_cap(enum ccd_capability cap,
			    enum ccd_capability_state state)
{
	config.capabilities[cap / 4] &= ~(3 << (2 * (cap % 4)));
	config.capabilities[cap / 4] |= (state & 3) << (2 * (cap % 4));
}

/**
 * Check if a password is set.
 * @return 1 if password is set, 0 if it's not
 */
static int raw_has_password(void)
{
	uint8_t set = 0;
	int i;

	/* Password is set unless salt and digest are all zero */
	for (i = 0; i < sizeof(config.password_salt); i++)
		set |= config.password_salt[i];
	for (i = 0; i < sizeof(config.password_digest); i++)
		set |= config.password_digest[i];

	return !!set;
}

/**
 * Calculate the expected digest for a password.
 *
 * Uses the unique device ID and the salt from the config.
 *
 * @param digest	Pointer to a CCD_PASSWORD_DIGEST_SIZE buffer
 * @param password	The password to digest
 */
static void ccd_password_digest(uint8_t *digest, const char *password)
{
	HASH_CTX sha;
	uint8_t *unique_id;
	int unique_id_len;

	unique_id_len = system_get_chip_unique_id(&unique_id);

	DCRYPTO_SHA256_init(&sha, 0);
	HASH_update(&sha, config.password_salt, sizeof(config.password_salt));
	HASH_update(&sha, unique_id, unique_id_len);
	HASH_update(&sha, password, strlen(password));
	memcpy(digest, HASH_final(&sha), CCD_PASSWORD_DIGEST_SIZE);
}

/**
 * Check the password.
 *
 * @param password	The password to check
 * @return EC_SUCCESS, EC_ERROR_BUSY if too soon since last attempt, or
 *	   EC_ERROR_ACCESS_DENIED if mismatch.
 */
static int raw_check_password(const char *password)
{
	/*
	 * Time of last password attempt; initialized to 0 at boot.  Yes, we're
	 * only keeping the bottom 32 bits of the timer here, so on a
	 * wraparound (every ~4000 seconds) it's possible for an attacker to
	 * get one extra attempt.  But it still behaves properly at boot,
	 * requiring the system to be up PASSWORD_RATE_LIMIT_US before allowing
	 * the first attempt.
	 */
	static uint32_t last_password_time;

	uint8_t digest[CCD_PASSWORD_DIGEST_SIZE];
	uint32_t t;

	/* If no password is set, match only an empty password */
	if (!raw_has_password())
		return *password ? EC_ERROR_ACCESS_DENIED : EC_SUCCESS;

	/* Rate limit password attempts */
	t = get_time().le.lo;
	if (t - last_password_time < PASSWORD_RATE_LIMIT_US)
		return EC_ERROR_BUSY;
	last_password_time = t;

	/* Calculate the digest of the password */
	ccd_password_digest(digest, password);

	if (safe_memcmp(digest, config.password_digest,
			sizeof(config.password_digest)))
		return EC_ERROR_ACCESS_DENIED;

	return EC_SUCCESS;
}

/**
 * Clear the password.
 *
 * This does NOT call ccd_save_config() or lock the mutex.  Caller must do
 * those.
 */
static void raw_reset_password(void)
{
	memset(config.password_salt, 0, sizeof(config.password_salt));
	memset(config.password_digest, 0, sizeof(config.password_digest));
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED, 0);
}

/**
 * Set the password.
 *
 * @param password	New password; must be non-empty
 */
static void raw_set_password(const char *password)
{
	/* Get a new salt */
	rand_bytes(config.password_salt, sizeof(config.password_salt));

	/* Update the password digest */
	ccd_password_digest(config.password_digest, password);

	/* Track whether we were opened when we set the password */
	raw_set_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED,
				     ccd_state == CCD_STATE_UNLOCKED);
}

/******************************************************************************/
/* Internal methods */

#ifdef CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
/* TODO(rspangler): remove when we wire this up to real capabilities */
void test_ccd_change_hook(void)
{
	CPRINTS("CCD change hook called");
}
DECLARE_HOOK(HOOK_CCD_CHANGE, test_ccd_change_hook, HOOK_PRIO_DEFAULT);
#endif

/**
 * Set the CCD state.
 *
 * @param state		New CCD state
 */
static void ccd_set_state(enum ccd_state state)
{
	if (state == ccd_state)
		return;

	ccd_state = state;

	/* Notify CCD users of configuration change */
	hook_notify(HOOK_CCD_CHANGE);
}

/**
 * Load CCD config from nvmem_vars
 *
 * @return EC_SUCCESS or non-zero error code.
 */
static void ccd_load_config(void)
{
	const struct tuple *t;

	/* Don't reload if we're already loaded */
	if (ccd_config_loaded)
		return;

	/* Load config data from nvmem */
	t = getvar(&k_ccd_config, sizeof(k_ccd_config));

	/* Use defaults if config data is not present */
	if (!t) {
		if (board_is_first_factory_boot()) {
			/* Give factory RMA access */
			CPRINTS("CCD using factory config");
			ccd_reset_config(CCD_RESET_TEST_LAB | CCD_RESET_RMA);
		} else {
			/* Somehow we lost our config; normal defaults */
			CPRINTS("CCD using default config");
			ccd_reset_config(CCD_RESET_TEST_LAB);
		}

		ccd_config_loaded = 1;
		return;
	}

	/* Copy the tuple data */
	memcpy(&config, tuple_val(t), MIN(sizeof(config), t->val_len));

	/* If version or size is wrong, reset to defaults */
	if (config.version != CCD_CONFIG_VERSION ||
	    t->val_len != sizeof(config)) {
		CPRINTS("CCD config mismatch; using defaults");
		/*
		 * If the config data was big enough to hold the test lab bit,
		 * preserve it.  That's guaranteed to be in the same place for
		 * all data versions.
		 */
		ccd_reset_config(t->val_len < 2 ? CCD_RESET_TEST_LAB : 0);
	}

	ccd_config_loaded = 1;

	/* Notify CCD users of configuration change */
	hook_notify(HOOK_CCD_CHANGE);
}

/**
 * Save CCD config to nvmem_vars
 *
 * @return EC_SUCCESS or non-zero error code.
 */
static int ccd_save_config(void)
{
	int rv;

	rv = setvar(&k_ccd_config, sizeof(k_ccd_config),
		      (const uint8_t *)&config, sizeof(config));
	if (rv)
		return rv;

	rv = writevars();

	/* Notify CCD users of configuration change */
	hook_notify(HOOK_CCD_CHANGE);

	return rv;
}

/**
 * Set a CCD capability to a new state.
 *
 * @param cap		Capability to set
 * @param state		New state for capability
 * @return EC_SUCCESS or non-zero error code.
 */
static int ccd_set_cap(enum ccd_capability cap, enum ccd_capability_state state)
{
	if (!ccd_config_loaded)
		return EC_ERROR_BUSY;

	if (state == raw_get_cap(cap, 0))
		return EC_SUCCESS;	/* Capability not changed */

	mutex_lock(&ccd_config_mutex);
	raw_set_cap(cap, state);
	mutex_unlock(&ccd_config_mutex);

	return ccd_save_config();
}

/**
 * Reset CCD config to defaults.
 *
 * @param flags		Reset flags (see enum ccd_reset_config_flags)
 * @return EC_SUCCESS, or non-zero if error.
 */
static int ccd_reset_config(unsigned flags)
{
	int old_lab = ccd_get_flag(CCD_FLAG_TEST_LAB);

	mutex_lock(&ccd_config_mutex);

	if (flags & CCD_RESET_UNLOCKED_ONLY) {
		/* Only set config options that are mutable when unlocked */
		int i;

		/* Reset the password if it was set when unlocked */
		if (ccd_get_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED))
			raw_reset_password();

		/* Reset all capabilities that aren't IfOpened */
		for (i = 0; i < CCD_CAP_COUNT; i++) {
			if (raw_get_cap(i, 1) == CCD_CAP_STATE_IF_OPENED)
				continue;
			raw_set_cap(i, CCD_CAP_STATE_DEFAULT);
		}

		/* Flags all require IfOpened, so don't touch those */
	} else {
		/* Reset the entire config */
		memset(&config, 0, sizeof(config));
		config.version = CCD_CONFIG_VERSION;
	}

	if (flags & CCD_RESET_RMA) {
		/* Force RMA settings */
		int i;

		/* Allow all capabilities all the time */
		for (i = 0; i < CCD_CAP_COUNT; i++) {
			/*
			 * Restricted console commands are still IfOpened, but
			 * that's kinda meaningless because we set a
			 * well-defined password below.
			 */
			if (i == CCD_CAP_CR50_RESTRICTED_CONSOLE)
				continue;

			raw_set_cap(i, CCD_CAP_STATE_ALWAYS);
		}

		/* Force WP disabled at boot */
		raw_set_flag(CCD_FLAG_OVERRIDE_WP_AT_BOOT, 1);
		raw_set_flag(CCD_FLAG_OVERRIDE_WP_STATE_ENABLED, 0);
	}

	/* Restore test lab flag unless explicitly resetting it */
	if (!(flags & CCD_RESET_TEST_LAB))
		raw_set_flag(CCD_FLAG_TEST_LAB, old_lab);

	mutex_unlock(&ccd_config_mutex);

	return ccd_save_config();
}

/**
 * Convert a string to a capability index.
 *
 * @param name		Capability name to find
 * @return The capability index, or CCD_CAP_COUNT if error
 */
static enum ccd_capability ccd_cap_from_name(const char *name)
{
	int i;

	for (i = 0; i < CCD_CAP_COUNT; i++) {
		if (!strcasecmp(name, cap_info[i].name))
			return i;
	}

	return CCD_CAP_COUNT;
}

/**
 * Reset the password.
 *
 * @return EC_SUCCESS or non-zero error code.
 */
static int ccd_reset_password(void)
{
	mutex_lock(&ccd_config_mutex);
	raw_reset_password();
	mutex_unlock(&ccd_config_mutex);

	return ccd_save_config();
}

/**
 * Set the password.
 *
 * @param password	New password; must be non-empty
 * @return EC_SUCCESS or non-zero error code.
 */
static int ccd_set_password(const char *password)
{
	mutex_lock(&ccd_config_mutex);
	raw_set_password(password);
	mutex_unlock(&ccd_config_mutex);

	return ccd_save_config();
}

/******************************************************************************/
/* Handlers for state changes requiring physical presence */

static void ccd_open_done(void)
{
	if (!ccd_is_cap_enabled(CCD_CAP_OPEN_WITHOUT_TPM_WIPE)) {
		/* Can't open unless wipe succeeds */
		if (board_wipe_tpm() != EC_SUCCESS) {
			CPRINTS("CCD open TPM wipe failed");
			return;
		}
	}

	if (!ccd_is_cap_enabled(CCD_CAP_UNLOCK_WITHOUT_AP_REBOOT))
		board_reboot_ap();

	CPRINTS("CCD opened");
	ccd_set_state(CCD_STATE_OPENED);
}

static void ccd_unlock_done(void)
{
	if (!ccd_is_cap_enabled(CCD_CAP_UNLOCK_WITHOUT_AP_REBOOT))
		board_reboot_ap();

	CPRINTS("CCD unlocked");
	ccd_set_state(CCD_STATE_UNLOCKED);
}

static void ccd_testlab_toggle(void)
{
	int v = !ccd_get_flag(CCD_FLAG_TEST_LAB);

	CPRINTS("Test lab mode %sbled", v ? "ena" : "dis");

	/* Use raw_set_flag() because the test lab flag is internal */
	mutex_lock(&ccd_config_mutex);
	raw_set_flag(CCD_FLAG_TEST_LAB, v);
	mutex_unlock(&ccd_config_mutex);
}

/******************************************************************************/
/* External interface */

void ccd_config_init(enum ccd_state state)
{
	/* Set initial state, after making sure it's a valid one */
	if (state != CCD_STATE_UNLOCKED && state != CCD_STATE_OPENED)
		state = CCD_STATE_LOCKED;
	ccd_state = state;

	ccd_load_config();
}

int ccd_get_flag(enum ccd_flag flag)
{
	uint32_t f = raw_get_flags();

	if (!ccd_config_loaded || force_disabled)
		return 0;

	return !!(f & flag);
}

int ccd_set_flag(enum ccd_flag flag, int value)
{
	if (force_disabled)
		return EC_ERROR_ACCESS_DENIED;

	/* Fail if trying to set a private flag */
	if (flag & ~k_public_flags)
		return EC_ERROR_ACCESS_DENIED;

	if (!ccd_config_loaded)
		return EC_ERROR_BUSY;

	if (ccd_get_flag(flag) == !!value)
		return EC_SUCCESS;

	mutex_lock(&ccd_config_mutex);
	raw_set_flag(flag, value);
	mutex_unlock(&ccd_config_mutex);
	return ccd_save_config();
}

int ccd_is_cap_enabled(enum ccd_capability cap)
{
	if (!ccd_config_loaded || force_disabled)
		return 0;

	switch (raw_get_cap(cap, 1)) {
	case CCD_CAP_STATE_ALWAYS:
		return 1;
	case CCD_CAP_STATE_UNLESS_LOCKED:
		return ccd_state != CCD_STATE_LOCKED;
	case CCD_CAP_STATE_IF_OPENED:
	default:
		return ccd_state == CCD_STATE_OPENED;
	}
}

enum ccd_state ccd_get_state(void)
{
	return ccd_state;
}

void ccd_disable(void)
{
	CPRINTS("CCD disabled");
	force_disabled = 1;
	ccd_set_state(CCD_STATE_LOCKED);
}

/******************************************************************************/
/* Console commands */

static int command_ccdinfo(int argc, char **argv)
{
	int i;

	ccprintf("State: %s%s\n", ccd_state_names[ccd_state],
		 force_disabled ? " (Disabled)" : "");
	ccprintf("Password: %s\n", raw_has_password() ? "set" : "none");
	ccprintf("Flags: 0x%06x\n", raw_get_flags());

	ccprintf("Capabilities: %.8h\n", config.capabilities);
	for (i = 0; i < CCD_CAP_COUNT; i++) {
		int c = raw_get_cap(i, 0);

		ccprintf("%-15s %c %d=%s",
			 cap_info[i].name,
			 ccd_is_cap_enabled(i) ? 'Y' : '-',
			 c, ccd_cap_state_names[c]);
		if (c == CCD_CAP_STATE_DEFAULT)
			ccprintf(" (%s)",
				ccd_cap_state_names[cap_info[i].default_state]);
		ccprintf("\n");
		cflush();
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdinfo, command_ccdinfo,
			     "",
			     "Print CCD state");

static int command_ccdreset(int argc, char **argv)
{
	int flags = 0;

	if (argc > 1) {
		if (!strcasecmp(argv[1], "rma"))
			flags = CCD_RESET_RMA;
		else
			return EC_ERROR_PARAM1;
	}

	switch (ccd_state) {
	case CCD_STATE_OPENED:
		ccprintf("%sResetting all settings.\n",
			 flags & CCD_RESET_RMA ? "RMA " : "");
		/* Note that this does not reset the testlab flag */
		return ccd_reset_config(flags);

	case CCD_STATE_UNLOCKED:
		ccprintf("Resetting unlocked settings.\n");
		return ccd_reset_config(CCD_RESET_UNLOCKED_ONLY);

	default:
		return EC_ERROR_ACCESS_DENIED;
	}
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdreset, command_ccdreset,
			     "[rma]",
			     "Reset CCD config");

static int command_ccdset(int argc, char **argv)
{
	enum ccd_capability cap;
	enum ccd_capability_state old;
	enum ccd_capability_state new;

	/* Only works if unlocked or opened */
	if (ccd_state == CCD_STATE_LOCKED)
		return EC_ERROR_ACCESS_DENIED;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	/* Get capability to set */
	cap = ccd_cap_from_name(argv[1]);
	if (cap == CCD_CAP_COUNT)
		return EC_ERROR_PARAM1;

	/* Get new state */
	for (new = CCD_CAP_STATE_DEFAULT; new < CCD_CAP_STATE_COUNT; new++) {
		if (!strcasecmp(argv[2], ccd_cap_state_names[new]))
			break;
	}
	if (new == CCD_CAP_STATE_COUNT)
		return EC_ERROR_PARAM2;

	/* Get current state */
	old = raw_get_cap(cap, 1);

	/* If we're only unlocked, can't change to/from IfOpened */
	if (ccd_state == CCD_STATE_UNLOCKED &&
	    (new == CCD_CAP_STATE_IF_OPENED || old == CCD_CAP_STATE_IF_OPENED))
		return EC_ERROR_ACCESS_DENIED;

	/* Set new state */
	return ccd_set_cap(cap, new);
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdset, command_ccdset,
			     "<cap> <state>",
			     "Set CCD capability state");

static int command_ccdpassword(int argc, char **argv)
{
	/* Only works if unlocked or opened */
	if (ccd_state == CCD_STATE_LOCKED)
		return EC_ERROR_ACCESS_DENIED;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* If password was set from Opened, can't change if just Unlocked */
	if (raw_has_password() && ccd_state == CCD_STATE_UNLOCKED &&
	    !ccd_get_flag(CCD_FLAG_PASSWORD_SET_WHEN_UNLOCKED))
		return EC_ERROR_ACCESS_DENIED;

	if (!strcasecmp(argv[1], "clear"))
		return ccd_reset_password();

	/* Set new password */
	return ccd_set_password(argv[1]);
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdpassword, command_ccdpassword,
			     "[<new password> | clear]",
			     "Set or clear CCD password");

static int command_ccdopen(int argc, char **argv)
{
	int is_long = 1;
	int need_pp = 1;
	int rv;

	if (force_disabled)
		return EC_ERROR_ACCESS_DENIED;

	if (ccd_state == CCD_STATE_OPENED)
		return EC_SUCCESS;

	if (raw_has_password()) {
		if (argc < 2)
			return EC_ERROR_PARAM_COUNT;

		rv = raw_check_password(argv[1]);
		if (rv)
			return rv;
	} else if (!board_fwmp_allows_unlock()) {
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Fail and abort if already checking physical presence */
	if (physical_detect_busy()) {
		physical_detect_abort();
		return EC_ERROR_BUSY;
	}

	/* Reduce physical presence if enabled via config */
	if (ccd_is_cap_enabled(CCD_CAP_OPEN_WITHOUT_LONG_PP))
		is_long = 0;
	if (!is_long && ccd_is_cap_enabled(CCD_CAP_UNLOCK_WITHOUT_SHORT_PP))
		need_pp = 0;

	/* Bypass physical presence check entirely if battery is removed */
	if (ccd_is_cap_enabled(CCD_CAP_REMOVE_BATTERY_BYPASSES_PP) &&
	    !board_battery_is_present()) {
		need_pp = 0;
	}

	if (need_pp) {
		/* Start physical presence detect */
		ccprintf("Starting CCD open...\n");
		return physical_detect_start(is_long, ccd_open_done);
	} else {
		/* No physical presence required; go straight to done */
		ccd_open_done();
		return EC_SUCCESS;
	}
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdopen, command_ccdopen,
			     "[password]",
			     "Change CCD state to Opened");

static int command_ccdunlock(int argc, char **argv)
{
	int need_pp = 1;
	int rv;

	if (force_disabled)
		return EC_ERROR_ACCESS_DENIED;

	if (ccd_state == CCD_STATE_UNLOCKED)
		return EC_SUCCESS;

	/* Can go from opened to unlocked with no delay or password */
	if (ccd_state == CCD_STATE_OPENED) {
		ccd_unlock_done();
		return EC_SUCCESS;
	}

	if (raw_has_password()) {
		if (argc < 2)
			return EC_ERROR_PARAM_COUNT;

		rv = raw_check_password(argv[1]);
		if (rv)
			return rv;
	} else if (!board_fwmp_allows_unlock()) {
		/* Unlock disabled by FWMP */
		return EC_ERROR_ACCESS_DENIED;
	} else {
		/*
		 * When unlock is requested via the console, physical presence
		 * is required unless disabled by config.  This prevents a
		 * malicious peripheral from setitng a password.
		 *
		 * If this were a TPM vendor command from the AP, we would
		 * instead check unlock restrictions based on the user login
		 * state stored in ccd_unlock_restrict:
		 *
		 * 1) Unlock from the AP is unrestricted before any users
		 * login, so enrollment policy scripts can update CCD config.
		 *
		 * 2) Owner accounts can unlock, but require physical presence
		 * to prevent OS-level compromises from setting a password.
		 *
		 * 3) A non-owner account logging in blocks CCD config until
		 * the next AP reboot, as implied by TPM reboot.
		 */
	}

	/* Fail and abort if already checking physical presence */
	if (physical_detect_busy()) {
		physical_detect_abort();
		return EC_ERROR_BUSY;
	}

	/* Bypass physical presence check if configured to do so */
	if (ccd_is_cap_enabled(CCD_CAP_UNLOCK_WITHOUT_SHORT_PP))
		need_pp = 0;

	/* Bypass physical presence check entirely if battery is removed */
	if (ccd_is_cap_enabled(CCD_CAP_REMOVE_BATTERY_BYPASSES_PP) &&
	    !board_battery_is_present()) {
		need_pp = 0;
	}

	if (need_pp) {
		/* Start physical presence detect */
		ccprintf("Starting CCD unlock...\n");
		return physical_detect_start(0, ccd_unlock_done);
	} else {
		/* Unlock immediately */
		ccd_unlock_done();
		return EC_SUCCESS;
	}
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdunlock, command_ccdunlock,
			     "[password]",
			     "Change CCD state to Unlocked");

static int command_ccdlock(int argc, char **argv)
{
	/* Lock always works */
	ccprintf("CCD locked.\n");
	ccd_set_state(CCD_STATE_LOCKED);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdlock, command_ccdlock,
			     "",
			     "Change CCD state to Locked");

/* NOTE: Testlab command is console-only; no TPM vendor command for this */
static int command_testlab(int argc, char **argv)
{
	int newflag = 0;

	if (force_disabled)
		return EC_ERROR_ACCESS_DENIED;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "open")) {
		if (!ccd_get_flag(CCD_FLAG_TEST_LAB))
			return EC_ERROR_ACCESS_DENIED;

		/* Go directly to open state without wiping TPM or rebooting */
		ccd_set_state(CCD_STATE_OPENED);
		return EC_SUCCESS;
	}

	/* All other commands require CCD opened */
	if (ccd_state != CCD_STATE_OPENED)
		return EC_ERROR_ACCESS_DENIED;

	if (!parse_bool(argv[1], &newflag))
		return EC_ERROR_PARAM1;

	if (newflag == ccd_get_flag(CCD_FLAG_TEST_LAB))
		return EC_SUCCESS;  /* No change */

	/* If we're still here, need to toggle test lab flag */
	ccprintf("Requesting change of test lab flag.\n");
	if (newflag)
		ccprintf("NOTE: THIS WILL MAKE THIS DEVICE INSECURE!!!\n");
	return physical_detect_start(0, ccd_testlab_toggle);
}
DECLARE_SAFE_CONSOLE_COMMAND(testlab, command_testlab,
			     "<enable | disable | open>",
			     "Toggle testlab mode or open CCD");


#ifdef CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
/**
 * Test command to forcibly reset CCD config
 */
static int command_ccdoops(int argc, char **argv)
{
	/* Completely reset CCD config and go to opened state */
	force_disabled = 0;
	ccprintf("Aborting physical detect...\n");
	physical_detect_abort();
	ccprintf("Resetting CCD config...\n");
	ccd_reset_config(CCD_RESET_TEST_LAB);
	ccprintf("Opening CCD...\n");
	ccd_set_state(CCD_STATE_OPENED);
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdoops, command_ccdoops,
			     "",
			     "Force-reset CCD config");
#endif  /* CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE */

#ifdef CONFIG_CMD_CCDDISABLE
static int command_ccddisable(int argc, char **argv)
{
	ccd_disable();
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccddisable, command_ccddisable,
			     "",
			     "Force disable CCD config");
#endif  /* CONFIG_CMD_CCDDISABLE */
