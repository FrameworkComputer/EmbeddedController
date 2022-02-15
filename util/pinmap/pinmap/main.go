// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	_ "pinmap/chips"
	"pinmap/pm"
	_ "pinmap/readers/csv"
)

var chipFlag = flag.String("chip", "", "Chip to use for pinmap")
var output = flag.String("output", "gpio.dts", "Output file")
var reader = flag.String("reader", "csv", "Input source type")
var names = flag.Bool("names", false, "Generate gpio-line-names")
var force = flag.Bool("force", false, "Overwrite output file")

func main() {
	flag.Usage = Usage
	flag.Parse()
	if len(flag.Args()) == 0 {
		Error("No input arguments")
	}
	chip := pm.FindChip(*chipFlag)
	if chip == nil {
		Error(fmt.Sprintf("No matching chip for '%s'", *chipFlag))
	}
	pins, err := pm.ReadPins(*reader, *chipFlag, flag.Arg(0))
	if err != nil {
		Error(fmt.Sprintf("%s - %s: %v", *reader, flag.Arg(0), err))
	}
	if !*force && fileExists(*output) {
		Error(fmt.Sprintf("%s already exists - use --force to overwrite", *output))
	}
	out, err := os.Create(*output)
	defer out.Close()
	if err != nil {
		Error(fmt.Sprintf("Failed to create %s: %v", *output, err))
	}
	pm.Generate(out, pins, chip, *names)
}

// fileExists returns true if the file currently exists.
func fileExists(name string) bool {
	_, err := os.Stat(name)
	return err == nil
}

// Error prints an error message to stderr and prints the usage.
func Error(msg string) {
	fmt.Fprintf(os.Stderr, "%s\n", msg)
	Usage()
}

// Usage prints the usage of the command.
func Usage() {
	fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
	fmt.Fprintf(os.Stderr, "%s [ flags ] input-argument\n", os.Args[0])
	flag.PrintDefaults()
	fmt.Fprintf(os.Stderr, "Available chips are:\n")
	for _, c := range pm.Chips() {
		fmt.Fprintf(os.Stderr, "%s\n", c)
	}
	fmt.Fprintf(os.Stderr, "Available readers are:\n")
	for _, r := range pm.Readers() {
		fmt.Fprintf(os.Stderr, "%s\n", r)
	}
	os.Exit(1)
}
