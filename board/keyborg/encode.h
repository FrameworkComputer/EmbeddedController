/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Touch data encoding/decoding */

#ifndef __KEYBORG_ENCODE_H
#define __KEYBORG_ENCODE_H

/* Start encoding a new frame */
void encode_reset(void);

/* Encode a single column */
void encode_add_column(const uint8_t *dptr);

/* Decode and print the encoded frame to console */
void encode_dump_matrix(void);

#endif  /* __KEYBORG_ENCODE_H */
