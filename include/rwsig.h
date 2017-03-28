/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RWSIG_H
#define __CROS_EC_RWSIG_H

#include "config.h"
#include "rsa.h"

#ifndef __ASSEMBLER__

/* Checks RW signature. */
void check_rw_signature(void);

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
/* The pubkey goes at the end of the RO region */
#define CONFIG_RO_PUBKEY_ADDR (CONFIG_PROGRAM_MEMORY_BASE	\
			       + CONFIG_RO_MEM_OFF		\
			       + CONFIG_RO_SIZE			\
			       - CONFIG_RO_PUBKEY_SIZE)
#else
/*
 * usbpd1 type assumes pubkey location at the end of first half of flash,
 * which might actually be in the PSTATE region.
 */
#define CONFIG_RO_PUBKEY_ADDR (CONFIG_PROGRAM_MEMORY_BASE	\
			       + (CONFIG_FLASH_SIZE / 2)	\
			       - CONFIG_RO_PUBKEY_SIZE)
#endif
#endif /* CONFIG_RO_PUBKEY_ADDR */

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
#ifndef CONFIG_RW_SIG_ADDR
/* The signature goes at the end of the RW region */
#define CONFIG_RW_SIG_ADDR (CONFIG_PROGRAM_MEMORY_BASE	\
			    + CONFIG_RW_MEM_OFF		\
			    + CONFIG_RW_SIZE		\
			    - CONFIG_RW_SIG_SIZE)
#endif /* !CONFIG_RW_SIG_ADDR */

#endif /* __CROS_EC_RWSIG_H */
