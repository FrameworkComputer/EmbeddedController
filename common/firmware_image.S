/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Build the full image with up to three program components (one Read only,
 * and one or two Read write).
 */

#include "config.h"
#include "rollback.h"

#define FW_FILE(builddir,proj,sect,suffix,ext) \
	builddir##/##sect##/##proj##.##sect##suffix##.flat##ext

#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)
#define FW_IMAGE(sect,suffix) \
	STRINGIFY(FW_FILE(FINAL_OUTDIR,PROJECT,sect,suffix,))
#define FW_IMAGE_DRAM(sect,suffix) \
	STRINGIFY(FW_FILE(FINAL_OUTDIR,PROJECT,sect,suffix,.dram))
#define FW_IMAGE_SIGN(sect,suffix) \
	STRINGIFY(FW_FILE(FINAL_OUTDIR,PROJECT,sect,suffix,.sig))

/* Read Only firmware */
#ifdef CONFIG_FW_INCLUDE_RO
.section .image.RO, "ax"
.incbin FW_IMAGE(RO,)
#endif

#ifdef CONFIG_RWSIG_TYPE_RWSIG
.section .image.RO.key, "a"
.incbin STRINGIFY(FINAL_OUTDIR/key.vbpubk2)
#endif

#ifdef CONFIG_ROLLBACK
/* Note: matches struct rollback_data in common/rollback.c. */
.section .image.ROLLBACK, "a"
.long 0
.long CONFIG_ROLLBACK_VERSION
#ifdef CONFIG_ROLLBACK_SECRET_SIZE
.space CONFIG_ROLLBACK_SECRET_SIZE, 0
#endif
.long CROS_EC_ROLLBACK_COOKIE
#endif

/* Shared objects library */
#ifdef CONFIG_SHAREDLIB
.section .image.libsharedobjs, "ax"
.incbin STRINGIFY(FINAL_OUTDIR/libsharedobjs/libsharedobjs.flat)
#endif

/* Read Write firmware */
.section .image.RW, "ax"
.incbin FW_IMAGE(RW,)

#ifdef CONFIG_RWSIG_TYPE_RWSIG
.section .image.RW.sign, "a"
.incbin FW_IMAGE_SIGN(RW,)
#endif

#ifdef CONFIG_DRAM_BASE
/* Read Write firmware in DRAM */
.section .image.RW.dram, "ax"
.incbin FW_IMAGE_DRAM(RW,)
#endif

#ifdef CONFIG_RW_B
#ifdef CONFIG_RWSIG_TYPE_RWSIG
.section .image.RW_B, "ax"
.incbin FW_IMAGE(RW,)
.section .image.RW_B.sign, "a"
.incbin FW_IMAGE_SIGN(RW,)
#else
.section .image.RW_B, "ax"
.incbin FW_IMAGE(RW,_B)
#endif
#endif
