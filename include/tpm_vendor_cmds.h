/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __INCLUDE_TPM_VENDOR_CMDS_H
#define __INCLUDE_TPM_VENDOR_CMDS_H

#include "common.h"  /* For __packed. */
#include "compile_time_macros.h"  /* For BIT. */

/*
 * This file includes definitions of extended/vendor TPM2 commands and their
 * return codes. The definitions are shared between the embedded code and the
 * gsctool utility running on the host.
 */

/* Extension and vendor commands. */
enum vendor_cmd_cc {
	/* Original extension commands */
	EXTENSION_AES = 0,
	EXTENSION_HASH = 1,
	EXTENSION_RSA = 2,
	EXTENSION_ECC = 3,
	EXTENSION_FW_UPGRADE = 4,
	EXTENSION_HKDF = 5,
	EXTENSION_ECIES = 6,
	EXTENSION_POST_RESET = 7,

	LAST_EXTENSION_COMMAND = 15,

	/* Our TPMv2 vendor-specific command codes. 16 bits available. */
	VENDOR_CC_GET_LOCK = 16,
	VENDOR_CC_SET_LOCK = 17,
	VENDOR_CC_SYSINFO = 18,
	/*
	 * VENDOR_CC_IMMEDIATE_RESET may have an argument, which is a (uint16_t)
	 * time delay (in milliseconds) in doing a reset. Max value is 1000.
	 * The command may also be called without an argument, which will be
	 * regarded as zero time delay.
	 */
	VENDOR_CC_IMMEDIATE_RESET = 19,
	VENDOR_CC_INVALIDATE_INACTIVE_RW = 20,
	VENDOR_CC_COMMIT_NVMEM = 21,
	/* DEPRECATED(22): deep sleep control command. */
	VENDOR_CC_REPORT_TPM_STATE = 23,
	VENDOR_CC_TURN_UPDATE_ON = 24,
	VENDOR_CC_GET_BOARD_ID = 25,
	VENDOR_CC_SET_BOARD_ID = 26,
	VENDOR_CC_U2F_APDU = 27,
	VENDOR_CC_POP_LOG_ENTRY = 28,
	VENDOR_CC_GET_REC_BTN = 29,
	VENDOR_CC_RMA_CHALLENGE_RESPONSE = 30,
	/* DEPRECATED(31): CCD password command (now part of VENDOR_CC_CCD) */
	/*
	 * Disable factory mode. Reset all ccd capabilities to default and reset
	 * write protect to follow battery presence.
	 */
	VENDOR_CC_DISABLE_FACTORY = 32,
	/* DEPRECATED(33): Manage CCD password phase */
	VENDOR_CC_CCD = 34,
	VENDOR_CC_GET_ALERTS_DATA = 35,
	VENDOR_CC_SPI_HASH = 36,
	VENDOR_CC_PINWEAVER = 37,
	/*
	 * Check the factory reset settings. If they're all set correctly, do a
	 * factory reset to enable ccd factory mode. All capabilities will be
	 * set to Always and write protect will be permanently disabled. This
	 * mode can't be reset unless VENDOR_CC_DISABLE_FACTORY is called or
	 * the 'ccd reset' console command is run.
	 */
	VENDOR_CC_RESET_FACTORY = 38,
	/*
	 * Get the write protect setting. This will return a single byte with
	 * bits communicating the write protect setting as described by the
	 * WPV subcommands.
	 */
	VENDOR_CC_WP = 39,
	/*
	 * Either enable or disable TPM mode. This is allowed for one-time only
	 * until next TPM reset EVENT. In other words, once TPM mode is set,
	 * then it cannot be altered to the other mode value. The allowed input
	 * values are either TPM_MODE_ENABLED or TPM_MODE_DISABLED as defined
	 * in 'enum tpm_modes', tpm_registers.h.
	 * If the input size is zero, it won't change TPM_MODE.
	 * If either the input size is zero or the input value is valid,
	 * it will respond with the current tpm_mode value in uint8_t format.
	 *
	 *  Return code:
	 *   VENDOR_RC_SUCCESS: completed successfully.
	 *   VENDOR_RC_INTERNAL_ERROR: failed for an internal reason.
	 *   VENDOR_RC_NOT_ALLOWED: failed in changing TPM_MODE,
	 *                          since it is already set.
	 *   VENDOR_RC_NO_SUCH_SUBCOMMAND: failed because the given input
	 *                                 is undefined.
	 */
	VENDOR_CC_TPM_MODE = 40,
	/*
	 * Initializes INFO1 SN data space, and sets SN hash. Takes three
	 * int32 as parameters, which are written as the SN hash.
	 */
	VENDOR_CC_SN_SET_HASH = 41,
	/*
	 * Increments the RMA count in the INFO1 SN data space. The space must
	 * have been previously initialized with the _SET_HASH command above for
	 * this to succeed. Takes one byte as parameter, which indicates the
	 * number to increment the RMA count by; this is typically 1 or 0.
	 *
	 * Incrementing the RMA count by 0 will set the RMA indicator, but not
	 * incremement the count. This is useful to mark that a device has been
	 * RMA'd, but that we were not able to log the new serial number.
	 *
	 * Incrementing the count by the maximum RMA count (currently 7) will
	 * always set the RMA count to the maximum value, regardless of the
	 * previous value. This can be used with any device, regardless of
	 * current state, to mark it as RMA'd but with an unknown RMA count.
	 */
	VENDOR_CC_SN_INC_RMA = 42,

