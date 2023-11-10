/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_FINGERPRINT_V4L2_TYPES_H_
#define ZEPHYR_INCLUDE_FINGERPRINT_V4L2_TYPES_H_

/*  Four-character-code */
#define FOURCC(a, b, c, d)                                        \
	((uint32_t)(0xff & (a)) | ((uint32_t)(0xff & (b)) << 8) | \
	 ((uint32_t)(0xff & (c)) << 16) | ((uint32_t)(0xff & (d)) << 24))

/* 8-bit greyscale pixel format as defined by V4L2 headers */
#define V4L2_PIX_FMT_GREY FOURCC('G', 'R', 'E', 'Y')

#endif /* ZEPHYR_INCLUDE_FINGERPRINT_V4L2_TYPES_H_ */
