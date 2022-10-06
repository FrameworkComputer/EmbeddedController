// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package csv

import (
	"bufio"
	"encoding/csv"
	"fmt"
	"os"
	"strings"

	"pinmap/pm"
)

// CSVReader reads the EC pin references from a comma separated
// values file.
type CSVReader struct {
}

// Name returns the name of this reader.
func (r *CSVReader) Name() string {
	return "csv"
}

// Read reads the CSV file (provided as an argument) and extracts
// the pin reference data. The first line is expected to be column
// titles that are used to identify the columns.
// columnKey identifies the column to use for the pin allocations,
// such as "A", "B" etc.
func (r *CSVReader) Read(columnKey, filepath string) (*pm.Pins, error) {
	if len(columnKey) != 1 {
		return nil, fmt.Errorf("illegal column name: '%s'", columnKey)
	}
	column := int(strings.ToLower(columnKey)[0] - 'a')
	if column < 0 || column > 'Z'-'A' {
		return nil, fmt.Errorf("illegal column name (should be 'A' - 'Z')")
	}
	f, err := os.Open(filepath)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	rdr := csv.NewReader(bufio.NewReader(f))
	data, err := rdr.ReadAll()
	if err != nil {
		return nil, err
	}
	if len(data) < 2 {
		return nil, fmt.Errorf("no data in file")
	}
	if len(data[0]) < column {
		return nil, fmt.Errorf("Column '%s' is out of range", columnKey)
	}
	// Put the CSV headers into a map.
	cmap := make(map[string]int)
	for c, s := range data[0] {
		cmap[s] = c
	}
	// Find the matching columns that are needed.
	signal, ok := cmap["Signal Name"]
	if !ok {
		return nil, fmt.Errorf("missing 'Signal Name' column")
	}
	chipKey := data[0][column]
	chip, ok := cmap[chipKey]
	if !ok {
		return nil, fmt.Errorf("missing '%s' chip column", chipKey)
	}
	ptype, ok := cmap["Type"]
	if !ok {
		return nil, fmt.Errorf("missing 'Type' column")
	}
	enum, ok := cmap["Enum"]
	if !ok {
		return nil, fmt.Errorf("missing 'Enum' column")
	}
	var pins pm.Pins
	// Read the rest of the rows.
	for i, row := range data[1:] {
		p := new(pm.Pin)
		switch row[ptype] {
		default:
			fmt.Printf("%s:%d: Unknown signal type (%s) - ignored\n", filepath, i+1, row[ptype])
			continue
		case "OTHER":
		case "PWM":
		case "PWM_INVERT":
			// Skipped
			continue
		case "ADC":
			p.PinType = pm.ADC
			pins.Adc = append(pins.Adc, p)
		case "I2C_DATA":
			// Only the clock pin is used for the config
			continue
		case "I2C_CLOCK":
			p.PinType = pm.I2C
			pins.I2c = append(pins.I2c, p)
		case "INPUT":
			p.PinType = pm.Input
			pins.Gpio = append(pins.Gpio, p)
		case "INPUT_L":
			p.PinType = pm.InputL
			pins.Gpio = append(pins.Gpio, p)
		case "INPUT_PU":
			p.PinType = pm.InputPU
			pins.Gpio = append(pins.Gpio, p)
		case "INPUT_PU_L":
			p.PinType = pm.InputPUL
			pins.Gpio = append(pins.Gpio, p)
		case "INPUT_PD":
			p.PinType = pm.InputPD
			pins.Gpio = append(pins.Gpio, p)
		case "OUTPUT":
			p.PinType = pm.Output
			pins.Gpio = append(pins.Gpio, p)
		case "OUTPUT_L":
			p.PinType = pm.OutputL
			pins.Gpio = append(pins.Gpio, p)
		case "OUTPUT_ODL":
			p.PinType = pm.OutputODL
			pins.Gpio = append(pins.Gpio, p)
		case "OUTPUT_ODR":
			p.PinType = pm.OutputOD
			pins.Gpio = append(pins.Gpio, p)
		}
		p.Signal = row[signal]
		p.Pin = row[chip]
		p.Enum = row[enum]
	}

	return &pins, nil
}
