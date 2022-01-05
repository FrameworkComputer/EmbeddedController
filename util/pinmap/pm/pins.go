// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm

// Pin types enum constants
const (
	ADC = iota
	PWM
	PWM_INVERT
	I2C
	Input
	InputPU
	InputPD
	Output
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
	Pwm  []*Pin // Pwm pins
}
