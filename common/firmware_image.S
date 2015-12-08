/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Build the full image with up to three program components (one Read only,
 * and one or two Read write).
 */

#include "config.h"

#define FW_FILE(builddir,proj,sect,suffix) \
	builddir##/##sect##/##proj##.##sect##suffix##.flat
#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)
#define FW_IMAGE(sect,suffix) \
	STRINGIFY(FW_FILE(FINAL_OUTDIR,PROJECT,sect,suffix))

/* Read Only firmware */
#ifdef CONFIG_FW_INCLUDE_RO
.section .image.RO, "ax"
.incbin FW_IMAGE(RO,)
#endif

/* Shared objects library */
#ifdef CONFIG_SHAREDLIB
.section .image.libsharedobjs, "ax"
.incbin STRINGIFY(FINAL_OUTDIR/libsharedobjs/libsharedobjs.flat)
#endif

/* Read Write firmware */
.section .image.RW, "ax"
.incbin FW_IMAGE(RW,)

#ifdef CONFIG_RW_B
.section .image.RW_B, "ax"
.incbin FW_IMAGE(RW,_B)
#endif
