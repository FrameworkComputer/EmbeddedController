// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm

// Pin types enum constants
const (
	ADC = iota
	I2C
	Input
	InputL
	InputPU
	InputPUL
	InputPD
	Output
	OutputL
	OutputOD
	OutputODL
)

// Pin represents one EC pin.
type Pin struct {
	PinType int    // Type of pin (from above)
	Pin     string // The reference of the physical pin.
	Signal  string // The net (circuit) name of the pin
	Enum    string // If set, the internal s/w name of the pin
}

// The accumulated pins of the EC.
type Pins struct {
	Adc  []*Pin // Analogue to digital converters
	I2c  []*Pin // I2C busses
	Gpio []*Pin // GPIO pins
}
