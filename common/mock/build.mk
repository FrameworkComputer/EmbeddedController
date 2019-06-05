# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See common/mock/README.md for more information.

mock-$(HAS_MOCK_FPSENSOR) += fpsensor_mock.o
mock-$(HAS_MOCK_ROLLBACK) += rollback_mock.o
