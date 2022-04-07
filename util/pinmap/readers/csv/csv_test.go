// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package csv_test

import (
	"testing"

	"path/filepath"
	"reflect"

	"pinmap/pm"
	"pinmap/readers/csv"
)

const column = "B"

func TestName(t *testing.T) {
	var r csv.CSVReader
	if r.Name() != "csv" {
		t.Errorf("expected %s, got %s", "csv", r.Name())
	}
	pins, err := r.Read(column, filepath.Join("testdata", "data.csv"))
	if err != nil {
		t.Fatalf("data.csv: %v", err)
	}
	exp := &pm.Pins{
		Adc: []*pm.Pin{
			&pm.Pin{pm.ADC, "A1", "EC_ADC_1", "ENUM_ADC_1"},
		},
		I2c: []*pm.Pin{
			&pm.Pin{pm.I2C, "G7", "EC_I2C_CLK_0", "SENSOR"},
		},
		Gpio: []*pm.Pin{
			&pm.Pin{pm.Input, "D4", "EC_GPIO_1", "GPIO1"},
			&pm.Pin{pm.Output, "E5", "EC_GPIO_2", "GPIO2"},
			&pm.Pin{pm.OutputODL, "F6", "EC_GPIO_3", ""},
			&pm.Pin{pm.InputPU, "K10", "EC_GPIO_4", ""},
		},
	}
	check(t, "ADc", exp.Adc, pins.Adc)
	check(t, "I2c", exp.I2c, pins.I2c)
	check(t, "Gpio", exp.Gpio, pins.Gpio)
}

func check(t *testing.T, name string, exp, got []*pm.Pin) {
	if !reflect.DeepEqual(exp, got) {
		t.Errorf("%s - expected:", name)
		for _, p := range exp {
			t.Errorf("%v", *p)
		}
		t.Errorf("got:")
		for _, p := range got {
			t.Errorf("%v", *p)
		}
	}
}
