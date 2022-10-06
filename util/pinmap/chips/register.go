// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package chips

import (
	"pinmap/pm"
)

// init registers the chips.
func init() {
	pm.RegisterChip(&It81302{})
	pm.RegisterChip(&Npcx993{})
}
