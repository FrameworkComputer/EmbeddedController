// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utility to read a flattened device tree file (DTB) and create a
// DTS fragment initialising the GPIO controller gpio-line-names
// property.
// The input DTB can be generated from device tree source (DTS) using
// the standard device tree compiler (dtc) e.g
//
//   dtc --out zephyr.dtb zephyr.dts
//   gt-gpionames --output gpio_names.dts --input zephyr.dtb
//

package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"os"

	"github.com/u-root/u-root/pkg/dt"
)

var input = flag.String("input", "", "Input DTB file")
var output = flag.String("output", "", "Output file")

type gpio struct {
	node *dt.Node // Device tree node
	pins []string // Slice of pin names
	max  int      // Last slice entry containing a name
}

func main() {
	flag.Parse()
	f, err := os.Open(*input)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	fdt, err := dt.ReadFDT(f)
	if err != nil {
		log.Fatal(err)
	}

	gm := findGpioControllers(fdt)
	ng := findNamedGpios(fdt)
	setLineNames(gm, ng)

	of, err := os.Create(*output)
	if err != nil {
		log.Fatal(err)
	}
	defer of.Close()
	writeLineNames(of, gm)
}

// writeLineNames generates DTS to set the gpio-line-names
// string array property for each of the GPIO controllers
// TODO: It may be cleaner to use a template rather than
// generating each line separately.
func writeLineNames(of io.Writer, gm map[dt.PHandle]*gpio) {
	fmt.Fprintf(of, "/ {\n")
	fmt.Fprintf(of, "\tsoc {\n")
	for _, g := range gm {
		if g.max < 0 {
			continue
		}
		first := true
		fmt.Fprintf(of, "\t\t%s {\n", g.node.Name)
		fmt.Fprintf(of, "\t\t\tgpio-line-names =")
		for i := 0; i <= g.max; i++ {
			if !first {
				fmt.Fprintf(of, ",")
			}
			fmt.Fprintf(of, "\n\t\t\t\t\"%s\"", g.pins[i])
			first = false
		}
		fmt.Fprintf(of, ";\n")
		fmt.Fprintf(of, "\t\t};\n")
	}
	fmt.Fprintf(of, "\t};\n")
	fmt.Fprintf(of, "};\n")
}

// findGpioControllers walks the nodes of the
// flattened device tree, detecting the
// GPIO controller nodes (which should have the 'gpio-controller'
// property on them), and adds them to the map.
func findGpioControllers(fdt *dt.FDT) map[dt.PHandle]*gpio {
	gm := make(map[dt.PHandle]*gpio)
	// Walk the tree and find all the GPIO controllers
	gpios, err := fdt.Root().FindAll(func(n *dt.Node) bool {
		_, ok := n.LookProperty("gpio-controller")
		return ok
	})
	if err != nil {
		log.Fatal(err)
	}
	// For each of the controllers, create a structure containing
	// the slice of pin names, and add it to the map using
	// the phandle as the key.
	for _, n := range gpios {
		// Default of 32 pins per controller.
		var npins uint32 = 32
		pinsProp, ok := n.LookProperty("ngpios")
		if ok {
			npins, err = pinsProp.AsU32()
			if err != nil {
				log.Printf("%s illegal number of pins (%v), ignored\n", n.Name, err)
				continue
			}
		}
		p, ok := n.LookProperty("phandle")
		if !ok {
			// No phandle, so not referenced.
			log.Printf("%s not referenced, ignored\n", n.Name)
			continue
		}
		ph, err := p.AsPHandle()
		if err != nil {
			log.Printf("%s illegal phandle (%v), ignored\n", n.Name, err)
			continue
		}
		gpio := new(gpio)
		gpio.node = n
		gpio.pins = make([]string, npins)
		gpio.max = -1
		gm[ph] = gpio
	}
	return gm
}

// findNamesGpios will walk the device tree and extract all the
// child nodes of the nodes with compatible = "named-gpio",
// and return the slice containing these nodes.
func findNamedGpios(fdt *dt.FDT) []*dt.Node {
	ng := make([]*dt.Node, 0)
	err := fdt.RootNode.Walk(func(n *dt.Node) error {
		p, ok := n.LookProperty("compatible")
		if ok {
			s, err := p.AsType(dt.StringType)
			if err == nil && s == "named-gpios" {
				ng = append(ng, n.Children...)
			}
		}
		return nil
	})
	if err != nil {
		log.Fatal(err)
	}
	return ng
}

// setLineNames will add the named-gpio node names to the GPIO controller
// line names for the associated pin.
func setLineNames(gm map[dt.PHandle]*gpio, ngList []*dt.Node) {
	for _, ng := range ngList {
		gp, ok := ng.LookProperty("gpios")
		if !ok {
			log.Printf("No valid gpios on %s\n", ng.Name)
			continue
		}
		// Get the flattened byte string representing
		// the GPIO. The array contains:
		//   32 bit PHandle referencing the GPIO controller
		//   32 bit pin number
		//   32 bit flags
		blk, err := gp.AsPropEncodedArray()
		if err != nil {
			log.Printf("%s: %v\n", ng.Name, err)
			continue
		}
		if len(blk) != 12 {
			log.Printf("%s: Wrong size on gpios PHA(%d)\n", ng.Name, len(blk))
			continue
		}
		ph := binary.BigEndian.Uint32(blk[0:])
		pin := binary.BigEndian.Uint32(blk[4:])
		gpio, ok := gm[dt.PHandle(ph)]
		if !ok {
			log.Printf("No GPIO controller for %s (PH %d)\n", ng.Name, ph)
			continue
		}
		if int(pin) >= len(gpio.pins) {
			log.Printf("GPIO %s pin %d out of range for %s (max %d)\n",
				ng.Name, pin, gpio.node.Name, len(gpio.pins))
			continue
		}
		if gpio.pins[pin] != "" {
			log.Printf("GPIO %s pin %d already has name (%s)\n",
				ng.Name, pin, gpio.pins[pin])
			continue
		}
		gpio.pins[pin] = ng.Name
		if int(pin) > gpio.max {
			gpio.max = int(pin)
		}
	}
}
