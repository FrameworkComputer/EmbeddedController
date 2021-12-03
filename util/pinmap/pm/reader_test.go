// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm_test

import (
	"testing"

	"reflect"

	"pinmap/pm"
)

type testReader struct {
	name string
	arg  string
	chip string
	pins pm.Pins
}

func (r *testReader) Name() string {
	return r.name
}

func (r *testReader) Read(arg, chip string) (*pm.Pins, error) {
	r.arg = arg
	r.chip = chip
	return &r.pins, nil
}

func TestReader(t *testing.T) {
	n := "Test1"
	tr1 := &testReader{name: n}
	pm.RegisterReader(tr1)
	p, err := pm.ReadPins(n, "arg1", "chiptest")
	if err != nil {
		t.Errorf("Error %v on reading pins", err)
	}
	if p != &tr1.pins {
		t.Errorf("Did not match Pins")
	}
	p, err = pm.ReadPins("notMine", "arg1", "chiptest")
	if err == nil {
		t.Errorf("Should heve returned error")
	}
	readers := pm.Readers()
	exp := []string{n}
	if !reflect.DeepEqual(exp, readers) {
		t.Errorf("Expected %v, got %v", exp, readers)
	}
}
