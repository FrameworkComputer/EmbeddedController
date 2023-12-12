/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_VIDEO_H
#define __CROS_EC_VIDEO_H

#include "common.h"

/*
 * Video decoder supported capability
 */
#define VDEC_CAP_4K_DISABLED BIT(4)
#define VDEC_CAP_MM21 BIT(5)
#define VDEC_CAP_MT21C BIT(6)
#define VDEC_CAP_H264_SLICE BIT(8)
#define VDEC_CAP_VP8_FRAME BIT(9)
#define VDEC_CAP_VP9_FRAME BIT(10)
#define VDEC_CAP_AV1_FRAME BIT(11)
#define VDEC_CAP_HEVC_SLICE BIT(12)
#define VDEC_CAP_IRQ_IN_SCP BIT(16)
#define VDEC_CAP_INNER_RACING BIT(17)
#define VDEC_CAP_IS_SUPPORT_10BIT BIT(18)

/*
 * Video encoder supported capability:
 * BIT(0): enable 4K
 */
#define VENC_CAP_4K BIT(0)

uint32_t video_get_enc_capability(void);
uint32_t video_get_dec_capability(void);

#endif /* #ifndef __CROS_EC_VIDEO_H */
