/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug configuration
 */

#include "common.h"
#include "byteorder.h"
#include "ccd_config.h"
#include "console.h"
#include "cryptoc/sha256.h"
#include "cryptoc/util.h"
#include "dcrypto.h"
#include "extension.h"
#include "hooks.h"
#include "nvmem_vars.h"
#include "physical_presence.h"
#include "shared_mem.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "timer.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"
#include "trng.h"
#include "wp.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CCD, format, ## args)

/* Let's make sure that CCD capability state enum fits into two bits. */
BUILD_ASSERT(CCD_CAP_STATE_COUNT <= 4);

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

/*
 * CCD command header; including the subcommand code used to demultiplex
 * various CCD commands over the same TPM vendor command.
 */
struct ccd_vendor_cmd_header {
	struct tpm_cmd_header tpm_header;
	uint8_t ccd_subcommand;
} __packed;

/* Size of password salt and digest in bytes */
#define CCD_PASSWORD_SALT_SIZE 4
#define CCD_PASSWORD_DIGEST_SIZE 16

/* Way longer than practical. */
#define CCD_MAX_PASSWORD_SIZE 40

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

/* Nvmem variable name for CCD config */
static const uint8_t k_ccd_config = NVMEM_VAR_CCD_CONFIG;

/* Flags which can be set via ccd_set_flag() */
static const uint32_t k_public_flags =
		CCD_FLAG_OVERRIDE_WP_AT_BOOT |
		CCD_FLAG_OVERRIDE_WP_STATE_ENABLED;

/* List of CCD capability info; must be in same order as enum ccd_capability */
static const struct ccd_capability_info cap_info[CCD_CAP_COUNT] = CAP_INFO_DATA;

static const char *ccd_state_names[CCD_STATE_COUNT] = CCD_STATE_NAMES;
static const char *ccd_cap_state_names[CCD_CAP_STATE_COUNT] =
	CCD_CAP_STATE_NAMES;

static enum ccd_state ccd_state = CCD_STATE_LOCKED;
static struct ccd_config config;
static uint8_t ccd_config_loaded;
static uint8_t force_disabled;
static struct mutex ccd_config_mutex;
static uint8_t ccd_console_active; /* CCD console command is in progress. */

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

int ccd_reset_config(unsigned int flags)
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
		for (i = 0; i < CCD_CAP_COUNT; i++)
			raw_set_cap(i, CCD_CAP_STATE_ALWAYS);

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

/*
 * Could be invoked synchronously on the TPM task context, or asynchronously,
 * after physical presence is established, on the hooks task context.
 *
 * The appropriate TPM reset entry point needs to be invoked. Also, make sure
 * that the board is always rebooted when TPM is reset.
 *
 * @param sync   Non-zero to invoke synchronously.
 */
static void ccd_open_done(int sync)
{
	int rv;

	if (!ccd_is_cap_enabled(CCD_CAP_OPEN_WITHOUT_TPM_WIPE)) {
		/* Can't open unless wipe succeeds */
		if (sync)
			rv = tpm_sync_reset(1);
		else
			rv = board_wipe_tpm();

		if (rv != EC_SUCCESS) {
			CPRINTS("CCD open TPM wipe failed");
			return;
		}
	}

	if (!ccd_is_cap_enabled(CCD_CAP_UNLOCK_WITHOUT_AP_REBOOT) ||
	    (!ccd_is_cap_enabled(CCD_CAP_OPEN_WITHOUT_TPM_WIPE) && sync))
		board_reboot_ap();

	CPRINTS("CCD opened");
	ccd_set_state(CCD_STATE_OPENED);
}

