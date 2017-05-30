/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Verify RW image and jump to it
 *
 * Calling this API results in one of the followings:
 * 1. Returns, expecting PD will provide enough power after negotiation
 * 2. Jumps to RW (no return)
 * 3. Returns, requesting more power
 * 4. Returns, requesting recovery
 */
void vboot_ec(void);
