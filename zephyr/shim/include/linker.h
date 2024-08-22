/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LINKER_H
#define __CROS_EC_LINKER_H

#ifdef CONFIG_FAKE_SHMEM
/* Define __shared_mem_buf for the fake shared memory which is used with
 * tests.
 */
#define __shared_mem_buf fake_shmem_buf
#else /* CONFIG_FAKE_SHMEM */
/* Put the start of shared memory after all allocated RAM symbols */
#define __shared_mem_buf _image_ram_end
#endif /* CONFIG_FAKE_SHMEM */

#endif
