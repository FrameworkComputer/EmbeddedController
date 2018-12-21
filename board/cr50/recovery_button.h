/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_BOARD_CR50_RECOVERY_BUTTON_H
#define __EC_BOARD_CR50_RECOVERY_BUTTON_H

/**
 * Latch a recovery button sequence.  This state is latched for
 * RECOVERY_BUTTON_TIMEOUT or until the AP requests the recovery button
 * state.
 */
void recovery_button_record(void);

#endif  /* ! __EC_BOARD_CR50_RECOVERY_BUTTON_H */