	/*
	 * Gets the latched state of a power button press to indicate user
	 * recent user presence. The power button state is automatically cleared
	 * after PRESENCE_TIMEOUT.
	 */
	VENDOR_CC_GET_PWR_BTN = 43,

	/*
	 * U2F commands.
	 */
	VENDOR_CC_U2F_GENERATE = 44,
	VENDOR_CC_U2F_SIGN = 45,
	VENDOR_CC_U2F_ATTEST = 46,

	VENDOR_CC_FLOG_TIMESTAMP = 47,
	VENDOR_CC_ENDORSEMENT_SEED = 48,

	VENDOR_CC_U2F_MODE = 49,

	LAST_VENDOR_COMMAND = 65535,
};

/*
 * Error codes reported by extension and vendor commands.
 *
 * As defined by the TPM2 spec, the TPM response code is all zero for success,
 * and errors are a little complicated:
 *
 *   Bits 31:12 must be zero.
 *
 *   Bit 11     S=0   Error
 *   Bit 10     T=1   Vendor defined response code
 *   Bit  9     r=0   reserved
 *   Bit  8     V=1   Conforms to TPMv2 spec
 *   Bit  7     F=0   Confirms to Table 14, Format-Zero Response Codes
 *   Bits 6:0   num   128 possible failure reasons
 */

enum vendor_cmd_rc {
	/* EXTENSION_HASH error codes */
	/* Attempt to start a session on an active handle. */
	EXC_HASH_DUPLICATED_HANDLE = 1,
	EXC_HASH_TOO_MANY_HANDLES = 2,  /* No room to allocate a new context. */
	/* Continuation/finish on unknown context. */
	EXC_HASH_UNKNOWN_CONTEXT = 3,

	/* Our TPMv2 vendor-specific response codes. */
	VENDOR_RC_SUCCESS = 0,
	VENDOR_RC_BOGUS_ARGS = 1,
	VENDOR_RC_READ_FLASH_FAIL = 2,
	VENDOR_RC_WRITE_FLASH_FAIL = 3,
	VENDOR_RC_REQUEST_TOO_BIG = 4,
	VENDOR_RC_RESPONSE_TOO_BIG = 5,
	VENDOR_RC_INTERNAL_ERROR = 6,
	VENDOR_RC_NOT_ALLOWED = 7,
	VENDOR_RC_NO_SUCH_SUBCOMMAND = 8,
	VENDOR_RC_IN_PROGRESS = 9,
	VENDOR_RC_PASSWORD_REQUIRED = 10,

	/* Maximum possible failure reason. */
	VENDOR_RC_NO_SUCH_COMMAND = 127,

	/*
	 * Bits 10 and 8 set, this is to be ORed with the rest of the error
	 * values to make the combined value compliant with the spec
	 * requirements.
	 */
	VENDOR_RC_ERR = 0x500,
};

/*
 * The TPMv2 Spec mandates that vendor-specific command codes have bit 29 set,
 * while bits 15-0 indicate the command. All other bits should be zero.
 * We will define one of those 16-bit command values for Cr50 purposes, and use
 * the subcommand_code in struct tpm_cmd_header to further distinguish the
 * desired operation.
 */
#define TPM_CC_VENDOR_BIT_MASK 0x20000000
#define VENDOR_CC_MASK         0x0000ffff
/* Our vendor-specific command codes go here */
#define TPM_CC_VENDOR_CR50         0x0000

/*** Structures and constants for VENDOR_CC_SPI_HASH ***/

enum vendor_cc_spi_hash_request_subcmd {
	/* Relinquish the bus */
	SPI_HASH_SUBCMD_DISABLE = 0,
	/* Acquire the bus for AP SPI */
	SPI_HASH_SUBCMD_AP = 1,
	/* Acquire the bus for EC SPI */
	SPI_HASH_SUBCMD_EC = 2,
	/* Hash SPI data */
	SPI_HASH_SUBCMD_SHA256 = 4,
	/* Read SPI data */
	SPI_HASH_SUBCMD_DUMP = 5,
	/* Poll spi hash PP state. */
	SPI_HASH_PP_POLL = 6,
};

enum vendor_cc_spi_hash_request_flags {
	/* EC uses gang programmer mode */
	SPI_HASH_FLAG_EC_GANG = BIT(0),
};

/* Structure for VENDOR_CC_SPI_HASH request which follows tpm_header */
struct vendor_cc_spi_hash_request {
	uint8_t subcmd;		/* See vendor_cc_spi_hash_request_subcmd */
	uint8_t flags;		/* See vendor_cc_spi_hash_request_flags */
	/* Offset and size used by SHA256 and DUMP; ignored by other subcmds */
	uint32_t offset;	/* Offset in flash to hash/read */
	uint32_t size;		/* Size in bytes to hash/read */
} __packed;

/* Maximum size of a response = SHA-256 hash or 1-32 bytes of data */
#define SPI_HASH_MAX_RESPONSE_BYTES 32

/*
 * Subcommand code, used to set write protect.
 */
#define WPV_UPDATE		BIT(0)
#define WPV_ENABLE		BIT(1)
#define WPV_FORCE		BIT(2)
#define WPV_ATBOOT_SET		BIT(3)
#define WPV_ATBOOT_ENABLE	BIT(4)

#endif /* __INCLUDE_TPM_VENDOR_CMDS_H */
