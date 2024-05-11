/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RWSIG_H
#define __CROS_EC_RWSIG_H

#include "config.h"
#include "rsa.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __ASSEMBLER__
#ifdef HAS_TASK_RWSIG
/* The functions below only make sense if RWSIG task is defined. */

/* Current status of RW signature verification */
enum rwsig_status {
	RWSIG_UNKNOWN = 0, /* Unknown/not started */
	RWSIG_IN_PROGRESS,
	RWSIG_VALID,
	RWSIG_INVALID,
	RWSIG_ABORTED,
};

/* Returns current rwsig verification status. */
enum rwsig_status rwsig_get_status(void);

/*
 * Aborts current verification, also prevents RWSIG task from automatically
 * jumping to RW.
 * This is used by usb_updater2 when a RW update is required, giving it enough
 * time to actually perform the update.
 */
void rwsig_abort(void);

/*
 * Tells RWSIG task to jump to RW immediately, if the signature is correct.
 * This is used by usb_updater2 when no RW update is required, to speed up
 * boot time.
 */
void rwsig_continue(void);

#else /* !HAS_TASK_RWSIG */
/* These functions can only be called directly if RWSIG task is not defined. */

/* Checks RW signature. Returns a boolean indicating success. */
int rwsig_check_signature(void);

/* Jumps to RW, if signature is fine, returns on error (otherwise, jumps). */
void rwsig_jump_now(void);

#endif

#endif /* !__ASSEMBLER__ */

/*
 * The signer puts the public key and signature into the RO and RW images
 * (respectively) at known locations after the complete image is assembled. But
 * since we compile the RO & RW images separately, the other image's addresses
 * can't be computed by the linker. So we just hardcode the addresses here.
 * These can be overridden in board.h files if desired.
 */

#ifndef CONFIG_RO_PUBKEY_SIZE
#ifdef CONFIG_RWSIG_TYPE_RWSIG
/*
 * rwsig type: 1024 bytes is enough to fit RSA-3072 public key.
 *
 * TODO(crosbug.com/p/62321): This still wastes space. We could pack the key at
 * any arbitrary location, but we need proper signer support to make sure it
 * can overwrite the key correctly.
 */
#define CONFIG_RO_PUBKEY_SIZE 1024
#else
#define CONFIG_RO_PUBKEY_SIZE RSA_PUBLIC_KEY_SIZE
#endif
#endif /* ! CONFIG_RO_PUBKEY_SIZE */
#ifndef CONFIG_RO_PUBKEY_ADDR
#ifdef CONFIG_RWSIG_TYPE_RWSIG
#ifndef CONFIG_RO_PUBKEY_STORAGE_OFF
#define CONFIG_RO_PUBKEY_STORAGE_OFF \
	(CONFIG_RO_STORAGE_OFF + CONFIG_RO_SIZE - CONFIG_RO_PUBKEY_SIZE)
#endif /* CONFIG_RO_PUBKEY_STORAGE_OFF */

/* The pubkey resides at the end of the RO image */
#define CONFIG_RO_PUBKEY_ADDR                                           \
	(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_EC_PROTECTED_STORAGE_OFF + \
	 CONFIG_RO_PUBKEY_STORAGE_OFF)
#else
/*
 * usbpd1 type assumes pubkey location at the end of first half of flash,
 * which might actually be in the PSTATE region.
 */
#define CONFIG_RO_PUBKEY_ADDR                                         \
	(CONFIG_PROGRAM_MEMORY_BASE + (CONFIG_FLASH_SIZE_BYTES / 2) - \
	 CONFIG_RO_PUBKEY_SIZE)
#endif
#endif /* CONFIG_RO_PUBKEY_ADDR */

/* Some chips require reading pubkey from a memory mapped address */
#ifndef CONFIG_RO_PUBKEY_READ_ADDR
#define CONFIG_RO_PUBKEY_READ_ADDR CONFIG_RO_PUBKEY_ADDR
#endif

/* Some chips require reading signature from a memory mapped address */
#ifndef CONFIG_RWSIG_READ_ADDR
#define CONFIG_RWSIG_READ_ADDR CONFIG_RW_SIG_ADDR
#endif

#ifndef CONFIG_RW_SIG_SIZE
#ifdef CONFIG_RWSIG_TYPE_RWSIG
/*
 * rwsig type: futility expects signature to be 1024 bytes from the end of
 * the file.
 */
#define CONFIG_RW_SIG_SIZE 1024
#else
#define CONFIG_RW_SIG_SIZE RSANUMBYTES
#endif
#endif /* ! CONFIG_RW_SIG_SIZE */
/* The signature resides at the end of each RW copy */
#define RW_SIG_OFFSET (CONFIG_RW_SIZE - CONFIG_RW_SIG_SIZE)
#define RW_A_ADDR                                                      \
	(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_EC_WRITABLE_STORAGE_OFF + \
	 CONFIG_RW_STORAGE_OFF)
/* Assume the layout is same as RW_A and it sits right after RW_A */
#define RW_B_ADDR                                                      \
	(CONFIG_PROGRAM_MEMORY_BASE + CONFIG_EC_WRITABLE_STORAGE_OFF + \
	 CONFIG_RW_B_STORAGE_OFF)
#ifndef CONFIG_RW_SIG_ADDR
#define CONFIG_RW_SIG_ADDR (RW_A_ADDR + RW_SIG_OFFSET)
#endif
#ifndef CONFIG_RW_B_SIG_ADDR
#define CONFIG_RW_B_SIG_ADDR (RW_B_ADDR + RW_SIG_OFFSET)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_RWSIG_H */
