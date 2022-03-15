// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pm

import (
	"fmt"
)

// Reader reads the pin configuration from a source.
type Reader interface {
	Name() string
	Read(key string, arg string) (*Pins, error)
}

// readerlist is registered list of readers.
var readerList []Reader

// ReadPins will use the selected reader and the key to
// read the EC pin data.
func ReadPins(reader, key, arg string) (*Pins, error) {
	for _, r := range readerList {
		if r.Name() == reader {
			return r.Read(key, arg)
		}
	}
	return nil, fmt.Errorf("%s: unknown reader", reader)
}

// Readers returns a list of the reader names.
func Readers() []string {
	var l []string
	for _, r := range readerList {
		l = append(l, r.Name())
	}
	return l
}

// RegisterReader will add this reader to the registered list of readers.
func RegisterReader(reader Reader) {
	readerList = append(readerList, reader)
}
