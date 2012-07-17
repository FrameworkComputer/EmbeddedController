/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Build the full image with up to 3 copies (Read only, A, B) of the program
 */

#include "board.h"
#include "config.h"

#define FW_FILE(builddir,proj,sect) builddir##/##proj##.##sect##.flat
#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)
#define FW_IMAGE(sect) STRINGIFY(FW_FILE(OUTDIR,PROJECT,sect))

/* Read Only firmware */
.section .image.RO, "ax"
.incbin FW_IMAGE(RO)

/* Read Write firmware copy A */
.section .image.RW, "ax"
.incbin FW_IMAGE(RW)

#ifdef CONFIG_RW_B
/* Read Write firmware copy B */
.section .image.RW_B, "ax"
.incbin FW_IMAGE(RW_B)
#endif
