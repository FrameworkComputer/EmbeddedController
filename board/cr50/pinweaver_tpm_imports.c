/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pinweaver_tpm_imports.h>

#include <Global.h>
#include <util.h>

void get_storage_seed(void *buf, size_t *len)
{
	*len = MIN(*len, sizeof(gp.SPSeed));
	memcpy(buf, &gp.SPSeed, *len);
}
