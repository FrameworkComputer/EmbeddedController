// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm

/*
 * Chip represents an Embedded Controller IC, where
 * pin names can be used to lookup various types of
 * pin usages such as I2C buses, GPIOs etc.
 * The pins are referenced as physical pin names such as "A4" etc.
 */
type Chip interface {
	/*
	 * Name returns the name of the chip
	 */
	Name() string
	/*
	 * EnabledNodes returns a list of names of DTS nodes that
	 * require enabling i.e adding 'status = "okay"' on the nodes.
	 */
	EnabledNodes() []string
	/*
	 * Adc will return a DTS reference to the appropriate ADC
	 * that is connected to this pin.
	 */
	Adc(pin string) string
	/*
	 * Gpio will return a DTS reference to the appropriate GPIO
	 * that is connected to this pin.
	 */
	Gpio(pin string) string
	/*
	 * I2C will return a DTS reference to the appropriate I2C
	 * bus that is connected to this pin. The pin is assumed to be
	 * the I2C clock pin of the 2 wire bus.
	 */
	I2c(pin string) string
	/*
	 * Pwm will return a DTS reference to the appropriate PWM
	 * that is connected to this pin.
	 */
	Pwm(pin string) string
}

// chipList contains a list of registered chips.
// Each chip has a unique name that is used to match it.
var chipList []Chip

// RegisterChip adds this chip into the list of registered chips.
func RegisterChip(chip Chip) {
	chipList = append(chipList, chip)
}

// FindChip returns the registered chip matching this name, or nil
// if none are found.
func FindChip(name string) Chip {
	for _, c := range chipList {
		if c.Name() == name {
			return c
		}
	}
	return nil
}

// Chips returns the list of names of the registered chips.
func Chips() []string {
	var l []string
	for _, c := range chipList {
		l = append(l, c.Name())
	}
	return l
}
