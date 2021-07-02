# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hypothesis

hypothesis.settings.register_profile(
    "cq", suppress_health_check=hypothesis.HealthCheck.all()
)
