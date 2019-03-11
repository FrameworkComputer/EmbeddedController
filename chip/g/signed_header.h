/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_SIGNED_HEADER_H
#define __CROS_EC_SIGNED_HEADER_H

#include "compile_time_macros.h"
#include "stdint.h"

#define FUSE_PADDING 0x55555555  /* baked in hw! */
#define FUSE_IGNORE 0xa3badaac   /* baked in rom! */
#define FUSE_MAX 128             /* baked in rom! */

#define INFO_MAX 128             /* baked in rom! */
#define INFO_IGNORE 0xaa3c55c3   /* baked in rom! */

/* Default value for _pad[] words */
#define SIGNED_HEADER_PADDING 0x33333333

struct SignedHeader {
	uint32_t magic;       /* -1 (thanks, boot_sys!) */
	uint32_t signature[96];
	uint32_t img_chk_;    /* top 32 bit of expected img_hash */
	/* --------------------- everything below is part of img_hash */
	uint32_t tag[7];      /* words 0-6 of RWR/FWR */
	uint32_t keyid;       /* word 7 of RWR */
	uint32_t key[96];     /* public key to verify signature with */
	uint32_t image_size;
	uint32_t ro_base;     /* readonly region */
	uint32_t ro_max;
	uint32_t rx_base;     /* executable region */
	uint32_t rx_max;
	uint32_t fusemap[FUSE_MAX / (8 * sizeof(uint32_t))];
	uint32_t infomap[INFO_MAX / (8 * sizeof(uint32_t))];
	uint32_t epoch_;      /* word 7 of FWR */
	uint32_t major_;      /* keyladder count */
	uint32_t minor_;
	uint64_t timestamp_;  /* time of signing */
	uint32_t p4cl_;
	/* bits to and with FUSE_FW_DEFINED_BROM_APPLYSEC */
	uint32_t applysec_;
	/* bits to mesh with FUSE_FW_DEFINED_BROM_CONFIG1 */
	uint32_t config1_;
	/* bits to or with FUSE_FW_DEFINED_BROM_ERR_RESPONSE */
	uint32_t err_response_;
	/* action to take when expectation is violated */
	uint32_t expect_response_;

	union {
		// 2nd FIPS signature (gnubby RW / Cr51)
		struct {
			uint32_t keyid;
			uint32_t r[8];
			uint32_t s[8];
		} ext_sig;

		// FLASH trim override (Dauntless RO)
		// iff config1_ & 65536
		struct {
			uint32_t FSH_SMW_SETTING_OPTION3;
			uint32_t FSH_SMW_SETTING_OPTION2;
			uint32_t FSH_SMW_SETTING_OPTIONA;
			uint32_t FSH_SMW_SETTING_OPTIONB;
			uint32_t FSH_SMW_SMP_WHV_OPTION1;
			uint32_t FSH_SMW_SMP_WHV_OPTION0;
			uint32_t FSH_SMW_SME_WHV_OPTION1;
			uint32_t FSH_SMW_SME_WHV_OPTION0;
		} fsh;
	} u;

	/* Padding to bring the total structure size to 1K. */
	uint32_t _pad[5];
	struct {
		unsigned size:12;
		unsigned offset:20;
	} swap_mark;

	/* Field for managing updates between RW product families. */
	uint32_t rw_product_family_;
	/* Board ID type, mask, flags (stored ^SIGNED_HEADER_PADDING) */
	uint32_t board_id_type;
	uint32_t board_id_type_mask;
	uint32_t board_id_flags;
	uint32_t dev_id0_;    /* node id, if locked */
	uint32_t dev_id1_;
	uint32_t fuses_chk_;  /* top 32 bit of expected fuses hash */
	uint32_t info_chk_;   /* top 32 bit of expected info hash */
};

BUILD_ASSERT(sizeof(struct SignedHeader) == 1024);
BUILD_ASSERT(offsetof(struct SignedHeader, info_chk_) == 1020);
#define TOP_IMAGE_SIZE_BIT (1 <<			\
	    (sizeof(((struct SignedHeader *)0)->image_size) * 8 - 1))

/*
 * It is a mere convention, but all prod keys are required to have key IDs
 * such, that bit D2 is set, and all dev keys are required to have key IDs
 * such, that bit D2 is not set.
 *
 * This convention is enforced at the key generation time.
 */
#define G_SIGNED_FOR_PROD(h) ((h)->keyid & BIT(2))


#endif /* __CROS_EC_SIGNED_HEADER_H */
