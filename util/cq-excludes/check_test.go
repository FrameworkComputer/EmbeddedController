// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"github.com/bmatcuk/doublestar"
)

func TestCheckPatterns(t *testing.T) {
	for idx, tc := range []struct {
		Filename    string
		Pattern     string
		ExpectMatch bool
	}{
		{"src/platform/ec/zephyr/app/CMakeLists.txt", "src/platform/ec/zephyr/[^z]*/**", true},
		{"src/platform/ec/zephyr/.pylint", "src/platform/ec/zephyr/[^z]*/**", false},
		{"src/platform/ec/zephyr/zmake/zmake.py", "src/platform/ec/zephyr/[^z]*/**", false},
		{"src/platform/ec/chip/host/config_chip.h", "src/platform/ec/{board,chip}/host/**", true},
	} {
		ok, err := doublestar.Match(tc.Pattern, tc.Filename)
		if err != nil {
			t.Errorf("[%d] Unexpected error: %v", idx, err)
		} else if ok != tc.ExpectMatch {
			t.Errorf("[%d] %q didn't match %q as expected. got %v, want %v", idx, tc.Filename, tc.Pattern, ok, tc.ExpectMatch)
		}
	}
}