static void ccd_open_done_async(void)
{
	ccd_open_done(0);
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

	/* Use raw_set_flag() because the test lab flag is internal */
	mutex_lock(&ccd_config_mutex);
	raw_set_flag(CCD_FLAG_TEST_LAB, v);
	mutex_unlock(&ccd_config_mutex);

	if (ccd_save_config() == EC_SUCCESS)
		CPRINTS("CCD test lab mode %sbled", v ? "ena" : "disa");
	else
		CPRINTS("Error setting CCD test lab mode!");
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

static int command_ccd_info(void)
{
	int i;

	ccprintf("State: %s%s\n", ccd_state_names[ccd_state],
		 force_disabled ? " (Disabled)" : "");
	ccprintf("Password: %s\n", raw_has_password() ? "set" : "none");
	ccprintf("Flags: 0x%06x\n", raw_get_flags());

	ccprintf("Capabilities: %.8h\n", config.capabilities);
	for (i = 0; i < CCD_CAP_COUNT; i++) {
		int c = raw_get_cap(i, 0);

		ccprintf("  %-15s %c %d=%s",
			 cap_info[i].name,
			 ccd_is_cap_enabled(i) ? 'Y' : '-',
			 c, ccd_cap_state_names[c]);
		if (c == CCD_CAP_STATE_DEFAULT)
			ccprintf(" (%s)",
				ccd_cap_state_names[cap_info[i].default_state]);
		ccprintf("\n");
		cflush();
	}

	ccprintf("TPM:%s%s\n",
		 board_fwmp_allows_unlock() ? "" : " fwmp_lock",
		 board_vboot_dev_mode_enabled() ? " dev_mode" : "");

	ccputs("Use 'ccd help' to print subcommands\n");
	return EC_SUCCESS;
}

static int command_ccd_reset(int argc, char **argv)
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

static int command_ccd_set(int argc, char **argv)
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

static int do_ccd_password(char *password)
{
	/* Only works if unlocked or opened */
	if (ccd_state == CCD_STATE_LOCKED)
		return EC_ERROR_ACCESS_DENIED;

	if (raw_has_password()) {
		const char clear_prefix[] = {'c', 'l', 'e', 'a', 'r', ':'};

		/*
		 * The only allowed action at this point is to clear the
		 * password. To do it the user is supposed to enter
		 * 'clear:<passwd>'
		 */
		if (strncasecmp(password, clear_prefix, sizeof(clear_prefix)))
			return EC_ERROR_ACCESS_DENIED;

		if (raw_check_password(password + sizeof(clear_prefix)) !=
		    EC_SUCCESS)
			return EC_ERROR_ACCESS_DENIED;

		return ccd_reset_password();
	}

	/* Set new password */
	return ccd_set_password(password);
}

/*
 * Common wrapper for CCD commands which are passed through the TPM task
 * context.
 *
 * All commands could have a single parameter, which is the password (to be
 * set, cleared, or entered to open/unlock). If argc calue exceeds 1, the
 * pointer to password is set, it is checked not to exceed maximum size.
 *
 * If the check succeeds, prepare a message containing a TPM vendor command,
 * have the TPM task process the message and report the result to the caller.
 *
 * Message header is always the same, the payload is the password, if
 * supplied.
 */
static int ccd_command_wrapper(int argc, char *password,
			       enum ccd_vendor_subcommands subcmd)
{
	int rv;
	struct ccd_vendor_cmd_header *vch;
	size_t command_size;
	size_t password_size;
	uint32_t return_code;

	if (argc > 1) {
		password_size = strlen(password);

		if (password_size  > CCD_MAX_PASSWORD_SIZE)
			return EC_ERROR_PARAM1;
	} else {
		password_size = 0;
	}

	command_size = sizeof(*vch) + password_size;
	rv = shared_mem_acquire(command_size, (char **)&vch);
	if (rv != EC_SUCCESS)
		return rv;

	/* Build the extension command to set/clear CCD password. */
	vch->tpm_header.tag = htobe16(0x8001); /* TPM_ST_NO_SESSIONS */
	vch->tpm_header.size = htobe32(command_size);
	vch->tpm_header.command_code = htobe32(TPM_CC_VENDOR_BIT_MASK);
	vch->tpm_header.subcommand_code = htobe16(VENDOR_CC_CCD);
	vch->ccd_subcommand = subcmd;

	memcpy(vch + 1, password, password_size);
	tpm_alt_extension(&vch->tpm_header, command_size);

	/*
	 * Return status in the command code field now, in case of error,
	 * error code is the first byte after the header.
	 */
	return_code = be32toh(vch->tpm_header.command_code);
	if ((return_code != EC_SUCCESS) &&
	    (return_code != VENDOR_RC_IN_PROGRESS)) {
		rv = vch->ccd_subcommand;
	} else {
		rv = EC_SUCCESS;
	}

	shared_mem_release(vch);
	return rv;
}

static enum vendor_cmd_rc ccd_open(void *buf,
				   size_t input_size,
				   size_t *response_size)
{
	int is_long = 1;
	int need_pp = 1;
	int rv;
	char *buffer = buf;

	if (force_disabled) {
		*response_size = 1;
		buffer[0] = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
	}

	if (ccd_state == CCD_STATE_OPENED)
		return VENDOR_RC_SUCCESS;

	if (raw_has_password()) {
		if (!input_size) {
			*response_size = 1;
			buffer[0] = EC_ERROR_PARAM_COUNT;
			return VENDOR_RC_PASSWORD_REQUIRED;
		}

		/*
		 * We know there is plenty of room in the TPM buffer this is
		 * stored in.
		 */
		buffer[input_size] = '\0';
		rv = raw_check_password(buffer);
		if (rv) {
			*response_size = 1;
			buffer[0] = rv;
			return VENDOR_RC_INTERNAL_ERROR;
		}
	} else if (!board_fwmp_allows_unlock()) {
		*response_size = 1;
		buffer[0] = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
	}

	/* Fail and abort if already checking physical presence */
	if (physical_detect_busy()) {
		physical_detect_abort();
		*response_size = 1;
		buffer[0] = EC_ERROR_BUSY;
		return VENDOR_RC_INTERNAL_ERROR;
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
		rv = physical_detect_start(is_long, ccd_open_done_async);
		if (rv != EC_SUCCESS) {
			*response_size = 1;
			buffer[0] = rv;
			return VENDOR_RC_INTERNAL_ERROR;
		}
		return VENDOR_RC_IN_PROGRESS;
	}

	/* No physical presence required; go straight to done */
	ccd_open_done(1);

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_unlock(void *buf,
				     size_t input_size,
				     size_t *response_size)
{
	int need_pp = 1;
	int rv;
	char *buffer = buf;

	if (force_disabled) {
		*response_size = 1;
		buffer[0] = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
	}

	if (ccd_state == CCD_STATE_UNLOCKED)
		return VENDOR_RC_SUCCESS;

	/* Can go from opened to unlocked with no delay or password */
	if (ccd_state == CCD_STATE_OPENED) {
		ccd_unlock_done();
		return VENDOR_RC_SUCCESS;
	}

	if (raw_has_password()) {
		if (!input_size) {
			*response_size = 1;
			buffer[0] = EC_ERROR_PARAM_COUNT;
			return VENDOR_RC_PASSWORD_REQUIRED;
		}

		/*
		 * We know there is plenty of room in the TPM buffer this is
		 * stored in.
		 */
		buffer[input_size] = '\0';
		rv = raw_check_password(buffer);
		if (rv) {
			*response_size = 1;
			buffer[0] = rv;
			return VENDOR_RC_INTERNAL_ERROR;
		}
	} else if (!board_fwmp_allows_unlock()) {
		*response_size = 1;
		buffer[0] = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
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
		*response_size = 1;
		buffer[0] = EC_ERROR_BUSY;
		return VENDOR_RC_INTERNAL_ERROR;
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
		rv = physical_detect_start(0, ccd_unlock_done);
		if (rv != EC_SUCCESS) {
			*response_size = 1;
			buffer[0] = rv;
			return VENDOR_RC_INTERNAL_ERROR;
		}
		return VENDOR_RC_IN_PROGRESS;
	}

	/* Unlock immediately */
	ccd_unlock_done();

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_lock(void *unused0,
				   size_t unused1,
				   size_t *response_size)
{
	/* Lock always works */
	ccprintf("CCD locked.\n");
	ccd_set_state(CCD_STATE_LOCKED);
	return VENDOR_RC_SUCCESS;
}



/* NOTE: Testlab command is console-only; no TPM vendor command for this */
static int command_ccd_testlab(int argc, char **argv)
{
	int newflag = 0;

	if (force_disabled)
		return EC_ERROR_ACCESS_DENIED;

	if (argc < 2) {
		ccprintf("CCD test lab mode %sbled\n",
			ccd_get_flag(CCD_FLAG_TEST_LAB) ? "ena" : "disa");
		return EC_SUCCESS;
	}

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

#ifdef CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
/**
 * Test command to forcibly reset CCD config
 */
static int command_ccd_oops(void)
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
#endif  /* CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE */

#ifdef CONFIG_CMD_CCD_DISABLE
static int command_ccd_disable(void)
{
	ccd_disable();
	return EC_SUCCESS;
}
#endif  /* CONFIG_CMD_CCD_DISABLE */

static int command_ccd_help(void)
{
	int i;

	ccputs("usage: ccd [cmd [args]]\n\n"
	       "get (or just 'ccd')\n"
	       "\tPrint current config\n\n"
	       "lock\n"
	       "unlock [password]\n"
	       "open [password]\n"
	       "\tSet CCD state\n\n"
	       "set <capability> [");
	cflush();

	for (i = 0; i < CCD_CAP_STATE_COUNT; i++)
		ccprintf("%s%s", i ? " | " : "", ccd_cap_state_names[i]);
	ccputs("]\n"
	       "\tSet capability to state\n\n"
	       "password [<new password> | clear]\n"
	       "\tSet or clear CCD password\n\n"
	       "reset [rma]\n"
	       "\tReset CCD config\n\n"
	       "testlab [enable | disable | open]\n"
	       "\tToggle testlab mode or force CCD open\n\n");
	cflush();

#ifdef CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
	ccputs("oops\n"
	       "\tForce-reset CCD config\n\n");
#endif
#ifdef CONFIG_CMD_CCD_DISABLE
	ccputs("disable\n"
	       "\tTemporarily disable CCD\n\n");
#endif

	return EC_SUCCESS;
}

/**
 * Case closed debugging config command.
 */
static int command_ccd_body(int argc, char **argv)
{
	/* If no args or 'get', print info */
	if (argc < 2 || !strcasecmp(argv[1], "get"))
		return command_ccd_info();

	/* Check test lab command first */
	if (!strcasecmp(argv[1], "testlab"))
		return command_ccd_testlab(argc - 1, argv + 1);

	/* Commands to set state */
	if (!strcasecmp(argv[1], "lock"))
		return ccd_command_wrapper(0, NULL, CCDV_LOCK);
	if (!strcasecmp(argv[1], "unlock")) {
		if (!raw_has_password()) {
			ccprintf("Unlock only allowed after password is set\n");
			return EC_ERROR_ACCESS_DENIED;
		}
		return ccd_command_wrapper(argc - 1, argv[2], CCDV_UNLOCK);
	}
	if (!strcasecmp(argv[1], "open"))
		return ccd_command_wrapper(argc - 1, argv[2], CCDV_OPEN);

	/* Commands to configure capabilities */
	if (!strcasecmp(argv[1], "set"))
		return command_ccd_set(argc - 1, argv + 1);
	if (!strcasecmp(argv[1], "password")) {
		if (argc != 3)
			return EC_ERROR_PARAM_COUNT;
		return ccd_command_wrapper(argc - 1, argv[2], CCDV_PASSWORD);
	}
	if (!strcasecmp(argv[1], "reset"))
		return command_ccd_reset(argc - 1, argv + 1);

	/* Optional commands */
#ifdef CONFIG_CASE_CLOSED_DEBUG_V1_UNSAFE
	if (!strcasecmp(argv[1], "oops"))
		return command_ccd_oops();
#endif
#ifdef CONFIG_CMD_CCD_DISABLE
	if (!strcasecmp(argv[1], "disable"))
		return command_ccd_disable();
#endif

	/* Anything else (including "help") prints help */
	return command_ccd_help();
}

static int command_ccd(int argc, char **argv)
{
	int rv;

	ccd_console_active = 1;
	rv = command_ccd_body(argc, argv);
	ccd_console_active = 0;

	return rv;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccd, command_ccd,
			     "[help | ...]",
			     "Configure case-closed debugging");

/*
 * Password handling on Cr50 passes the following states:
 *
 * - password setting is not allowed after Cr50 reset until an upstart (as
 *   opposed to resume) TPM startup happens, as signalled by the TPM callback.
 *   After the proper TPM reset the state changes to 'POST_RESET_STATE' which
 *   means that the device was just reset/rebooted (not resumed) and no user
 *   logged in yet.
 *
 *  - if the owner logs in in this state, the state changes to
 *    'PASSWORD_ALLOWED_STATE'. The owner can open crosh session and set the
 *    password.
 *
 *   - when the owner logs out or any user but the owner logs in, the state
 *     changes to PASSWORD_NOT_ALLOWED_STATE and does not change until TPM is
 *     reset. This makes sure that password can be set only by the owner and
 *     only before anybody else logged in.
 */
enum password_reset_phase {
	POST_RESET_STATE,
	PASSWORD_ALLOWED_STATE,
	PASSWORD_NOT_ALLOWED_STATE
};

static uint8_t password_state = PASSWORD_NOT_ALLOWED_STATE;

void ccd_tpm_reset_callback(void)
{
	CPRINTS("%s: TPM Startup processed", __func__);
	password_state = POST_RESET_STATE;
}

/*
 * Handle the VENDOR_CC_MANAGE_CCD_PASSWORD command.
 *
 * The payload of the command is a single byte Boolean which sets the controls
 * if CCD password can be set or not.
 *
 * After reset the pasword can not be set using VENDOR_CC_CCD_PASSWORD; once
 * this command is received with value of True, the phase stars when the
 * password can be set. As soon as this command is received with a value of
 * False, the password can not be set any more until device is rebooted, even
 * if this command is re-sent with the value of True.
 */
static enum vendor_cmd_rc manage_ccd_password(enum vendor_cmd_cc code,
					      void *buf,
					      size_t input_size,
					      size_t *response_size)
{
	uint8_t prev_state = password_state;
	/* The vendor command status code. */
	enum vendor_cmd_rc rv = VENDOR_RC_SUCCESS;
	/* Actual error code. */
	uint8_t error_code = EC_SUCCESS;

	do {
		int value;

		if (input_size != 1) {
			rv = VENDOR_RC_INTERNAL_ERROR;
			error_code = EC_ERROR_PARAM1;
			break;
		}

		value = *((uint8_t *)buf);

		if (!value) {
			/* No more password setting allowed. */
			password_state = PASSWORD_NOT_ALLOWED_STATE;
			break;
		}

		if (password_state == POST_RESET_STATE) {
			/* The only way to allow password setting. */
			password_state = PASSWORD_ALLOWED_STATE;
			break;
		}

		password_state = PASSWORD_NOT_ALLOWED_STATE;
		rv = VENDOR_RC_BOGUS_ARGS;
		error_code = EC_ERROR_INVAL;
	} while (0);

	if (prev_state != password_state)
		CPRINTF("%s: state change from %d to %d\n",
			__func__, prev_state, password_state);

	if (rv == VENDOR_RC_SUCCESS) {
		*response_size = 0;
	} else {
		*response_size = 1;
		((uint8_t *)buf)[0] = error_code;
	}

	return rv;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_MANAGE_CCD_PWD, manage_ccd_password);

/*
 * Handle the CCVD_PASSWORD subcommand.
 *
 * The payload of the command is a text string to use to set or clear the
 * password.
 */
static enum vendor_cmd_rc ccd_password(void *buf,
				       size_t input_size,
				       size_t *response_size)
{
	int rv = EC_SUCCESS;
	char password[CCD_MAX_PASSWORD_SIZE + 1];
	char *response = buf;

	if (password_state != PASSWORD_ALLOWED_STATE) {
		*response_size = 1;
		*response = EC_ERROR_ACCESS_DENIED;
		return VENDOR_RC_NOT_ALLOWED;
	}

	if (!input_size || (input_size >= sizeof(password))) {
		rv = EC_ERROR_PARAM1;
	} else {
		memcpy(password, buf, input_size);
		password[input_size] = '\0';
		rv = do_ccd_password(password);
		always_memset(password, 0, input_size);
	}

	if (rv != EC_SUCCESS) {
		*response = rv;
		*response_size = 1;
		return VENDOR_RC_INTERNAL_ERROR;
	}

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_pp_poll(void *buf,
				      size_t input_size,
				      size_t *response_size)
{
	char *buffer = buf;

	if ((ccd_state == CCD_STATE_OPENED) ||
	    (ccd_state == CCD_STATE_UNLOCKED)) {
		buffer[0] = CCD_PP_DONE;
	} else {
		switch (physical_presense_fsm_state()) {
		case PP_AWAITING_PRESS:
			buffer[0] = CCD_PP_AWAITING_PRESS;
			break;
		case PP_BETWEEN_PRESSES:
			buffer[0] = CCD_PP_BETWEEN_PRESSES;
			break;
		default:
			buffer[0] = CCD_PP_CLOSED;
			break;
		}
	}
	*response_size = 1;
	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_pp_poll_unlock(void *buf,
					     size_t input_size,
					     size_t *response_size)
{
	char *buffer;

	if ((ccd_state != CCD_STATE_OPENED) &&
	    (ccd_state != CCD_STATE_UNLOCKED))
		return ccd_pp_poll(buf, input_size, response_size);


	buffer = buf;
	*response_size = 1;
	buffer[0] = CCD_PP_DONE;

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_pp_poll_open(void *buf,
					   size_t input_size,
					   size_t *response_size)
{
	char *buffer;

	if (ccd_state != CCD_STATE_OPENED)
		return ccd_pp_poll(buf, input_size, response_size);


	buffer = buf;
	*response_size = 1;
	buffer[0] = CCD_PP_DONE;

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc ccd_get_info(void *buf,
				       size_t input_size,
				       size_t *response_size)
{
	int i;
	struct ccd_info_response response = {};

	for (i = 0; i < CCD_CAP_COUNT; i++) {
		int index;
		int shift;

		/* Each capability takes 2 bits. */
		index = i / (32/2);
		shift = (i % (32/2)) * 2;
		response.ccd_caps_current[index] |= raw_get_cap(i, 1) << shift;
		response.ccd_caps_defaults[index] |=
			cap_info[i].default_state << shift;
	}

	response.ccd_flags = htobe32(raw_get_flags());
	response.ccd_state = ccd_get_state();
	response.ccd_has_password = raw_has_password();
	response.ccd_force_disabled = force_disabled;
	for (i = 0; i < ARRAY_SIZE(response.ccd_caps_current); i++) {
		response.ccd_caps_current[i] =
			htobe32(response.ccd_caps_current[i]);
		response.ccd_caps_defaults[i] =
			htobe32(response.ccd_caps_defaults[i]);
	}

	*response_size = sizeof(response);
	memcpy(buf, &response, sizeof(response));

	return VENDOR_RC_SUCCESS;
}

/*
 * Common TPM Vendor command handler used to demultiplex various CCD commands
 * which need to be available both throuh CLI and over /dev/tpm0.
 */
static enum vendor_cmd_rc ccd_vendor(enum vendor_cmd_cc code,
				     void *buf,
				     size_t input_size,
				     size_t *response_size)
{
	enum vendor_cmd_rc (*handler)(void *x, size_t y, size_t *t) = NULL;
	char *buffer;
	enum vendor_cmd_rc rc;

	/*
	 * buf points to the next byte after tpm header, i.e. to the CCD
	 * subcommand. Cache the pointer to make it easier to access and
	 * manipulate.
	 */
	buffer = buf;

	/* Pick what to do based on subcommand. */
	switch (buffer[0]) {
	case CCDV_PASSWORD:
		handler = ccd_password;
		break;

	case CCDV_OPEN:
		handler = ccd_open;
		break;

	case CCDV_UNLOCK:
		handler = ccd_unlock;
		break;

	case CCDV_LOCK:
		handler = ccd_lock;
		break;

	case CCDV_PP_POLL_UNLOCK:
		handler = ccd_pp_poll_unlock;
		break;

	case CCDV_PP_POLL_OPEN:
		handler = ccd_pp_poll_open;
		break;

	case CCDV_GET_INFO:
		handler = ccd_get_info;
		break;

	default:
		CPRINTS("%s:%d - unknown subcommand\n", __func__, __LINE__);
		break;
	}

	if (handler) {
		/*
		 * Let's be optimistic: 0 usually means success.
		 *
		 * We know the buffer is large enough to accommodate any CCD
		 * subcommand response, so there is no size checks in the
		 * processing functions.
		 */
		*response_size = 0;

		rc = handler(buf + 1, input_size - 1, response_size);

		/*
		 * Move response up for the master to see it in the right
		 * place in the response buffer.
		 */
		memmove(buf, buf + 1, *response_size);
	} else {
		rc = VENDOR_RC_NO_SUCH_SUBCOMMAND;
		*response_size = 0;
	}

	return rc;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_CCD, ccd_vendor);

static enum vendor_cmd_rc ccd_disable_rma(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	int rv = EC_SUCCESS;
	int error_line;

	do {
		if (raw_has_password()) {
			error_line = __LINE__;
			rv = EC_ERROR_ACCESS_DENIED;
			break;
		}

		/* Check if physical presence is required to unlock. */
		if (!ccd_is_cap_enabled(CCD_CAP_REMOVE_BATTERY_BYPASSES_PP) ||
		    board_battery_is_present()) {
			const uint8_t required_capabilities[] = {
				CCD_CAP_OPEN_WITHOUT_TPM_WIPE,
				CCD_CAP_UNLOCK_WITHOUT_AP_REBOOT,
				CCD_CAP_OPEN_WITHOUT_LONG_PP,
				CCD_CAP_UNLOCK_WITHOUT_SHORT_PP
			};
			unsigned int i;

			for (i = 0;
			     i < ARRAY_SIZE(required_capabilities);
			     i++) {
				if (!ccd_is_cap_enabled
				    (required_capabilities[i]))
					break;
			}

			if (i < ARRAY_SIZE(required_capabilities)) {
				CPRINTF("Capability %d is not present\n",
					required_capabilities[i]);
				error_line = __LINE__;
				rv = EC_ERROR_ACCESS_DENIED;
				break;
			}
		}

		ccd_set_state(CCD_STATE_OPENED);

		rv = command_ccd_reset(0, NULL);
		if (rv != EC_SUCCESS) {
			error_line = __LINE__;
			break;
		}


		ccd_lock(NULL, 0, NULL);

		/*
		 * We do it here to make sure that the device comes out of RMA
		 * with WP enabled, but in general CCD reset needs to enforce
		 * WP state.
		 *
		 * TODO(rspangler): sort out CCD state and WP correlation,
		 * b/73075443.
		 */
		set_wp_follow_ccd_config();

		*response_size = 0;
		return VENDOR_RC_SUCCESS;
	} while (0);

	CPRINTF("%s: error in line %d\n", __func__, error_line);

	((uint8_t *)buf)[0] = (uint8_t)rv;
	*response_size = 1;
	return VENDOR_RC_INTERNAL_ERROR;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_DISABLE_RMA, ccd_disable_rma);
