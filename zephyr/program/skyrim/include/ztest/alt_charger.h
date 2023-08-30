/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __SKYRIM_TEST_ALT_CHARGER
#define __SKYRIM_TEST_ALT_CHARGER

#ifdef CONFIG_ZTEST
#undef CHG_ENABLE_ALTERNATE
void chg_enable_alternate_test(int port);
#define CHG_ENABLE_ALTERNATE(x) chg_enable_alternate_test(x)
#endif /* CONFIG_ZTEST */

#endif /* __SKYRIM_TEST_ALT_CHARGER */
