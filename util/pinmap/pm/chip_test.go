// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm_test

import (
	"testing"

	"reflect"
	"sort"

	"pinmap/pm"
)

type testChip struct {
	name  string
	nodes []string
	adc   string
	gc    string
	gp    int
	i2c   string
	pwm   string
}

func (c *testChip) Name() string {
	return c.name
}

func (c *testChip) EnabledNodes() []string {
	return c.nodes
}

func (c *testChip) Adc(pin string) string {
	return c.adc
}

func (c *testChip) Gpio(pin string) (string, int) {
	return c.gc, c.gp
}

func (c *testChip) I2c(pin string) string {
	return c.i2c
}

func (c *testChip) Pwm(pin string) string {
	return c.pwm
}

func TestName(t *testing.T) {
	n1 := "Test1"
	n2 := "Test2"
	tc1 := &testChip{name: n1}
	tc2 := &testChip{name: n2}
	pm.RegisterChip(tc1)
	pm.RegisterChip(tc2)
	if pm.FindChip(n1) != tc1 {
		t.Errorf("Did not match tc1")
	}
	if pm.FindChip(n2) != tc2 {
		t.Errorf("Did not match tc2")
	}
	chips := pm.Chips()
	sort.Strings(chips)
	exp := []string{n1, n2}
	if !reflect.DeepEqual(exp, chips) {
		t.Errorf("Expected %v, got %v", exp, chips)
	}
}
