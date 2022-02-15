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

type lineName struct {
	pin  int    // Pin number
	name string // Pin name
}

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
func Generate(out io.Writer, pins *Pins, chip Chip, names bool) {
	lineNameMap := make(map[string][]lineName)
	// Write header with date.
	fmt.Fprintf(out, header, time.Now().Year())
	// Default sort function (by Signal)
	sortSignal := func(iPin, jPin *Pin) bool {
		return jPin.Signal > iPin.Signal
	}
	// Sort function for I2C (by port).
	sortI2c := func(iPin, jPin *Pin) bool {
		return chip.I2c(jPin.Pin) > chip.I2c(iPin.Pin)
	}
	pinConfig(out, "named-adc-channels", pins.Adc, chip, sortSignal, adcConfig)
	pinConfig(out, "named-gpios", pins.Gpio, chip, sortSignal, func(out io.Writer, pin *Pin, chip Chip) {
		gpioConfig(out, pin, chip, lineNameMap)
	})
	pinConfig(out, "named-i2c-ports", pins.I2c, chip, sortI2c, i2cConfig)
	pinConfig(out, "named-pwms", pins.Pwm, chip, sortSignal, pwmConfig)
	fmt.Fprintf(out, "};\n")
	// Retrieve the enabled nodes, sort, de-dup and
	// generate overlays.
	generateEnabledNodes(out, chip.EnabledNodes())
	// If gpio line names are required, generate them.
	if names {
		generateLineNames(out, lineNameMap)
	}
}

// pinConfig creates the DTS for a single pin.
func pinConfig(out io.Writer, block string, pins []*Pin, chip Chip, sortFun func(*Pin, *Pin) bool, cfunc func(io.Writer, *Pin, Chip)) {
	if len(pins) == 0 {
		return
	}
	// Sort the pins.
	sort.Slice(pins, func(i, j int) bool {
		return sortFun(pins[i], pins[j])
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
	fmt.Fprintf(out, "\t\t\tio-channels = <&adc0 %s>;\n", c)
	fmt.Fprintf(out, "\t\t};\n")
}

// gpioConfig is the handler for GPIO pins. It also stores
// the line name into the map.
func gpioConfig(out io.Writer, pin *Pin, chip Chip, lineNameMap map[string][]lineName) {
	gc, gp := chip.Gpio(pin.Pin)
	if len(gc) == 0 {
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
	case InputL:
		gtype = "(GPIO_INPUT | GPIO_ACTIVE_LOW)"
	case InputPU:
		gtype = "GPIO_INPUT_PULL_UP"
	case InputPUL:
		gtype = "(GPIO_INPUT_PULL_UP | GPIO_ACTIVE_LOW)"
	case InputPD:
		gtype = "GPIO_INPUT_PULL_DOWN"
	case Output:
		gtype = "GPIO_OUTPUT"
	case OutputL:
		gtype = "(GPIO_OUTPUT | GPIO_ACTIVE_LOW)"
	case OutputOD:
		gtype = "GPIO_ODR_HIGH"
	case OutputODL:
		gtype = "GPIO_ODR_LOW"
	}
	lc := strings.ToLower(pin.Signal)
	fmt.Fprintf(out, "\t\tgpio_%s: %s {\n", lc, lc)
	fmt.Fprintf(out, "\t\t\tgpios = <&%s %d %s>;\n", gc, gp, gtype)
	if len(pin.Enum) > 0 {
		fmt.Fprintf(out, "\t\t\tenum-name = \"%s\";\n", pin.Enum)
	}
	fmt.Fprintf(out, "\t\t};\n")
	lineNameMap[gc] = append(lineNameMap[gc], lineName{gp, lc})
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

// generateEnabledNodes generates a "status = okay"
// property for the list of nodes passed.
func generateEnabledNodes(out io.Writer, nodes []string) {
	if len(nodes) != 0 {
		sort.Strings(nodes)
		var prev string
		for _, s := range nodes {
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

// generateLineNames generates GPIO line names
// for the GPIO controller map passed.
// Empty strings are added for missing pins.
// The format generated is:
//
//  &gpioX {
//     gpio-line-names =
//          "",
//          "gpio_name_1",
//          "",
//          "",
//          "gpio_name_2";
//  };
//
func generateLineNames(out io.Writer, gpios map[string][]lineName) {
	// Sort the GPIO controller names.
	var gcList []string
	for gc, _ := range gpios {
		gcList = append(gcList, gc)
	}
	sort.Strings(gcList)
	for _, gc := range gcList {
		ln := gpios[gc]
		// Sort names into pin order.
		sort.Slice(ln, func(i, j int) bool {
			return ln[j].pin > ln[i].pin
		})
		fmt.Fprintf(out, "\n&%s {\n", gc)
		fmt.Fprintf(out, "\tgpio-line-names =\n")
		fmt.Fprintf(out, "\t\t")
		for i, v := range ln {
			// If not the first, add comma and step to next line
			if i != 0 {
				fmt.Fprintf(out, ",\n\t\t")
			}
			// Add filler empty strings
			for sk := i; sk < v.pin; sk++ {
				fmt.Fprintf(out, "\"\",\n\t\t")
			}
			fmt.Fprintf(out, "\"%s\"", v.name)
		}
		fmt.Fprintf(out, ";\n")
		fmt.Fprintf(out, "};\n")
	}
}
