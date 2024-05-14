/* Copyright 2022 Google LLC
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BBRAM_H
#define __CROS_EC_BBRAM_H

#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(named_bbram_regions) == 1,
	     "only one named-bbram-regions compatible node may be present");

#define BBRAM_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(named_bbram_regions)

/*
 * Check if a specific region has been defined under the named-bbram-region
 * node.
 */
#define BBRAM_HAS_REGION(name) (DT_NODE_EXISTS(DT_CHILD(BBRAM_NODE, name)))

/*
 * Get the size of a specific region.
 */
#define BBRAM_REGION_SIZE(name) (DT_PROP(DT_CHILD(BBRAM_NODE, name), size))

/*
 * Get the offset of a specific region.
 */
#define BBRAM_REGION_OFFSET(name) (DT_PROP(DT_CHILD(BBRAM_NODE, name), offset))

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BBRAM_H */
