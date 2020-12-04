/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LINKER_H
#define __CROS_EC_LINKER_H

/* Put the start of shared memory after all allocated RAM symbols */
#define __shared_mem_buf	_image_ram_end

#endif
