// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package chips_test

import (
	"testing"

	"reflect"
	"sort"

	"pinmap/chips"
)

func TestName(t *testing.T) {
	expName := "NPCX993"
	var n chips.Npcx993
	name := n.Name()
	if name != expName {
		t.Errorf("Expected %s, got %s for Name()", expName, name)
	}
}

func TestMissing(t *testing.T) {
	var n chips.Npcx993

	none := "None"
	if n.Adc(none) != "" {
		t.Errorf("Expected empty string, got %s for Adc()", n.Adc(none))
	}
	gc, gp := n.Gpio(none)
	if gc != "" {
		t.Errorf("Expected empty string, got %s %d for Gpio()", gc, gp)
	}
	if n.Pwm(none) != "" {
		t.Errorf("Expected empty string, got %s for Pwm()", n.Pwm(none))
	}
	if n.I2c(none) != "" {
		t.Errorf("Expected empty string, got %s for I2c()", n.I2c(none))
	}
}

func TestMulti(t *testing.T) {
	var n chips.Npcx993

	pin := "F4"
	if n.Adc(pin) != "10" {
		t.Errorf("Expected \"10\", got %s for Adc()", n.Adc(pin))
	}
	gc, gp := n.Gpio(pin)
	if gc != "gpioe" || gp != 0 {
		t.Errorf("Expected \"gpioe 0\", got %s %d for Gpio()", gc, gp)
	}
	if n.Pwm(pin) != "" {
		t.Errorf("Expected empty string, got %s for Pwm()", n.Pwm(pin))
	}
	if n.I2c(pin) != "" {
		t.Errorf("Expected empty string, got %s for I2c()", n.I2c(pin))
	}
	pin = "L9"
	if n.Pwm(pin) != "pwm4 4" {
		t.Errorf("Expected \"pwm4 4\", got %s for Pwm()", n.Pwm(pin))
	}
	pin = "F8"
	if n.I2c(pin) != "i2c3_0" {
		t.Errorf("Expected \"i2c3_0\", got %s for I2c()", n.I2c(pin))
	}
}

func TestAdcEnable(t *testing.T) {
	var n chips.Npcx993

	pin := "F4"
	if n.Adc(pin) != "10" {
		t.Errorf("Expected \"10\", got %s for Adc()", n.Adc(pin))
	}
	exp := []string{"adc0"}
	if !reflect.DeepEqual(n.EnabledNodes(), exp) {
		t.Errorf("Expected %v, got %v for EnabledNodes()", exp, n.EnabledNodes())
	}
}

func TestI2cEnable(t *testing.T) {
	var n chips.Npcx993

	n.I2c("F5")  // i2c4_1
	n.I2c("C12") // i2c0_0
	exp := []string{"i2c0_0", "i2c4_1", "i2c_ctrl0", "i2c_ctrl4"}
	nodes := n.EnabledNodes()
	sort.Strings(nodes)
	if !reflect.DeepEqual(nodes, exp) {
		t.Errorf("Expected %v, got %v for EnabledNodes()", exp, n.EnabledNodes())
	}
}
