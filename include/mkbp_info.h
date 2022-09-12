/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MKBP info host command for Chrome EC */

#ifndef __CROS_EC_MKBP_INFO_H
#define __CROS_EC_MKBP_INFO_H

/**
 * Board specific function to set support volume buttons.
 *
 * Although we're able to define CONFIG_VOLUME_BUTTONS for ec volume buttons,
 * some boards might need to configure this settings at run time by several
 * cases such as sharing the firmware with different designs.
 *
 * @return 1 if volume buttons supported else 0
 */
__override_proto int mkbp_support_volume_buttons(void);

#endif /* __CROS_EC_MKBP_INFO_H */
