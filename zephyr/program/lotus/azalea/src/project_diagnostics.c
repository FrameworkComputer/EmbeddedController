/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "diagnostics.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

void project_diagnostics(void)
{
	CPRINTS("project_diafnostic");
}
