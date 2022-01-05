// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm

import (
	"fmt"
	"io"
	"sort"
	"strings"
	"time"
)

const header = `/* Copyright %d The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This file is auto-generated - do not edit!
 */

/ {
`

// Generate creates the DTS configuration from the pins using the chip as a
// reference and writes the DTS to the output.
func Generate(out io.Writer, pins *Pins, chip Chip) {
	// Write header with date.
	fmt.Fprintf(out, header, time.Now().Year())
	pinConfig(out, "named-adc-channels", pins.Adc, chip, adcConfig)
	pinConfig(out, "named-gpios", pins.Gpio, chip, gpioConfig)
	pinConfig(out, "named-i2c-ports", pins.I2c, chip, i2cConfig)
	pinConfig(out, "named-pwms", pins.Pwm, chip, pwmConfig)
	fmt.Fprintf(out, "};\n")
	// Retrieve the enabled nodes, sort, de-dup and
	// generate overlays.
	en := chip.EnabledNodes()
	if len(en) != 0 {
		sort.Strings(en)
		var prev string
		for _, s := range en {
			if s == prev {
				continue
			}
			fmt.Fprintf(out, "\n&%s {\n", s)
			fmt.Fprintf(out, "\tstatus = \"okay\";\n")
			fmt.Fprintf(out, "};\n")
			prev = s
		}
	}
}

// pinConfig creates the DTS for a single pin.
func pinConfig(out io.Writer, block string, pins []*Pin, chip Chip, cfunc func(io.Writer, *Pin, Chip)) {
	if len(pins) == 0 {
		return
	}
	// Sort the pins into alphbetical order.
	sort.Slice(pins, func(i, j int) bool {
		return pins[j].Signal > pins[i].Signal
	})
	// Generate start of block.
	fmt.Fprintf(out, "\n\t%s {\n", block)
	fmt.Fprintf(out, "\t\tcompatible = \"%s\";\n\n", block)
	for _, p := range pins {
		cfunc(out, p, chip)
	}
	fmt.Fprintf(out, "\t};\n")
}

// adcConfig is the handler for ADC pins.
func adcConfig(out io.Writer, pin *Pin, chip Chip) {
	if pin.PinType != ADC {
		fmt.Printf("Unknown ADC type (%d) for pin %s, ignored\n", pin.PinType, pin.Pin)
		return
	}
	c := chip.Adc(pin.Pin)
	if len(c) == 0 {
		fmt.Printf("No matching ADC for pin %s, ignored\n", pin.Pin)
		return
	}
	lc := strings.ToLower(pin.Signal)
	fmt.Fprintf(out, "\t\tadc_%s: %s {\n", lc, lc)
	fmt.Fprintf(out, "\t\t\tlabel = \"%s\";\n", pin.Signal)
	if len(pin.Enum) > 0 {
		fmt.Fprintf(out, "\t\t\tenum-name = \"%s\";\n", pin.Enum)
	}
	fmt.Fprintf(out, "\t\t\tchannel = <%s>;\n", c)
	fmt.Fprintf(out, "\t\t};\n")
}

// gpioConfig is the handler for GPIO pins.
func gpioConfig(out io.Writer, pin *Pin, chip Chip) {
	c := chip.Gpio(pin.Pin)
	if len(c) == 0 {
		fmt.Printf("No matching GPIO for pin %s, ignored\n", pin.Pin)
		return
	}
	var gtype string
	switch pin.PinType {
	default:
		fmt.Printf("Unknown GPIO type (%d) for pin %s, ignored\n", pin.PinType, pin.Pin)
		return
	case Input:
		gtype = "GPIO_INPUT"
	case InputPU:
		gtype = "GPIO_INPUT_PULL_UP"
	case InputPD:
		gtype = "GPIO_INPUT_PULL_DOWN"
	case Output:
		gtype = "GPIO_OUTPUT"
	case OutputOD:
		gtype = "GPIO_ODR_HIGH"
	case OutputODL:
		gtype = "GPIO_ODR_LOW"
	}
	lc := strings.ToLower(pin.Signal)
	fmt.Fprintf(out, "\t\tgpio_%s: %s {\n", lc, lc)
	fmt.Fprintf(out, "\t\t\t#gpio-cells = <0>;\n")
	fmt.Fprintf(out, "\t\t\tgpios = <&%s %s>;\n", c, gtype)
	if len(pin.Enum) > 0 {
		fmt.Fprintf(out, "\t\t\tenum-name = \"%s\";\n", pin.Enum)
	}
	fmt.Fprintf(out, "\t\t};\n")
}

// i2cConfig is the handler for I2C pins.
func i2cConfig(out io.Writer, pin *Pin, chip Chip) {
	if pin.PinType != I2C {
		fmt.Printf("Unknown I2C type (%d) for pin %s, ignored\n", pin.PinType, pin.Pin)
		return
	}
	c := chip.I2c(pin.Pin)
	if len(c) == 0 {
		fmt.Printf("No matching I2C for pin %s, ignored\n", pin.Pin)
		return
	}
	// Trim off trailing clock name (if any)
	lc := strings.TrimRight(strings.ToLower(pin.Signal), "_scl")
	fmt.Fprintf(out, "\t\ti2c_%s: %s {\n", lc, lc)
	fmt.Fprintf(out, "\t\t\ti2c-port = <&%s>;\n", c)
	if len(pin.Enum) > 0 {
		fmt.Fprintf(out, "\t\t\tenum-name = \"%s\";\n", pin.Enum)
	}
	fmt.Fprintf(out, "\t\t};\n")
}

// pwmConfig is the handler for PWM pins.
func pwmConfig(out io.Writer, pin *Pin, chip Chip) {
	var inv string
	switch pin.PinType {
	default:
		fmt.Printf("Unknown PWM type (%d) for pin %s, ignored\n", pin.PinType, pin.Pin)
		return
	case PWM:
		inv = "0"
	case PWM_INVERT:
		inv = "1"
	}
	c := chip.Pwm(pin.Pin)
	if len(c) == 0 {
		fmt.Printf("No matching PWM for pin %s, ignored\n", pin.Pin)
		return
	}
	lc := strings.ToLower(pin.Signal)
	fmt.Fprintf(out, "\t\tpwm_%s: %s {\n", lc, lc)
	fmt.Fprintf(out, "\t\t\tpwms = <&%s %s>;\n", c, inv)
	if len(pin.Enum) > 0 {
		fmt.Fprintf(out, "\t\t\tenum-name = \"%s\";\n", pin.Enum)
	}
	fmt.Fprintf(out, "\t\t};\n")
}
